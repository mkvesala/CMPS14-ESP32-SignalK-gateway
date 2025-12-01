#include "globals.h"
#include "CMPS14Instances.h"
#include "display.h"
#include "signalk.h"
#include "webui.h"
#include "OTA.h"

// CMPS14
CMPS14Sensor sensor(CMPS14_ADDR);
CMPS14Processor compass(sensor);

// ===== S E T U P ===== //
void setup() {

  Serial.begin(115200);
  delay(47);

  Wire.begin(I2C_SDA, I2C_SCL);
  delay(47);
  Wire.setClock(400000);
  delay(47);

  initLCD();
  delay(47);

  if (!compass.begin(Wire)) {
    updateLCD("CMPS14 ERROR", "INIT FAILED!");
    delay(UINT32_MAX);
  }
  delay(47);

  pinMode(LED_PIN_BL, OUTPUT);
  pinMode(LED_PIN_GR, OUTPUT);
  digitalWrite(LED_PIN_BL, LOW);
  digitalWrite(LED_PIN_GR, LOW);

  // Get saved configuration from ESP32 preferences
  loadSavedPreferences();

  // Init CMPS14 with appropriate calibration mode or use-mode
  compass.initCalibrationModeBoot(cal_mode_boot);
  delay(1009);

  // Stop bluetooth to save power
  btStop(); 

  // Init WiFi
  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false);
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  updateLCD("WIFI", "CONNECT...");
  unsigned long t0 = millis();

  // Try to connect WiFi until timeout
  while (!WiFi.isConnected() && (millis() - t0) < WIFI_TIMEOUT_MS) { delay(250); } 

  // Execute if WiFi successfully connected
  if (WiFi.isConnected()) {  
    
    // URL, source, IP address and RSSI stuff
    setSignalKURL();
    setSignalKSource();
    setIPAddrCstr();
    setRSSICstr();
    
    updateLCD(IPc, RSSIc);
    delay(1009);

    // OTA
    initOTA();

    // Webserver handlers
    setupWebserverCallbacks();

    // Websocket event handlers
    setupWebsocketCallbacks();

  // No WiFi connection, use only LCD output and power off WiFi 
  } else {  
    LCD_ONLY = true;
    WiFi.disconnect(true);
    WiFi.mode(WIFI_OFF); 
    updateLCD("LCD ONLY MODE", "NO WIFI");
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

  if (!LCD_ONLY) { 
    
    // OTA
    // Todo: consider moving from loop to separate task
    ArduinoOTA.handle();                  
    
    // Webserver
    // Todo: consider moving from loop to separate task
    server.handleClient();                
    
    // Keep websocket alive
    if (ws_open) ws.poll();               
    
    // Kill ghost websocket (websocket still open but wifi has dropped)
    if (!WiFi.isConnected() && ws_open){  
      ws.close();
      ws_open = false;
    }
  
    // Websocket reconnect and keep using manual variation if websocket not opened
    // Todo: consider moving from loop to separate task or to set a max retries counter
    if (WiFi.isConnected() && !ws_open && (long)(now - next_ws_try_ms) >= 0){ 
      updateLCD("SIGNALK WS", "CONNECT...");
      ws.connect(SK_URL);
      next_ws_try_ms = now + expn_retry_ms;
      expn_retry_ms = min(expn_retry_ms * 2, WS_RETRY_MAX);
    }
    if (ws_open) expn_retry_ms = WS_RETRY_MS;
    else use_manual_magvar = true;
  }

  // Read values from CMPS14
  if ((long)(now - last_read_ms) >= READ_MS) {
    last_read_ms = now; 
    compass.update();                                               
  }

  // Send heading, pitch and roll to SignalK server
  if ((long)(now - last_tx_ms) >= MIN_TX_INTERVAL_MS) {
    last_tx_ms = now;
    sendHdgPitchRollDelta();
  }

  // Send pitch and roll min and max to SignalK server
  if ((long)(now - last_minmax_tx_ms) >= MINMAX_TX_INTERVAL_MS) {
    last_minmax_tx_ms = now;
    sendPitchRollMinMaxDelta();
  }
  
  // Monitor calibration status
  if ((long)(now - last_cal_poll_ms) >= CAL_POLL_MS) {
    last_cal_poll_ms = now;
    compass.monitorCalibration(cal_mode_runtime == CAL_SEMI_AUTO);
  }

  // Monitor FULL AUTO mode timeout
  if (cal_mode_runtime == CAL_FULL_AUTO && full_auto_stop_ms > 0) { 
    long left = full_auto_stop_ms - (now - full_auto_start_ms);
    if (left <= 0) {
      if (compass.stopCalibration()) updateLCD("FULL AUTO", "TIMEOUT", true);
      left = 0;
    }
    full_auto_left_ms = left;
  }

  // Display heading (T or M) on LCD
  if ((long)(now - last_lcd_ms) >= LCD_MS) {                      
    last_lcd_ms = now;
    if (now >= lcd_hold_ms) {
      float heading_true_deg = compass.getHeadingTrueDeg();
      float heading_deg = compass.getHeadingDeg();
      if (send_hdg_true && validf(heading_true_deg)) {
        char buf[17];
        snprintf(buf, sizeof(buf), "      %03.0f%c", heading_true_deg, 223);
        updateLCD("  HEADING (T):", buf);
      } else if (validf(heading_deg)) {
        char buf[17];
        snprintf(buf, sizeof(buf), "      %03.0f%c", heading_deg, 223);
        updateLCD("  HEADING (M):", buf);
      }
    }
  }

  // Led indicators
  updateLedByCalMode();                                        
  updateLedByConnStatus();                                     

} 
