#include "display.h"

// Helper for safe LCD printing
static inline void copy16(char* dst, const char* src) {
  strncpy(dst, src, 16);   // copy max 16 characters
  dst[16] = '\0';          // ensure 0 termination
}

// Initialize LCD screen
void initLCD() {

  uint8_t addr = 0;
  if (i2cAvailable(LCD_ADDR1)) addr = LCD_ADDR1;
  else if (i2cAvailable(LCD_ADDR2)) addr = LCD_ADDR2;

  if (addr) {
    lcd = std::make_unique<LiquidCrystal_I2C>(addr, 16, 2);
    lcd->init();
    lcd->backlight();
    lcd_present = true;
  } else {
    lcd_present = false;
  }
}

// LCD basic printing on two lines
void updateLCD(const char* l1, const char* l2, bool hold) {
  if (!lcd_present) return;
  if (!strcmp(prev_top, l1) && !strcmp(prev_bot, l2)) return;

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

  if (hold) lcd_hold_ms = millis() + LCD_MS;

}

// LED indicator for calibration mode, blue led at GPIO2
void updateLedByCalMode(){
  static unsigned long last = 0;
  static bool state = false;
  const unsigned long now = millis();

  switch (cal_mode_runtime){
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
void updateLedByConnStatus(){
  static unsigned long last = 0;
  static bool state = false;
  const unsigned long now = millis();

  if (LCD_ONLY) {
    const unsigned long toggle_ms = 1009;
    if (now - last >= toggle_ms) {
      state = !state;
      digitalWrite(LED_PIN_GR, state ? HIGH : LOW);
      last = now;
    }
    return;
  }

  if (ws_open) {
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