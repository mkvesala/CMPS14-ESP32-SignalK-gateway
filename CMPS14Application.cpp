#include "CMPS14Application.h"

// === P U B L I C ===

// Constructor
CMPS14Application::CMPS14Application():
  sensor(CMPS14_ADDR),
  compass(sensor),
  compass_prefs(compass),
  signalk(compass),
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

  // Init WiFi
  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false);
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  wifi_state = WifiState::CONNECTING;
  wifi_conn_start_ms = millis();
  display.showInfoMessage("WIFI", "CONNECTING");
  display.setWifiState(wifi_state);

  // Compass ok?
  display.showSuccessMessage("CMPS14 INIT", compass_ok);
}

// Repeat stuff
void CMPS14Application::loop() {

  const unsigned long now = millis();
  this->handleWifi(now);
  this->handleOTA();
  this->handleWebUI();
  this->handleWebsocket(now);
  this->handleCompass(now);
  this->handleSignalK(now);
  this->handleDisplay();

}

// === P R I V A T E ===

// Wifi
void CMPS14Application::handleWifi(unsigned long now) {
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
        IPAddress ip = WiFi.localIP();
        display.setWifiInfo(rssi, ip[0], ip[1], ip[2], ip[3]);
        display.showSuccessMessage("WIFI CONNECT", true);
        display.showWifiStatus();
        display.setWifiState(wifi_state);
        this->initWifiServices(); // Init wifi-dependent stuff
        expn_retry_ms = WS_RETRY_MS;
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
      if(!WiFi.isConnected()) {
        wifi_state = WifiState::DISCONNECTED;
        display.showInfoMessage("WIFI", "LOST");
        WiFi.disconnect();
        WiFi.begin(WIFI_SSID, WIFI_PASS);
        wifi_state = WifiState::CONNECTING;
        display.setWifiState(wifi_state);
        wifi_conn_start_ms = now;
      }
      break;
    }

    case WifiState::FAILED:
    case WifiState::DISCONNECTED: // Todo add retry?
    case WifiState::OFF:
      break;
  }

}

// OTA
void CMPS14Application::handleOTA() {
  if (!WiFi.isConnected()) return;
  ArduinoOTA.handle();
}

// Webserver
void CMPS14Application::handleWebUI() {
  if (!WiFi.isConnected()) return;
  webui.handleRequest();
}

// Websocket poll and reconnect
void CMPS14Application::handleWebsocket(unsigned long now) {
  if (!WiFi.isConnected()) {
    compass.setUseManualVariation(true);
    return;
  }
  signalk.handleStatus();
  
  if (!signalk.isOpen() && (long)(now - next_ws_try_ms) >= 0){ 
      display.showInfoMessage("SK WEBSOCKET", "CONNECTING");
      signalk.connectWebsocket();
      next_ws_try_ms = now + expn_retry_ms;
      expn_retry_ms = min(expn_retry_ms * 2, WS_RETRY_MAX);
  }
  if (signalk.isOpen()) expn_retry_ms = WS_RETRY_MS;
  else compass.setUseManualVariation(true);
}

// Compass
void CMPS14Application::handleCompass(unsigned long now) {
  
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
void CMPS14Application::handleSignalK(unsigned long now) {
  if (!WiFi.isConnected()) return;

  // Send heading, pitch and roll to SignalK server
  if ((long)(now - last_tx_ms) >= MIN_TX_INTERVAL_MS) {
    last_tx_ms = now;
    signalk.sendHdgPitchRollDelta();
  }

  // Send pitch and roll min and max to SignalK server
  if ((long)(now - last_minmax_tx_ms) >= MINMAX_TX_INTERVAL_MS) {
    last_minmax_tx_ms = now;
    signalk.sendPitchRollMinMaxDelta();
  }

}

// LCD and LEDs
void CMPS14Application::handleDisplay() {

  display.handle();
  
}

// Init wifi-dependent stuff
void CMPS14Application::initWifiServices() {
  // SignalK websocket
  signalk.begin();

  // OTA
  ArduinoOTA.setHostname(signalk.getSignalKSource());
  ArduinoOTA.setPassword(WIFI_PASS);
  ArduinoOTA.onStart([this](){
    display.showInfoMessage("OTA UPDATE", "UPLOADING");
  });
  ArduinoOTA.onEnd([]() {});
  ArduinoOTA.onProgress([](unsigned int progress, unsigned int total){});
  ArduinoOTA.onError([] (ota_error_t error) {});
  ArduinoOTA.begin();

  // Webserver handlers
  webui.begin();
}