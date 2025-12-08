#include "globals.h"

// ESP32 WiFi and OTA settings
char RSSIc[16];                                   // WiFi signal quality description
char IPc[16];                                     // IP address
bool LCD_ONLY                   = false;          // True when no WiFi available, using only LCD output

// CMPS14 reading parameters
unsigned long lcd_hold_ms                 = 0;
const uint8_t CMPS14_ADDR                 = 0x60;        // I2C address of CMPS14

const unsigned long LCD_MS                = 1009;        // Frequency to print on LCD in loop()

// SH-ESP32 default pins for I2C
const uint8_t I2C_SDA = 16;
const uint8_t I2C_SCL = 17;

// SH-ESP32 led pins
const uint8_t LED_PIN_BL = 2;
const uint8_t LED_PIN_GR = 13;

// Webserver
WebServer server(80);

// I2C LCD 16x2
std::unique_ptr<LiquidCrystal_I2C> lcd;
bool lcd_present          = false;
char prev_top[17]         = "";      // Previous value of top line
char prev_bot[17]         = "";      // Previous value of bottom line
const uint8_t LCD_ADDR1   = 0x27;    // Scan both I2C addresses when init LCD
const uint8_t LCD_ADDR2   = 0x3F;

// Scan I2C address
bool i2cAvailable(uint8_t addr) {
  Wire.beginTransmission(addr);
  return (Wire.endTransmission() == 0);
}

// Description for WiFi signal level
void setRSSICstr() {
  int rssi = WiFi.RSSI();
  const char* label =
      (rssi > -55) ? "EXCELLENT" :
      (rssi < -80) ? "POOR" : "OK";
  strncpy(RSSIc, label, sizeof(RSSIc) - 1);
  RSSIc[sizeof(RSSIc) - 1] = '\0';
}

// Update IP address cstring
void setIPAddrCstr() {
  IPAddress ip = WiFi.localIP();
  snprintf(IPc, sizeof(IPc), "%u.%u.%u.%u", ip[0], ip[1], ip[2], ip[3]);
}
