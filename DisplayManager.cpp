#include "DisplayManager.h"

// === P U B L I C === //

// Constructor
DisplayManager::DisplayManager(CMPS14Processor &compassref, SignalKBroker &signalkref) : lcd(LCD_ADDR, 16, 2), compass(compassref), signalk(signalkref) {}

// Initialization
bool DisplayManager::begin() {
  pinMode(LED_PIN_BL, OUTPUT);
  pinMode(LED_PIN_GR, OUTPUT);
  this->setLedState(LED_PIN_BL, blue_led_current_state, false);
  this->setLedState(LED_PIN_GR, green_led_current_state, false);
  return this->initLCD();
}

void DisplayManager::handle() {
  const unsigned long now = millis();
  if ((long)now - last_lcd_ms >= LCD_MS) {
    last_lcd_ms = now;
    MsgItem msg;
    if (this->popMsgItem(msg)) this->updateLCD(msg.l1, msg.l2);
    else this->showHeading();
  }
  this->updateBlueLed();
  this->updateGreenLed();
}

// Show generic OK/FAILED message
void DisplayManager::showSuccessMessage(const char* who, bool success) {
  MsgItem msg;
  if (success) {
    this->copy16(msg.l1, who);
    this->copy16(msg.l2, "OK");
    this->pushMsgItem(msg);
  }
  else {
    this->copy16(msg.l1, who);
    this->copy16(msg.l2, "FAIL");
    this->pushMsgItem(msg);
  }
}

// Show generic status message
void DisplayManager::showInfoMessage(const char* who, const char* what) {
  MsgItem msg;
  this->copy16(msg.l1, who);
  this->copy16(msg.l2, what);
  this->pushMsgItem(msg);
}

// Show IP Address and RSSI descriptor
void DisplayManager::showWifiStatus() {
  MsgItem msg;
  this->copy16(msg.l1, IPc);
  this->copy16(msg.l2, RSSIc);
  this->pushMsgItem(msg);
}

void DisplayManager::setWifiInfo(int32_t rssi, uint8_t ip0, uint8_t ip1, uint8_t ip2, uint8_t ip3) {
  this->setRSSIc(rssi);
  this->setIPc(ip0, ip1, ip2, ip3);
}

void DisplayManager::setWifiState(WifiState state) {
  wifi_state = state;
}

// === P R I V A T E === //

// LCD basic printing on two lines
void DisplayManager::updateLCD(const char* l1, const char* l2) {
  if (!lcd_present) return;
  if (!strcmp(prev_top, l1) && !strcmp(prev_bot, l2)) return;

  char t[17], b[17];
  this->copy16(t, l1);
  this->copy16(b, l2);

  lcd.setCursor(0, 0);
  lcd.print(t);
  for (int i = (int)strlen(t); i < 16; i++) lcd.print(' ');

  lcd.setCursor(0, 1);
  lcd.print(b);
  for (int i = (int)strlen(b); i < 16; i++) lcd.print(' ');

  this->copy16(prev_top, t);
  this->copy16(prev_bot, b);

}

// Initialize LCD screen
bool DisplayManager::initLCD() {
  if (this->i2cAvailable(LCD_ADDR)) {
    lcd.init();
    lcd.backlight();
    lcd_present = true;
  } else {
    lcd_present = false;
  }
  return lcd_present;
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
void DisplayManager::setRSSIc(int32_t rssi) {
  const char* label =
      (rssi > -55) ? "EXCELLENT" :
      (rssi < -80) ? "POOR" : "OK";
  strncpy(RSSIc, label, sizeof(RSSIc) - 1);
  RSSIc[sizeof(RSSIc) - 1] = '\0';
}

// Set IP Address descriptor
void DisplayManager::setIPc(uint8_t ip0, uint8_t ip1, uint8_t ip2, uint8_t ip3) {
  snprintf(IPc, sizeof(IPc), "%u.%u.%u.%u", ip0, ip1, ip2, ip3);
}

// Push a message for LCD printing to FIFO queue
bool DisplayManager::pushMsgItem(const auto &msg) {
  if (this->fifoIsFull()) return false;
  fifo[head] = msg;
  head = (head + 1) % FIFO_SIZE;
  count++;
  return true;
}

// Pop a message for LCD printing from FIFO queue
bool DisplayManager::popMsgItem(auto &msg) {
  if (this->fifoIsEmpty()) return false;
  msg = fifo[tail];
  tail = (tail + 1) % FIFO_SIZE;
  count--;
  return true;
}

// LED indicator for calibration mode, green led at GPI13
void DisplayManager::updateGreenLed(){
  static unsigned long last = 0;
  static bool blink_state = false;
  const unsigned long now = millis();

  switch (compass.getCalibrationModeRuntime()){
    case CalMode::USE:
      this->setLedState(LED_PIN_GR, green_led_current_state, true); // solid
      return;
    case CalMode::FULL_AUTO: {
      const unsigned long toggle_ms = 997; // 0.5 hz
      if (now - last >= toggle_ms) {
        blink_state = !blink_state;
        this->setLedState(LED_PIN_GR, green_led_current_state, blink_state);
        last = now;
      }
      break;
    }
    case CalMode::AUTO:
    case CalMode::MANUAL: {
      const unsigned long toggle_ms = 101; // 5 hz
      if (now - last >= toggle_ms) {
        blink_state = !blink_state;
        this->setLedState(LED_PIN_GR, green_led_current_state, blink_state); 
        last = now;
      }
      break;
    }
    default:
      this->setLedState(LED_PIN_GR, green_led_current_state, false); // off
      break;
  }
}

// LED indicator for wifi mode, blue led at GPIO2
void DisplayManager::updateBlueLed(){
  static unsigned long last = 0;
  static bool blink_state = false;
  const unsigned long now = millis();

  if (signalk.isOpen()) {
    this->setLedState(LED_PIN_BL, blue_led_current_state, true); // solid
    return;
  }

  switch (wifi_state) {

    case WifiState::OFF:
    case WifiState::DISCONNECTED:
    case WifiState::FAILED:
    case WifiState::INIT: {
      const unsigned long toggle_ms = 97;  // 5 hz
      if (now - last >= toggle_ms) {
        blink_state = !blink_state;
        this->setLedState(LED_PIN_BL, blue_led_current_state, blink_state);
        last = now;
      }
      break;
    } 

    case WifiState::CONNECTING: {
      const unsigned long toggle_ms = 251;  // 2 hz
      if (now - last >= toggle_ms) {
        blink_state = !blink_state;
        this->setLedState(LED_PIN_BL, blue_led_current_state, blink_state);
        last = now;
      }
      break;
    } 

    case WifiState::CONNECTED: {
      const unsigned long toggle_ms = 503;  // 1 hz
      if (now - last >= toggle_ms) {
        blink_state = !blink_state;
        this->setLedState(LED_PIN_BL, blue_led_current_state, blink_state);
        last = now;
      }
      break;
    } 

    default:
      this->setLedState(LED_PIN_BL, blue_led_current_state, false); // off
      break;

  }

}
  
// Small wrapper for LED GPIO operations
void DisplayManager::setLedState(uint8_t pin, bool &current_state, bool new_state) {
  if (current_state != new_state) {
    digitalWrite(pin, new_state ? HIGH : LOW);
    current_state = new_state;
  }
}