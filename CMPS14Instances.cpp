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

// Init basically everything
void CMPS14Application::begin() {

  // Init I2C
  Wire.begin(I2C_SDA, I2C_SCL);
  delay(47);
  Wire.setClock(400000);
  delay(47);

  // Init display
  display.begin();

  // Init compass
  compass_ok = compass.begin(Wire));

  // Get saved configuration from ESP32 preferences
  compass_prefs.load();

  // Init appropriate calibration mode or use-mode
  calmode_ok = compass.initCalibrationModeBoot();

  // Stop bluetooth
  btStop(); 

  // Init WiFi
  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false);
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  unsigned long t0 = millis();
  while (!WiFi.isConnected() && (long)(millis() - t0) < WIFI_TIMEOUT_MS) { delay(250); } 

  // Execute if WiFi successfully connected
  if (WiFi.isConnected()) {

    display.setWifiInfo(WiFi.RSSI(), WiFi.localIP());

    // SignalK websocket
    signalk.begin();

    // OTA
    ArduinoOTA.setHostname(signalk.getSignalKSource());
    ArduinoOTA.setPassword(WIFI_PASS);
    ArduinoOTA.onStart([](){});
    ArduinoOTA.onEnd([]() { ota_ok = true; });
    ArduinoOTA.onProgress([](unsigned int progress, unsigned int total){});
    ArduinoOTA.onError([] (ota_error_t error) { ota_ok = false; });
    ArduinoOTA.begin();

    // Webserver handlers
    webui.begin();

  // No WiFi connection, use only LCD output and power off WiFi 
  } else {  
    WiFi.disconnect(true);
    WiFi.mode(WIFI_OFF); 
  }

}

// Heavily blocking status update
// Todo: implement message queue in DisplayManager
void CMPS14Application::status() {
  display.showSuccessMessage("CAL MODE INIT", calmode_ok);
  delay(1009);
  if (calmode_ok) display.showInfoMessage("CAL MODE INIT", calModeToString(compass.getCalibrationModeRuntime()));
  delay(1009);
  display.showSuccessMessage("WIFI CONNECT", WiFi.isConnected());
  delay(1009);
  display.showWifiStatus();
  delay(1009);
  display.showSuccessMessage("SK WEBSOCKET", signalk.isOpen());
  delay(1009);
  display.showSuccessMessage("OTA INIT", ota_ok);
  delay(1009);
  display.showSuccessMessage("CMPS14 INIT", compass_ok);
  delay(1009);
}

// Repeat stuff
void CMPS14Application::loop() {

  const unsigned long now = millis();
  this->handleOTA();
  this->handleWebUI();
  this->handleCompass(now);
  this->handleSignalK(now);
  this->handleDisplay(now);

}

// === P R I V A T E ===

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

// Compass
void CMPS14Application::handleCompass() {
  
  if ((long)(now - last_read_ms) >= READ_MS) {
    last_read_ms = now; 
    compass.update();                                               
  }

  // Monitor calibration status
  if ((long)(now - last_cal_poll_ms) >= CAL_POLL_MS) {
    last_cal_poll_ms = now;
    compass.monitorCalibration(compass.getCalibrationModeRuntime() == CAL_SEMI_AUTO);
  }

  // Monitor FULL AUTO mode timeout
  if (compass.getCalibrationModeRuntime() == CAL_FULL_AUTO && compass.getFullAutoTimeout() > 0) { 
    long left = compass.getFullAutoTimeout() - (now - compass.getFullAutoStart());
    if (left <= 0) {
      if (compass.stopCalibration()) display.showInfoMessage("FULL AUTO", "TIMEOUT", true);
      left = 0;
    }
    compass.setFullAutoLeft(left);
  }

}

// SignalK websocket
void CMPS14Application::handleSignalK(unsigned long now) {
  if (!WiFi.isConnected()) return;
  
  signalk.handleStatus();
  
  if (!signalk.isOpen() && (long)(now - next_ws_try_ms) >= 0){ 
      display.showInfoMessage("SK WEBSOCKET", "CONNECTING");
      signalk.connectWebsocket();
      next_ws_try_ms = now + expn_retry_ms;
      expn_retry_ms = min(expn_retry_ms * 2, WS_RETRY_MAX);
  }
  if (signalk.isOpen()) expn_retry_ms = WS_RETRY_MS;
  else compass.setUseManualVariation(true);

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
  
  // Display heading (T or M) on LCD
  if ((long)(now - last_lcd_ms) >= LCD_MS) {                      
    last_lcd_ms = now;
    if (now >= display.getTimeToShow() + LCD_MS) {
      display.showHeading();
    }
  }

  // Led indicators
  display.showCalibrationStatus();
  display.showConnectionStatus();  
  
}