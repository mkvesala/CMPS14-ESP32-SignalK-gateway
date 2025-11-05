#include <WiFi.h>
#include <WebServer.h>
#include <ArduinoJson.h>
#include <ArduinoWebsockets.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <ArduinoOTA.h>
#include <Preferences.h>
#include "harmonic.h"
#include "secrets.h"

using namespace websockets;

// Wifi and OTA settings
const String SK_URL             = String("ws://") + SK_HOST + ":" + String(SK_PORT) + "/signalk/v1/stream" + ((strlen(SK_TOKEN) > 0) ? "?token=" + String(SK_TOKEN) : "");
String RSSIc                    = "NA";           // Wifi quality
String SK_SOURCE                = "esp32.cmps14"; // SignalK server source, used also as the OTA hostname
bool LCD_ONLY                   = false;          // True when no wifi available, using only LCD
const uint32_t WIFI_TIMEOUT_MS  = 90000;          // Trying wifi connection max 1.5 minutes

// CMPS14 I2C address and registers
const uint8_t CMPS14_ADDR       = 0x60;  // I2C address of CMPS14
const uint8_t REG_ANGLE_16_H    = 0x02;  // 16-bit angle * 10 (hi)
const uint8_t REG_ANGLE_16_L    = 0x03;  // 16-bit angle * 10 (lo)
const uint8_t REG_PITCH         = 0x04;  // signed degrees
const uint8_t REG_ROLL          = 0x05;  // signed degrees

// CMPS14 calibration
bool cmps14_autocal_on                = false;  // Autocalibration flag
bool autocal_next_boot                = false;  // To show chosen autocalibration next boot flag on web UI
bool cmps14_cal_on                    = false;  // Manual calibration flag
bool cmps14_cal_profile_stored        = false;  // Calibration profile stored flag
bool cmps14_factory_reset             = false;  // Factory reset flag
unsigned long last_cal_poll_ms        = 0;      // Calibration monitoring counters
uint8_t cal_ok_count                  = 0;
const unsigned long CAL_POLL_MS       = 500;
const uint8_t CAL_OK_REQUIRED         = 2;      // Wait for 2 consequtive OKs

// CMPS14 reading parameters
const float HEADING_ALPHA                 = 0.15f;                     // Smoothing factor 0...1, larger value less smoothing
float installation_offset_deg             = 0.0f;                      // Physical installation error of the compass module, configured via web UI
float dev_deg                             = 0.0f;                      // Deviation at heading_deg calculated by harmonic model
float magvar_manual_deg                   = 0.0f;                      // Variation that is set manually from web UI
bool send_hdg_true                        = true;                      // By default, use magnetic variation to calculate and send headingTrue - user might switch this off via web UI
bool use_manual_magvar                    = true;                      // Use magvar_manual_deg if true
const unsigned long MIN_TX_INTERVAL_MS    = 150;                       // Max frequency for sending deltas to SignalK
const float DB_HDG_RAD                    = 0.005f;                    // ~0.29°: deadband threshold for heading
const float DB_ATT_RAD                    = 0.003f;                    // ~0.17°: pitch/roll deadband threshold
unsigned long last_minmax_tx_ms           = 0;
float last_sent_pitch_min                 = NAN;                       // Min and Max for pitch and roll
float last_sent_pitch_max                 = NAN;
float last_sent_roll_min                  = NAN;
float last_sent_roll_max                  = NAN;
const unsigned long MINMAX_TX_INTERVAL_MS = 1000;                      // Frequency for pitch/roll maximum values sending
unsigned long last_lcd_ms                 = 0;
const unsigned long LCD_MS                = 1000;                      // Frequency to print on LCD
unsigned long last_read_ms                = 0;
const unsigned long READ_MS               = 50;                        // Frequency to read values from CMPS14

// SH-ESP32 default pins for I2C
const uint8_t I2C_SDA = 16;
const uint8_t I2C_SCL = 17;

// Permanently stored preferences
Preferences prefs;

// Websocket
WebsocketsClient ws;
volatile bool ws_open = false;
unsigned long next_ws_try_ms = 0;
const unsigned long WS_RETRY_MS = 2000;
const unsigned long WS_RETRY_MAX = 20000;

// Webserver
WebServer server(80);

// Values in degrees for LCD output and webserver
float heading_deg       = NAN;
float pitch_deg         = NAN;
float roll_deg          = NAN;
float compass_deg       = NAN;
float heading_true_deg  = NAN;
float magvar_deg        = NAN;

// Values in radians to communicate with SignalK
float heading_rad       = NAN;
float heading_true_rad  = NAN;
float pitch_rad         = NAN;
float roll_rad          = NAN;
float pitch_min_rad     = NAN;
float pitch_max_rad     = NAN;
float roll_min_rad      = NAN;
float roll_max_rad      = NAN;
float magvar_rad        = NAN; // Value FROM SignalK navigation.magneticVariation

// I2C LCD screen
std::unique_ptr<LiquidCrystal_I2C> lcd;
bool lcd_present          = false;
char prev_top[17]  = "";
char prev_bot[17]  = "";
const uint8_t LCD_ADDR1   = 0x27;             // Scan both addressess in init
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

// Set SignalK source and OTA hostname (equal) based on MAC address ending
void make_source_from_mac() {
  uint8_t m[6]; WiFi.macAddress(m);
  char tail[7]; snprintf(tail, sizeof(tail), "%02x%02x%02x", m[3], m[4], m[5]);
  SK_SOURCE = String("esp32.cmps14-") + tail;
}

// Websocket
void setup_ws_callbacks() {
  
  ws.onEvent([](WebsocketsEvent e, String){
    if (e == WebsocketsEvent::ConnectionOpened) {
      ws_open = true;
      if (!send_hdg_true) return;                             // Do nothing from here if user has switched off sending of headingTrue
      StaticJsonDocument<256> sub;                            // Subscribe navigation.magneticVariation from SignalK server
      sub["context"] = "vessels.self";
      auto subscribe = sub.createNestedArray("subscribe");
      auto s = subscribe.createNestedObject();
      s["path"] = "navigation.magneticVariation";
      s["period"] = 1000;                                     // Request ~1 Hz updates

      char buf[256];
      size_t n = serializeJson(sub, buf, sizeof(buf));
      ws.send(buf, n);
    }
    if (e == WebsocketsEvent::ConnectionClosed)  { ws_open = false; }
    if (e == WebsocketsEvent::GotPing)           { ws.pong(); }
  });

  ws.onMessage([](WebsocketsMessage msg){
    if (!send_hdg_true) return;                                                   // Do nothing if user has switched headingTrue sending off
    if (!msg.isText()) return;
    StaticJsonDocument<768> d;
    DeserializationError err = deserializeJson(d, msg.data());
    if (err) return;

    if (d.containsKey("updates")) {                                           // Search updates[].values[].path == navigation.magneticVariation
      for (JsonObject up : d["updates"].as<JsonArray>()) {
        if (!up.containsKey("values")) continue;
        for (JsonObject v : up["values"].as<JsonArray>()) {
          const char* path = v["path"] | nullptr;
          if (!path) continue;
          if (strcmp(path, "navigation.magneticVariation") == 0) {
            if (v["value"].is<float>() || v["value"].is<double>()) {          // Value should be in rad in SignalK path
              float mv = v["value"].as<float>();
              if (validf(mv)) {
                magvar_rad = mv;
                use_manual_magvar = false;
                magvar_deg = magvar_rad * RAD_TO_DEG;
              } else use_manual_magvar = true;
            }
          }
        }
      }
    }
  });

}

// Send batch of SignalK deltas but only if change exceeds the deadband limits (no unnecessary sending)
void send_batch_delta_if_needed() {
  
  if (LCD_ONLY || !ws_open) return;                                                                    // execute only if wifi and websocket ok
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

  if (changed_h) add("navigation.headingMagnetic", last_h);     // SignalK paths and values
  if (changed_p) add("navigation.attitude.pitch",  last_p);
  if (changed_r) add("navigation.attitude.roll",   last_r);
  if (changed_h && send_hdg_true) {                             
    float mv_rad = use_manual_magvar ? (magvar_manual_deg * DEG_TO_RAD) : magvar_rad;
    auto wrap2pi = [](float r){ while (r < 0) r += 2.0f*M_PI; while (r >= 2.0f*M_PI) r -= 2.0f*M_PI; return r; };
    heading_true_rad = wrap2pi(last_h + mv_rad);
    heading_true_deg = heading_true_rad * RAD_TO_DEG;
    add("navigation.headingTrue", heading_true_rad);
  }

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

  if (ch_pmin) add("navigation.attitude.pitch.min", pitch_min_rad);       // SignalK paths and values
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

// Compass deviation harmonic model
const float headings_deg[8] = { 0, 45, 90, 135, 180, 225, 270, 315 }; // Cardinal and ordinal directions N, NE, E, SE, S, SW, W, NE in deg
float dev_at_card_deg[8] = { 0,0,0,0,0,0,0,0 };                       // Measured deviations (deg) in cardinal and ordinal directions
HarmonicCoeffs hc {0,0,0,0,0};

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
  float deg = ((float)ang10) / 10.0f;               // 0..359.9°
  
  compass_deg = deg;                          // Tag the raw compass deg for global use
  deg += installation_offset_deg;             // Correct physical installation error if such
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
  compass_deg = heading_deg;
  dev_deg = deviation_harm_deg(hc, heading_deg);
  float hdg_corr_deg = heading_deg + dev_deg;
  if (hdg_corr_deg < 0) hdg_corr_deg += 360.0f;
  if (hdg_corr_deg >= 360.0f) hdg_corr_deg -= 360.0f;
  heading_deg = hdg_corr_deg;                 // Magnetic heading = compass heading + installation offset + deviation

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

// Send a command to CMPS14
bool cmps14_cmd(uint8_t cmd) {
  Wire.beginTransmission(CMPS14_ADDR);
  Wire.write(0x00);
  Wire.write(cmd);
  if (Wire.endTransmission() != 0) return false;
  delay(20);  // Delay as recommended on datasheet

  Wire.requestFrom(CMPS14_ADDR, (uint8_t)1);
  if (Wire.available() < 1) return false;
  uint8_t b = Wire.read();
  if (b == 0x55 || b == 0x07) return true;
  return false;
}

// Enable autocalibration with optional autosave
bool cmps14_enable_background_cal(bool autosave) {
  if (!cmps14_cmd(0x98)) return false;
  if (!cmps14_cmd(0x95)) return false;
  if (!cmps14_cmd(0x99)) return false;
  const uint8_t cfg = autosave ? 0x93 : 0x83;       // bit7 + Mag + Acc [+ autosave]
  if (!cmps14_cmd(cfg)) return false;
  cmps14_factory_reset = false;
  return true;
}

// Read calibration status (cmps14_cmd doesn't work here, for some reason... tried that)
uint8_t cmps14_read_cal_status() {
  Wire.beginTransmission(CMPS14_ADDR);
  Wire.write(0x1E);
  if (Wire.endTransmission(false) != 0) return 0xFF;
  Wire.requestFrom(CMPS14_ADDR, (uint8_t)1);
  if (Wire.available() < 1) return 0xFF;
  uint8_t b = Wire.read();
  return b;
}

// Save calibration AND stop calibrating
bool cmps14_store_profile() {
  if (!cmps14_cmd(0xF0)) return false;      // Sequence of storing the calibration profile
  if (!cmps14_cmd(0xF5)) return false;
  if (!cmps14_cmd(0xF6)) return false;
  if (!cmps14_cmd(0x80)) return false;      // Use mode
  cmps14_cal_profile_stored = true;
  cmps14_factory_reset = false;
  return true;
}

// Monitor and optional store
void cmps14_monitor_and_store(bool save) {
  const unsigned long now = millis();
  if (now - last_cal_poll_ms < CAL_POLL_MS) return;
  last_cal_poll_ms = now;

  uint8_t st = cmps14_read_cal_status(); 
  if (st == 0xFF) return;

  uint8_t mag   = (st     ) & 0x03;
  uint8_t accel = (st >> 2) & 0x03;
  uint8_t gyro  = (st >> 4) & 0x03;
  uint8_t sys   = (st >> 6) & 0x03;

  static uint8_t prev = 0xFF;
  if (save && prev != st) prev = st;

  if (sys >= 2 && accel == 3 && mag == 3) {
    if (cal_ok_count < 255) cal_ok_count++;
  } else {
    cal_ok_count = 0;
  }

  if (save && !cmps14_cal_profile_stored && cal_ok_count >= CAL_OK_REQUIRED) {
    if (cmps14_store_profile()) {
      lcd_print_lines("CALIBRATION", "SAVED");
    } else {
      lcd_print_lines("CALIBRATION", "NOT SAVED");
    }
  }
}

// Start calibration in manual mode (autosave off)
bool start_calibration_manual() {
  if (!cmps14_enable_background_cal(false)) return false; // 0x98,0x95,0x99, then 0x83
  cmps14_cal_on = true;
  cmps14_autocal_on = false;
  cmps14_cal_profile_stored = false;
  return true;
}

// Start calibration in auto mode (autosave on)
bool start_calibration_autosave() {
  if (!cmps14_enable_background_cal(true)) return false; // 0x98,0x95,0x99, then 0x93
  cmps14_cal_on = false;
  cmps14_autocal_on = true;
  cmps14_cal_profile_stored = false;
  return true;
}

// Stop all calibration
bool stop_calibration() {
  if (!cmps14_cmd(0x80)) return false;     // use mode
  cmps14_cal_on = false;
  cmps14_autocal_on = false;
  return true;
}

// Web UI handler for CALIBRATE button
void handle_calibrate_on(){
  if (start_calibration_manual()) {
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
    cmps14_autocal_on = false;
    cmps14_cal_on = false;
  }
  handle_root();
}

// Web UI handler for RESET button
void handle_reset(){
  if (cmps14_cmd(0xE0) && cmps14_cmd(0xE5) && cmps14_cmd(0xE2)) {
    delay(600);           // Wait for the sensor to boot
    cmps14_cmd(0x80);     // Use mode
    cmps14_cal_profile_stored = false;
    cmps14_autocal_on = false;
    cmps14_cal_on = false;
    cmps14_factory_reset = true;
    pitch_min_rad = pitch_max_rad = roll_min_rad = roll_max_rad = NAN;    // Reset the pitch/roll min and max values
    last_sent_pitch_min = last_sent_pitch_max = last_sent_roll_min = last_sent_roll_max = NAN;
  }
  handle_root(); 
}

// Web UI handler for STATUS block
void handle_status(){
  uint8_t st = cmps14_read_cal_status();
  uint8_t mag=255, acc=255, gyr=255, sys=255;
  if (st!=0xFF) { mag=(st)&3; acc=(st>>2)&3; gyr=(st>>4)&3; sys=(st>>6)&3; }

  const char* mode = cmps14_autocal_on ? "AUTO_CAL" : cmps14_cal_on ? "MANUAL_CAL" : "USE";
  float variation = use_manual_magvar ? magvar_manual_deg : magvar_deg;

  String json = "{";
  json += "\"mode\":\"" + String(mode) + "\",";
  json += "\"wifi\":\"" + (WiFi.isConnected()? WiFi.localIP().toString() : String("disconnected")) + "\",";
  json += "\"rssi\":" + String(WiFi.isConnected()? WiFi.RSSI(): 0) + ",";
  json += "\"hdg_deg\":" + String(validf(heading_deg)? heading_deg: NAN) + ",";
  json += "\"compass_deg\":" + String(validf(compass_deg)? compass_deg: NAN) + ",";
  json += "\"pitch_deg\":" + String(validf(pitch_deg)? pitch_deg: NAN) + ",";
  json += "\"roll_deg\":" + String(validf(roll_deg)? roll_deg: NAN) + ",";
  json += "\"acc\":" + String(acc) + ",";
  json += "\"mag\":" + String(mag) + ",";
  json += "\"sys\":" + String(sys) + ",";
  json += "\"offset\":" + String(validf(installation_offset_deg)? installation_offset_deg: NAN) + ",";
  json += "\"dev\":" + String(validf(dev_deg)? dev_deg: NAN) + ",";
  json += ",\"variation\":" + String(validf(variation)? variation : NAN) + ",";
  json += ",\"heading_true_deg\":" + String(validf(heading_true_deg)? heading_true_deg : NAN) + ",";
  json += ",\"use__manual_magvar\":" + String(use_manual_magvar ? "YES":"NO")  + ",";
  json += ",\"send_hdg_true\":" + String(send_hdg_true ? "YES":"NO")  + ",";
  json += "\"stored\":" + String(cmps14_cal_profile_stored? "YES":"NO");
  json += "}";
  server.sendHeader("Cache-Control", "no-cache, no-store, must-revalidate");
  server.sendHeader("Pragma", "no-cache");
  server.sendHeader("Expires", "0");
  server.send(200, "application/json; charset=utf-8", json);
}

// Web UI handler for installation offset
void handle_set_offset() {
  if (server.hasArg("v")) {
    float v = server.arg("v").toFloat();
    if (isnan(v) || !isfinite(v)) v = 0.0f;
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

// Web UI handler for 8 measured deviation values, to correct headingCompass --> headingMagnetic
void handle_dev8_set() {
  auto getf = [&](const char* k) -> float {
    if (!server.hasArg(k)) return 0.0f;
    float v = server.arg(k).toFloat();
    if (isnan(v) || !isfinite(v)) v = 0.0f;
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

  hc = fit_harmonic_from_8(headings_deg, dev_at_card_deg);

  // Save preferences permanently
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

  lcd_print_lines("DEVIATION (8)", "FIT & SAVED");

  handle_root();
}

// Web UI handler to save the autocal on boot setting
void handle_autocal_set() {
  autocal_next_boot = server.hasArg("en") && server.arg("en") == "1";
  prefs.begin("cmps14", false);
  prefs.putBool("autocal_pref", autocal_next_boot);
  prefs.end();
  lcd_print_lines("AUTOCAL IN BOOT", autocal_next_boot ? "ENABLED" : "DISABLED");
  handle_root();
}

// Web UI handler to set magnetic variation manually to determine headingTrue if value is not available from SignalK navigation.magneticVariation path
void handle_magvar_set() {
  
  if (server.hasArg("v")) {
    float v = server.arg("v").toFloat();
    if (!isfinite(v)) v = 0.0f;
    if (v < -90.0f) v = -90.0f;
    if (v >  90.0f) v =  90.0f;
    magvar_manual_deg = v;

    prefs.begin("cmps14", false);
    prefs.putFloat("magvar_manual_deg", magvar_manual_deg);
    prefs.end();

    char line2[17];
    snprintf(line2, sizeof(line2), "SET: %6.1f%c", magvar_manual_deg, 223);
    lcd_print_lines("MAG VARIATION", line2);
  }
  handle_root();
}

// Web UI handler to set heading mode: headingMagnetic or headingTrue
void handle_heading_mode() {
  
  if (server.hasArg("true")) {
    int en = server.arg("true").toInt();
    send_hdg_true = (en == 1);          // if 1 then true, otherwise false

    prefs.begin("cmps14", false);
    prefs.putBool("send_hdg_true", send_hdg_true);
    prefs.end();
  }
  handle_root();
}

// Web UI handler for the HTML page (memory-friendly streaming version instead of large String handling)
void handle_root() {

  const char* mode =
    cmps14_autocal_on ? "AUTO_CAL" :
    cmps14_cal_on     ? "MANUAL_CAL" : "USE";

  server.setContentLength(CONTENT_LENGTH_UNKNOWN);
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
    <h2>CMPS14 CONFIG</h2>
    )");

  // DIV Calibrate, Stop, Reset
  server.sendContent_P(R"(
    <div class='card'>)");
  {
    char buf[64];
    const char* nice =
      strcmp(mode, "AUTO_CAL")==0   ? "AUTO CAL" :
      strcmp(mode, "MANUAL_CAL")==0 ? "MANUAL CAL" : "USE";
    snprintf(buf, sizeof(buf), "<p>Mode: %s</p>", nice);
    server.sendContent(buf);
  }

  if (strcmp(mode, "AUTO_CAL") == 0 || strcmp(mode, "MANUAL_CAL") == 0) {
    server.sendContent_P(R"(<p><a href="/cal/off"><button class="button button2">STOP</button></a></p>)");
    if (!cmps14_cal_profile_stored) {
      server.sendContent_P(R"(<p><a href="/store/on"><button class="button">SAVE</button></a></p>)");
    } else {
      server.sendContent_P(R"(<p><a href="/store/on"><button class="button button2">REPLACE</button></a></p>)");
      }
  } else {
    server.sendContent_P(R"(<p><a href="/cal/on"><button class="button">CALIBRATE</button></a></p>)");
    }

  if (!cmps14_factory_reset){ server.sendContent_P(R"(<p><a href="/reset/on"><button class="button button2">RESET</button></a></p>)");}
  server.sendContent_P(R"(</div>)");

  // DIV Autocalibration at boot
  server.sendContent_P(R"(
    <div class='card'>
    <form action="/autocal/set" method="get" style="margin-top:8px;">
    <label style="min-width:180px;text-align:center;">Enable autocalibration on next boot.</label>
    <input type="checkbox" name="en" value="1")");
    { if (autocal_next_boot) server.sendContent_P(R"( checked)"); }
  server.sendContent_P(R"(><div style="margin-top:10px;"><input type="submit" class="button" value="SAVE"></div></form><p style="font-size:14px;color:#bbb;margin-top:8px;">Current setting: )");
  server.sendContent(autocal_next_boot ? "ENABLED" : "DISABLED");
  server.sendContent_P(R"( (takes effect after reboot)</p></div>)");

  // DIV Set installation offset
  server.sendContent_P(R"(
    <div class='card'>
    <form action="/offset/set" method="get">
    <label>Installation offset</label>
    <input type="number" name="v" step="0.1" min="-180" max="180" value=")");
      {
        char buf[32];
        snprintf(buf, sizeof(buf), "%.1f", installation_offset_deg);
        server.sendContent(buf);
      }
  server.sendContent_P(R"("><input type="submit" value="SAVE" class="button"></form></div>)");

  // DIV Set deviation 
  server.sendContent_P(R"(
    <div class='card'>
    <p>Deviations</p>
    <form action="/dev8/set" method="get"><div>)");

  // Row 1: N NE
  {
    char row1[256];
    snprintf(row1, sizeof(row1),
      "<label>N</label><input name=\"N\"  type=\"number\" step=\"0.1\" value=\"%.1f\">"
      "<label>NE</label><input name=\"NE\" type=\"number\" step=\"0.1\" value=\"%.1f\">",
      dev_at_card_deg[0], dev_at_card_deg[1]);
    server.sendContent(row1);
  }
  server.sendContent_P(R"(</div><div>)");

  // Row 2:  E SE
  {
  char row2[256];
    snprintf(row2, sizeof(row2),
      "<label>E</label><input name=\"E\"  type=\"number\" step=\"0.1\" value=\"%.1f\">"
      "<label>SE</label><input name=\"SE\" type=\"number\" step=\"0.1\" value=\"%.1f\">",
      dev_at_card_deg[2], dev_at_card_deg[3]);
    server.sendContent(row2);
  }
  server.sendContent_P(R"(</div><div>)");

  // Row 3: S SW
  {
  char row3[256];
    snprintf(row3, sizeof(row3),
      "<label>S</label><input name=\"S\"  type=\"number\" step=\"0.1\" value=\"%.1f\">"
      "<label>SW</label><input name=\"SW\" type=\"number\" step=\"0.1\" value=\"%.1f\">",
      dev_at_card_deg[4], dev_at_card_deg[5]);
    server.sendContent(row3);
  }
  server.sendContent_P(R"(</div><div>)");

  // Row 4: W NW
  {
    char row4[256];
    snprintf(row4, sizeof(row4),
      "<label>W</label><input name=\"W\"  type=\"number\" step=\"0.1\" value=\"%.1f\">"
      "<label>NW</label><input name=\"NW\" type=\"number\" step=\"0.1\" value=\"%.1f\">",
      dev_at_card_deg[6], dev_at_card_deg[7]);
    server.sendContent(row4);
  }

  server.sendContent_P(R"(
    </div>
    <input type="submit" class="button" value="SAVE"></form></div>)");

  // DIV Set variation 
  server.sendContent_P(R"(
    <div class='card'>
    <p>Variation</p>
    <form action="/magvar/set" method="get"><div>
    <label>Manual</label>
    <input type="number" name="v" step="0.1" min="-180" max="180" value="
    )");
  server.sendContent(String(magvar_manual_deg, 1));
  server.sendContent_P(R"(
    "></div>
    <input type="submit" class="button" value="SAVE"></form></div>)");
  server.sendContent_P(R"(
    <div class='card'>
    <form action="/heading/mode" method="get" style="margin-top:8px;">
    <label style="min-width:180px;text-align:center;">Send true heading</label>
    <input type="checkbox" name="true" value="1")");
    { if (send_hdg_true) server.sendContent_P(R"( checked)"); }
    server.sendContent_P(R"(><div style="margin-top:10px;"><input type="submit" class="button" value="SAVE"></div></form></div>)");

  // DIV Status
  server.sendContent_P(R"(<div class='card'><div id="st">Loading...</div></div>)");

  // Live JS updater script
  server.sendContent_P(R"(
    <script>
      function upd(){
        fetch('/status').then(r=>r.json()).then(j=>{
          const d=[
            'Heading (C): '+(isNaN(j.compass_deg)?'NA':j.compass_deg.toFixed(1))+'\u00B0',
            'Offset: '+(isNaN(j.offset)?'NA':j.offset.toFixed(1))+'\u00B0',
            'Deviation: '+(isNaN(j.dev)?'NA':j.dev.toFixed(1))+'\u00B0',
            'Heading (M): '+(isNaN(j.hdg_deg)?'NA':j.hdg_deg.toFixed(1))+'\u00B0',
            'Variation: '+(isNaN(j.variation)?'NA':j.dev.toFixed(1))+'\u00B0',
            'Heading (T): '+(isNaN(j.heading_true_deg)?'NA':j.heading_true_deg.toFixed(1))+'\u00B0',
            'Pitch: '+(isNaN(j.pitch_deg)?'NA':j.pitch_deg.toFixed(1))+'\u00B0',
            'Roll: '+(isNaN(j.roll_deg)?'NA':j.roll_deg.toFixed(1))+'\u00B0',
            'ACC='+j.acc+', MAG='+j.mag+', SYS='+j.sys,
            'WiFi: '+j.wifi+' ('+j.rssi+' dBm)',
            'Send true heading: '+j.send_hdg_true,
            'Use manual variation: '+j.use_manual_magvar,
            'Calibration saved since boot: '+j.stored
          ];
          document.getElementById('st').textContent=d.join('\n');
        }).catch(_=>{
          document.getElementById('st').textContent='Status fetch failed';
        });
      }
      setInterval(upd,1000);upd();
    </script>
    </body>
    </html>)");

}

// ===== S E T U P ===== //
void setup() {

  Serial.begin(115200);
  delay(100);

  Wire.begin(I2C_SDA, I2C_SCL);
  delay(50);
  Wire.setClock(400000);
  delay(50);

  lcd_init_safe();
  delay(100);

  prefs.begin("cmps14", false);                                       // Get saved preferences
  installation_offset_deg = prefs.getFloat("offset_deg", 0.0f);
  for (int i=0;i<8;i++) dev_at_card_deg[i] = prefs.getFloat((String("dev")+String(i)).c_str(), 0.0f);
  fit_harmonic_from_8(headings_deg, dev_at_card_deg);
  hc.A = prefs.getFloat("hc_A", 0.0f);
  hc.B = prefs.getFloat("hc_B", 0.0f);
  hc.C = prefs.getFloat("hc_C", 0.0f);
  hc.D = prefs.getFloat("hc_D", 0.0f);
  hc.E = prefs.getFloat("hc_E", 0.0f);
  cmps14_autocal_on = prefs.getBool("autocal_pref", false);
  autocal_next_boot = cmps14_autocal_on;
  magvar_manual_deg = prefs.getFloat("magvar_manual_deg", 0.0f);
  send_hdg_true = prefs.getBool("send_hdg_true", true);
  prefs.end();

  if (cmps14_autocal_on){
    if (start_calibration_autosave()) {                    // Switch on CMPS14 autocalibration with autosave
      lcd_print_lines("AUTOCALIBRATION", "ENABLED");
    } else {
      lcd_print_lines("AUTOCALIBRATION", "FAILED");
    }
  } else if (cmps14_cal_on){                        
    if (start_calibration_manual()) {                      // Switch on CMPS15 calibration but no autosave
      lcd_print_lines("CALIBRATION", "MANUAL MODE");
    } else {
      lcd_print_lines("MANUAL MODE", "FAILED");
    }
  } else {
    if (cmps14_cmd(0x80)) {
      lcd_print_lines("NO CALIBRATION", "USE MODE");       // Or go to use-mode with no calibration
    } else {
      lcd_print_lines("USE MODE", "FAILED");
    }
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
    server.on("/autocal/set", handle_autocal_set);
    server.on("/magvar/set", handle_magvar_set);
    server.on("/heading/mode", handle_heading_mode);
    server.begin();

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
    server.handleClient();                                            // Webserver
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

  bool success = false;
  if ((long)(now - last_read_ms) >= READ_MS) {
    last_read_ms = now;
    success = read_compass();                                         // Read values from CMPS14 only when timer is due
  }

  if (cmps14_autocal_on) {
    cmps14_monitor_and_store(true);                                   // Monitor and autosave
  } else {
    cmps14_monitor_and_store(false);                                  // Monitor but do not save automatically
  }

  if (!LCD_ONLY && success) {                                         // If not in LCD ONLY mode and if read was successful, send values to SignalK paths
    send_batch_delta_if_needed();
    send_minmax_delta_if_due();
  }

  if (success && (now - last_lcd_ms >= LCD_MS)) {                     // Execute only on the ticks when LCD timer is due
    last_lcd_ms = now;
    char buf[17];
    snprintf(buf, sizeof(buf), "      %03.0f%c", heading_deg, 223);
    lcd_print_lines("  HEADING (M):", buf);
  }
  
} 
