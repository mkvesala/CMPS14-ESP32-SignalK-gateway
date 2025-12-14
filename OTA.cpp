#include "OTA.h"

// Init OTA
void initOTA() {
  
  char OTA_HOST[32];
  uint8_t m[6];
  WiFi.macAddress(m);
  snprintf(OTA_HOST, sizeof(OTA_HOST), "esp32.cmps14-%02x%02x%02x", m[3], m[4], m[5]);
  
  ArduinoOTA.setHostname(OTA_HOST);

  ArduinoOTA.setPassword(WIFI_PASS);
  
  ArduinoOTA.onStart([](){
    // start
  });
  
  ArduinoOTA.onEnd([]() {
    // complete
  });
  
  ArduinoOTA.onProgress([](unsigned int progress, unsigned int total){
    // progress
  });
  
  ArduinoOTA.onError([] (ota_error_t error) {
    // error
  });
  
  ArduinoOTA.begin();

}
