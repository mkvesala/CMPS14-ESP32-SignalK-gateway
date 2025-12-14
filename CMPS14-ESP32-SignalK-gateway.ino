#include "globals.h"
#include "CMPS14Instances.h"
#include "webui.h"
#include "CalMode.h"

static constexpr unsigned long MIN_TX_INTERVAL_MS    = 101;         // Max frequency for sending deltas to SignalK
static constexpr unsigned long MINMAX_TX_INTERVAL_MS = 997;         // Frequency for pitch/roll maximum values sending
static constexpr unsigned long READ_MS               = 47;          // Frequency to read values from CMPS14 in loop()
static constexpr unsigned long CAL_POLL_MS           = 499;         // Frequency to poll calibration status in loop() 
static constexpr unsigned long WIFI_TIMEOUT_MS       = 90001;       // Try WiFi connection max 1.5 minutes
static constexpr unsigned long WS_RETRY_MS           = 1999;        // Shortest reconnect delay for SignalK websocket
static constexpr unsigned long WS_RETRY_MAX          = 119993;      // Max reconnect delay for SignalK websocket
static constexpr unsigned long LCD_MS                = 1009;        // Frequency to show heading on LCD

// ===== S E T U P ===== //
void setup() {

  Serial.begin(115200);
  delay(47);

  Wire.begin(I2C_SDA, I2C_SCL);
  delay(47);

  Wire.setClock(400000);
  delay(47);

  display.begin();
  delay(47);

  // Init compass
  if (!compass.begin(Wire)) {
    display.showSuccessMessage("CMPS14 INIT", false);
    while(true);
  }
  delay(47);

  // Get saved configuration from ESP32 preferences
  compass_prefs.load();

  // Init appropriate calibration mode or use-mode
  if (compass.initCalibrationModeBoot()) display.showInfoMessage("CAL MODE INIT", calModeToString(compass.getCalibrationModeRuntime()));
  else display.showSuccessMessage("CAL MODE INIT", false);
  delay(1009);

  // Stop bluetooth
  btStop(); 

  // Init WiFi
  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false);
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  display.showInfoMessage("WIFI", "CONNECTING");
  unsigned long t0 = millis();

  // Try to connect WiFi until timeout
  while (!WiFi.isConnected() && (long)(millis() - t0) < WIFI_TIMEOUT_MS) { delay(250); } 

  // Execute if WiFi successfully connected
  if (WiFi.isConnected()) {  

    display.setWifiInfo(WiFi.RSSI(), WiFi.localIP());
    display.showWifiStatus();
    delay(1009);

    display.showSuccessMessage("SK WEBSOCKET", signalk.begin());
    delay(1009);

    // OTA
    ArduinoOTA.setHostname(signalk.getSignalKSource());
    ArduinoOTA.setPassword(WIFI_PASS);
    ArduinoOTA.onStart([](){});
    ArduinoOTA.onEnd([]() {});
    ArduinoOTA.onProgress([](unsigned int progress, unsigned int total){});
    ArduinoOTA.onError([] (ota_error_t error) {});
    ArduinoOTA.begin();

    // Webserver handlers
    setupWebserverCallbacks();

  // No WiFi connection, use only LCD output and power off WiFi 
  } else {  
    WiFi.disconnect(true);
    WiFi.mode(WIFI_OFF); 
    display.showInfoMessage("WIFI", "NOT AVAILABLE");
    delay(1009);
  }

}

// ===== L O O P ===== //
void loop() {

  // Timers
  const unsigned long now = millis();
  static unsigned long expn_retry_ms      = WS_RETRY_MS;
  static unsigned long next_ws_try_ms     = 0;
  static unsigned long last_tx_ms         = 0;   
  static unsigned long last_minmax_tx_ms  = 0;         
  static unsigned long last_read_ms       = 0;
  static unsigned long last_cal_poll_ms   = 0;                           
  static unsigned long last_lcd_ms        = 0;

  if (WiFi.isConnected()) { 
    
    // OTA
    // Todo: consider moving from loop to separate task
    ArduinoOTA.handle();                  
    
    // Webserver
    // Todo: consider moving from loop to separate task
    server.handleClient();   

    // Websocket
    // Todo: consider moving from loop to separate task
    signalk.handleStatus();             
  
    // Websocket reconnect and keep using manual variation if websocket not opened
    // Todo: consider moving from loop to separate task or to set a max retries counter (websocket connect is a slow operation)
    if (!signalk.isOpen() && (long)(now - next_ws_try_ms) >= 0){ 
      display.showInfoMessage("SK WEBSOCKET", "CONNECTING");
      signalk.connectWebsocket();
      next_ws_try_ms = now + expn_retry_ms;
      expn_retry_ms = min(expn_retry_ms * 2, WS_RETRY_MAX);
    }
    if (signalk.isOpen()) expn_retry_ms = WS_RETRY_MS;
    else compass.setUseManualVariation(true);
  }

  // Read values from CMPS14
  if ((long)(now - last_read_ms) >= READ_MS) {
    last_read_ms = now; 
    compass.update();                                               
  }

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
