#pragma once

#include <Arduino.h>
#include <Preferences.h> 
#include "CMPS14Processor.h"
#include "harmonic.h"
#include "CalMode.h"

// === C M P S 1 4 P R E F E R E N C E S  C L A S S ===
//
// - Class CMPS14Preferences - "the compass_prefs" responsible of managing ESP32 NVS
// - Owns the Preferences instance
// - Provides public API to save config persistently to NVS
//   - Installation offset
//   - Manual variation
//   - Measured deviations at 8 cardinal/intercardinal points
//   - Computed 5 coefficients for deviation calculation
//   - Compass calibration mode to be loaded at ESP32 boot
//   - Timeout for FULL AUTO calibration mode
//   - Heading mode: HDG(T) / HDG(M)
// - Provides public API to load config from NVS
// - Association (1:1) to CMPS14Processor, "the compass"

class CMPS14Preferences {
public:

    explicit CMPS14Preferences(CMPS14Processor &compassref);

    void load();
    void saveInstallationOffset(float offset);
    void saveManualVariation(float deg);
    void saveDeviationSettings(const float dev[8], const HarmonicCoeffs &hc);
    void saveCalibrationSettings(CalMode mode, unsigned long ms);
    void saveSendHeadingTrue(bool enable);

private:
    const char* ns = "cmps14";
    Preferences prefs;
    CMPS14Processor &compass;
};
