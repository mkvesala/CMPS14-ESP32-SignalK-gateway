#pragma once

#include "globals.h"
#include "CMPS14Processor.h"

// Preferences subsystem for CMPS14Processor
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
