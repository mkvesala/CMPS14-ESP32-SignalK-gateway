#pragma once

#include <memory>
#include "globals.h"
#include "CMPS14Processor.h"
#include "SignalKBroker.h"

class DisplayManager {

  public:

    explicit DisplayManager(CMPS14Processor &compassref, SignalKBroker &signalkref);

    bool begin();
    void handle();
    void showSuccessMessage(const char* who, bool success);
    void showInfoMessage(const char* who, const char* what);
    void showWifiStatus();
    
    void setWifiInfo(int rssi, IPAddress ip);

    const char* getWifiQuality() const { return RSSIc; }
    const char* getWifiIPAddress() const { return IPc; }

  private:

    void updateLCD(const char* l1, const char* l2);
    bool initLCD();
    void showHeading();
    void updateBlueLed();
    void updateGreenLed();
    bool i2cAvailable(uint8_t addr);

    void copy16(char* dst, const char* src);
    void setRSSIc(int rssi);
    void setIPc(IPAddress ip);

    bool pushMsgItem(const auto &msg);
    bool popMsgItem(auto &msg);
    bool fifoIsEmpty() const { return count == 0; }
    bool fifoIsFull() const { return count == FIFO_SIZE; }

    CMPS14Processor &compass;
    SignalKBroker &signalk;

    // Had strange issues with LCD in my previous project
    // which disappeared with unique_ptr/make_unique
    // so copied that to here as well.
    std::unique_ptr<LiquidCrystal_I2C> lcd;

    static constexpr uint8_t LED_PIN_BL = 2;
    static constexpr uint8_t LED_PIN_GR = 13;
    static constexpr uint8_t LCD_ADDR1 = 0x27;
    static constexpr uint8_t LCD_ADDR2 = 0x3F;
    static constexpr uint8_t FIFO_SIZE = 16;
    static constexpr unsigned long LCD_MS = 1009;
                       
    unsigned long last_lcd_ms = 0;

    // FIFO queue for LCD printing
    struct MsgItem {
      char l1[17];
      char l2[17];
    };

    MsgItem fifo[FIFO_SIZE];
    uint8_t head = 0;   // index to write next
    uint8_t tail = 0;   // index to read next
    uint8_t count = 0;  // number of items in the buffer

    char RSSIc[16];
    char IPc[16];
    char prev_top[17] = "";
    char prev_bot[17] = "";

    bool lcd_present = false;

};