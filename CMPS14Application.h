#pragma once

#include "globals.h"
#include "CMPS14Sensor.h"
#include "CMPS14Processor.h"
#include "CMPS14Preferences.h"
#include "SignalKBroker.h"
#include "DisplayManager.h"
#include "WebUiManager.h"

class CMPS14Application {

  public:
  
    explicit CMPS14Application();

    void begin();
    void loop();
    bool compassOk() const { return compass_ok; }

  private:

    // CMPS14 I2C address
    static constexpr uint8_t CMPS14_ADDR = 0x60;

    // SH-ESP32 default pins for I2C
    static constexpr uint8_t I2C_SDA = 16;
    static constexpr uint8_t I2C_SCL = 17;

    static constexpr unsigned long MIN_TX_INTERVAL_MS    = 101;         // Max frequency for sending deltas to SignalK
    static constexpr unsigned long MINMAX_TX_INTERVAL_MS = 997;         // Frequency for pitch/roll maximum values sending
    static constexpr unsigned long READ_MS               = 47;          // Frequency to read values from CMPS14 in loop()
    static constexpr unsigned long CAL_POLL_MS           = 499;         // Frequency to poll calibration status in loop() 
    static constexpr unsigned long WIFI_STATUS_CHECK_MS  = 503;         // Frequency to check wifi status
    static constexpr unsigned long WIFI_TIMEOUT_MS       = 90001;       // Try WiFi connection max 1.5 minutes
    static constexpr unsigned long WS_RETRY_MS           = 1999;        // Shortest reconnect delay for SignalK websocket
    static constexpr unsigned long WS_RETRY_MAX          = 119993;      // Max reconnect delay for SignalK websocket

    // Timers
    unsigned long expn_retry_ms      = WS_RETRY_MS;
    unsigned long next_ws_try_ms     = 0;
    unsigned long last_tx_ms         = 0;   
    unsigned long last_minmax_tx_ms  = 0;         
    unsigned long last_read_ms       = 0;
    unsigned long last_cal_poll_ms   = 0;
    unsigned long wifi_conn_start_ms = 0;
    unsigned long wifi_last_check_ms = 0;

    bool compass_ok = false;

    enum class WifiState {
      INIT, CONNECTING, CONNECTED, FAILED, DISCONNECTED, OFF
    };

    WifiState wifi_state = WifiState::INIT;

    // Core instances for app
    CMPS14Sensor sensor;
    CMPS14Processor compass;
    CMPS14Preferences compass_prefs;
    SignalKBroker signalk;
    DisplayManager display;
    WebUIManager webui;

    // Handlers for loop - timers and operations
    void handleWifi(unsigned long now);
    void handleOTA();
    void handleWebUI();
    void handleWebsocket(unsigned long now);
    void handleCompass(unsigned long now);
    void handleSignalK(unsigned long now);
    void handleDisplay();

    void initWifiServices();

};