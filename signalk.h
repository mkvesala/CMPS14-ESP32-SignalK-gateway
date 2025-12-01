#pragma once

#include "globals.h"
#include "CMPS14Instances.h"
#include "CalMode.h"

void setSignalKURL();
void setSignalKSource();

void setupWebsocketCallbacks();
void sendHdgPitchRollDelta();
void sendPitchRollMinMaxDelta();