#pragma once

#include <Preferences.h>
#include "CMPS14Processor.h"
#include "CalMode.h"
#include "harmonic.h"

// Preferences subsystem for CMPS14Processor
class CMPS14ProcessorPrefs {
public:
    explicit CMPS14ProcessorPrefs(CMPS14Processor &compassref);

    void load();
    void saveInstallationOffset(float offset);
    void saveManualVariation(float deg);

    void saveMeasuredDeviations(const float dev[8]);
    void saveHarmonicCoeffs(const HarmonicCoeffs &hc);

    void saveCalibrationModeBoot(CalMode mode);
    void saveFullAutoTimeout(unsigned long ms);
    void saveSendHeadingTrue(bool enable);


private:
    const char* ns = "cmps14";
    Preferences prefs;
    CMPS14Processor &cmps14;
};
