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
static bool cmps14_autocal_on         = false;  // Autocalibration flag
static bool cmps14_cal_on             = false;  // Manual calibration flag
static bool cmps14_cal_profile_stored = false;  // Calibration profile stored flag
static bool cmps14_factory_reset      = false;  // Factory reset flag
static unsigned long last_cal_poll_ms = 0;      // Calibration monitoring counters
static uint8_t cal_ok_count           = 0;
const unsigned long CAL_POLL_MS       = 500;
const uint8_t CAL_OK_REQUIRED         = 2;      // Wait for 2 consequtive OKs

// CMPS14 reading parameters
const float HEADING_ALPHA                 = 0.15f;                     // Smoothing factor 0...1, larger value less smoothing
float installation_offset_deg             = 0.0f;                      // Physical installation error of the compass module, configured via web UI
const unsigned long MIN_TX_INTERVAL_MS    = 150;                       // Max frequency for sending deltas to SignalK
const float DB_HDG_RAD                    = 0.005f;                    // ~0.29°: deadband threshold for heading
const float DB_ATT_RAD                    = 0.003f;                    // ~0.17°: pitch/roll deadband threshold
static unsigned long last_minmax_tx_ms    = 0;
static float last_sent_pitch_min          = NAN;                       // Min and Max for pitch and roll
static float last_sent_pitch_max          = NAN;
static float last_sent_roll_min           = NAN;
static float last_sent_roll_max           = NAN;
const unsigned long MINMAX_TX_INTERVAL_MS = 1000;                      // Frequency for pitch/roll maximum values sending
unsigned long last_lcd_ms                 = 0;
const unsigned long LCD_MS                = 1000;                      // Frequency to print on LCD

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

  if (changed_h) add("navigation.headingMagnetic", last_h);     // SignalK paths and values
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
  float deg = ((float)ang10) / 10.0f;         // 0..359.9°
  
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

  float dev_deg = deviation_harm_deg(hc, heading_deg);
  float hdg_corr_deg = heading_deg + dev_deg;
  if (hdg_corr_deg < 0) hdg_corr_deg += 360.0f;
  if (hdg_corr_deg >= 360.0f) hdg_corr_deg -= 360.0f;
  heading_deg = hdg_corr_deg;

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

  String json = "{";
  json += "\"mode\":\"" + String(mode) + "\",";
  json += "\"wifi\":\"" + (WiFi.isConnected()? WiFi.localIP().toString() : String("disconnected")) + "\",";
  json += "\"rssi\":" + String(WiFi.isConnected()? WiFi.RSSI(): 0) + ",";
  json += "\"ws_open\":" + String(ws_open ? "true":"false") + ",";
  json += "\"hdg_deg\":" + String(validf(heading_deg)? heading_deg: NAN) + ",";
  json += "\"pitch_deg\":" + String(validf(pitch_deg)? pitch_deg: NAN) + ",";
  json += "\"roll_deg\":" + String(validf(roll_deg)? roll_deg: NAN) + ",";
  json += "\"acc\":" + String(acc) + ",";
  json += "\"mag\":" + String(mag) + ",";
  json += "\"g\":"   + String(gyr) + ",";
  json += "\"sys\":" + String(sys) + ",";
  json += "\"stored\":" + String(cmps14_cal_profile_stored? "true":"false");
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

// Web UI handler for the HTML page
void handle_root(){

  const char* mode = cmps14_autocal_on ? "AUTO_CAL" : cmps14_cal_on ? "MANUAL_CAL" : "USE";
  const char* dirs[8] = {"N","NE","E","SE","S","SW","W","NW"};

  String html = "<!DOCTYPE html><html><head><meta charset=\"utf-8\"><meta name=\"viewport\" content=\"width=device-width, initial-scale=1\">";
  html += "<link rel=\"icon\" href=\"data:,\">";
  html += "<style>";
  html += "html { font-family: Helvetica; display: inline-block; margin: 0 auto; text-align: center;}";
  html += "body { background:#000; color:#fff; }";
  html += ".button { background-color: #00A300; border: none; color: white; padding: 16px 40px; text-decoration: none; font-size: 30px; margin: 6px; cursor: pointer; border-radius:10px;}";
  html += ".button2 { background-color: #D10000; }";
  html += ".card { width:92%; margin:14px auto; padding:14px; background:#0b0b0b; border-radius:12px; box-shadow:0 0 0 1px #222 inset; }";
  html += "h1 { margin:16px 0 8px 0; } h2 { margin:10px 0; font-size:22px; } h3 { margin:8px 0; font-size:18px; }";
  html += "label { display:inline-block; min-width:40px; text-align:right; margin-right:8px; }";
  html += "input[type=number]{ font-size:18px; width:90px; padding:6px 8px; margin:4px; border-radius:8px; border:1px solid #333; background:#111; color:#fff; }";
  html += "#st { font-size: 3vmin; max-font-size: 24px; min-font-size: 10px; line-height: 1.4; color: #DBDBDB; background-color: #000; padding: 10px; border-radius: 10px; width: 90%; margin: auto; text-align: center; white-space: pre-line; font-family: monospace;}";
  html += "</style></head><body><h1>CMPS14 CONFIG</h1>";

  // === CAL / USE controls ===
  html += "<div class='card'>";
  html += "<h2>Mode: ";
  if (strcmp(mode, "AUTO_CAL")==0) html += "AUTO CAL";
  else if (strcmp(mode, "MANUAL_CAL")==0) html += "MANUAL CAL";
  else html += "USE";
  html += "</h2>";

  if (strcmp(mode, "AUTO_CAL") == 0 || strcmp(mode, "MANUAL_CAL") == 0){
    html += "<p><a href=\"/cal/off\"><button class=\"button button2\">STOP</button></a></p>";
    if (!cmps14_cal_profile_stored) {
      html += "<p><a href=\"/store/on\"><button class=\"button\">SAVE</button></a></p>";
    } else {
      html += "<p><a href=\"/store/on\"><button class=\"button button2\">REPLACE</button></a></p>";
    }
  } else {
    html += "<p><a href=\"/cal/on\"><button class=\"button\">CALIBRATE</button></a></p>";
  }

  if (!cmps14_factory_reset){
    html += "<p><a href=\"/reset/on\"><button class=\"button button2\">RESET</button></a></p>";
  }
  html += "</div>";

  // === INSTALLATION OFFSET ===
  html += "<div class='card'>";
  html += "<h2>Installation Offset</h2>";
  html += "<p>Korjaa fyysisen asennusvirheen asteina (−180…+180). Käytetään ennen eksymäkorjausta.</p>";
  html += "<form action=\"/offset/set\" method=\"get\">";
  html += "<label>Offset</label>";
  html += "<input type=\"number\" name=\"v\" step=\"0.1\" min=\"-180\" max=\"180\" value=\"" + String(installation_offset_deg, 1) + "\">";
  html += "<input type=\"submit\" value=\"SET\" class=\"button\">";
  html += "</form>";
  html += "</div>";

  // === 8-point deviation form (harmonic model) ===
  html += "<div class='card'>";
  html += "<h2>Deviation (deg) at Cardinal & Intercardinal Points</h2>";
  html += "<p>Syötä mitatut residuaalit <b>asteina</b> (N, NE, E, SE, S, SW, W, NW). Sovitus laskee harmonisen mallin (A,B,C,D,E).</p>";
  html += "<form action=\"/dev8/set\" method=\"get\">";
  // Row 1: N NE E SE
  html += "<div>";
  html += "<label>N</label><input name=\"N\"  type=\"number\" step=\"0.1\" value=\""  + String(dev_at_card_deg[0],1) + "\">";
  html += "<label>NE</label><input name=\"NE\" type=\"number\" step=\"0.1\" value=\"" + String(dev_at_card_deg[1],1) + "\">";
  html += "<label>E</label><input name=\"E\"  type=\"number\" step=\"0.1\" value=\""  + String(dev_at_card_deg[2],1) + "\">";
  html += "<label>SE</label><input name=\"SE\" type=\"number\" step=\"0.1\" value=\"" + String(dev_at_card_deg[3],1) + "\">";
  html += "</div>";
  // Row 2: S SW W NW
  html += "<div>";
  html += "<label>S</label><input name=\"S\"  type=\"number\" step=\"0.1\" value=\""  + String(dev_at_card_deg[4],1) + "\">";
  html += "<label>SW</label><input name=\"SW\" type=\"number\" step=\"0.1\" value=\"" + String(dev_at_card_deg[5],1) + "\">";
  html += "<label>W</label><input name=\"W\"  type=\"number\" step=\"0.1\" value=\""  + String(dev_at_card_deg[6],1) + "\">";
  html += "<label>NW</label><input name=\"NW\" type=\"number\" step=\"0.1\" value=\"" + String(dev_at_card_deg[7],1) + "\">";
  html += "</div>";
  html += "<input type=\"submit\" class=\"button\" value=\"FIT & SAVE\">";
  html += "</form>";
  html += "<h3>Info</h3><p style='font-size:14px;color:#bbb'>Sovitettu malli päivittyy heti ja sitä käytetään suuntakulman korjaukseen `read_compass()`-funktion sisällä.</p>";
  html += "</div>";

  // === STATUS ===
  html += "<div class='card'>";
  html += "<h2>STATUS</h2><div id=\"st\">Loading...</div>";
  html += "</div>";

  // === Live updater ===
  html += "<script>"
          "function upd(){"
          "fetch('/status').then(r=>r.json()).then(j=>{"
          "const d=["
          "'Heading (M): '+(isNaN(j.hdg_deg)?'NA':j.hdg_deg.toFixed(1))+'\\u00B0',"
          "'Pitch: '+(isNaN(j.pitch_deg)?'NA':j.pitch_deg.toFixed(1))+'\\u00B0',"
          "'Roll: '+(isNaN(j.roll_deg)?'NA':j.roll_deg.toFixed(1))+'\\u00B0',"
          "'ACC='+j.acc+', MAG='+j.mag+', SYS='+j.sys,"
          "'WiFi: '+j.wifi+' ('+j.rssi+' dBm)',"
          "'WS open: '+j.ws_open"
          "];"
          "document.getElementById('st').textContent=d.join('\\n');"
          "}).catch(_=>{document.getElementById('st').textContent='Status fetch failed';});}"
          "setInterval(upd,1000);upd();"
          "</script>";

  html += "</body></html>";
  server.send(200, "text/html; charset=utf-8", html);
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

  bool success = read_compass();                                      // Read values from CMPS14 on each loop

  if (cmps14_autocal_on) {
    cmps14_monitor_and_store(true);                                   // Monitor and autosave
  } else {
    cmps14_monitor_and_store(false);                                  // Monitor but do not save automatically
  }

  if (!LCD_ONLY && success) {                                         // If not in LCD ONLY mode and if read was successful, send values to SignalK paths
    send_batch_delta_if_needed();
    send_minmax_delta_if_due();
  }

  if (success && (now - last_lcd_ms >= LCD_MS)) {                                  // Execute only on the ticks when LCD timer is due
    last_lcd_ms = now;
    char buf[17];
    snprintf(buf, sizeof(buf), "      %03.0f%c", heading_deg, 223);
    lcd_print_lines("  HEADING (M):", buf);
  }
  
} 
