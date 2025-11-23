#include "globals.h"

// ESP32 WiFi and OTA settings
char SK_URL[512];                                 // URL of SignalK server
char SK_SOURCE[32];                               // ESP32 source name for SignalK, used also as the OTA hostname
char RSSIc[16];                                   // WiFi signal quality description
char IPc[16];                                     // IP address
bool LCD_ONLY                   = false;          // True when no WiFi available, using only LCD output
const uint32_t WIFI_TIMEOUT_MS  = 90001;          // Try WiFi connection max 1.5 minutes - prime number, to be exact

// CMPS14 calibration
bool cmps14_cal_profile_stored        = false;  // Calibration profile stored flag
bool cmps14_factory_reset             = false;  // Factory reset flag
const unsigned long CAL_POLL_MS       = 499;    // Autocalibration save condition timer - prime number
const uint8_t CAL_OK_REQUIRED         = 3;      // Autocalibration save condition threshold
unsigned long full_auto_start_ms      = 0;      // Full auto mode start timestamp
unsigned long full_auto_stop_ms       = 0;      // Full auto mode timeout, 0 = never
unsigned long full_auto_left_ms       = 0;      // Full auto mode time left

CalMode cal_mode_boot                 = CAL_USE;
CalMode cal_mode_runtime              = CAL_USE; 
const char* calmode_str(CalMode m){
  switch(m){
    case CAL_FULL_AUTO:               return "FULL AUTO";
    case CAL_SEMI_AUTO:               return "AUTO";
    case CAL_MANUAL:                  return "MANUAL";
    default:                          return "USE";
  }
}    

// CMPS14 reading parameters
float installation_offset_deg             = 0.0f;                      // Physical installation error of the compass module, configured via web UI
float dev_deg                             = 0.0f;                      // Deviation at heading_deg calculated by harmonic model
float magvar_manual_deg                   = 0.0f;                      // Variation that is set manually from web UI
float magvar_manual_rad                   = 0.0f;                      // Manual variation in rad
bool send_hdg_true                        = true;                      // By default, use magnetic variation to calculate and send headingTrue - user might switch this off via web UI
bool use_manual_magvar                    = true;                      // Use magvar_manual_deg if true
unsigned long lcd_hold_ms                 = 0;
const float HEADING_ALPHA                 = 0.15f;                     // Smoothing factor 0...1, larger value less smoothing
const unsigned long MIN_TX_INTERVAL_MS    = 101;                       // Max frequency for sending deltas to SignalK - prime number
const float DB_HDG_RAD                    = 0.00436f;                  // 0.25°: deadband threshold for heading
const float DB_ATT_RAD                    = 0.00436f;                  // 0.25°: pitch/roll deadband threshold
const unsigned long MINMAX_TX_INTERVAL_MS = 997;                       // Frequency for pitch/roll maximum values sending - prime number
const unsigned long LCD_MS                = 1009;                      // Frequency to print on LCD in loop() - prime number
const unsigned long READ_MS               = 47;                        // Frequency to read values from CMPS14 in loop() - prime number
const uint8_t CMPS14_ADDR                 = 0x60;                      // I2C address of CMPS14

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
const uint8_t LED_PIN_BL = 2;
const uint8_t LED_PIN_GR = 13;

// Permanently stored preferences
Preferences prefs;

// Websocket
WebsocketsClient ws;
volatile bool ws_open = false;
const unsigned long WS_RETRY_MS = 1999;
const unsigned long WS_RETRY_MAX = 119993;

// Webserver
WebServer server(80);

// I2C LCD 16x2
std::unique_ptr<LiquidCrystal_I2C> lcd;
bool lcd_present          = false;
char prev_top[17]         = "";                      // Previous value of top line
char prev_bot[17]         = "";                      // Previous value of bottom line
const uint8_t LCD_ADDR1   = 0x27;                    // Scan both I2C addresses when init LCD
const uint8_t LCD_ADDR2   = 0x3F;

// Compass deviation harmonic model for harmonic.h
const float headings_deg[8] = { 0, 45, 90, 135, 180, 225, 270, 315 }; // Cardinal and intercardinal directions N, NE, E, SE, S, SW, W, NE in deg
float dev_at_card_deg[8] = { 0,0,0,0,0,0,0,0 };                       // Measured deviations (deg) in cardinal and intercardinal directions given by user via Web UI
HarmonicCoeffs hc {0,0,0,0,0};                                        // Five coeffs to calculate full deviation curve

// Scan I2C address
bool i2c_device_present(uint8_t addr) {
  Wire.beginTransmission(addr);
  return (Wire.endTransmission() == 0);
}

// Get all permanently saved preferences
void get_config_from_prefs() {
  prefs.begin("cmps14", false);  
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
  full_auto_stop_ms = (unsigned long)prefs.getULong("fastop", 0);
  prefs.end();
}

// Description for WiFi signal level
void update_rssi_cstr() {
  int rssi = WiFi.RSSI();
  const char* label =
      (rssi > -55) ? "EXCELLENT" :
      (rssi < -80) ? "POOR" : "OK";
  strncpy(RSSIc, label, sizeof(RSSIc) - 1);
  RSSIc[sizeof(RSSIc) - 1] = '\0';
}

// Update IP address cstring
void update_ipaddr_cstr() {
  IPAddress ip = WiFi.localIP();
  snprintf(IPc, sizeof(IPc), "%u.%u.%u.%u", ip[0], ip[1], ip[2], ip[3]);
}
