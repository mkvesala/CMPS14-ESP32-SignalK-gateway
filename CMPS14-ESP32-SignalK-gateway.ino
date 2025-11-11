#include <WiFi.h>
#include <WebServer.h>
#include <ArduinoJson.h>
#include <ArduinoWebsockets.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <ArduinoOTA.h>
#include <Preferences.h>
#include <esp_system.h>
#include "harmonic.h"
#include "secrets.h"

using namespace websockets;

// ESP32 WiFi and OTA settings
char SK_URL[512];                                 // URL of SignalK server
char SK_SOURCE[32];                               // ESP32 source name for SignalK, used also as the OTA hostname
char RSSIc[16];                                   // WiFi signal quality description
bool LCD_ONLY                   = false;          // True when no WiFi available, using only LCD output
const uint32_t WIFI_TIMEOUT_MS  = 90001;          // Try WiFi connection max 1.5 minutes

// CMPS14 I2C address and registers
const uint8_t CMPS14_ADDR       = 0x60;  // I2C address of CMPS14
const uint8_t REG_ANGLE_16_H    = 0x02;  // 16-bit angle * 10 (hi)
const uint8_t REG_ANGLE_16_L    = 0x03;  // 16-bit angle * 10 (lo)
const uint8_t REG_PITCH         = 0x04;  // signed degrees
const uint8_t REG_ROLL          = 0x05;  // signed degrees
const uint8_t REG_USEMODE       = 0x80;  // Command use-mode
const uint8_t REG_CAL_STATUS    = 0x1E;  // Calibration status
const uint8_t REG_SAVE1         = 0xF0;  // Series of commands to store calibration profile
const uint8_t REG_SAVE2         = 0xF5;
const uint8_t REG_SAVE3         = 0xF6;
const uint8_t REG_CAL1          = 0x98;  // Series of commands to start calibration
const uint8_t REG_CAL2          = 0x95;
const uint8_t REG_CAL3          = 0x99;
const uint8_t REG_RESET1        = 0xE0;  // Series of commands to reset CMPS14
const uint8_t REG_RESET2        = 0xE5;
const uint8_t REG_RESET3        = 0xE2; 
const uint8_t REG_AUTO_ON       = 0x93;  // Autosave byte of CMPS14
const uint8_t REG_AUTO_OFF      = 0x83;  // Autosave off
const uint8_t REG_ACK1          = 0x55;  // Ack (new firmware)
const uint8_t REG_ACK2          = 0x07;  // Ack (CMPS12 compliant)
const uint8_t REG_NACK          = 0xFF;  // Nack
const uint8_t REG_CMD           = 0x00;  // Command byte, write before sending other commands
const uint8_t REG_MASK          = 0x03;  // Mask to read individual calibration status bits for sys, acc, gyr, mag

// CMPS14 calibration
bool cmps14_cal_profile_stored        = false;  // Calibration profile stored flag
bool cmps14_factory_reset             = false;  // Factory reset flag
unsigned long last_cal_poll_ms        = 0;      // Calibration monitoring counters
uint8_t cal_ok_count                  = 0;      // Autocalibration save condition counter
const unsigned long CAL_POLL_MS       = 499;    // Autocalibration save condition timer
const uint8_t CAL_OK_REQUIRED         = 3;      // Autocalibration save condition threshold

// Three calibration modes + use mode
enum CalMode               : uint8_t { CAL_USE=0, CAL_FULL_AUTO=1, CAL_SEMI_AUTO=2, CAL_MANUAL=3 };
CalMode cal_mode_boot      = CAL_USE;
CalMode cal_mode_runtime   = CAL_USE; 
const char* calmode_str(CalMode m){
  switch(m){
    case CAL_FULL_AUTO: return "FULL AUTO";
    case CAL_SEMI_AUTO: return "AUTO";
    case CAL_MANUAL:    return "MANUAL";
    default:            return "USE";
  }
}    

// CMPS14 reading parameters
const float HEADING_ALPHA                 = 0.15f;                     // Smoothing factor 0...1, larger value less smoothing
float installation_offset_deg             = 0.0f;                      // Physical installation error of the compass module, configured via web UI
float dev_deg                             = 0.0f;                      // Deviation at heading_deg calculated by harmonic model
float magvar_manual_deg                   = 0.0f;                      // Variation that is set manually from web UI
float magvar_manual_rad                   = 0.0f;                      // Manual variation in rad
bool send_hdg_true                        = true;                      // By default, use magnetic variation to calculate and send headingTrue - user might switch this off via web UI
bool use_manual_magvar                    = true;                      // Use magvar_manual_deg if true
const unsigned long MIN_TX_INTERVAL_MS    = 149;                       // Max frequency for sending deltas to SignalK - prime number
const float DB_HDG_RAD                    = 0.005f;                    // ~0.29°: deadband threshold for heading
const float DB_ATT_RAD                    = 0.003f;                    // ~0.17°: pitch/roll deadband threshold
unsigned long last_minmax_tx_ms           = 0;
float last_sent_pitch_min                 = NAN;                       // Min and Max for pitch and roll
float last_sent_pitch_max                 = NAN;
float last_sent_roll_min                  = NAN;
float last_sent_roll_max                  = NAN;
const unsigned long MINMAX_TX_INTERVAL_MS = 997;                       // Frequency for pitch/roll maximum values sending - prime number
unsigned long last_lcd_ms                 = 0;
const unsigned long LCD_MS                = 1009;                      // Frequency to print on LCD in loop() - prime number
unsigned long last_read_ms                = 0;
const unsigned long READ_MS               = 67;                        // Frequency to read values from CMPS14 in loop() - prime number

// CMPS14 values in degrees for LCD and WebServer
float heading_deg       = NAN;
float pitch_deg         = NAN;
float roll_deg          = NAN;
float compass_deg       = NAN;
float heading_true_deg  = NAN;
float magvar_deg        = NAN;

// CMPS14 values in radians for SignalK server
float heading_rad       = NAN;
float heading_true_rad  = NAN;
float pitch_rad         = NAN;
float roll_rad          = NAN;
float pitch_min_rad     = NAN;
float pitch_max_rad     = NAN;
float roll_min_rad      = NAN;
float roll_max_rad      = NAN;
float magvar_rad        = NAN; // Value FROM SignalK navigation.magneticVariation path via subscribe json

// SH-ESP32 default pins for I2C
const uint8_t I2C_SDA = 16;
const uint8_t I2C_SCL = 17;

// SH-ESP32 led pins
const uint8_t LED_PIN = 2;

// Permanently stored preferences
Preferences prefs;

// Websocket
WebsocketsClient ws;
volatile bool ws_open = false;
unsigned long next_ws_try_ms = 0;
const unsigned long WS_RETRY_MS = 1999;
const unsigned long WS_RETRY_MAX = 19997;

// Webserver
WebServer server(80);

// I2C LCD 16x2
std::unique_ptr<LiquidCrystal_I2C> lcd;
bool lcd_present          = false;
char prev_top[17]  = "";                      // Previous value of top line
char prev_bot[17]  = "";                      // Previous value of bottom line
const uint8_t LCD_ADDR1   = 0x27;             // Scan both I2C addresses when init LCD
const uint8_t LCD_ADDR2   = 0x3F;

// Return float validity
inline bool validf(float x) { return !isnan(x) && isfinite(x); }

// Return shortest arc on 360° (for instance 359° to 001° is 2° not 358°)
inline float ang_diff_rad(float a, float b) {
  float d = a - b;
  while (d >  M_PI) d -= 2.0f * M_PI;
  while (d <= -M_PI) d += 2.0f * M_PI;
  return d;
}

// Create SignalK server URL
void build_sk_url() {
  if (strlen(SK_TOKEN) > 0)
    snprintf(SK_URL, sizeof(SK_URL), "ws://%s:%d/signalk/v1/stream?token=%s", SK_HOST, SK_PORT, SK_TOKEN);
  else
    snprintf(SK_URL, sizeof(SK_URL), "ws://%s:%d/signalk/v1/stream", SK_HOST, SK_PORT);
}

// Set SignalK source and OTA hostname (equal) based on ESP32 MAC address tail
void make_source_from_mac() {
  uint8_t m[6];
  WiFi.macAddress(m);
  snprintf(SK_SOURCE, sizeof(SK_SOURCE), "esp32.cmps14-%02x%02x%02x", m[3], m[4], m[5]);
}

// Description for WiFi signal level
void classify_rssi(int rssi) {
  const char* label =
      (rssi > -55) ? "EXCELLENT" :
      (rssi < -80) ? "POOR" : "OK";
  strncpy(RSSIc, label, sizeof(RSSIc) - 1);
  RSSIc[sizeof(RSSIc) - 1] = '\0';
}

// Websocket callbacks
void setup_ws_callbacks() {
  
  ws.onEvent([](WebsocketsEvent e, String){
    if (e == WebsocketsEvent::ConnectionOpened) {
      ws_open = true;
      if (!send_hdg_true) return;                             // Do nothing if user has switched off sending of navigation.headingTrue
      StaticJsonDocument<256> sub;                            // Otherwise, subscribe navigation.magneticVariation path from SignalK server
      sub["context"] = "vessels.self";
      auto subscribe = sub.createNestedArray("subscribe");
      auto s = subscribe.createNestedObject();
      s["path"] = "navigation.magneticVariation";
      s["format"] = "delta";
      s["policy"] = "ideal";
      s["period"] = 1000;                                     // Request ~1 Hz updates

      char buf[256];
      size_t n = serializeJson(sub, buf, sizeof(buf));
      ws.send(buf, n);
    }
    if (e == WebsocketsEvent::ConnectionClosed)  { ws_open = false; }
    if (e == WebsocketsEvent::GotPing)           { ws.pong(); }
  });

  ws.onMessage([](WebsocketsMessage msg){
    if (!send_hdg_true) return;                                // Do nothing if user has switched off sending of navigation.headingTrue and if data is not valid / found
    if (!msg.isText()) return;
    StaticJsonDocument<1024> d;
    if (deserializeJson(d, msg.data())) return;
    if (!d.containsKey("updates")) return;
    for (JsonObject up : d["updates"].as<JsonArray>()) {
      if (!up.containsKey("values")) continue;
      for (JsonObject v : up["values"].as<JsonArray>()) {
        if (!v.containsKey("path")) continue;
        const char* path = v["path"];
        if (!path) continue;
        if (strcmp(path, "navigation.magneticVariation") == 0) {
          if (v["value"].is<float>() || v["value"].is<double>()) {  
            float mv = v["value"].as<float>();
            if (validf(mv)) {                                  // If received valid value, set global variation value to this and stop using manual value
              magvar_rad = mv;
              use_manual_magvar = false;
              magvar_deg = magvar_rad * RAD_TO_DEG;
            } else use_manual_magvar = true;
          }
        }
      }
    }
  });

}

// Send batch of SignalK deltas but only if change exceeds the deadband limits (no unnecessary sending)
void send_batch_delta_if_needed() {
  
  if (LCD_ONLY || !ws_open) return;                                                                    // execute only if WiFi and Websocket ok
  if (!validf(heading_rad) || !validf(pitch_rad) || !validf(roll_rad)) return;                         // execute only if values are valid

  static unsigned long last_tx_ms = 0;
  const unsigned long now = millis();
  if (now - last_tx_ms < MIN_TX_INTERVAL_MS) {
    return;                                                                                            // Timer for sending to SignalK
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

  if (!(changed_h || changed_p || changed_r)) return;                                                 // Exit if values have not changed

  StaticJsonDocument<512> doc;
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

  if (changed_h) add("navigation.headingMagnetic", last_h);     // SignalK paths and values
  if (changed_p) add("navigation.attitude.pitch",  last_p);
  if (changed_r) add("navigation.attitude.roll",   last_r);
  if (changed_h && send_hdg_true) {                             // Send navigation.headingTrue unless user has not switched this off
    float mv_rad = use_manual_magvar ? magvar_manual_rad : magvar_rad;
    auto wrap2pi = [](float r){ while (r < 0) r += 2.0f*M_PI; while (r >= 2.0f*M_PI) r -= 2.0f*M_PI; return r; };
    heading_true_rad = wrap2pi(last_h + mv_rad);
    heading_true_deg = heading_true_rad * RAD_TO_DEG;
    add("navigation.headingTrue", heading_true_rad);
  }

  if (values.size() == 0) return;

  char buf[640];
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
  if (LCD_ONLY || !ws_open) return;                                                 // execute only if WiFi and Websocket ok

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

  StaticJsonDocument<512> doc;
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

  if (ch_pmin) add("navigation.attitude.pitch.min", pitch_min_rad);       // SignalK paths and values
  if (ch_pmax) add("navigation.attitude.pitch.max", pitch_max_rad);
  if (ch_rmin) add("navigation.attitude.roll.min",  roll_min_rad);
  if (ch_rmax) add("navigation.attitude.roll.max",  roll_max_rad);

  char buf[640];
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

// Compass deviation harmonic model for harmonic.h
const float headings_deg[8] = { 0, 45, 90, 135, 180, 225, 270, 315 }; // Cardinal and intercardinal directions N, NE, E, SE, S, SW, W, NE in deg
float dev_at_card_deg[8] = { 0,0,0,0,0,0,0,0 };                       // Measured deviations (deg) in cardinal and intercardinal directions given by user via Web UI
HarmonicCoeffs hc {0,0,0,0,0};                                        // Five coeffs to calculate full deviation curve

// Debug print deviation table every 10°
void print_deviation_table_10deg() {
  Serial.println(F("=== Deviation table every 10 deg ==="));
  for (int h = 0; h <= 360; h += 10) {
    float dev = deviation_harm_deg(hc, (float)h); 
    Serial.printf("%03d,%+6.2f\n", h, dev);
  }
  Serial.println();
}

// Read values from CMPS14 compass and attitude sensor
bool read_compass(){
  
  Wire.beginTransmission(CMPS14_ADDR);
  Wire.write(REG_ANGLE_16_H);
  if (Wire.endTransmission(false) != 0) return false;

  const uint8_t toRead = 4;
  uint8_t n = Wire.requestFrom(CMPS14_ADDR, toRead);
  if (n != toRead) return false;

  uint8_t hi   = Wire.read();
  uint8_t lo   = Wire.read();
  int8_t pitch = (int8_t)Wire.read();         // Pitch and roll are -90°...90°
  int8_t roll  = (int8_t)Wire.read();

  uint16_t ang10 = ((uint16_t)hi << 8) | lo;  // 0..3599 (0.1°)
  float raw_deg = ((float)ang10) / 10.0f;     // 0..359.9° - raw value from CMPS14
  
  raw_deg += installation_offset_deg;         // Correct raw deg with physical installation error if such defined by user
  if (raw_deg >= 360.0f) raw_deg -= 360.0f;
  if (raw_deg <    0.0f) raw_deg += 360.0f;

  if (isnan(compass_deg)) {                   // If 1st iteration, set compass deg without any smoothing
    compass_deg = raw_deg;
  } else {                                    // Otherwise, let's apply smoothing factor to set compass deg
    float diff = raw_deg - compass_deg;
    if (diff > 180.0f)  diff -= 360.0f;       // Ensure shortest arc
    if (diff < -180.0f) diff += 360.0f;
    compass_deg += HEADING_ALPHA * diff;      // Compass deg = raw deg + offset + smoothing
    if (compass_deg >= 360.0f) compass_deg -= 360.0f;
    if (compass_deg < 0.0f)   compass_deg += 360.0f;
  }
 
  dev_deg = deviation_harm_deg(hc, compass_deg);  // Get the deviation deg for current compass deg - harmonic model
  heading_deg = compass_deg + dev_deg;            // Magnetic deg = compass deg + deviation
  if (heading_deg < 0) heading_deg += 360.0f;
  if (heading_deg >= 360.0f) heading_deg -= 360.0f;

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
  if (i2c_device_present(LCD_ADDR1)) addr = LCD_ADDR1;        // Scan both I2C addresses
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

// Helper for safe LCD printing
static inline void copy16(char* dst, const char* src) {
  strncpy(dst, src, 16);   // copy max 16 characters
  dst[16] = '\0';          // ensure 0 termination
}

// LCD basic printing on two lines
void lcd_print_lines(const char* l1, const char* l2) {
  if (!lcd_present) return;
  if (!strcmp(prev_top, l1) && !strcmp(prev_bot, l2)) return; // If content not changed, do nothing - less blinking

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

// LED indicator for calibration mode, blue led at GPIO2
void led_update_by_cal_mode(){
  static unsigned long last = 0;
  static bool state = false;
  const unsigned long now = millis();

  switch (cal_mode_runtime){
    case CAL_USE:
      digitalWrite(LED_PIN, HIGH);                    // blue led on continuously
      return;
    case CAL_FULL_AUTO: {
      const unsigned long toggle_ms = 500;
      if (now - last >= toggle_ms) {
        state = !state;
        digitalWrite(LED_PIN, state ? HIGH : LOW);    // blue led blinks on 1 hz frequency
        last = now;
      }
      break;
    }
    case CAL_SEMI_AUTO:
    case CAL_MANUAL: {
      const unsigned long toggle_ms = 100;
      if (now - last >= toggle_ms) {
        state = !state;
        digitalWrite(LED_PIN, state ? HIGH : LOW);    // blue led blinks on 5 hz frequency
        last = now;
      }
      break;
    }
    default:
      digitalWrite(LED_PIN, LOW);                     // blue led off
      break;
  }
}

// Send a command to CMPS14
bool cmps14_cmd(uint8_t cmd) {
  Wire.beginTransmission(CMPS14_ADDR);
  Wire.write(REG_CMD);
  Wire.write(cmd);
  if (Wire.endTransmission() != 0) return false;
  delay(25);  // Delay of 20 ms recommended on CMPS14 datasheet

  Wire.requestFrom(CMPS14_ADDR, (uint8_t)1);
  if (Wire.available() < 1) return false;
  uint8_t b = Wire.read();
  if (b == REG_ACK1 || b == REG_ACK2) return true;
  return false;
}

// Enable autocalibration with optional autosave
bool cmps14_enable_background_cal(bool autosave) {
  if (!cmps14_cmd(REG_CAL1)) return false;
  if (!cmps14_cmd(REG_CAL2)) return false;
  if (!cmps14_cmd(REG_CAL3)) return false;
  const uint8_t cfg = autosave ? REG_AUTO_ON : REG_AUTO_OFF;
  if (!cmps14_cmd(cfg)) return false;
  cmps14_factory_reset = false;                     // We are not anymore in resetted mode
  return true;
}

// Read calibration status
uint8_t cmps14_read_cal_status() {
  Wire.beginTransmission(CMPS14_ADDR);
  Wire.write(REG_CAL_STATUS);
  if (Wire.endTransmission(false) != 0) return REG_NACK;
  Wire.requestFrom(CMPS14_ADDR, (uint8_t)1);
  if (Wire.available() < 1) return REG_NACK;
  uint8_t b = Wire.read();
  return b;
}

// Save calibration AND stop calibrating
bool cmps14_store_profile() {
  if (!cmps14_cmd(REG_SAVE1)) return false;      // Sequence of storing the full calibration profile
  if (!cmps14_cmd(REG_SAVE2)) return false;
  if (!cmps14_cmd(REG_SAVE3)) return false;
  if (!cmps14_cmd(REG_USEMODE)) return false;    // Switch on use mode which exits calibration
  cmps14_cal_profile_stored = true;
  cmps14_factory_reset = false;                  // We are not anymore in resetted status
  return true;
}

// Monitor and optional storing of the calibration profile
void cmps14_monitor_and_store(bool save) {
  const unsigned long now = millis();
  if (now - last_cal_poll_ms < CAL_POLL_MS) return;
  last_cal_poll_ms = now;
  uint8_t st = cmps14_read_cal_status(); 
  if (st == REG_NACK) return;
  uint8_t mag   = (st     ) & REG_MASK;
  uint8_t accel = (st >> 2) & REG_MASK;
  uint8_t gyro  = (st >> 4) & REG_MASK;
  uint8_t sys   = (st >> 6) & REG_MASK;

  if (sys >= 2 && accel == 3 && mag == 3) {       // Require that SYS is 2, ACC is 3 and MAG is 3 - omit GYR as there's a firmware bug
    if (cal_ok_count < 255) cal_ok_count++;
  } else {
    cal_ok_count = 0;
  }
 
  if (save && !cmps14_cal_profile_stored && cal_ok_count >= CAL_OK_REQUIRED) { // When over threshold, save the calibration profile automatically
    if (cmps14_store_profile()) {
      lcd_print_lines("CALIBRATION", "SAVED");
      cal_mode_runtime = CAL_USE;
    } else {
      lcd_print_lines("CALIBRATION", "NOT SAVED");
    }
  }
}

// Start calibration in manual mode
bool start_calibration_manual_mode() {
  if (!cmps14_enable_background_cal(false)) return false;
  cal_mode_runtime = CAL_MANUAL;
  return true;
}

// Start calibration in full auto mode
bool start_calibration_fullauto() {
  if (!cmps14_enable_background_cal(true)) return false;
  cal_mode_runtime = CAL_FULL_AUTO;
  return true;
}

// Start calibration in semi auto mode
bool start_calibration_semiauto(){
  if (!cmps14_enable_background_cal(false)) return false;
  cal_ok_count = 0;
  cal_mode_runtime = CAL_SEMI_AUTO;
  return true;
}

// Stop all calibration
bool stop_calibration() {
  if (!cmps14_cmd(REG_USEMODE)) return false; 
  cal_mode_runtime = CAL_USE;
  return true;
}

// Web UI handler for CALIBRATE button
void handle_calibrate_on(){
  if (start_calibration_manual_mode()) {
    // ok
  }
  handle_root();
}

// Web UI handler for STOP button
void handle_calibrate_off(){
  if (stop_calibration()) {
    // ok
  }
  handle_root();
}

// Web UI handler for SAVE button
void handle_store(){
  if (cmps14_store_profile()) {
    cal_mode_runtime = CAL_USE;
  }
  handle_root();
}

// Web UI handler for RESET button
void handle_reset(){
  if (cmps14_cmd(REG_RESET1) && cmps14_cmd(REG_RESET2) && cmps14_cmd(REG_RESET3)) {
    delay(600);                   // Wait 600 ms for the sensor to boot
    cmps14_cmd(REG_USEMODE);      // Use mode
    cal_mode_runtime = CAL_USE;
    cal_mode_boot = CAL_USE;
    cmps14_cal_profile_stored = false;
    cmps14_factory_reset = true;
    pitch_min_rad = pitch_max_rad = roll_min_rad = roll_max_rad = NAN;    // Reset the pitch/roll min and max values
    last_sent_pitch_min = last_sent_pitch_max = last_sent_roll_min = last_sent_roll_max = NAN;
  }
  handle_root(); 
}

// Web UI handler for status block, build json with appropriate data
void handle_status() {
  
  uint8_t st = cmps14_read_cal_status();
  uint8_t mag = 255, acc = 255, gyr = 255, sys = 255;
  if (st != REG_NACK) {
    mag =  (st     ) & REG_MASK;
    acc =  (st >> 2) & REG_MASK;
    gyr =  (st >> 4) & REG_MASK;
    sys =  (st >> 6) & REG_MASK;
  }
 
  float variation_deg = use_manual_magvar ? magvar_manual_deg : magvar_deg;

  char ipChar[16];

  if (WiFi.isConnected()) {
    IPAddress ip = WiFi.localIP();
    snprintf(ipChar, sizeof(ipChar), "%u.%u.%u.%u", ip[0], ip[1], ip[2], ip[3]);
  } else snprintf(ipChar, sizeof(ipChar), "DISCONNECTED");

  StaticJsonDocument<1024> doc;

  doc["cal_mode"]             = calmode_str(cal_mode_runtime);
  doc["cal_mode_boot"]        = calmode_str(cal_mode_boot);
  doc["wifi"]                 = ipChar;
  doc["rssi"]                 = RSSIc;
  doc["hdg_deg"]              = heading_deg;
  doc["compass_deg"]          = compass_deg;
  doc["pitch_deg"]            = pitch_deg;
  doc["roll_deg"]             = roll_deg;
  doc["offset"]               = installation_offset_deg;
  doc["dev"]                  = dev_deg;
  doc["variation"]            = variation_deg;
  doc["heading_true_deg"]     = heading_true_deg;
  doc["acc"]                  = acc;
  doc["mag"]                  = mag;
  doc["sys"]                  = sys;
  doc["hca"]                  = hc.A;
  doc["hcb"]                  = hc.B;
  doc["hcc"]                  = hc.C;
  doc["hcd"]                  = hc.D;
  doc["hce"]                  = hc.E;
  doc["use_manual_magvar"]    = use_manual_magvar;   
  doc["send_hdg_true"]        = send_hdg_true;         
  doc["stored"]               = cmps14_cal_profile_stored; 
  doc["factory_reset"]        = cmps14_factory_reset;

  server.sendHeader("Cache-Control", "no-cache, no-store, must-revalidate");
  server.sendHeader("Pragma", "no-cache");
  server.sendHeader("Expires", "0");

  char out[1024];
  size_t n = serializeJson(doc, out, sizeof(out));
  server.send(200, "application/json; charset=utf-8", out);
}

// Web UI handler for installation offset, to correct raw compass heading
void handle_set_offset() {
  if (server.hasArg("v")) {
    float v = server.arg("v").toFloat();
    if (!validf(v)) v = 0.0f;
    if (v < -180.0f) v = -180.0f;
    if (v >  180.0f) v =  180.0f;

    installation_offset_deg = v;

    // Save prefrences permanently
    prefs.begin("cmps14", false);
    prefs.putFloat("offset_deg", installation_offset_deg);
    prefs.end();

    char line2[17];
    snprintf(line2, sizeof(line2), "SET: %6.1f%c", installation_offset_deg, 223);
    lcd_print_lines("INSTALL OFFSET", line2);
  }
  handle_root();
}

// Web UI handler for 8 measured deviation values, to correct raw compass heading --> navigation.headingMagnetic
void handle_dev8_set() {
  auto getf = [&](const char* k) -> float {
    if (!server.hasArg(k)) return 0.0f;
    float v = server.arg(k).toFloat();
    if (!validf(v)) v = 0.0f;
    if (v < -90.0f) v = -90.0f;
    if (v >  90.0f) v =  90.0f;
    return v;
  };

  dev_at_card_deg[0] = getf("N");
  dev_at_card_deg[1] = getf("NE");
  dev_at_card_deg[2] = getf("E");
  dev_at_card_deg[3] = getf("SE");
  dev_at_card_deg[4] = getf("S");
  dev_at_card_deg[5] = getf("SW");
  dev_at_card_deg[6] = getf("W");
  dev_at_card_deg[7] = getf("NW");

  hc = fit_harmonic_from_8(headings_deg, dev_at_card_deg); // Calculate 5 coeffs

  print_deviation_table_10deg();  // Debug

  prefs.begin("cmps14", false);
  for (int i = 0; i < 8; i++) {
    prefs.putFloat((String("dev") + String(i)).c_str(), dev_at_card_deg[i]);
  }
  prefs.putFloat("hc_A", hc.A);
  prefs.putFloat("hc_B", hc.B);
  prefs.putFloat("hc_C", hc.C);
  prefs.putFloat("hc_D", hc.D);
  prefs.putFloat("hc_E", hc.E);
  prefs.end();

  lcd_print_lines("DEVIATION TABLE", "SAVED");

  handle_root();
}

// Web UI handler to choose calibration mode on boot
void handle_calmode_set() {
  if (server.hasArg("calmode")) {
    String m = server.arg("calmode");
    CalMode v = CAL_USE;
    if (m == "full")         v = CAL_FULL_AUTO;
    else if (m == "semi")    v = CAL_SEMI_AUTO;
    else if (m == "manual")  v = CAL_MANUAL;
    else                     v = CAL_USE;
    cal_mode_boot = v;
    prefs.begin("cmps14", false);
    prefs.putUChar("cal_mode_boot", (uint8_t)v);
    prefs.end();
    lcd_print_lines("CAL MODE (BOOT)", calmode_str(v));
  }
  handle_root();
}

// Web UI handler to set magnetic variation manually. Used automatically if SignalK server navigation.magneticVariation is not available
void handle_magvar_set() {
  
  if (server.hasArg("v")) {
    float v = server.arg("v").toFloat();
    if (!validf(v)) v = 0.0f;
    if (v < -90.0f) v = -90.0f;
    if (v >  90.0f) v =  90.0f;
    magvar_manual_deg = v;
    magvar_manual_rad = magvar_manual_deg * DEG_TO_RAD;

    prefs.begin("cmps14", false);
    prefs.putFloat("mv_man_deg", magvar_manual_deg);
    prefs.end();

    char line2[17];
    snprintf(line2, sizeof(line2), "SET: %5.1f%c %c", fabs(magvar_manual_deg), 223, (magvar_manual_deg >= 0 ? 'E':'W'));
    lcd_print_lines("MAG VARIATION", line2);
  }
  handle_root();
}

// Web UI handler to set heading mode TRUE or MAGNETIC
void handle_heading_mode() {
  if (server.hasArg("mode")) {
    String mode = server.arg("mode");
    send_hdg_true = (mode == "true");
  }

  prefs.begin("cmps14", false);
  prefs.putBool("send_hdg_true", send_hdg_true);
  prefs.end();

  lcd_print_lines("HEADING MODE", send_hdg_true ? "TRUE" : "MAGNETIC");
  handle_root();
}

// Web UI handler for the HTML page (memory-friendly streaming version instead of large String handling)
void handle_root() {

  server.setContentLength(CONTENT_LENGTH_UNKNOWN);
  server.sendHeader("Connection", "close");
  server.sendHeader("Cache-Control", "no-cache, no-store, must-revalidate");
  server.sendHeader("Pragma", "no-cache");
  server.sendHeader("Expires", "0");
  server.send(200, "text/html; charset=utf-8", "");

  // Head and CSS
  server.sendContent_P(R"(
    <!DOCTYPE html><html><head><meta charset="utf-8"><meta name="viewport" content="width=device-width, initial-scale=1"><link rel="icon" href="data:,"><style>
    html { font-family: Helvetica; display: inline-block; margin: 0 auto; text-align: center;}
    body { background:#000; color:#fff; }
    .button { background-color: #00A300; border: none; color: white; padding: 6px 20px; text-decoration: none; font-size: 3vmin; max-font-size: 24px; min-font-size: 10px; margin: 2px; cursor: pointer; border-radius:6px; text-align:center}
    .button2 { background-color: #D10000; }
    .card { width:92%; margin:2px auto; padding:4px; background:#0b0b0b; border-radius:6px; box-shadow:0 0 0 1px #222 inset; }
    h1 { margin:12px 0 8px 0; } h2 { margin:8px 0; font-size: 4vmin; max-font-size: 16px; min-font-size: 10px; } h3 { margin:6px 0; font-size: 3vmin; max-font-size: 14px; min-font-size: 8px; }
    label { display:inline-block; min-width:40px; text-align:right; margin-right:6px; }
    input[type=number]{ font-size: 3vmin; max-font-size: 14px; min-font-size: 8px; width:60px; padding:4px 6px; margin:4px; border-radius:6px; border:1px solid #333; background:#111; color:#fff; }
    #st { font-size: 2vmin; max-font-size: 18px; min-font-size: 6px; line-height: 1.2; color: #DBDBDB; background-color: #000; padding: 8px; border-radius: 8px; width: 90%; margin: auto; text-align: center; white-space: pre-line; font-family: monospace;}
    </style></head><body>
    <h2><a href="/" style="color:white; text-decoration:none;">CMPS14 CONFIG</a></h2>
    )");

  // DIV Calibrate, Stop, Reset
  server.sendContent_P(R"(
    <div class='card' id='controls'>)");
  {
    char buf[64];
    snprintf(buf, sizeof(buf), "<p>Mode: %s</p><p>", calmode_str(cal_mode_runtime));
    server.sendContent(buf);
  }

  if (cal_mode_runtime == CAL_SEMI_AUTO || cal_mode_runtime == CAL_MANUAL) {
    server.sendContent_P(R"(<a href="/cal/off"><button class="button button2">STOP</button></a>)");
    if (!cmps14_cal_profile_stored) {
      server.sendContent_P(R"(<a href="/store/on"><button class="button">SAVE</button></a>)");
    } else {
      server.sendContent_P(R"(<a href="/store/on"><button class="button button2">REPLACE</button></a>)");
    }
  } else if (cal_mode_runtime == CAL_USE) {
    server.sendContent_P(R"(<a href="/cal/on"><button class="button">CALIBRATE</button></a>)");
  }
  server.sendContent_P(R"(<a href="/reset/on"><button class="button button2">RESET</button></a></p></div>)");

  // DIV Calibration mode on boot
  server.sendContent_P(R"(
    <div class='card'>
    <form action="/calmode/set" method="get">
    <label>Mode </label><label><input type="radio" name="calmode" value="full")");
    { if (cal_mode_boot == CAL_FULL_AUTO) server.sendContent_P(R"( checked)"); }
    server.sendContent_P(R"(>Full auto </label><label>
    <input type="radio" name="calmode" value="semi")");
    { if (cal_mode_boot == CAL_SEMI_AUTO) server.sendContent_P(R"( checked)"); }
    server.sendContent_P(R"(>Auto </label><label>
    <input type="radio" name="calmode" value="use")");
    { if (cal_mode_boot == CAL_USE) server.sendContent_P(R"( checked)"); }
    server.sendContent_P(R"(>Manual</label>
    <input type="submit" class="button" value="SAVE"></form></div>)");

  // DIV Set installation offset
  server.sendContent_P(R"(
    <div class='card'>
    <form action="/offset/set" method="get">
    <label>Installation offset</label>
    <input type="number" name="v" step="1" min="-180" max="180" value=")");
      {
        char buf[32];
        snprintf(buf, sizeof(buf), "%.0f", installation_offset_deg);
        server.sendContent(buf);
      }
  server.sendContent_P(R"("><input type="submit" value="SAVE" class="button"></form></div>)");

  // DIV Set deviation 
  server.sendContent_P(R"(
    <div class='card'>
    <form action="/dev8/set" method="get"><div>)");

  // Row 1: N NE
  {
    char row1[256];
    snprintf(row1, sizeof(row1),
      "<label>N</label><input name=\"N\"  type=\"number\" step=\"1\" value=\"%.0f\">"
      "<label>NE</label><input name=\"NE\" type=\"number\" step=\"1\" value=\"%.0f\">",
      dev_at_card_deg[0], dev_at_card_deg[1]);
    server.sendContent(row1);
  }
  server.sendContent_P(R"(</div><div>)");

  // Row 2:  E SE
  {
  char row2[256];
    snprintf(row2, sizeof(row2),
      "<label>E</label><input name=\"E\"  type=\"number\" step=\"1\" value=\"%.0f\">"
      "<label>SE</label><input name=\"SE\" type=\"number\" step=\"1\" value=\"%.0f\">",
      dev_at_card_deg[2], dev_at_card_deg[3]);
    server.sendContent(row2);
  }
  server.sendContent_P(R"(</div><div>)");

  // Row 3: S SW
  {
  char row3[256];
    snprintf(row3, sizeof(row3),
      "<label>S</label><input name=\"S\"  type=\"number\" step=\"1\" value=\"%.0f\">"
      "<label>SW</label><input name=\"SW\" type=\"number\" step=\"1\" value=\"%.0f\">",
      dev_at_card_deg[4], dev_at_card_deg[5]);
    server.sendContent(row3);
  }
  server.sendContent_P(R"(</div><div>)");

  // Row 4: W NW
  {
    char row4[256];
    snprintf(row4, sizeof(row4),
      "<label>W</label><input name=\"W\"  type=\"number\" step=\"1\" value=\"%.0f\">"
      "<label>NW</label><input name=\"NW\" type=\"number\" step=\"1\" value=\"%.0f\">",
      dev_at_card_deg[6], dev_at_card_deg[7]);
    server.sendContent(row4);
  }

  server.sendContent_P(R"(
    </div>
    <label>Deviation table</label>
    <input type="submit" class="button" value="SAVE"></form></div>)");

  // DIV Set variation 
  server.sendContent_P(R"(
    <div class='card'>
    <form action="/magvar/set" method="get">
    <label>Manual variation</label>
    <input type="number" name="v" step="1" min="-180" max="180" value=")");
    {
      char buf[32];
      snprintf(buf, sizeof(buf), "%.0f", magvar_manual_deg);
      server.sendContent(buf);
    }
  server.sendContent_P(R"("><input type="submit" value="SAVE" class="button"></form></div>)");

  // DIV Set heading mode TRUE or MAGNETIC
  server.sendContent_P(R"(
    <div class='card'>
    <form action="/heading/mode" method="get">
    <label>Use heading</label><label><input type="radio" name="mode" value="true")");
    { if (send_hdg_true) server.sendContent_P(R"( checked)"); }
    server.sendContent_P(R"(>True</label><label>
    <input type="radio" name="mode" value="mag")");
    { if (!send_hdg_true) server.sendContent_P(R"( checked)"); }
    server.sendContent_P(R"(>Magnetic</label>
    <input type="submit" class="button" value="SAVE"></form></div>)");

  // DIV Status
  server.sendContent_P(R"(<div class='card'><div id="st">Loading...</div></div>)");

  // Live JS updater script
  server.sendContent_P(R"(
    <script>
      function fmt0(x) {
        return (x === null || x === undefined || Number.isNaN(x)) ? 'NA' : x.toFixed(0);
      }
      function fmt1(x) {
        return (x === null || x === undefined || Number.isNaN(x)) ? 'NA' : x.toFixed(1);
      }
      function renderControls(j) {
        const el = document.getElementById('controls');
        if (!el || !j) return;
        let html = '';
        html += `<p>Mode: ${j.cal_mode}</p><p>`;
        if (j.cal_mode === 'AUTO' || j.cal_mode === 'MANUAL') {
          html += `<a href="/cal/off"><button class="button button2">STOP</button></a>`;
          if (j.stored) {
            html += `<a href="/store/on"><button class="button button2">REPLACE</button></a>`;
          } else {
            html += `<a href="/store/on"><button class="button">SAVE</button></a>`;
          }
        } else if (j.cal_mode === 'USE') {
          html += `<a href="/cal/on"><button class="button">CALIBRATE</button></a>`;
        }
        html += `<a href="/reset/on"><button class="button button2">RESET</button></a></p>`;
        el.innerHTML = html;
      }
      function upd(){
        fetch('/status').then(r=>r.json()).then(j=>{
          const d=[
            'Installation offset: '+fmt0(j.offset)+'\u00B0',
            'Heading (C): '+fmt0(j.compass_deg)+'\u00B0',
            'Deviation: '+fmt0(j.dev)+'\u00B0',
            'Heading (M): '+fmt0(j.hdg_deg)+'\u00B0',
            'Variation: '+fmt0(j.variation)+'\u00B0',
            'Heading (T): '+fmt0(j.heading_true_deg)+'\u00B0',
            'Pitch: '+fmt1(j.pitch_deg)+'\u00B0'+' Roll: '+fmt1(j.roll_deg)+'\u00B0',
            'Acc: '+j.acc+', Mag: '+j.mag+', Sys: '+j.sys,
            'HcA: '+fmt1(j.hca)+', HcB: '+fmt1(j.hcb)+', HcC: '+fmt1(j.hcc)+', HcD: '+fmt1(j.hcd)+', HcE: '+fmt1(j.hce),
            'WiFi: '+j.wifi+' ('+j.rssi+')',
            'Calibration mode on boot: '+j.cal_mode_boot,
            'Use manual variation: '+j.use_manual_magvar,
            'Send true heading: '+j.send_hdg_true,
            'Factory reset: '+j.factory_reset,
            'Calibration saved since boot: '+j.stored
          ];
          document.getElementById('st').textContent=d.join('\n');
          renderControls(j);
        }).catch(_=>{
          document.getElementById('st').textContent='Status fetch failed';
        });
      }
      setInterval(upd,1000);upd();
    </script>)");
  server.sendContent_P(R"(
    <div class='card'>
    <a href="/deviationdetails"><button class="button">SHOW DEVIATION CURVE</button></a></div>
    </body>
    </html>)");
  server.sendContent_P(R"(
    <div class='card'>
    <a href="/restart?ms=5000"><button class="button button2">RESTART ESP32</button></a></div>
    </body>
    </html>)");
  server.sendContent("");
}

// WebUI handler to draw deviation table and deviation curve
void handle_deviation_details(){
  server.setContentLength(CONTENT_LENGTH_UNKNOWN);
  server.sendHeader("Connection", "close");
  server.sendHeader("Cache-Control", "no-cache, no-store, must-revalidate");
  server.sendHeader("Pragma", "no-cache");
  server.sendHeader("Expires", "0");
  server.send(200, "text/html; charset=utf-8", "");
  server.sendContent_P(R"(
    <!DOCTYPE html><html><head><meta charset="utf-8">
    <meta name="viewport" content="width=device-width, initial-scale=1">
    <link rel="icon" href="data:,">
    <title>Deviation details</title>
    <style>
      body{background:#000;color:#fff;font-family:Helvetica;margin:0 auto;text-align:center}
      .card{width:92%;margin:8px auto;padding:8px;background:#0b0b0b;border-radius:6px;box-shadow:0 0 0 1px #222 inset}
      table{margin:8px auto;border-collapse:collapse;color:#ddd}
      td,th{border:1px solid #333;padding:4px 8px}
      a{color:#fff}
    </style>
    </head><body>
    <h2><a href="/">CMPS14 CONFIG</a> — Deviation details</h2>
    <div class="card">
  )");

  // SVG asetukset
  const int W=800, H=320;
  const float xpad=40, ypad=20;
  const float xmin=0, xmax=360;
  // y-akseli ±max |dev|. Otetaan 10° marginaali, mutta rajoitetaan minimiin 5°
  float ymax = 0.0f;
  for (int d=0; d<=360; ++d){
    float v = deviation_harm_deg(hc, (float)d);
    if (fabs(v) > ymax) ymax = fabs(v);
  }
  ymax = max(ymax + 1.0f, 5.0f); // varaa vähän tilaa

  auto xmap = [&](float x){ return xpad + (x - xmin) * ( (W-2*xpad) / (xmax-xmin) ); };
  auto ymap = [&](float y){ return H-ypad - (y + ymax) * ( (H-2*ypad) / (2*ymax) ); };

  // Akselit ja ruudukko
  char buf[160];
  server.sendContent_P(R"(<svg width="100%" viewBox="0 0 )");
  snprintf(buf, sizeof(buf), "%d %d", W, H);
  server.sendContent(buf);
  server.sendContent_P(R"(" preserveAspectRatio="xMidYMid meet" style="background:#000">
    <rect x="0" y="0" width="100%" height="100%" fill="#000"/>
  )");

  // X-akseli
  snprintf(buf, sizeof(buf),
    "<line x1=\"%.1f\" y1=\"%.1f\" x2=\"%.1f\" y2=\"%.1f\" stroke=\"#444\"/>",
    xmap(xmin), ymap(0), xmap(xmax), ymap(0));
  server.sendContent(buf);

  // Y-akseli keskellä (0° ja 360° kohdalla merkkejä ei aina tarvita)
  snprintf(buf, sizeof(buf),
    "<line x1=\"%.1f\" y1=\"%.1f\" x2=\"%.1f\" y2=\"%.1f\" stroke=\"#444\"/>",
    xmap(0), ymap(-ymax), xmap(0), ymap(ymax));
  server.sendContent(buf);

  // Ruudukko ja x-tickit 0, 45, 90, ..., 360
  for (int k=0;k<=360;k+=45){
    float X = xmap(k);
    snprintf(buf,sizeof(buf),
      "<line x1=\"%.1f\" y1=\"%.1f\" x2=\"%.1f\" y2=\"%.1f\" stroke=\"#222\"/>",
      X, ymap(-ymax), X, ymap(ymax));
    server.sendContent(buf);
    snprintf(buf,sizeof(buf),
      "<text x=\"%.1f\" y=\"%.1f\" fill=\"#aaa\" font-size=\"12\" text-anchor=\"middle\">%03d</text>",
      X, ymap(-ymax)-4, k);
    server.sendContent(buf);
  }

  // Y-tickit -ymax..ymax 1° välein ei kannata; tehdään esim. 1° välein labelit  –5…+5 (riippuen ymaxista)
  for (int j=(int)ceil(-ymax); j<= (int)floor(ymax); j++){
    float Y = ymap(j);
    snprintf(buf,sizeof(buf),
      "<line x1=\"%.1f\" y1=\"%.1f\" x2=\"%.1f\" y2=\"%.1f\" stroke=\"#222\"/>",
      xmap(xmin), Y, xmap(xmax), Y);
    server.sendContent(buf);
    snprintf(buf,sizeof(buf),
      "<text x=\"%.1f\" y=\"%.1f\" fill=\"#aaa\" font-size=\"12\" text-anchor=\"end\">%+d°</text>",
      xmap(xmin)-6, Y+4, j);
    server.sendContent(buf);
  }

  // Polyline: dev(h) 1° välein
  server.sendContent_P(R"(<polyline fill="none" stroke="#0af" stroke-width="2" points=")");
  for (int d=0; d<=360; ++d){
    float X=xmap((float)d);
    float Y=ymap(deviation_harm_deg(hc,(float)d));
    snprintf(buf,sizeof(buf),"%.1f,%.1f ",X,Y);
    server.sendContent(buf);
  }
  server.sendContent_P(R"("/>)");

  server.sendContent_P(R"(</svg></div>)");

  // TAULUKKO 10° välein
  server.sendContent_P(R"(<div class="card"><h3>Deviation table (every 10°)</h3><table><tr><th>Compass</th><th>Deviation</th></tr>)");
  for (int d=0; d<=360; d+=10){
    float v = deviation_harm_deg(hc, (float)d);
    snprintf(buf,sizeof(buf),"<tr><td>%03d\u00B0</td><td>%+.2f\u00B0</td></tr>", d, v);
    server.sendContent(buf);
  }
  server.sendContent_P(R"(</table></div>)");

  server.sendContent_P(R"(<p style="margin:20px;"><a href="/">Back</a></p></body></html>)");
  server.sendContent("");
}

// Web UI handler for software restart of ESP32
void handle_restart() {
  uint32_t ms = 3000;
  if (server.hasArg("ms")){
    long v = server.arg("ms").toInt();
    if (v > ms && v < 20000) ms = (uint32_t)v;
  }

  char line2[17];
  snprintf(line2, sizeof(line2), "IN %5lu ms", (unsigned long)ms);
  lcd_print_lines("RESTARTING...", line2);

  server.setContentLength(CONTENT_LENGTH_UNKNOWN);            // Draw HTML page which refreshes to / in 30 seconds
  server.sendHeader("Connection", "close");
  server.sendHeader("Cache-Control", "no-cache, no-store, must-revalidate");
  server.sendHeader("Pragma", "no-cache");
  server.sendHeader("Expires", "0");
  server.send(200, "text/html; charset=utf-8", "");
  server.sendContent_P(R"(
    <!DOCTYPE html><html><head><meta charset="utf-8">
    <meta name="viewport" content="width=device-width, initial-scale=1">
    <meta http-equiv="refresh" content="30; url=/">
    <style>
      body{background:#000;color:#fff;font-family:Helvetica;text-align:center;margin:18vh 0 0 0}
      .msg{font-size:5vmin;max-font-size:24px;min-font-size:12px}
      p{color:#bbb}
    </style>
    <script>
      setTimeout(function() { location.replace("/"); }, 31000);
    </script>
    </head><body>
      <div class="msg">RESTARTING...</div>
      <p>Please wait.</p>
      <p>This page will refresh in 30 seconds.</p>
    </body></html>
  )");
  server.sendContent("");

  delay(300);

  WiFiClient client = server.client();
  if (client) client.stop();

  if(ws_open) {
    ws.close();
    ws_open = false;
  }

  delay(ms);
  ESP.restart();
}

// ===== S E T U P ===== //
void setup() {

  Serial.begin(115200);
  delay(50);

  Wire.begin(I2C_SDA, I2C_SCL);
  delay(50);
  Wire.setClock(400000);
  delay(50);

  lcd_init_safe();
  delay(50);

  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, LOW); // blue led off

  prefs.begin("cmps14", false);                                       // Get all permanently saved preferences
  installation_offset_deg = prefs.getFloat("offset_deg", 0.0f);
  magvar_manual_deg = prefs.getFloat("mv_man_deg", 0.0f);
  if (validf(magvar_manual_deg)) magvar_manual_rad = magvar_manual_deg * DEG_TO_RAD;
  for (int i=0;i<8;i++) dev_at_card_deg[i] = prefs.getFloat((String("dev")+String(i)).c_str(), 0.0f);
  bool haveCoeffs = prefs.isKey("hc_A") && prefs.isKey("hc_B") && prefs.isKey("hc_C") && prefs.isKey("hc_D") && prefs.isKey("hc_E");
  if (haveCoeffs) {
    hc.A = prefs.getFloat("hc_A", 0.0f);
    hc.B = prefs.getFloat("hc_B", 0.0f);
    hc.C = prefs.getFloat("hc_C", 0.0f);
    hc.D = prefs.getFloat("hc_D", 0.0f);
    hc.E = prefs.getFloat("hc_E", 0.0f);
  } else {
    hc = fit_harmonic_from_8(headings_deg, dev_at_card_deg);
    prefs.putFloat("hc_A", hc.A);
    prefs.putFloat("hc_B", hc.B);
    prefs.putFloat("hc_C", hc.C);
    prefs.putFloat("hc_D", hc.D);
    prefs.putFloat("hc_E", hc.E);
  }
  send_hdg_true = prefs.getBool("send_hdg_true", true);
  cal_mode_boot = (CalMode)prefs.getUChar("cal_mode_boot", (uint8_t)CAL_USE);
  prefs.end();

  if (i2c_device_present(CMPS14_ADDR)){
    bool started = false;
    switch (cal_mode_boot){                           // Start calibration or use mode based on preferences, default is use mode, manual never used here
      case CAL_FULL_AUTO:
        started = start_calibration_fullauto();
        break;
      case CAL_SEMI_AUTO:
        started = start_calibration_semiauto();
        break;
      case CAL_MANUAL:
        started = start_calibration_manual_mode();
        break;
      default:
        if (cmps14_cmd(REG_USEMODE)){
          cal_mode_runtime = CAL_USE;
          started = true;
        } break;
    }
    if (!started) lcd_print_lines("CAL MODE", "START FAILED");
    else lcd_print_lines("CAL MODE", calmode_str(cal_mode_runtime));
  } else lcd_print_lines("CMPS14 N/A", "CHECK WIRING");

  delay(250);

  btStop();                                                // Stop bluetooth to save power
  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false);
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  lcd_print_lines("WIFI", "CONNECT...");
  unsigned long t0 = millis();
  while (WiFi.status() != WL_CONNECTED && (millis() - t0) < WIFI_TIMEOUT_MS) { delay(250); } // Try to connect WiFi until timeout
  if (WiFi.status() == WL_CONNECTED) {                                                       // Execute if WiFi successfully connected
    build_sk_url();
    make_source_from_mac();
    char ipbuf[16];
    IPAddress ip = WiFi.localIP();
    snprintf(ipbuf, sizeof(ipbuf), "%u.%u.%u.%u", ip[0], ip[1], ip[2], ip[3]);
    lcd_print_lines("WIFI OK", ipbuf);
    delay(250);

    classify_rssi(WiFi.RSSI());
    lcd_print_lines("SIGNAL LEVEL:", RSSIc);
    delay(250);

    // OTA
    ArduinoOTA.setHostname(SK_SOURCE);
    ArduinoOTA.setPassword(WIFI_PASS);
    ArduinoOTA.onStart([](){
      lcd_print_lines("OTA UPDATE", "INIT");
    });
    ArduinoOTA.onEnd([]() {
      lcd_print_lines("OTA UPDATE", "COMPLETE");
    });
    ArduinoOTA.onProgress([](unsigned int progress, unsigned int total){
      char buf[17];
      snprintf(buf, sizeof(buf), "RUNNING: %u%%\r", (unsigned)(progress / (total /100)));
      lcd_print_lines("OTA UPDATE", buf);
    });
    ArduinoOTA.onError([] (ota_error_t error) {
      if (error == OTA_AUTH_ERROR) lcd_print_lines("OTA UPDATE", "AUTH FAIL");
      else if (error == OTA_BEGIN_ERROR) lcd_print_lines("OTA UPDATE", "INIT FAIL");
      else if (error == OTA_CONNECT_ERROR) lcd_print_lines("OTA UPDATE", "CONNECT FAIL");
      else if (error == OTA_RECEIVE_ERROR) lcd_print_lines("OTA UPDATE", "RECEIVE FAIL");
      else if (error == OTA_END_ERROR) lcd_print_lines("OTA UPDATE", "ENDING FAIL");
      else lcd_print_lines("OTA UPDATE", "ERROR");
    });
    ArduinoOTA.begin();

    // Set up the Webserver to call the handlers
    server.on("/", handle_root);
    server.on("/status", handle_status);
    server.on("/cal/on", handle_calibrate_on);
    server.on("/cal/off", handle_calibrate_off);
    server.on("/store/on", handle_store);
    server.on("/reset/on", handle_reset);
    server.on("/offset/set", handle_set_offset);
    server.on("/dev8/set", handle_dev8_set);
    server.on("/calmode/set", handle_calmode_set);
    server.on("/magvar/set", handle_magvar_set);
    server.on("/heading/mode", handle_heading_mode);
    server.on("/restart", handle_restart);
    server.on("/deviationdetails", handle_deviation_details);
    server.begin();

    setup_ws_callbacks();                                             // Websocket
  } else {                                                            // No WiFi, use only LCD output
    LCD_ONLY = true;
    WiFi.disconnect(true);
    WiFi.mode(WIFI_OFF);                                              // Power off WiFi to save power
    lcd_print_lines("LCD ONLY MODE", "NO WIFI");
    delay(250);
  }

}

// ===== L O O P ===== //
void loop() {

  const unsigned long now = millis();                                 // Timestamp of this tick              

  if (!LCD_ONLY) {                                                    // Execute except in LCD ONLY mode
    ArduinoOTA.handle();                                              // OTA
    server.handleClient();                                            // Webserver
    ws.poll();                                                        // Keep websocket alive
    if (WiFi.status() != WL_CONNECTED && ws_open) {                   // Kill ghost websocket
      ws.close();
      ws_open = false;
    }
  }

  static unsigned long expn_retry_ms = WS_RETRY_MS;
  if (!LCD_ONLY && !ws_open && (long)(now - next_ws_try_ms) >= 0){     // Execute only on ticks when timer is due and only if Websocket dropped and if not in LCD only mode
    ws.connect(SK_URL);
    next_ws_try_ms = now + expn_retry_ms;
    expn_retry_ms = min(expn_retry_ms * 2, WS_RETRY_MAX);
  }
  if (ws_open) expn_retry_ms = WS_RETRY_MS;

  bool success = false;
  if ((long)(now - last_read_ms) >= READ_MS) {
    last_read_ms = now;
    success = read_compass();                                         // Read values from CMPS14 only when timer is due
  }
  
  if (cal_mode_runtime == CAL_SEMI_AUTO) {
    cmps14_monitor_and_store(true);                                   // Monitor and save automatically when profile is good enough
  } else {
    cmps14_monitor_and_store(false);                                  // Monitor but do not save automatically, user saves profile from Web UI
  }

  if (success && !LCD_ONLY) {                                         // If not in LCD ONLY mode and if read was successful, send values to SignalK paths
    send_batch_delta_if_needed();
    send_minmax_delta_if_due();
  }

  if ((long)(now - last_lcd_ms) >= LCD_MS) {                          // Execute only on ticks when LCD timer is due
    last_lcd_ms = now;
    if (success && validf(heading_deg)) {
      char buf[17];
      snprintf(buf, sizeof(buf), "      %03.0f%c", heading_deg, 223);
      lcd_print_lines("  HEADING (M):", buf);
    }
  }

  led_update_by_cal_mode();

} 
