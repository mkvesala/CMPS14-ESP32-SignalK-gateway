#pragma once

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
extern char SK_URL[512];                  // URL of SignalK server
extern char SK_SOURCE[32];                // ESP32 source name for SignalK, used also as the OTA hostname
extern char RSSIc[16];                    // WiFi signal quality description
extern char IPc[16];                      // IP address cstring
extern bool LCD_ONLY;                     // True when no WiFi available, using only LCD output
extern const uint32_t WIFI_TIMEOUT_MS;    // Try WiFi connection max 

// Three calibration modes + use mode
enum CalMode : uint8_t { CAL_USE=0, CAL_FULL_AUTO=1, CAL_SEMI_AUTO=2, CAL_MANUAL=3 };
extern CalMode cal_mode_boot;
extern CalMode cal_mode_runtime; 
const char* calmode_str(CalMode m);

// CMPS14 calibration
extern bool cmps14_cal_profile_stored;   // Calibration profile stored flagextern 
extern bool cmps14_factory_reset;        // Factory reset flag
extern const unsigned long CAL_POLL_MS;  // Autocalibration save condition timer 
extern const uint8_t CAL_OK_REQUIRED;    // Autocalibration save condition threshold
extern unsigned long full_auto_start_ms; // Full auto mode start timestamp
extern unsigned long full_auto_stop_ms;  // Full auto mode timeout, 0 = never
extern unsigned long full_auto_left_ms;  // Full auto mode time left

// CMPS14 reading parameters
extern float installation_offset_deg;                      // Physical installation error of the compass module, configured via web UI
extern float dev_deg;                                      // Deviation at heading_deg calculated by harmonic model
extern float magvar_manual_deg;                            // Variation that is set manually from web UI
extern float magvar_manual_rad;                            // Manual variation in rad
extern bool send_hdg_true;                                 // By default, use magnetic variation to calculate and send headingTrue - user might switch this off via web UI
extern bool use_manual_magvar;                             // Use magvar_manual_deg if true
extern unsigned long lcd_hold_ms;
extern const float HEADING_ALPHA;                          // Smoothing factor 0...1, larger value less smoothing
extern const unsigned long MIN_TX_INTERVAL_MS;             // Max frequency for sending deltas to SignalK 
extern const float DB_HDG_RAD;                             // Deadband threshold for heading
extern const float DB_ATT_RAD;                             // Pitch/roll deadband threshold
extern const unsigned long MINMAX_TX_INTERVAL_MS;          // Frequency for pitch/roll maximum values sending 
extern const unsigned long LCD_MS;                         // Frequency to print on LCD in loop()
extern const unsigned long READ_MS;                        // Frequency to read values from CMPS14 in loop()
extern const uint8_t CMPS14_ADDR;                          // I2C address of CMPS14

// CMPS14 values in degrees for LCD and WebServer
extern float heading_deg;
extern float pitch_deg;
extern float roll_deg;
extern float compass_deg;
extern float heading_true_deg;
extern float magvar_deg;

// CMPS14 values in radians for SignalK server
extern float heading_rad;
extern float heading_true_rad;
extern float pitch_rad;
extern float roll_rad;
extern float pitch_min_rad;
extern float pitch_max_rad;
extern float roll_min_rad;
extern float roll_max_rad;
extern float magvar_rad;      // Value FROM SignalK navigation.magneticVariation path via subscribe json

// SH-ESP32 default pins for I2C
extern const uint8_t I2C_SDA;
extern const uint8_t I2C_SCL;

// SH-ESP32 led pins
extern const uint8_t LED_PIN_BL;
extern const uint8_t LED_PIN_GR;

// Permanently stored preferences
extern Preferences prefs;

// Websocket
extern WebsocketsClient ws;
extern volatile bool ws_open;
extern const unsigned long WS_RETRY_MS;
extern const unsigned long WS_RETRY_MAX;

// Webserver
extern WebServer server;

// I2C LCD 16x2
extern std::unique_ptr<LiquidCrystal_I2C> lcd;
extern bool lcd_present;
extern char prev_top[17];                      // Previous value of top line
extern char prev_bot[17];                      // Previous value of bottom line
extern const uint8_t LCD_ADDR1;                // Scan both I2C addresses when init LCD
extern const uint8_t LCD_ADDR2;

// Compass deviation harmonic model for harmonic.h
extern const float headings_deg[8];                    // Cardinal and intercardinal directions N, NE, E, SE, S, SW, W, NE in deg
extern float dev_at_card_deg[8];                       // Measured deviations (deg) in cardinal and intercardinal directions given by user via Web UI
extern HarmonicCoeffs hc;                              // Five coeffs to calculate full deviation curve;

// Return float validity
inline bool validf(float x) { return !isnan(x) && isfinite(x); }

// Return shortest arc on 360° (for instance 359° to 001° is 2° not 358°)
inline float ang_diff_rad(float a, float b) {
  float d = a - b;
  while (d >  M_PI) d -= 2.0f * M_PI;
  while (d <= -M_PI) d += 2.0f * M_PI;
  return d;
}

// Milliseconds to hh:mm:ss
inline const char* ms_to_hms_str(unsigned long ms) {
  static char buf[12];
  unsigned long total_secs = ms / 1000;
  unsigned int h = total_secs / 3600;
  unsigned int m = (total_secs % 3600) / 60;
  unsigned int s = (total_secs % 60);
  snprintf(buf, sizeof(buf), "%02u:%02u:%02u", h, m, s);
  return buf;
}

// Scan I2C address
bool i2c_device_present(uint8_t addr);

// Get saved configuration from ESP32 preferences
void get_config_from_prefs();

// Update RSSI description
void update_rssi_cstr();

// Update IP Address cstring
void update_ipaddr_cstr();
