#include "PreferencesManager.h"

CMPS14ProcessorPrefs::CMPS14ProcessorPrefs(Preferences &p) : prefs(p) {}

// LOAD ALL SETTINGS INTO CMPS14 PROCESSOR
void CMPS14ProcessorPrefs::load(CMPS14Processor &compass) {

    // Installation offset
    compass.setInstallationOffset(prefs.getFloat("offset_deg", 0.0f));

    // Manual variation
    compass.setManualVariation(prefs.getFloat("mv_man_deg", 0.0f));

    // Deviation 8 points THIS GOES TO WEBUI PREFS
    float dev8[8];
    for (int i = 0; i < 8; i++) {
        char key[8];
        snprintf(key, sizeof(key), "dev%d", i);
        dev8[i] = prefs.getFloat(key, 0.0f);
    }

    // Harmonic coefficients
    HarmonicCoeffs hc;
    if (prefs.isKey("hc_A")) {
        hc.A = prefs.getFloat("hc_A");
        hc.B = prefs.getFloat("hc_B");
        hc.C = prefs.getFloat("hc_C");
        hc.D = prefs.getFloat("hc_D");
        hc.E = prefs.getFloat("hc_E");
    } else {
        // Compute if not saved
        hc = computeHarmonicCoeffs(dev8);
        saveHarmonicCoeffs(hc);
    }

    compass.setHarmonicCoeffs(hc);

    // Send heading mode: true vs magnetic
    extern bool send_hdg_true;   // stays global for now
    send_hdg_true = prefs.getBool("sendTrue", true);

    // Calibration boot mode
    CalMode boot = (CalMode)prefs.getUChar("cal_mode_boot", (uint8_t)CAL_USE);
    compass.setCalibrationModeBoot(boot);

    // Full auto timeout
    extern unsigned long full_auto_stop_ms;
    full_auto_stop_ms = prefs.getULong("fastop", 0);
}

void CMPS14ProcessorPrefs::saveInstallationOffset(float offset) {
    prefs.putFloat("offset_deg", offset);
}

void CMPS14ProcessorPrefs::saveManualVariation(float deg) {
    prefs.putFloat("mv_man_deg", deg);
}

void CMPS14ProcessorPrefs::saveMeasuredDeviations(const float dev[8]) {
    for (int i = 0; i < 8; i++) {
        char key[8];
        snprintf(key, sizeof(key), "dev%d", i);
        prefs.putFloat(key, dev[i]);
    }
}

void CMPS14ProcessorPrefs::saveHarmonicCoeffs(const HarmonicCoeffs &hc) {
    prefs.putFloat("hc_A", hc.A);
    prefs.putFloat("hc_B", hc.B);
    prefs.putFloat("hc_C", hc.C);
    prefs.putFloat("hc_D", hc.D);
    prefs.putFloat("hc_E", hc.E);
}

void CMPS14ProcessorPrefs::saveCalibrationModeBoot(CalMode mode) {
    prefs.putUChar("cal_mode_boot", (uint8_t)mode);
}

void CMPS14ProcessorPrefs::saveFullAutoTimeout(unsigned long ms) {
    prefs.putULong("fastop", ms);
}

void CMPS14ProcessorPrefs::saveSendHeadingTrue(bool enable) {
    prefs.putBool("send_hdg_true", enable);
}

// ============================================================
// PreferencesManager implementation

PreferencesManager::PreferencesManager()
: prefs(), compass_prefs(prefs), signalk_prefs(prefs), display_prefs(prefs)
{
    // All prefs live under this single namespace
    prefs.begin("cmps14", false);
}
