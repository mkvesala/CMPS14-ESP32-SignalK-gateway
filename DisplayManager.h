#pragma once

#include <memory>
#include "globals.h"
#include "CMPS14Processor.h"
#include "CalMode.h"
#include "SignalKBroker.h"

class DisplayManager {

  public:

    explicit DisplayManager(CMPS14Processor &compassref, SignalKBroker &signalkref);

    bool begin();
    void showSuccessMessage(const char* who, bool success, bool hold = false);
    void showInfoMessage(const char* who, const char* what, bool hold = false);
    void showWifiStatus(bool hold = false);
    void showHeading();
    void showCalibrationStatus();
    void showConnectionStatus();

    void setTimeToShow(unsigned long ms) { lcd_hold_ms = ms; }
    void setWifiInfo(int rssi, IPAddress ip);

    unsigned long getTimeToShow() const { return lcd_hold_ms; }
    const char* getWifiQuality() const { return RSSIc; }
    const char* getWifiIPAddress() const { return IPc; }

  private:

    void updateLCD(const char* l1, const char* l2, bool hold = false);
    bool initLCD();
    void updateBlueLed();
    void updateGreenLed();
    bool i2cAvailable(uint8_t addr);

    void copy16(char* dst, const char* src);
    void setRSSIc(int rssi);
    void setIPc(IPAddress ip);

  private:

    CMPS14Processor &compass;
    SignalKBroker &signalk;
    std::unique_ptr<LiquidCrystal_I2C> lcd;

    static constexpr uint8_t LED_PIN_BL = 2;
    static constexpr uint8_t LED_PIN_GR = 13;
    static constexpr uint8_t LCD_ADDR1 = 0x27;
    static constexpr uint8_t LCD_ADDR2 = 0x3F;

    unsigned long lcd_hold_ms = 0;

    char RSSIc[16];
    char IPc[16];
    char prev_top[17] = "";
    char prev_bot[17] = "";

    bool lcd_present = false;

};