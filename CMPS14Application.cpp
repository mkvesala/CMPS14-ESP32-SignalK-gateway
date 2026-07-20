#include "CMPS14Application.h"
#include "secrets.h"

// === P U B L I C ===

// Constructor
CMPS14Application::CMPS14Application():
  sensor(CMPS14_ADDR),
  compass(sensor),
  compass_prefs(compass),
  signalk(compass),
  espnow(compass),
  display(compass, signalk),
  webui(compass, compass_prefs, signalk, display) {}

// Init non-wifi-dependent stuff
void CMPS14Application::begin() {

  // Init I2C
  Wire.begin(I2C_SDA, I2C_SCL);
  delay(47);
  Wire.setClock(400000);
  delay(47);

  // Init display
  display.begin();

  // Init compass
  compass_ok = compass.begin(Wire);

  // Get saved configuration from ESP32 preferences
  compass_prefs.load();

  // Init appropriate calibration mode or use-mode
  compass.initCalibrationModeBoot();
  
  // Stop bluetooth
  btStop(); 

  // Init WiFi (AP_STA mode enables ESP-NOW alongside WiFi).
  // softAP() secures the AP interface immediately — hidden SSID, WPA2, single connection max.
  WiFi.mode(WIFI_AP_STA);
  WiFi.softAP(AP_SSID, AP_PASS, 1 /*channel*/, 1 /*ssid_hidden*/, 1 /*max_connection*/);

  // Register AP intruder callback before WiFi.begin() so no event is missed.
  // Callback runs in FreeRTOS "arduino_events" task: deauth immediately, flag loop().
  // MAC is copied before setting the flag so loop() always reads a complete address.
  WiFi.onEvent([this](arduino_event_id_t /*id*/, arduino_event_info_t info) {
    uint8_t aid = info.wifi_ap_staconnected.aid;
    memcpy(ap_intruder_mac, info.wifi_ap_staconnected.mac, 6);
    esp_wifi_deauth_sta(aid);
    ap_intruder = true;
  }, ARDUINO_EVENT_WIFI_AP_STACONNECTED);

  this->applyStaticIP();

  WiFi.setSleep(false);
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  wifi_state = WifiState::CONNECTING;
  wifi_conn_start_ms = millis();
  display.showInfoMessage("WIFI", "CONNECTING");
  display.setWifiState(wifi_state);

  // Init ESP-NOW 
  display.showSuccessMessage("ESPNOW INIT", espnow.begin());

  // Compass ok?
  display.showSuccessMessage("CMPS14 INIT", compass_ok);

}

// Repeat stuff
void CMPS14Application::loop() {

  const unsigned long loop_start = micros();   // Debug
  const unsigned long now = millis();
  this->handleWifi(now);
  this->handleAPIntruder();
  this->handleOTA();
  this->handleWebUI();
  this->handleWebsocket(now);
  this->handleWatchdog(now);
  this->handleCompass(now);
  this->handleSignalK(now);
  this->handleESPNow(now);
  this->handleMemory(now); // Debug
  this->handleDisplay();
  const unsigned long loop_runtime = micros() - loop_start; // Debug
  this->monitorLoopRuntime(loop_runtime); // Debug
  this->handleLoopRuntime(now); // Debug

}

// === P R I V A T E ===

// Wifi
void CMPS14Application::handleWifi(const unsigned long now) {
  if ((long)(now - wifi_last_check_ms) < WIFI_STATUS_CHECK_MS) {
    return;
  }
  wifi_last_check_ms = now;
  switch (wifi_state) {
    
    case WifiState::INIT:
      break;
    
    case WifiState::CONNECTING: {
      wl_status_t status = WiFi.status();
      if (status == WL_CONNECTED) {
        wifi_state = WifiState::CONNECTED;
        int32_t rssi = WiFi.RSSI();
        uint32_t ip = (uint32_t)WiFi.localIP();
        display.setWifiInfo(rssi, ip);
        display.showSuccessMessage("WIFI CONNECT", true);
        display.showWifiStatus();
        display.setWifiState(wifi_state);
        this->initWifiServices(); // Init wifi-dependent stuff
        expn_retry_ms = WS_RETRY_MS;
        next_ws_try_ms = now; // discard stale pre-outage backoff timestamp
      }
      else if ((long)(now - wifi_conn_start_ms) >= WIFI_TIMEOUT_MS) {
        wifi_state = WifiState::FAILED;
        display.showSuccessMessage("WIFI CONNECT", false);
        WiFi.disconnect(true);
        WiFi.mode(WIFI_OFF);
        wifi_state = WifiState::OFF;
        display.setWifiState(wifi_state);
      }
      else if (status == WL_CONNECT_FAILED || status == WL_NO_SSID_AVAIL) {
        wifi_state = WifiState::FAILED;
        display.showSuccessMessage("WIFI CONNECT", false);
        WiFi.disconnect(true);
        WiFi.mode(WIFI_OFF);
        wifi_state = WifiState::OFF;
        display.setWifiState(wifi_state);
      }
      break;
    }

    case WifiState::CONNECTED: {
      int32_t rssi = WiFi.RSSI();
      display.setWifiInfo(rssi, (uint32_t)WiFi.localIP());
      if (!WiFi.isConnected()) {
        wifi_reconnect_count++;
        char msg[16];
        snprintf(msg, sizeof(msg), "RECONNECT #%u", wifi_reconnect_count);
        display.showInfoMessage("WIFI LOST", msg);
        signalk.closeWebsocket();    // safe even if TCP already dead
        wifi_state = WifiState::CONNECTING;
        WiFi.disconnect(true);       // wifioff=true: full STA teardown, AP_STA recovers
        delay(200);                  // let radio settle before reconnecting
        WiFi.setSleep(false);        // STA teardown resets this — reapply
        this->applyStaticIP();       // static config doesn't survive STA teardown
        WiFi.begin(WIFI_SSID, WIFI_PASS);
        display.setWifiState(wifi_state);
        wifi_conn_start_ms = now;
      } else if ((long)(now - last_rssi_display_ms) >= RSSI_DISPLAY_MS) {
        last_rssi_display_ms = now;
        char rssi_msg[16];
        snprintf(rssi_msg, sizeof(rssi_msg), "%d dBm", rssi);
        display.showInfoMessage("WIFI RSSI", rssi_msg);
      }
      break;
    }

    case WifiState::FAILED:
    case WifiState::DISCONNECTED: // Todo add retry?
    case WifiState::OFF:
      break;
  }

}

// AP intruder alert — deauth already done in event callback; log + display here
void CMPS14Application::handleAPIntruder() {
  if (!ap_intruder) return;
  ap_intruder = false;
  char mac[18];
  snprintf(mac, sizeof(mac), "%02X:%02X:%02X:%02X:%02X:%02X",
           ap_intruder_mac[0], ap_intruder_mac[1], ap_intruder_mac[2],
           ap_intruder_mac[3], ap_intruder_mac[4], ap_intruder_mac[5]);
  display.showInfoMessage("AP: INTRUDER!", mac);
}

// OTA
void CMPS14Application::handleOTA() {
  if (wifi_state != WifiState::CONNECTED) return;
  ArduinoOTA.handle();
}

// Webserver
void CMPS14Application::handleWebUI() {
  if (wifi_state != WifiState::CONNECTED) return;
  webui.handleRequest();
}

// Websocket poll and reconnect
void CMPS14Application::handleWebsocket(const unsigned long now) {
  if (wifi_state != WifiState::CONNECTED) {
    compass.setUseManualVariation(true);
    return;
  }
  signalk.handleStatus();   // poll() — also delivers the GotPong event that feeds isStale()

  if (signalk.isOpen()) {
    last_ws_activity_ms = now;             // feeds network watchdog
    expn_retry_ms = WS_RETRY_MS;           // reset backoff only when already open
    if ((long)(now - last_ping_ms) >= (long)WS_PING_MS) {
      signalk.ping();                      // active liveness probe
      last_ping_ms = now;
    }
    if (signalk.isStale(now)) {            // half-open TCP: open but no pong within timeout
      signalk.closeWebsocket();           // next iterations reconnect via existing backoff
    }
  } else {
    compass.setUseManualVariation(true);
    if ((long)(now - next_ws_try_ms) >= 0) {
      display.showInfoMessage("SK WEBSOCKET", "CONNECTING");
      signalk.connectWebsocket();
      next_ws_try_ms = now + expn_retry_ms;
      expn_retry_ms = min(expn_retry_ms * 2, WS_RETRY_MAX_MS);
    }
  }
}

// Watchdog — restart on bloated loop runtime, or if WiFi layer-2 up but TCP/IP stack silently dead
void CMPS14Application::handleWatchdog(const unsigned long now) {
  // Loop runtime watchdog: restart if ws.connect() or other blocking call pushes EMA above threshold
  if (monitoring && loop_avg_us > LOOP_WATCHDOG_US) {
    compass_prefs.saveResetReason(ResetReason::LOOP_WATCHDOG);
    display.showInfoMessage("LOOP WATCHDOG", "RESTARTING...");
    delay(1999);
    ESP.restart();
  }
  // Network watchdog: restart if WiFi layer-2 appears connected but TCP/IP stack is silently dead
  if (wifi_state != WifiState::CONNECTED) return;
  if (signalk.isOpen()) return;
  if (last_ws_activity_ms == 0) return;  // never connected — no restart without a prior session
  if ((long)(now - last_ws_activity_ms) < WS_WATCHDOG_MS) return;
  compass_prefs.saveResetReason(ResetReason::NETWORK_WATCHDOG);
  display.showInfoMessage("WATCHDOG", "RESTARTING...");
  delay(1999);
  ESP.restart();
}

// Compass
void CMPS14Application::handleCompass(const unsigned long now) {
  
  if ((long)(now - last_read_ms) >= READ_MS) {
    last_read_ms = now; 
    compass.update();                                               
  }

  // Monitor calibration status
  if ((long)(now - last_cal_poll_ms) >= CAL_POLL_MS) {
    last_cal_poll_ms = now;
    compass.monitorCalibration(compass.getCalibrationModeRuntime() == CalMode::AUTO);
  }

  // Monitor FULL AUTO mode timeout
  if (compass.getCalibrationModeRuntime() == CalMode::FULL_AUTO && compass.getFullAutoTimeout() > 0) { 
    long left = compass.getFullAutoTimeout() - (now - compass.getFullAutoStart());
    if (left <= 0) {
      if (compass.stopCalibration()) display.showInfoMessage("FULL AUTO", "TIMEOUT");
      left = 0;
    }
    compass.setFullAutoLeft(left);
  }

}

// SignalK delta sending
void CMPS14Application::handleSignalK(const unsigned long now) {
  if (wifi_state != WifiState::CONNECTED) return;

  // Send heading, pitch and roll to SignalK server
  if ((long)(now - last_tx_ms) >= MIN_TX_INTERVAL_MS) {
    last_tx_ms = now;
    signalk.sendHdgPitchRollDelta();
  }

}

// ESP-NOW broadcast
void CMPS14Application::handleESPNow(const unsigned long now) {
  if ((long)(now - last_espnow_tx_ms) < ESPNOW_TX_INTERVAL_MS) return;
  last_espnow_tx_ms = now;
  espnow.sendHeadingDelta();
}

// LCD and LEDs
void CMPS14Application::handleDisplay() {

  display.handle();
  
}

// Init wifi-dependent stuff
void CMPS14Application::initWifiServices() {
  if (wifi_services_initialized) return;
  wifi_services_initialized = true;

  // SignalK websocket
  signalk.begin();

  // OTA
  ArduinoOTA.setHostname(signalk.getSignalKSource());
  ArduinoOTA.setPassword(OTA_PASS);
  // ArduinoOTA.onStart([](){});
  // ArduinoOTA.onEnd([](){});
  // ArduinoOTA.onProgress([](unsigned int progress, unsigned int total){});
  // ArduinoOTA.onError([](ota_error_t error) {});
  ArduinoOTA.begin();

  // Webserver handlers
  webui.begin();
}

// Apply static IP/gateway/subnet from secrets.h.
// WiFi.disconnect(true) discards this config, so it must be reapplied
// before every WiFi.begin() — both at boot and during hardened reconnect.
void CMPS14Application::applyStaticIP() {
  IPAddress ip, gateway, subnet;
  ip.fromString(WIFI_STATIC_IP);
  gateway.fromString(WIFI_GATEWAY);
  subnet.fromString(WIFI_SUBNET);
  if (!WiFi.config(ip, gateway, subnet, gateway)) {
    display.showInfoMessage("STATIC IP", "CONFIG FAILED");
  }
}

// Debug: show memory status
void CMPS14Application::handleMemory(const unsigned long now) {
  if ((long)(now - last_mem_check_ms) < MEM_CHECK_MS) return;
  last_mem_check_ms = now;

  uint16_t heap_free = ESP.getFreeHeap() / 1024;
  uint16_t heap_total = ESP.getHeapSize() / 1024;
  uint8_t heap_percent = (heap_free * 100) / heap_total;

  char line1[17];
  char line2[17];
  
  snprintf(line1, sizeof(line1), "MEM: %u%%", heap_percent);
  snprintf(line2, sizeof(line2), "%uK/%uK", heap_free, heap_total);
  
  display.showInfoMessage(line1, line2);
}

// Debug: monitor exponential movig avg runtime of app.loop() in microseconds
void CMPS14Application::monitorLoopRuntime(const unsigned long us) {
  
  // Track the raw peak for diagnostics — reported and reset by handleLoopRuntime()
  if (us > loop_peak_us) loop_peak_us = us;

  // Clamp before the EMA: one blocking call (ws.connect(), WiFi teardown) must not dominate
  // the average. The watchdog detects sustained degradation, not a one-off network stall.
  const unsigned long sample = min(us, LOOP_SAMPLE_CAP_US);

  // EMA: alpha = 0.01 (1% new data, 99% history avg), forgets 95 % of history after ~600 iterations
  if (!monitoring) {
    loop_avg_us = sample;
    monitoring = true;
  } else {
    loop_avg_us = 0.01 * sample + 0.99 * loop_avg_us;
  }

}

// Debug: display app.loop() runtime stats on LCD and provide data to web UI
void CMPS14Application::handleLoopRuntime(const unsigned long now) {

  if ((long)(now - last_runtime_check_ms) < RUNTIME_CHECK_MS) return;
  last_runtime_check_ms = now;

  char l1[17];
  char l2[17];

  snprintf(l1, sizeof(l1), "LOOP AVG/PEAK ms");
  snprintf(l2, sizeof(l2), "%.1f/%lu ms", loop_avg_us / 1000.0f, loop_peak_us / 1000UL);
  display.showInfoMessage(l1, l2);
  webui.setLoopRuntimeInfo(loop_avg_us, loop_peak_us);
  loop_peak_us = 0;   // peak window restarts on every report

}

