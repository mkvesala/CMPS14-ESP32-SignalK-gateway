#include "globals.h"

// ESP32 WiFi and OTA settings
// char SK_URL[512];                                 // URL of SignalK server
// char SK_SOURCE[32];                               // ESP32 source name for SignalK, used also as the OTA hostname
char RSSIc[16];                                   // WiFi signal quality description
char IPc[16];                                     // IP address
bool LCD_ONLY                   = false;          // True when no WiFi available, using only LCD output
const uint32_t WIFI_TIMEOUT_MS  = 90001;          // Try WiFi connection max 1.5 minutes - prime number, to be exact

// CMPS14 reading parameters
unsigned long lcd_hold_ms                 = 0;
const unsigned long MIN_TX_INTERVAL_MS    = 101;         // Max frequency for sending deltas to SignalK - prime number
// const float DB_HDG_RAD                    = 0.00436f;    // 0.25°: deadband threshold for heading
// const float DB_ATT_RAD                    = 0.00436f;    // 0.25°: pitch/roll deadband threshold
const unsigned long MINMAX_TX_INTERVAL_MS = 997;         // Frequency for pitch/roll maximum values sending - prime number
const unsigned long LCD_MS                = 1009;        // Frequency to print on LCD in loop() - prime number
const unsigned long READ_MS               = 47;          // Frequency to read values from CMPS14 in loop() - prime number
const uint8_t CMPS14_ADDR                 = 0x60;        // I2C address of CMPS14

// SH-ESP32 default pins for I2C
const uint8_t I2C_SDA = 16;
const uint8_t I2C_SCL = 17;

// SH-ESP32 led pins
const uint8_t LED_PIN_BL = 2;
const uint8_t LED_PIN_GR = 13;

// Websocket
// WebsocketsClient ws;
// volatile bool ws_open = false;
const unsigned long WS_RETRY_MS = 1999;
const unsigned long WS_RETRY_MAX = 119993;

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
