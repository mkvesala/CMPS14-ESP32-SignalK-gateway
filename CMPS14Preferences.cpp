#include "CMPS14Preferences.h"

// === P U B L I C ===

CMPS14Preferences::CMPS14Preferences(CMPS14Processor &compassref) : compass(compassref) {}

// Load all settings to CMPS14Processor
void CMPS14Preferences::load() {

    if (!prefs.begin(ns, false)) return;

    // Installation offset
    compass.setInstallationOffset(prefs.getFloat("offset_deg", 0.0f));

    // Manual variation
    compass.setManualVariation(prefs.getFloat("mv_man_deg", 0.0f));

    // Measured deviations at 8 cardinal and intercardinal points
    float in[8];
    for (int i=0;i<8;i++) in[i] = prefs.getFloat((String("dev")+String(i)).c_str(), 0.0f);
    compass.setMeasuredDeviations(in);

    // Harmonic coefficients
    HarmonicCoeffs hc;
    bool haveCoeffs = prefs.isKey("hc_A") && prefs.isKey("hc_B") && prefs.isKey("hc_C") && prefs.isKey("hc_D") && prefs.isKey("hc_E");
    if (haveCoeffs) {
        hc.A = prefs.getFloat("hc_A", 0.0f);
        hc.B = prefs.getFloat("hc_B", 0.0f);
        hc.C = prefs.getFloat("hc_C", 0.0f);
        hc.D = prefs.getFloat("hc_D", 0.0f);
        hc.E = prefs.getFloat("hc_E", 0.0f);
    } else {
        hc = computeHarmonicCoeffs(in);
        prefs.putFloat("hc_A", hc.A);
        prefs.putFloat("hc_B", hc.B);
        prefs.putFloat("hc_C", hc.C);
        prefs.putFloat("hc_D", hc.D);
        prefs.putFloat("hc_E", hc.E);
    }
    compass.setHarmonicCoeffs(hc);

    // Send heading mode: true vs magnetic
    compass.setSendHeadingTrue(prefs.getBool("send_hdg_true", true));

    // Calibration boot mode
    compass.setCalibrationModeBoot((CalMode)prefs.getUChar("cal_mode_boot", (uint8_t)CalMode::USE));
  
    // Full auto timeout
    compass.setFullAutoTimeout((unsigned long)prefs.getULong("fastop", 0));

    prefs.end();
}

// Save physical installation offset
void CMPS14Preferences::saveInstallationOffset(float offset) {
    if (!prefs.begin(ns, false)) return;
    prefs.putFloat("offset_deg", offset);
    prefs.end();
}

// Save manual variation
void CMPS14Preferences::saveManualVariation(float deg) {
    if (!prefs.begin(ns, false)) return;
    prefs.putFloat("mv_man_deg", deg);
    prefs.end();
}

// Save measured deviations and 5 coeffs of the harmonic model
void CMPS14Preferences::saveDeviationSettings(const float out[8], const HarmonicCoeffs &hc) {
    if (!prefs.begin(ns, false)) return;

    for (int i = 0; i < 8; i++) {
        char key[8];
        snprintf(key, sizeof(key), "dev%d", i);
        prefs.putFloat(key, out[i]);
    }

    prefs.putFloat("hc_A", hc.A);
    prefs.putFloat("hc_B", hc.B);
    prefs.putFloat("hc_C", hc.C);
    prefs.putFloat("hc_D", hc.D);
    prefs.putFloat("hc_E", hc.E);

    prefs.end();
}

// Save calibration mode at boot and timeout of FULL AUTO calibration mode
void CMPS14Preferences::saveCalibrationSettings(CalMode mode, unsigned long ms) {
    if (!prefs.begin(ns, false)) return;
    prefs.putUChar("cal_mode_boot", (uint8_t)mode);
    prefs.putULong("fastop", ms);
    prefs.end();
}

// Save send true heading option
void CMPS14Preferences::saveSendHeadingTrue(bool enable) {
    if (!prefs.begin(ns, false)) return;
    prefs.putBool("send_hdg_true", enable);
    prefs.end();
}
