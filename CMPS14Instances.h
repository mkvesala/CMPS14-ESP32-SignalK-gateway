#pragma once

#include "CMPS14Sensor.h"
#include "CMPS14Processor.h"
#include "CMPS14Preferences.h"
#include "SignalKBroker.h"
#include "DisplayManager.h"

// class CMPS14Sensor;
// class CMPS14Processor;
// class CMPS14Preferences;
// class SignalKBroker;
// class DisplayManager;

extern CMPS14Sensor sensor;
extern CMPS14Processor compass;
extern CMPS14Preferences compass_prefs;
extern SignalKBroker signalk;
extern DisplayManager display;