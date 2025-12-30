#pragma once

#include <Arduino.h>
#include <WebServer.h>
#include <ArduinoJson.h>
#include <esp_system.h>
#include "harmonic.h"
#include "CalMode.h"
#include "CMPS14Processor.h"
#include "CMPS14Preferences.h"
#include "SignalKBroker.h"
#include "DisplayManager.h"
#include "version.h"

// === W E B U I M A N A G E R  C L A S S ===
//
// - Class WebUIManager - "the webui" responsible of providing
//   a web based UI for the user to configure the compass
// - Owns the WebServer instance
// - Init (start the WebServer): webui.begin()
// - Handle client request: webui.handleRequest() - this actually
//   wraps WebServer.handleClient() to be called in loop()
// - The web UI: http://<yourESP32ipaddress>
// - Descriptions of the endpoints and UI in README file
// - Association (1:1) to
//   - CMPS14Processor
//   - CMPS14Preferences
//   - SignalKBroker
//   - DisplayManager
 
class WebUIManager {

public:

  explicit WebUIManager(CMPS14Processor &compassref, CMPS14Preferences &compass_prefsref, SignalKBroker &signalkref, DisplayManager &displayref);

  void begin();
  void handleRequest();

  void setLoopRuntimeInfo(float avg_us); // Debug

private:
  
  WebServer server;
  CMPS14Processor &compass;
  CMPS14Preferences &compass_prefs;
  SignalKBroker &signalk;
  DisplayManager &display;

  // Reusable JSON document
  StaticJsonDocument<1024> status_doc;

  // Debug app.loop() runtime
  float runtime_avg_us = 0.0f;

  // Webserver endpoint handlers
  void setupRoutes();
  void handleStatus();
  void handleSetOffset();
  void handleSetDeviations();
  void handleSetCalmode();
  void handleSetMagvar();
  void handleSetHeadingMode();
  void handleRoot();
  void handleDeviationTable();
  void handleRestart();
  void handleStartCalibration();
  void handleStopCalibration();
  void handleSaveCalibration();
  void handleReset();
  void handleLevel();

  // Milliseconds to hh:mm:ss
  const char* ms_to_hms_str(unsigned long ms) {
    static char buf[12];
    unsigned long total_secs = ms / 1000;
    unsigned int h = total_secs / 3600;
    unsigned int m = (total_secs % 3600) / 60;
    unsigned int s = (total_secs % 60);
    snprintf(buf, sizeof(buf), "%02u:%02u:%02u", h, m, s);
    return buf;
  }

};