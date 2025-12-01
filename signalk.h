#pragma once

#include "globals.h"

void setSignalKURL();
void setSignalKSource();

void setupWebsocketCallbacks();
void sendHdgPitchRollDelta();
void sendPitchRollMinMaxDelta();