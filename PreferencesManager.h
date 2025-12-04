#pragma once

#include <Preferences.h>
#include "CMPS14Processor.h"
#include "CalMode.h"
#include "harmonic.h"

// Forward declarations for future subsystems
class SignalKBroker;
class DisplayController;

// Preferences subsystem for CMPS14Processor
class CMPS14ProcessorPrefs {
public:
    CMPS14ProcessorPrefs(Preferences &prefs);

    void load(CMPS14Processor &compass);
    void saveInstallationOffset(float offset);
    void saveManualVariation(float deg);

    void saveMeasuredDeviations(const float dev[8]);
    void saveHarmonicCoeffs(const HarmonicCoeffs &hc);

    void saveCalibrationModeBoot(CalMode mode);
    void saveFullAutoTimeout(unsigned long ms);
    void saveSendHeadingTrue(bool enable);


private:
    Preferences &prefs;
};

// Preferences subsystem for SignalK message broker
class SignalKBrokerPrefs {
public:
    SignalKBrokerPrefs(Preferences &prefs) : prefs(prefs) {}
    void loadDefault() {}
private:
    Preferences &prefs;
};

// Preferences subsystem for DisplayController
class DisplayControllerPrefs {
public:
    DisplayControllerPrefs(Preferences &prefs) : prefs(prefs) {}
    void loadDefault() {}
private:
    Preferences &prefs;
};

// Generic PreferencesManager
class PreferencesManager {
public:
    PreferencesManager();

    CMPS14ProcessorPrefs compass_prefs;
    SignalKBrokerPrefs signalk_prefs;
    DisplayControllerPrefs display_prefs;

private:
    Preferences prefs;
};
