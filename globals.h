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
extern char SK_SOURCE[32];                // ESP32's source name for SignalK, used also as the OTA hostname
extern char RSSIc[16];                    // WiFi signal quality description
extern char IPc[16];                      // IP address cstring
extern bool LCD_ONLY;                     // True when no WiFi available, using only LCD output
extern const uint32_t WIFI_TIMEOUT_MS;    // Try WiFi connection max 

// CMPS14 calibration 
extern unsigned long full_auto_start_ms; // Full auto mode start timestamp
extern unsigned long full_auto_stop_ms;  // Full auto mode timeout, 0 = never
extern unsigned long full_auto_left_ms;  // Full auto mode time left

// CMPS14 reading parameters
extern bool send_hdg_true;                            // By default, use magnetic variation to calculate and send headingTrue - user might switch this off via web UI
extern unsigned long lcd_hold_ms;
extern const unsigned long MIN_TX_INTERVAL_MS;        // Max frequency for sending deltas to SignalK 
extern const float DB_HDG_RAD;                        // Deadband threshold for heading
extern const float DB_ATT_RAD;                        // Pitch/roll deadband threshold
extern const unsigned long MINMAX_TX_INTERVAL_MS;     // Frequency for pitch/roll maximum values sending 
extern const unsigned long LCD_MS;                    // Frequency to print on LCD in loop()
extern const unsigned long READ_MS;                   // Frequency to read values from CMPS14 in loop()
extern const uint8_t CMPS14_ADDR;                     // I2C address of CMPS14

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
extern char prev_top[17];                 // Previous value of top line
extern char prev_bot[17];                 // Previous value of bottom line
extern const uint8_t LCD_ADDR1;           // Scan both I2C addresses when init LCD
extern const uint8_t LCD_ADDR2;

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
bool i2cAvailable(uint8_t addr);

// Update RSSI description
void setRSSICstr();

// Update IP Address cstring
void setIPAddrCstr();
