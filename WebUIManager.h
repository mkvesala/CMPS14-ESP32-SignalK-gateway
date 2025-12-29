#pragma once

#include "globals.h"
#include "CMPS14Processor.h"
#include "CMPS14Preferences.h"
#include "SignalKBroker.h"
#include "DisplayManager.h"


class WebUIManager {

public:

  explicit WebUIManager(CMPS14Processor &compassref, CMPS14Preferences &compass_prefsref, SignalKBroker &signalkref, DisplayManager &displayref);

  void begin();
  void handleRequest();

  void setLoopRuntimeInfo(uint32_t min_us, uint32_t max_us, float avg_us); // Debug

private:
  
  WebServer server;
  CMPS14Processor &compass;
  CMPS14Preferences &compass_prefs;
  SignalKBroker &signalk;
  DisplayManager &display;

  // Reusable JSON document
  StaticJsonDocument<1024> status_doc;

  // Debug app.loop() runtime stats
  uint32_t runtime_min_us = 0;
  uint32_t runtime_max_us = 0;
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