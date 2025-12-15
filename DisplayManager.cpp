#include "DisplayManager.h"

// === P U B L I C === //

// Constructor
DisplayManager::DisplayManager(CMPS14Processor &compassref, SignalKBroker &signalkref) : compass(compassref), signalk(signalkref) {}

// Initialization
bool DisplayManager::begin() {
  pinMode(LED_PIN_BL, OUTPUT);
  pinMode(LED_PIN_GR, OUTPUT);
  digitalWrite(LED_PIN_BL, LOW);
  digitalWrite(LED_PIN_GR, LOW);
  return this->initLCD();
}

// Show generic OK/FAILED message
void DisplayManager::showSuccessMessage(const char* who, bool success, bool hold) {
  if (success) this->updateLCD(who, "OK", hold);
  else this->updateLCD(who, "FAIL", hold);
}

// Show generic status message
void DisplayManager::showInfoMessage(const char* who, const char* what, bool hold) {
  this->updateLCD(who, what, hold);
}

// Show IP Address and RSSI descriptor
void DisplayManager::showWifiStatus(bool hold) {
  this->updateLCD(IPc, RSSIc, hold);
}

// Show heading T/M
void DisplayManager::showHeading() {
  float heading_true_deg = compass.getHeadingTrueDeg();
  float heading_deg = compass.getHeadingDeg();
  if (compass.isSendingHeadingTrue() && validf(heading_true_deg)) {
    char buf[17];
    snprintf(buf, sizeof(buf), "      %03.0f%c", heading_true_deg, 223);
    this->updateLCD("  HEADING (T):", buf);
  } else if (validf(heading_deg)) {
    char buf[17];
    snprintf(buf, sizeof(buf), "      %03.0f%c", heading_deg, 223);
    this->updateLCD("  HEADING (M):", buf);
  }
}

// Show calibration status
void DisplayManager::showCalibrationStatus() {
  this->updateBlueLed();
}

// Show connection status
void DisplayManager::showConnectionStatus() {
  this->updateGreenLed();
}

void DisplayManager::setWifiInfo(int rssi, IPAddress ip) {
  this->setRSSIc(rssi);
  this->setIPc(ip);
}

// === P R I V A T E === //

// LCD basic printing on two lines
void DisplayManager::updateLCD(const char* l1, const char* l2, bool hold) {
  if (!lcd_present) return;
  if (!strcmp(prev_top, l1) && !strcmp(prev_bot, l2)) return;

  char t[17], b[17];
  this->copy16(t, l1);
  this->copy16(b, l2);

  lcd->setCursor(0, 0);
  lcd->print(t);
  for (int i = (int)strlen(t); i < 16; i++) lcd->print(' ');

  lcd->setCursor(0, 1);
  lcd->print(b);
  for (int i = (int)strlen(b); i < 16; i++) lcd->print(' ');

  this->copy16(prev_top, t);
  this->copy16(prev_bot, b);

  if (hold) lcd_hold_ms = millis();

}

// Initialize LCD screen
bool DisplayManager::initLCD() {
  uint8_t addr = 0;
  if (this->i2cAvailable(LCD_ADDR1)) addr = LCD_ADDR1;
  else if (this->i2cAvailable(LCD_ADDR2)) addr = LCD_ADDR2;

  if (addr) {
    lcd = std::make_unique<LiquidCrystal_I2C>(addr, 16, 2);
    lcd->init();
    lcd->backlight();
    lcd_present = true;
  } else {
    lcd_present = false;
  }
  return lcd_present;
}

// LED indicator for calibration mode, blue led at GPIO2
void DisplayManager::updateBlueLed(){
  static unsigned long last = 0;
  static bool state = false;
  const unsigned long now = millis();

  switch (compass.getCalibrationModeRuntime()){
    case CAL_USE:
      digitalWrite(LED_PIN_BL, HIGH); 
      return;
    case CAL_FULL_AUTO: {
      const unsigned long toggle_ms = 997;
      if (now - last >= toggle_ms) {
        state = !state;
        digitalWrite(LED_PIN_BL, state ? HIGH : LOW); 
        last = now;
      }
      break;
    }
    case CAL_SEMI_AUTO:
    case CAL_MANUAL: {
      const unsigned long toggle_ms = 101;
      if (now - last >= toggle_ms) {
        state = !state;
        digitalWrite(LED_PIN_BL, state ? HIGH : LOW); 
        last = now;
      }
      break;
    }
    default:
      digitalWrite(LED_PIN_BL, LOW);
      break;
  }
}

// LED indicator for wifi mode, green led at GPIO13
void DisplayManager::updateGreenLed(){
  static unsigned long last = 0;
  static bool state = false;
  const unsigned long now = millis();

  if (WiFi.getMode() == WIFI_OFF) {
    const unsigned long toggle_ms = 1009;
    if (now - last >= toggle_ms) {
      state = !state;
      digitalWrite(LED_PIN_GR, state ? HIGH : LOW);
      last = now;
    }
    return;
  }

  if (signalk.isOpen()) {
    digitalWrite(LED_PIN_GR, HIGH);
    return;
  }

  if (WiFi.isConnected()) {
    const unsigned long toggle_ms = 97;
    if (now - last >= toggle_ms) {
      state = !state;
      digitalWrite(LED_PIN_GR, state ? HIGH : LOW);
      last = now;
    }
    return;
  }

  digitalWrite(LED_PIN_GR, LOW); 

}

// Scan I2C address
bool DisplayManager::i2cAvailable(uint8_t addr) {
  Wire.beginTransmission(addr);
  return (Wire.endTransmission() == 0);
}

// Helper for safe LCD printing
void DisplayManager::copy16(char* dst, const char* src) {
  strncpy(dst, src, 16);   // copy max 16 characters
  dst[16] = '\0';          // ensure 0 termination
}

// Set RSSI descriptor
void DisplayManager::setRSSIc(int rssi) {
  const char* label =
      (rssi > -55) ? "EXCELLENT" :
      (rssi < -80) ? "POOR" : "OK";
  strncpy(RSSIc, label, sizeof(RSSIc) - 1);
  RSSIc[sizeof(RSSIc) - 1] = '\0';
}

// Set IP Address descriptor
void DisplayManager::setIPc(IPAddress ip) {
  snprintf(IPc, sizeof(IPc), "%u.%u.%u.%u", ip[0], ip[1], ip[2], ip[3]);
}