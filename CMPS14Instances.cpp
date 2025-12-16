#include "CMPS14Instances.h"

CMPS14Sensor sensor(CMPS14_ADDR);
CMPS14Processor compass(sensor);
CMPS14Preferences compass_prefs(compass);
SignalKBroker signalk(compass);
DisplayManager display(compass, signalk);
WebUIManager webui(compass, compass_prefs, signalk, display);