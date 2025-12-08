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
extern char RSSIc[16];                    // WiFi signal quality description
extern char IPc[16];                      // IP address cstring
extern bool LCD_ONLY;                     // True when no WiFi available, using only LCD output

// CMPS14 reading parameters
extern unsigned long lcd_hold_ms;
extern const uint8_t CMPS14_ADDR;                     // I2C address of CMPS14

extern const unsigned long LCD_MS;        // Frequency to print on LCD in loop()

// SH-ESP32 default pins for I2C
extern const uint8_t I2C_SDA;
extern const uint8_t I2C_SCL;

// SH-ESP32 led pins
extern const uint8_t LED_PIN_BL;
extern const uint8_t LED_PIN_GR;

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
