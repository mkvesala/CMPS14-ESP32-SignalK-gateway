#include "globals.h"
#include "cmps14.h"
#include "display.h"
#include "signalk.h"
#include "webui.h"

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
  digitalWrite(LED_PIN_BL, LOW); // blue led off
  digitalWrite(LED_PIN_GR, LOW); // green led off

  prefs.begin("cmps14", false);                                       // Get all permanently saved preferences
  installation_offset_deg = prefs.getFloat("offset_deg", 0.0f);
  magvar_manual_deg = prefs.getFloat("mv_man_deg", 0.0f);
  if (validf(magvar_manual_deg)) magvar_manual_rad = magvar_manual_deg * DEG_TO_RAD;
  for (int i=0;i<8;i++) dev_at_card_deg[i] = prefs.getFloat((String("dev")+String(i)).c_str(), 0.0f);
  bool haveCoeffs = prefs.isKey("hc_A") && prefs.isKey("hc_B") && prefs.isKey("hc_C") && prefs.isKey("hc_D") && prefs.isKey("hc_E");
  if (haveCoeffs) {
    hc.A = prefs.getFloat("hc_A", 0.0f);
    hc.B = prefs.getFloat("hc_B", 0.0f);
    hc.C = prefs.getFloat("hc_C", 0.0f);
    hc.D = prefs.getFloat("hc_D", 0.0f);
    hc.E = prefs.getFloat("hc_E", 0.0f);
  } else {
    hc = fit_harmonic_from_8(headings_deg, dev_at_card_deg);
    prefs.putFloat("hc_A", hc.A);
    prefs.putFloat("hc_B", hc.B);
    prefs.putFloat("hc_C", hc.C);
    prefs.putFloat("hc_D", hc.D);
    prefs.putFloat("hc_E", hc.E);
  }
  send_hdg_true = prefs.getBool("send_hdg_true", true);
  cal_mode_boot = (CalMode)prefs.getUChar("cal_mode_boot", (uint8_t)CAL_USE);
  full_auto_stop_ms = (unsigned long)prefs.getULong("fastop", 0);
  prefs.end();

  if (i2c_device_present(CMPS14_ADDR)){
    bool started = false;
    switch (cal_mode_boot){                           // Start calibration or use mode based on preferences, default is use mode, manual never used here
      case CAL_FULL_AUTO:
        started = start_calibration_fullauto();
        break;
      case CAL_SEMI_AUTO:
        started = start_calibration_semiauto();
        break;
      case CAL_MANUAL:
        started = start_calibration_manual_mode();
        break;
      default:
        if (stop_calibration()){
          started = true;
        } break;
    }
    if (!started) lcd_print_lines("CAL MODE", "START FAILED");
    else lcd_print_lines("CAL MODE", calmode_str(cal_mode_runtime));
  } else lcd_print_lines("CMPS14 N/A", "CHECK WIRING");

  delay(1009);

  btStop();                                                // Stop bluetooth to save power
  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false);
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  lcd_print_lines("WIFI", "CONNECT...");
  unsigned long t0 = millis();
  while (WiFi.status() != WL_CONNECTED && (millis() - t0) < WIFI_TIMEOUT_MS) { delay(250); } // Try to connect WiFi until timeout
  if (WiFi.status() == WL_CONNECTED) {                                                       // Execute if WiFi successfully connected
    build_sk_url();
    make_source_from_mac();
    char ipbuf[16];
    IPAddress ip = WiFi.localIP();
    snprintf(ipbuf, sizeof(ipbuf), "%u.%u.%u.%u", ip[0], ip[1], ip[2], ip[3]);

    classify_rssi(WiFi.RSSI());
    lcd_print_lines(ipbuf, RSSIc);
    delay(1009);

    // OTA
    ArduinoOTA.setHostname(SK_SOURCE);
    ArduinoOTA.setPassword(WIFI_PASS);
    ArduinoOTA.onStart([](){
      lcd_print_lines("OTA UPDATE", "STARTED");
    });
    ArduinoOTA.onEnd([]() {
      lcd_print_lines("OTA UPDATE", "COMPLETE");
    });
    ArduinoOTA.onProgress([](unsigned int progress, unsigned int total){
      static uint8_t last_step = 255;
      uint8_t pct = (progress * 100) / total;       // integer divisions here, not floats
      uint8_t step = pct / 10;
      if (step !=last_step) {
        last_step = step;
        char buf[17];
        snprintf(buf, sizeof(buf), "RUN: %3u%%", step * 10);
        lcd_print_lines("OTA UPDATE", buf);
      }
    });
    ArduinoOTA.onError([] (ota_error_t error) {
      if (error == OTA_AUTH_ERROR) lcd_print_lines("OTA UPDATE", "AUTH FAIL");
      else if (error == OTA_BEGIN_ERROR) lcd_print_lines("OTA UPDATE", "INIT FAIL");
      else if (error == OTA_CONNECT_ERROR) lcd_print_lines("OTA UPDATE", "CONNECT FAIL");
      else if (error == OTA_RECEIVE_ERROR) lcd_print_lines("OTA UPDATE", "RECEIVE FAIL");
      else if (error == OTA_END_ERROR) lcd_print_lines("OTA UPDATE", "ENDING FAIL");
      else lcd_print_lines("OTA UPDATE", "ERROR");
    });
    ArduinoOTA.begin();

    // Set up the Webserver to call the handlers
    server.on("/", handle_root);
    server.on("/status", handle_status);
    server.on("/cal/on", handle_calibrate_on);
    server.on("/cal/off", handle_calibrate_off);
    server.on("/store/on", handle_store);
    server.on("/reset/on", handle_reset);
    server.on("/offset/set", handle_set_offset);
    server.on("/dev8/set", handle_dev8_set);
    server.on("/calmode/set", handle_calmode_set);
    server.on("/magvar/set", handle_magvar_set);
    server.on("/heading/mode", handle_heading_mode);
    server.on("/restart", handle_restart);
    server.on("/deviationdetails", handle_deviation_details);
    server.begin();

    setup_ws_callbacks();                                             // Websocket
  } else {                                                            // No WiFi, use only LCD output
    LCD_ONLY = true;
    WiFi.disconnect(true);
    WiFi.mode(WIFI_OFF);                                              // Power off WiFi to save power
    lcd_print_lines("LCD ONLY MODE", "NO WIFI");
    delay(1009);
  }

}

// ===== L O O P ===== //
void loop() {

  const unsigned long now = millis();                                 // Timestamp of this tick              
  static unsigned long last_read_ms = 0;                              
  static unsigned long last_lcd_ms = 0;
  static unsigned long next_ws_try_ms = 0;

  if (!LCD_ONLY) {                                                    // Execute except in LCD ONLY mode
    ArduinoOTA.handle();                                              // OTA
    server.handleClient();                                            // Webserver
    ws.poll();                                                        // Keep websocket alive
    if (WiFi.status() != WL_CONNECTED && ws_open) {                   // Kill ghost websocket
      ws.close();
      ws_open = false;
    }
  }

  static unsigned long expn_retry_ms = WS_RETRY_MS;
  if (!LCD_ONLY && !ws_open && (long)(now - next_ws_try_ms) >= 0){     // Execute only on ticks when timer is due and only if Websocket dropped and if not in LCD only mode
    ws.connect(SK_URL);
    next_ws_try_ms = now + expn_retry_ms;
    expn_retry_ms = min(expn_retry_ms * 2, WS_RETRY_MAX);
  }
  if (ws_open) expn_retry_ms = WS_RETRY_MS;

  if ((long)(now - last_read_ms) >= READ_MS) {
    last_read_ms = now;
    bool success = read_compass();                                    // Read values from CMPS14 only when timer is due
  }

  send_batch_delta_if_needed();                                       // And send values to SignalK server
  send_minmax_delta_if_due();
  
  if (cal_mode_runtime == CAL_SEMI_AUTO) {
    cmps14_monitor_and_store(true);                                   // Monitor and save automatically when profile is good enough
  } else {
    cmps14_monitor_and_store(false);                                  // Monitor but do not save automatically, user saves profile from Web UI
  }

  if (cal_mode_runtime == CAL_FULL_AUTO && full_auto_stop_ms > 0) {   // Monitor FULL AUTO mode timeout
    long left = full_auto_stop_ms - (now - full_auto_start_ms);
    if (left <= 0) {
      stop_calibration();
      lcd_show_info("FULL AUTO", "TIMEOUT");
      left = 0;
    }
    full_auto_left_ms = left;
  }

  if ((long)(now - last_lcd_ms) >= LCD_MS) {                          // Execute only on ticks when LCD timer is due
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

  led_update_by_cal_mode();                                            // blue led
  led_update_by_conn_status();                                         // green led

} 
