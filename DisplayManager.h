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
    
    void setWifiInfo(int32_t rssi, uint8_t ip0, uint8_t ip1, uint8_t ip2, uint8_t ip3);
    void setWifiState(WifiState state);

    const char* getWifiQuality() const { return RSSIc; }
    const char* getWifiIPAddress() const { return IPc; }

  private:

    // LCD
    void updateLCD(const char* l1, const char* l2);
    bool initLCD();
    void showHeading();
    bool i2cAvailable(uint8_t addr);
    void copy16(char* dst, const char* src);
    
    // Wifi info
    void setRSSIc(int32_t rssi);
    void setIPc(uint8_t ip0, uint8_t ip1, uint8_t ip2, uint8_t ip3);

    // Fifo
    bool pushMsgItem(const auto &msg);
    bool popMsgItem(auto &msg);
    bool fifoIsEmpty() const { return count == 0; }
    bool fifoIsFull() const { return count == FIFO_SIZE; }

    // LEDs
    void updateBlueLed();
    void updateGreenLed();
    void setLedState(uint8_t pin, bool &current_state, bool new_state);

    CMPS14Processor &compass;
    SignalKBroker &signalk;
    LiquidCrystal_I2C lcd;

    static constexpr uint8_t LED_PIN_BL = 2;
    static constexpr uint8_t LED_PIN_GR = 13;
    static constexpr uint8_t LCD_ADDR = 0x27;
    static constexpr uint8_t FIFO_SIZE = 16;
    static constexpr unsigned long LCD_MS = 1499;
                       
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

    // Wifi info
    char RSSIc[16];
    char IPc[16];

    // Wifi state
    WifiState wifi_state = WifiState::INIT;

    // LCD content for strcmp
    char prev_top[17] = "";
    char prev_bot[17] = "";

    bool lcd_present = false;

    // Track the GPIO state of leds
    bool blue_led_current_state = false;
    bool green_led_current_state = false;

};