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

void DisplayManager::handle() {
  const unsigned long now = millis();
  if ((long)now - last_lcd_ms >= LCD_MS) {
    last_lcd_ms = now;
    if (this->fifoIsEmpty()) this->showHeading();
    MsgItem msg;
    if (this->popMsgItem(msg)) this->updateLCD(msg.l1, msg.l2);
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

// Show heading T/M
void DisplayManager::showHeading() {
  float heading_true_deg = compass.getHeadingTrueDeg();
  float heading_deg = compass.getHeadingDeg();
  if (compass.isSendingHeadingTrue() && validf(heading_true_deg)) {
    char buf[17];
    snprintf(buf, sizeof(buf), "      %03.0f%c", heading_true_deg, 223);
    MsgItem msg;
    this->copy16(msg.l1, "  HEADING (T):");
    this->copy16(msg.l2, buf);
    this->pushMsgItem(msg);
  } else if (validf(heading_deg)) {
    char buf[17];
    snprintf(buf, sizeof(buf), "      %03.0f%c", heading_deg, 223);
    MsgItem msg;
    this->copy16(msg.l1, "  HEADING (M):");
    this->copy16(msg.l2, buf);
    this->pushMsgItem(msg);
  }
}

void DisplayManager::setWifiInfo(int rssi, IPAddress ip) {
  this->setRSSIc(rssi);
  this->setIPc(ip);
}

// === P R I V A T E === //

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
  tail = (tail - 1) % FIFO_SIZE;
  count--;
  return true;
}

// LCD basic printing on two lines
void DisplayManager::updateLCD(const char* l1, const char* l2) {
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

}

// Initialize LCD screen
// Had strange issues with LCD in my previous project
// which disappeared with unique_ptr/make_unique
// so copied that to here as well.
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