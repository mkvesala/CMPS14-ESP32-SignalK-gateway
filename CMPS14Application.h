#pragma once

#include <Arduino.h>
#include <Wire.h>
#include <WiFi.h>
#include <ArduinoOTA.h>
#include <esp_system.h>
#include "WifiState.h"
#include "CMPS14Sensor.h"
#include "CMPS14Processor.h"
#include "CMPS14Preferences.h"
#include "SignalKBroker.h"
#include "DisplayManager.h"
#include "WebUIManager.h"

// === C M P S 1 4 A P P L I C A T I O N  C L A S S ===
//
// - Class CMPS14Application - "the app" responsible of orchestrating everything
// - Owns:
//   - CMPS14Sensor, "the sensor"
//   - CMPS14Processor, "the compass"
//   - CMPS14Preferences, "the compass_prefs"
//   - SignalKBroker, "the signalk"
//   - DisplayManager, "the display"
//   - WebUIManager, "the webui"
// - Uses: WifiState, CalMode
// - Init: app.begin() - called in setup() of the main program
// - Loop: app.loop() - called in loop() of the main program
// - Critical check: app.compassOk() - without the compass, well, it's not a compass

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
    static constexpr unsigned long WS_RETRY_MAX_MS       = 119993;      // Max reconnect delay for SignalK websocket
    static constexpr unsigned long MEM_CHECK_MS          = 120007;      // Memory check every 2 mins to LCD - debug
    static constexpr unsigned long RUNTIME_CHECK_MS      = 59999;       // Runtime monitoring of app.loop() - debug

    // Timers
    unsigned long expn_retry_ms         = WS_RETRY_MS;
    unsigned long next_ws_try_ms        = 0;
    unsigned long last_tx_ms            = 0;   
    unsigned long last_minmax_tx_ms     = 0;         
    unsigned long last_read_ms          = 0;
    unsigned long last_cal_poll_ms      = 0;
    unsigned long wifi_conn_start_ms    = 0;
    unsigned long wifi_last_check_ms    = 0;
    unsigned long last_mem_check_ms     = 0; // Debug
    unsigned long last_runtime_check_ms = 0; // Debug

    // Debug app.loop() runtime monitoring
    float loop_avg_us = 0.0f;
    bool monitoring = false;

    bool compass_ok = false;

    WifiState wifi_state = WifiState::INIT;

    // Core instances for app
    CMPS14Sensor sensor;
    CMPS14Processor compass;
    CMPS14Preferences compass_prefs;
    SignalKBroker signalk;
    DisplayManager display;
    WebUIManager webui;

    // Handlers for loop - timers and operations
    void handleWifi(const unsigned long now);
    void handleOTA();
    void handleWebUI();
    void handleWebsocket(const unsigned long now);
    void handleCompass(const unsigned long now);
    void handleSignalK(const unsigned long now);
    void handleMemory(const unsigned long now); // Debug
    void handleDisplay();

    void initWifiServices();
    
    void monitorLoopRuntime(const unsigned long us); // Debug 
    void handleLoopRuntime(const unsigned long now); // Debug

};