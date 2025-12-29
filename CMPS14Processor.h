#pragma once

#include <Arduino.h>
#include <Wire.h>
#include "CalMode.h"
#include "harmonic.h"
#include "CMPS14Sensor.h"

class CMPS14Processor {
public:
    explicit CMPS14Processor (CMPS14Sensor &cmps14Sensor);

    bool begin(TwoWire &wirePort);
    bool update();
    void level();

    // Calibration
    bool reset();
    bool startCalibration(CalMode mode);
    bool stopCalibration();
    void monitorCalibration(bool autosave);
    bool initCalibrationModeBoot();
    bool saveCalibrationProfile();
    void requestCalStatus(uint8_t out[4]);
    
    // Getters
    float getCompassDeg() const { return compass_deg; }
    float getHeadingDeg() const { return heading_deg; }
    float getHeadingTrueDeg() const { return heading_true_deg; }
    float getPitchDeg() const { return pitch_deg; }
    float getRollDeg() const { return roll_deg; }
    float getPitchLevel() const { return pitch_level; }
    float getRollLevel() const { return roll_level; }
    float getInstallationOffset() const { return installation_offset_deg; }
    float getDeviation() const { return dev_deg; }
    float getVariation() const {return use_manual_magvar ? magvar_manual_deg : magvar_live_deg; }
    float getManualVariation() const { return magvar_manual_deg; }
    unsigned long getFullAutoTimeout() const { return full_auto_stop_ms; }
    unsigned long getFullAutoStart() const { return full_auto_start_ms; }
    unsigned long getFullAutoLeft() const { return full_auto_left_ms; }

    void getMeasuredDeviations(float out[8]) const { memcpy(out, measured_deviations, sizeof(measured_deviations)); }

    auto getHeadingDelta() const { return headingDelta; }
    auto getMinMaxDelta() const { return minMaxDelta; }
    CalMode getCalibrationModeBoot() const { return cal_mode_boot; }
    CalMode getCalibrationModeRuntime() const { return cal_mode_runtime; }
    HarmonicCoeffs getHarmonicCoeffs() const { return hc; }
    const DeviationLookup& getDeviationLookup() const { return dev_lut; }

    bool isUsingManualVariation() const { return use_manual_magvar; }
    bool isCalProfileStored() const { return cal_profile_stored; }
    bool isSendingHeadingTrue() const { return send_hdg_true; }

    // Setters
    void setInstallationOffset(float offset) { installation_offset_deg = offset; }
    void setManualVariation(float variation) { magvar_manual_deg = variation; }
    void setLiveVariation(float variation) { magvar_live_deg = variation; }
    void setUseManualVariation(bool manual) { use_manual_magvar = manual; }
    void setCalProfileStored(bool stored) { cal_profile_stored = stored; }
    void setSendHeadingTrue(bool hdg) { send_hdg_true = hdg; }
    void setCalibrationModeBoot(CalMode mode) { cal_mode_boot = mode; }
    void setCalibrationModeRuntime(CalMode mode) { cal_mode_runtime = mode; }
    void setMeasuredDeviations(const float in[8]) { memcpy(measured_deviations, in, sizeof(measured_deviations)); }
    void setFullAutoTimeout(unsigned long ms) { full_auto_stop_ms = ms; }
    void setFullAutoLeft(unsigned long ms) { full_auto_left_ms = ms; }
    void setHarmonicCoeffs(const HarmonicCoeffs &coeffs) {
        hc = coeffs;
        dev_lut.build(hc);
    }

private:

    bool enableBackgroundCal(bool autosave);
    uint8_t readCalStatusByte();
    void updateHeadingDelta();
    void updateMinMaxDelta();
    
    CMPS14Sensor &sensor;
    TwoWire *wire;

    CalMode cal_mode_boot = CalMode::USE;
    CalMode cal_mode_runtime = CalMode::USE;

    // CMPS14 processing
    float installation_offset_deg = 0.0f;       // Physical installation offset of the CMPS14 sensor in degrees
    float dev_deg = 0.0f;                       // Deviation calculated by harmonic model
    float magvar_manual_deg = 0.0f;             // Variation that is set manually from web UI
    float magvar_live_deg = 0.0f;               // Variation from SignalK navigation.magneticVariation path
    float pitch_level = 0.0f;                   // Leveling of pitch
    float roll_level = 0.0f;                    // Leveling of roll
    float pitch_level_raw = NAN;                // Leveling of pitch
    float roll_level_raw = NAN;                // Leveling of roll
    bool use_manual_magvar = true;              // Use magvar_manual_deg if true
    bool cal_profile_stored = false;            // Calibration profile saved if true
    bool send_hdg_true = true;                  // Send also true heading

    HarmonicCoeffs hc = { 0,0,0,0,0 };                  // Five harmonic coeffs to compute deviations - as a struct, because part of computing model A, B, C, D and E.
    DeviationLookup dev_lut;                            // Lookup table for deviations
    
    float measured_deviations[8] = { 0,0,0,0,0,0,0,0 }; // Measured deviations (deg) in cardinal and intercardinal directions, as an array, because imput only

    static constexpr float HEADING_ALPHA = 0.15f;       // Smoothing factor for Heading (C)
    static constexpr uint8_t CAL_OK_REQUIRED = 3;       // Autocalibration save condition threshold

    // Compass and attitude in degrees
    float compass_deg = NAN;
    float heading_deg = NAN;
    float heading_true_deg = NAN;
    float pitch_deg = NAN;
    float roll_deg = NAN;

    // Compass and attitude in radians
    struct HeadingDelta {
        float heading_rad = NAN, heading_true_rad = NAN, pitch_rad = NAN, roll_rad = NAN;
    } headingDelta;

    // Pitch and roll min/max values in radians
    struct MinMaxDelta {
        float pitch_min_rad = NAN, pitch_max_rad = NAN, roll_min_rad = NAN, roll_max_rad = NAN;
    } minMaxDelta;

    // Calibration
    uint8_t cal_ok_count = 0;
    unsigned long full_auto_start_ms      = 0;      // Full auto mode start timestamp
    unsigned long full_auto_stop_ms       = 0;      // Full auto mode timeout, 0 = never
    unsigned long full_auto_left_ms       = 0;      // Full auto mode time left


    // CMPS14 register map
    static constexpr uint8_t REG_USEMODE       = 0x80;  // Use-mode = normal operation
    static constexpr uint8_t REG_CAL_STATUS    = 0x1E;  // Calibration status
    static constexpr uint8_t REG_SAVE1         = 0xF0;  // Series of commands to store calibration profile
    static constexpr uint8_t REG_SAVE2         = 0xF5;
    static constexpr uint8_t REG_SAVE3         = 0xF6;
    static constexpr uint8_t REG_CAL1          = 0x98;  // Series of commands to start calibration
    static constexpr uint8_t REG_CAL2          = 0x95;
    static constexpr uint8_t REG_CAL3          = 0x99;
    static constexpr uint8_t REG_RESET1        = 0xE0;  // Series of commands to reset CMPS14
    static constexpr uint8_t REG_RESET2        = 0xE5;
    static constexpr uint8_t REG_RESET3        = 0xE2; 
    static constexpr uint8_t REG_AUTO_ON       = 0x93;  // Autosave byte of CMPS14
    static constexpr uint8_t REG_AUTO_OFF      = 0x83;  // Autosave off
    static constexpr uint8_t REG_MASK          = 0x03;  // Mask to read individual calibration status bits for sys, acc, gyr, mag
};
