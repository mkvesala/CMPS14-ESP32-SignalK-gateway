#pragma once

#include "globals.h"
#include "SignalKBroker.h"
#include "display.h"
#include "CMPS14Instances.h"
#include "CalMode.h"
#include "CMPS14Preferences.h"

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