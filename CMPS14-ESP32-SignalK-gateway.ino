#include <WiFi.h>
#include <ArduinoJson.h>
#include <ArduinoWebsockets.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <ArduinoOTA.h>
#include "secrets.h"

using namespace websockets;

// Wifi and OTA settings
const String SK_URL             = String("ws://") + SK_HOST + ":" + String(SK_PORT) + "/signalk/v1/stream" + ((strlen(SK_TOKEN) > 0) ? "?token=" + String(SK_TOKEN) : "");
String RSSIc                    = "NA"; // Wifi quality
String SK_SOURCE                = "esp32.cmps14"; // SignalK server source, used also as the OTA hostname
bool LCD_ONLY                   = false; // True when no wifi available, using only LCD
const uint32_t WIFI_TIMEOUT_MS  = 90000; // Trying wifi connection max 1.5 minutes

// CMPS14 I2C address and registers
const uint8_t CMPS14_ADDR       = 0x60;
const uint8_t REG_ANGLE_16_H    = 0x02;  // 16-bit angle * 10 (hi)
const uint8_t REG_ANGLE_16_L    = 0x03;  // 16-bit angle * 10 (lo)
const uint8_t REG_PITCH         = 0x04;  // signed degrees
const uint8_t REG_ROLL          = 0x05;  // signed degrees

// CMPS14 calibration
const bool CMPS14_AUTOCAL_ON          = false;
static bool cal_profile_stored        = false;
static unsigned long last_cal_poll_ms = 0;
static uint8_t cal_ok_count           = 0;
const unsigned long CAL_POLL_MS       = 500;    // 2 x per second
const uint8_t CAL_OK_REQUIRED         = 3;      // expect 3 consequtive OK signs
static bool CMPS14_MANUAL_CAL         = false;  // manual calibration started?

// CMPS14 reading parameters
const float HEADING_ALPHA               = 0.15f;                     // Smoothing factor 0...1, larger value less smoothing
const float INSTALLATION_OFFSET_DEG     = 0.0f;                      // Physical installation error of the compass module
const unsigned long MIN_TX_INTERVAL_MS  = 150;                       // Max frequency for sending deltas to SignalK
const float DB_HDG_RAD                  = 0.005f;                    // ~0.29°: deadband threshold for heading
const float DB_ATT_RAD                  = 0.003f;                    // ~0.17°: pitch/roll deadband threshold
static unsigned long last_minmax_tx_ms  = 0;
static float last_sent_pitch_min = NAN, last_sent_pitch_max = NAN;
static float last_sent_roll_min  = NAN, last_sent_roll_max  = NAN;
const unsigned long MINMAX_TX_INTERVAL_MS                   = 1000;  // Frequency for pitch/roll maximum values sending
unsigned long last_lcd_ms               = 0;
const unsigned long LCD_MS              = 1000;                      // Frequency to print on LCD

// SH-ESP32 default pins for I2C
const uint8_t I2C_SDA = 16;
const uint8_t I2C_SCL = 17;

// Websocket
WebsocketsClient ws;
volatile bool ws_open = false;
unsigned long next_ws_try_ms = 0;
const unsigned long WS_RETRY_MS = 2000;
const unsigned long WS_RETRY_MAX = 20000;

// Values in degrees for LCD output
float heading_deg     = NAN;
float pitch_deg       = NAN;
float roll_deg        = NAN;

// Values in radians for SignalK output
float heading_rad     = NAN;
float pitch_rad       = NAN;
float roll_rad        = NAN;
float pitch_min_rad   = NAN;
float pitch_max_rad   = NAN;
float roll_min_rad    = NAN;
float roll_max_rad    = NAN;

// I2C LCD screen
std::unique_ptr<LiquidCrystal_I2C> lcd;
bool lcd_present          = false;
static char prev_top[17]  = "";
static char prev_bot[17]  = "";
const uint8_t LCD_ADDR1   = 0x27;
const uint8_t LCD_ADDR2   = 0x3F;

// Float validity
inline bool validf(float x) { return !isnan(x) && isfinite(x); }

// Return shortest arc on 360° (for instance 359° to 001° is 2° not 358°)
inline float ang_diff_rad(float a, float b) {
  float d = a - b;
  while (d >  M_PI) d -= 2.0f * M_PI;
  while (d <= -M_PI) d += 2.0f * M_PI;
  return d;
}

// Set SignalK source and OTA hostname based on MAC address ending
void make_source_from_mac() {
  uint8_t m[6]; WiFi.macAddress(m);
  char tail[7]; snprintf(tail, sizeof(tail), "%02x%02x%02x", m[3], m[4], m[5]);
  SK_SOURCE = String("esp32.cmps14-") + tail;
}

// Websocket
void setup_ws_callbacks() {
  ws.onEvent([](WebsocketsEvent e, String){
    if (e == WebsocketsEvent::ConnectionOpened)  { ws_open = true; }
    if (e == WebsocketsEvent::ConnectionClosed)  { ws_open = false; }
    if (e == WebsocketsEvent::GotPing)           { ws.pong(); }
  });
}

// Send batch of SignalK deltas but only if change exceeds the deadband limits (no unnecessary sending)
void send_batch_delta_if_needed() {
  if (LCD_ONLY || !ws_open) return;                                             // execute only if wifi and websocket ok
  if (!validf(heading_rad) || !validf(pitch_rad) || !validf(roll_rad)) return;  // execute only if values are valid

  static unsigned long last_tx_ms = 0;
  const unsigned long now = millis();
  if (now - last_tx_ms < MIN_TX_INTERVAL_MS) {
    return;                                                                     // max 10 Hz, timer 100 ms for sending to SignalK
  }

  static float last_h = NAN, last_p = NAN, last_r = NAN;
  bool changed_h = false, changed_p = false, changed_r = false;

  if (!validf(last_h) || fabsf(ang_diff_rad(heading_rad, last_h)) >= DB_HDG_RAD) {
    changed_h = true; last_h = heading_rad;
  }
  if (!validf(last_p) || fabsf(pitch_rad - last_p) >= DB_ATT_RAD) {
    changed_p = true; last_p = pitch_rad;
  }
  if (!validf(last_r) || fabsf(roll_rad - last_r) >= DB_ATT_RAD) {
    changed_r = true; last_r = roll_rad;
  }

  if (!(changed_h || changed_p || changed_r)) return;

  StaticJsonDocument<384> doc;
  doc["context"] = "vessels.self";
  auto updates = doc.createNestedArray("updates");
  auto up      = updates.createNestedObject();
  up["$source"] = SK_SOURCE;
  auto values  = up.createNestedArray("values");

  auto add = [&](const char* path, float v) {
    auto o = values.createNestedObject();
    o["path"]  = path;
    o["value"] = v;
  };

  if (changed_h) add("navigation.headingMagnetic", last_h);
  if (changed_p) add("navigation.attitude.pitch",  last_p);
  if (changed_r) add("navigation.attitude.roll",   last_r);

  if (values.size() == 0) return;

  char buf[448];
  size_t n = serializeJson(doc, buf, sizeof(buf));
  bool ok = ws.send(buf, n);
  if (!ok) {
    ws.close();
    ws_open = false;
  }

  last_tx_ms = now;
}

// Send pitch and roll maximum values to SignalK if changed and less frequently than "live" values
void send_minmax_delta_if_due() {
  if (LCD_ONLY || !ws_open) return;                                                 // execute only if wifi and websocket ok

  const unsigned long now = millis();
  if (now - last_minmax_tx_ms < MINMAX_TX_INTERVAL_MS) return;                      // execute only if timer is due

  bool ch_pmin = (validf(pitch_min_rad) && pitch_min_rad != last_sent_pitch_min);
  bool ch_pmax = (validf(pitch_max_rad) && pitch_max_rad != last_sent_pitch_max);
  bool ch_rmin = (validf(roll_min_rad)  && roll_min_rad  != last_sent_roll_min);
  bool ch_rmax = (validf(roll_max_rad)  && roll_max_rad  != last_sent_roll_max);

  if (!(ch_pmin || ch_pmax || ch_rmin || ch_rmax)) {                                // execute only if values have been changed
    last_minmax_tx_ms = now;
    return;
  }

  StaticJsonDocument<384> doc;
  doc["context"] = "vessels.self";
  auto updates = doc.createNestedArray("updates");
  auto up      = updates.createNestedObject();
  up["$source"] = SK_SOURCE;
  auto values  = up.createNestedArray("values");

  auto add = [&](const char* path, float v) {
    auto o = values.createNestedObject();
    o["path"]  = path;
    o["value"] = v; 
  };

  if (ch_pmin) add("navigation.attitude.pitch.min", pitch_min_rad);
  if (ch_pmax) add("navigation.attitude.pitch.max", pitch_max_rad);
  if (ch_rmin) add("navigation.attitude.roll.min",  roll_min_rad);
  if (ch_rmax) add("navigation.attitude.roll.max",  roll_max_rad);

  char buf[448];
  size_t n = serializeJson(doc, buf, sizeof(buf));
  bool ok = ws.send(buf, n);
  if (!ok){
    ws.close();
    ws_open = false;
  }

  if (ch_pmin) last_sent_pitch_min = pitch_min_rad;
  if (ch_pmax) last_sent_pitch_max = pitch_max_rad;
  if (ch_rmin) last_sent_roll_min  = roll_min_rad;
  if (ch_rmax) last_sent_roll_max  = roll_max_rad;
  last_minmax_tx_ms = now;
}

// Read values from CMPS14 compass and attitude sensor
bool readCompass(){
  
  Wire.beginTransmission(CMPS14_ADDR);
  Wire.write(REG_ANGLE_16_H);
  if (Wire.endTransmission(false) != 0) return false;

  const uint8_t toRead = 4;
  uint8_t n = Wire.requestFrom(CMPS14_ADDR, toRead);
  if (n != toRead) return false;

  uint8_t hi   = Wire.read();
  uint8_t lo   = Wire.read();
  int8_t pitch = (int8_t)Wire.read();
  int8_t roll  = (int8_t)Wire.read();

  uint16_t ang10 = ((uint16_t)hi << 8) | lo;  // 0..3599 (0.1°)
  float deg = ((float)ang10) / 10.0f;         // 0..359.9°
  
  deg += INSTALLATION_OFFSET_DEG;             // Correct physical installation error
  if (deg >= 360.0f) deg -= 360.0f;
  if (deg <    0.0f) deg += 360.0f;

  if (isnan(heading_deg)) {
    heading_deg = deg;
  } else {
    float diff = deg - heading_deg;
    if (diff > 180.0f)  diff -= 360.0f;       // Ensure shortest arc
    if (diff < -180.0f) diff += 360.0f;
    heading_deg += HEADING_ALPHA * diff;      // Smoothing of heading
    if (heading_deg >= 360.0f) heading_deg -= 360.0f;
    if (heading_deg < 0.0f)   heading_deg += 360.0f;
  }

  pitch_deg   = (float)pitch;
  roll_deg    = (float)roll;

  heading_rad = heading_deg * DEG_TO_RAD;
  pitch_rad   = pitch_deg * DEG_TO_RAD;
  roll_rad    = roll_deg * DEG_TO_RAD;

  if (isnan(pitch_max_rad)) {                 // Update the new maximum values
    pitch_max_rad = pitch_rad;
  } else if (pitch_rad > pitch_max_rad) {
    pitch_max_rad = pitch_rad;
  }
  
  if (isnan(pitch_min_rad)) {
    pitch_min_rad = pitch_rad;
  } else if (pitch_rad < pitch_min_rad) {
    pitch_min_rad = pitch_rad;
  }

  if (isnan(roll_max_rad)) {
    roll_max_rad = roll_rad;
  } else if (roll_rad > roll_max_rad) {
    roll_max_rad = roll_rad;
  }

  if (isnan(roll_min_rad)) {
    roll_min_rad = roll_rad;
  } else if (roll_rad < roll_min_rad) {
    roll_min_rad = roll_rad;
  }

  return true;

}

// Scan I2C address
bool i2c_device_present(uint8_t addr) {
  Wire.beginTransmission(addr);
  return (Wire.endTransmission() == 0);
}

// Initialize LCD screen
void lcd_init_safe() {

  uint8_t addr = 0;
  if (i2c_device_present(LCD_ADDR1)) addr = LCD_ADDR1;
  else if (i2c_device_present(LCD_ADDR2)) addr = LCD_ADDR2;

  if (addr) {
    lcd = std::make_unique<LiquidCrystal_I2C>(addr, 16, 2);
    lcd->init();
    lcd->backlight();
    lcd_present = true;
  } else {
    lcd_present = false;
  }
}

// Helper for LCD printing
static inline void copy16(char* dst, const char* src) {
  strncpy(dst, src, 16);   // copy max 16 characters
  dst[16] = '\0';          // ensure 0 termination
}

// LCD basic printing on two lines
void lcd_print_lines(const char* l1, const char* l2) {
  if (!lcd_present) return;

  char t[17], b[17];
  copy16(t, l1);
  copy16(b, l2);

  lcd->setCursor(0, 0);
  lcd->print(t);
  for (int i = (int)strlen(t); i < 16; i++) lcd->print(' ');

  lcd->setCursor(0, 1);
  lcd->print(b);
  for (int i = (int)strlen(b); i < 16; i++) lcd->print(' ');

  copy16(prev_top, t);
  copy16(prev_bot, b);
}

// Helper for CMPS14 calibration
bool cmps14_cmd(uint8_t cmd) {
  Wire.beginTransmission(CMPS14_ADDR);
  Wire.write(0x00);
  Wire.write(cmd);
  if (Wire.endTransmission() != 0) {
    Serial.println("cmps14_cmd false end transmission not 0");
    return false;
  }
  delay(20);  

  Wire.requestFrom(CMPS14_ADDR, (uint8_t)1);
  if (Wire.available() < 1) {
    Serial.println("cmps14_cmd false wire not available");
    return false;
  }
  uint8_t b = Wire.read();
  if (b == 0x55 || b == 0x07) {
    Serial.printf("cmps14_cmd true got 0x%02X \n", b);
    return true;
  }
  Serial.printf("cmps14_cmd false got 0x%02X \n", b);
  return false;
}

// Enable autocalibration
bool cmps14_enable_background_cal(bool autosave) {

  if (!cmps14_cmd(0x98)) return false;
  if (!cmps14_cmd(0x95)) return false;
  if (!cmps14_cmd(0x99)) return false;

  const uint8_t cfg = autosave ? 0x93 : 0x83;       // bit7 + Mag + Acc [+ autosave]
  Wire.beginTransmission(CMPS14_ADDR);
  Wire.write(0x00);       
  Wire.write(cfg);
  if (Wire.endTransmission() != 0) return false;
  return true;
}

// Read calibration status
uint8_t cmps14_read_cal_status() {
  Wire.beginTransmission(CMPS14_ADDR);
  Wire.write(0x1E);
  if (Wire.endTransmission(false) != 0) {
    Serial.println("cmps14_read_cal_status wire end transmission not 0");
    return 0xFF;
  }
  Wire.requestFrom(CMPS14_ADDR, (uint8_t)1);
  if (Wire.available() < 1) {
    Serial.println("cmps14_read_cal_status wire not available");
    return 0xFF;
  }
  uint8_t b = Wire.read();
  Serial.printf("cmps_read_cal_status return 0x%02X \n", b);
  return b;
}

// Save calibration
bool cmps14_store_profile() {
  if (!cmps14_cmd(0xF0)) return false;
  if (!cmps14_cmd(0xF5)) return false;
  if (!cmps14_cmd(0xF6)) return false;
  return true;
}

// Monitor and save autocalibration
void cmps14_monitor_and_store(bool save) {
  const unsigned long now = millis();
  if (now - last_cal_poll_ms < CAL_POLL_MS) return;
  last_cal_poll_ms = now;

  uint8_t st = cmps14_read_cal_status(); 
  if (st == 0xFF) {
    Serial.println("cmps_monitor_and_store got 0xFF");
    return;
  }

  uint8_t mag   = (st     ) & 0x03;
  uint8_t accel = (st >> 2) & 0x03;
  uint8_t gyro  = (st >> 4) & 0x03;
  uint8_t sys   = (st >> 6) & 0x03;

  if (!save) Serial.printf("Cal: SYS=%u G=%u A=%u M=%u\n", sys, gyro, accel, mag);

  static uint8_t prev = 0xFF;
  if (save && prev != st) {
    Serial.printf("Cal: SYS=%u G=%u A=%u M=%u\n", sys, gyro, accel, mag);
    prev = st;
  }

  if (sys == 3 && accel == 3 && mag == 3) {
    if (cal_ok_count < 255) cal_ok_count++;
  } else {
    cal_ok_count = 0;
  }

  if (save && !cal_profile_stored && cal_ok_count >= CAL_OK_REQUIRED) {
    if (cmps14_store_profile()) {
      lcd_print_lines("CALIBRATION", "SAVED");
      cal_profile_stored = true;
    } else {
      lcd_print_lines("CALIBRATION", "NOT SAVED");
    }
  }
}

// Read C, S, M or R command from serial port to enable, save, monitor or reset CMPS14 calibration
void readCalCommandFromSerial() {
  if (Serial.available()) {
    char cmd = toupper(Serial.read());                                                // Read one letter from serial port
    if (cmd == '\r' || cmd == '\n') return;                                           // Ignore return and line endings
    switch (cmd) {
      case 'C':
        Serial.println("CMD: ENABLE BACKGROUND CALIBRATION (Mag+Accel+autosave)");
        if (cmps14_enable_background_cal(CMPS14_AUTOCAL_ON)) {
          Serial.println("CMPS14 manual background calibration ENABLED.");
        } else {
          Serial.println("Enable FAILED!");
        }
        break;

      case 'S':
        Serial.println("CMD: STORE CALIBRATION PROFILE");
        if (cmps14_store_profile()) {
          Serial.println("Calibration profile STORED.");
          cal_profile_stored = true;
        } else {
          Serial.println("Store FAILED!");
        }
        break;
      
      case 'M':
        Serial.println("CMD: MONITOR CALIBRATION STATUS");
        cmps14_monitor_and_store(CMPS14_AUTOCAL_ON);
        break;

      case 'R':
        Serial.println("CMD: RESET (DELETE CALIBRATION PROFILE)");
        if (cmps14_cmd(0xE0) && cmps14_cmd(0xE5) && cmps14_cmd(0xE2)) {       // 0xE0, 0xE5, 0xE2 per datasheet
          Serial.println("Calibration profile ERASED. CMPS14 resetting...");
          lcd_print_lines("CALIB ERASED!", "CMPS14 RESET...");
          delay(500);                                                         // Wait for the sensor to boot
        } else {
          Serial.println("Erase FAILED!");
        }
        break;

      default:
        Serial.printf("Unknown command '%c'\n", cmd);
        break;
    }
  }

}

// ===== S E T U P ===== //
void setup() {

  Serial.begin(115200);
  delay(2000);
  Serial.println("--");
  delay(2000);
  Serial.println("PROGRAM START");

  Wire.begin(I2C_SDA, I2C_SCL);
  Wire.setClock(400000);
  delay(100);

  lcd_init_safe();
  delay(100);

  if (CMPS14_AUTOCAL_ON){
    if (cmps14_enable_background_cal(CMPS14_AUTOCAL_ON)) {             // Switch on CMPS14 autocalibration
      lcd_print_lines("AUTOCALIBRATION", "ENABLED");
    } else {
      lcd_print_lines("AUTOCALIBRATION", "FAILED");
    }
  } else {
    cmps14_cmd((uint8_t)0x80);
    lcd_print_lines("CALIBRATION", "MANUAL MODE");
    Serial.println("MANUAL CALIBRATION MODE");
  }

  delay(250);

  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false);
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  lcd_print_lines("WIFI", "CONNECT...");
  unsigned long t0 = millis();
  while (WiFi.status() != WL_CONNECTED && (millis() - t0) < WIFI_TIMEOUT_MS) { delay(250); } // Try to connect wifi until timeout
  if (WiFi.status() == WL_CONNECTED) {                                                       // Execute if wifi successfully connected
    make_source_from_mac();
    lcd_print_lines("WIFI OK", WiFi.localIP().toString().c_str());
    delay(250);

    int RSSIi = WiFi.RSSI();                                                                 // Wifi signal quality

    if (RSSIi > -55) {
      RSSIc = "EXCELLENT";
    }
    else if (RSSIi < -80) {
      RSSIc = "POOR";
    }
    else {
      RSSIc = "OK";
    }

    lcd_print_lines("SIGNAL LEVEL:", RSSIc.c_str());
    delay(250);

    // OTA
    ArduinoOTA.setHostname(SK_SOURCE.c_str());
    ArduinoOTA.setPassword(WIFI_PASS);
    ArduinoOTA.onStart([](){
      Serial.println("OTA upload starting...");
    });
    ArduinoOTA.onEnd([]() {
      Serial.println("OTA upload complete!");
    });
    ArduinoOTA.onProgress([](unsigned int progress, unsigned int total){
      Serial.printf("Progress: %u%%\r", (unsigned)(progress / (total /100)));
    });
    ArduinoOTA.onError([] (ota_error_t error) {
      Serial.printf("Error:[%u]: ", error);
      if (error == OTA_AUTH_ERROR) Serial.println("Authentication failed.");
      else if (error == OTA_BEGIN_ERROR) Serial.println("Begin failed.");
      else if (error == OTA_CONNECT_ERROR) Serial.println("Connect failed.");
      else if (error == OTA_RECEIVE_ERROR) Serial.println("Receive failed.");
      else if (error == OTA_END_ERROR) Serial.println("End failed.");
      else Serial.println("Unknown error.");
    });
    ArduinoOTA.begin();

    setup_ws_callbacks();                                             // Websocket
  } else {                                                            // No wifi, use only LCD output
    LCD_ONLY = true;
    lcd_print_lines("LCD ONLY MODE", "NO WIFI");
    delay(250);
  }

}

// ===== L O O P ===== //
void loop() {

  const unsigned long now = millis();                                 // Timestamp of this tick              

  if (!LCD_ONLY) {                                                    // Execute on each tick, except in LCD ONLY mode
    ArduinoOTA.handle();                                              // OTA
    ws.poll();                                                        // Keep websocket alive
    if (WiFi.status() != WL_CONNECTED && ws_open) {                   // Kill ghost websocket
      ws.close();
      ws_open = false;
    }
  }

  static unsigned long expn_retry_ms = WS_RETRY_MS;
  if (!LCD_ONLY && !ws_open && (long)(now - next_ws_try_ms) >= 0){     // Execute only on ticks when timer is due and only if websocket dropped and if not in LCD only mode
    ws.connect(SK_URL);
    next_ws_try_ms = now + expn_retry_ms;
    expn_retry_ms = min(expn_retry_ms * 2, WS_RETRY_MAX);
  }
  if (ws_open) expn_retry_ms = WS_RETRY_MS;

  bool success = readCompass();                                       // Read values from CMPS14 on each loop

  if (CMPS14_AUTOCAL_ON) {
    cmps14_monitor_and_store(CMPS14_AUTOCAL_ON);                      // Monitor and autosave CMPS14 autocalibration
  } else {
    readCalCommandFromSerial();                                       // Read and execute manual calibration commands from serial port
  }
  if (!LCD_ONLY && success) {                                         // If not in LCD ONLY mode and if read was successful, send values to SignalK paths
    send_batch_delta_if_needed();
    send_minmax_delta_if_due();
  }

  if (now - last_lcd_ms >= LCD_MS) {                                  // Execute only on the ticks when LCD timer is due
    last_lcd_ms = now;
    char buf[17];
    snprintf(buf, sizeof(buf), "      %03.0f%c", heading_deg, 223);
    lcd_print_lines("  HEADING (M):", buf);
  }
  
} 
