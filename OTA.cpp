#include "OTA.h"

// Init OTA
void initOTA() {
  ArduinoOTA.setHostname(SK_SOURCE);
  ArduinoOTA.setPassword(WIFI_PASS);
  ArduinoOTA.onStart([](){
    updateLCD("OTA UPDATE", "STARTED");
  });
  ArduinoOTA.onEnd([]() {
    updateLCD("OTA UPDATE", "COMPLETE");
  });
  ArduinoOTA.onProgress([](unsigned int progress, unsigned int total){
    static uint8_t last_step = 255;
    uint8_t pct = (progress * 100) / total;  // integer divisions here, no floats
    uint8_t step = pct / 10;
    if (step !=last_step) {
      last_step = step;
      char buf[17];
      snprintf(buf, sizeof(buf), "RUN: %3u%%", step * 10);
      updateLCD("OTA UPDATE", buf);
    }
  });
  ArduinoOTA.onError([] (ota_error_t error) {
    if (error == OTA_AUTH_ERROR) updateLCD("OTA UPDATE", "AUTH FAIL");
    else if (error == OTA_BEGIN_ERROR) updateLCD("OTA UPDATE", "INIT FAIL");
    else if (error == OTA_CONNECT_ERROR) updateLCD("OTA UPDATE", "CONNECT FAIL");
    else if (error == OTA_RECEIVE_ERROR) updateLCD("OTA UPDATE", "RECEIVE FAIL");
    else if (error == OTA_END_ERROR) updateLCD("OTA UPDATE", "ENDING FAIL");
    else updateLCD("OTA UPDATE", "ERROR");
  });
  ArduinoOTA.begin();
}
