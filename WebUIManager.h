#pragma once

#include "globals.h"
#include "CMPS14Instances.h"
#include "CalMode.h"

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

void setupWebserverCallbacks();

// Milliseconds to hh:mm:ss
inline const char* ms_to_hms_str(unsigned long ms) {
  static char buf[12];
  unsigned long total_secs = ms / 1000;
  unsigned int h = total_secs / 3600;
  unsigned int m = (total_secs % 3600) / 60;
  unsigned int s = (total_secs % 60);
  snprintf(buf, sizeof(buf), "%02u:%02u:%02u", h, m, s);
  return buf;
}