#include "globals.h"
#include "cmps14.h"
#include "display.h"
#include "signalk.h"
#include "webui.h"
#include "OTA.h"

// ===== S E T U P ===== //
void setup() {

  Serial.begin(115200);
  delay(47);

  Wire.begin(I2C_SDA, I2C_SCL);
  delay(47);
  Wire.setClock(400000);
  delay(47);

  lcd_init_safe();
  delay(47);

  pinMode(LED_PIN_BL, OUTPUT);
  pinMode(LED_PIN_GR, OUTPUT);
  digitalWrite(LED_PIN_BL, LOW);
  digitalWrite(LED_PIN_GR, LOW);

  // Get saved configuration from ESP32 preferences
  get_config_from_prefs();

  // Init CMPS14 with appropriate calibration mode or use-mode
  cmps14_init_with_cal_mode();
  delay(1009);

  // Stop bluetooth to save power
  btStop(); 

  // Init WiFi
  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false);
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  lcd_print_lines("WIFI", "CONNECT...");
  unsigned long t0 = millis();

  // Try to connect WiFi until timeout
  while (!WiFi.isConnected() && (millis() - t0) < WIFI_TIMEOUT_MS) { delay(250); } 

  // Execute if WiFi successfully connected
  if (WiFi.isConnected()) {  
    
    // URL, source, IP address and RSSI stuff
    build_sk_url();
    build_sk_source();
    update_ipaddr_cstr();
    update_rssi_cstr();
    
    lcd_print_lines(IPc, RSSIc);
    delay(1009);

    // OTA
    init_OTA();

    // Webserver
    setup_webserver_callbacks();

    // Websocket
    setup_websocket_callbacks();  

  // No WiFi connection, use only LCD output and power off WiFi 
  } else {  
    LCD_ONLY = true;
    WiFi.disconnect(true);
    WiFi.mode(WIFI_OFF); 
    lcd_print_lines("LCD ONLY MODE", "NO WIFI");
    delay(1009);
  }

}

// ===== L O O P ===== //
void loop() {

  const unsigned long now = millis();              
  static unsigned long last_read_ms = 0;                              
  static unsigned long last_lcd_ms = 0;
  static unsigned long next_ws_try_ms = 0;

  if (!LCD_ONLY) { 
    ArduinoOTA.handle();                                           // OTA
    server.handleClient();                                         // Webserver
    ws.poll();                                                     // Keep websocket alive
    if (!WiFi.isConnected() && ws_open) {                          // Kill ghost websocket
      ws.close();
      ws_open = false;
    }
  }

  static unsigned long expn_retry_ms = WS_RETRY_MS;
  if (!LCD_ONLY && !ws_open && (long)(now - next_ws_try_ms) >= 0){  // Execute only on ticks when timer is due, only if Websocket dropped and only if not in LCD mode
    ws.connect(SK_URL);
    next_ws_try_ms = now + expn_retry_ms;
    expn_retry_ms = min(expn_retry_ms * 2, WS_RETRY_MAX);
  }
  if (ws_open) expn_retry_ms = WS_RETRY_MS;

  if ((long)(now - last_read_ms) >= READ_MS) {
    last_read_ms = now;
    read_compass();                                                 // Read values from CMPS14 only when timer is due
  }

  send_hdg_pitch_roll_delta();                                     // And send values to SignalK server
  send_pitch_roll_minmax_delta();
  
  if (cal_mode_runtime == CAL_SEMI_AUTO) {
    cmps14_monitor_and_store(true);                                 // Monitor and save automatically when profile is good enough
  } else {
    cmps14_monitor_and_store(false);                                // Monitor but do not save automatically, user saves profile from Web UI
  }

  if (cal_mode_runtime == CAL_FULL_AUTO && full_auto_stop_ms > 0) { // Monitor FULL AUTO mode timeout
    long left = full_auto_stop_ms - (now - full_auto_start_ms);
    if (left <= 0) {
      stop_calibration();
      lcd_show_info("FULL AUTO", "TIMEOUT");
      left = 0;
    }
    full_auto_left_ms = left;
  }

  if ((long)(now - last_lcd_ms) >= LCD_MS) {                        // Execute only on ticks when LCD timer is due
    last_lcd_ms = now;
    if (now >= lcd_hold_ms) {
      if (!LCD_ONLY && ws_open && send_hdg_true && validf(heading_true_deg)) {
        char buf[17];
        snprintf(buf, sizeof(buf), "      %03.0f%c", heading_true_deg, 223);
        lcd_print_lines("  HEADING (T):", buf);
      }
      else if (validf(heading_deg)) {
        char buf[17];
        snprintf(buf, sizeof(buf), "      %03.0f%c", heading_deg, 223);
        lcd_print_lines("  HEADING (M):", buf);
        heading_true_deg = NAN;
        heading_true_rad = NAN;
      }
    }
  }

  led_update_by_cal_mode();                                         // blue led
  led_update_by_conn_status();                                      // green led

} 
