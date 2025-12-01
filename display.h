#pragma once

#include "globals.h"
#include "CMPS14Instances.h"
#include "CalMode.h"

void initLCD();
void updateLCD(const char* l1, const char* l2, bool hold = false);

void updateLedByCalMode();
void updateLedByConnStatus();