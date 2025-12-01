#pragma once

#include "harmonic.h"
#include "display.h"
#include "globals.h"
#include "CMPS14Sensor.h"

class CMPS14Processor {
public:
    explicit CMPS14Processor (CMPS14Sensor &cmps14Sensor);

    bool begin(TwoWire &wirePort);
    bool update();

    // Calibration
    bool reset();
    bool startCalibration(CalMode mode);
    bool stopCalibration();
    void monitorCalibration(bool autosave);
    bool initCalibrationModeBoot(CalMode boot_mode);
    bool saveCalibrationProfile();
    void getCalStatus(uint8_t out[4]);
    
    // Getters
    float getCompassDeg() const { return compass_deg; }
    float getHeadingDeg() const { return heading_deg; }
    float getHeadingTrueDeg() const { return heading_true_deg; }
    float getPitchDeg() const { return pitch_deg; }
    float getRollDeg() const { return roll_deg; }

    auto getHeadingDelta() const { return headingDelta; }
    auto getMinMaxDelta() const { return minMaxDelta; }
    // CalMode getMode() const { return cal_mode_runtime; }

private:

    bool enableBackgroundCal(bool autosave);
    uint8_t readCalStatusByte();
    
    CMPS14Sensor &sensor;
    TwoWire *wire;

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

    // Monitor calibration
    uint8_t cal_ok_count = 0;
    bool cal_profile_stored = false;

    // Harmonic deviation model
    // HarmonicCoeffs hc {0,0,0,0,0};

    // CMPS14 register map
    static const uint8_t REG_USEMODE       = 0x80;  // Command use-mode
    static const uint8_t REG_CAL_STATUS    = 0x1E;  // Calibration status
    static const uint8_t REG_SAVE1         = 0xF0;  // Series of commands to store calibration profile
    static const uint8_t REG_SAVE2         = 0xF5;
    static const uint8_t REG_SAVE3         = 0xF6;
    static const uint8_t REG_CAL1          = 0x98;  // Series of commands to start calibration
    static const uint8_t REG_CAL2          = 0x95;
    static const uint8_t REG_CAL3          = 0x99;
    static const uint8_t REG_RESET1        = 0xE0;  // Series of commands to reset CMPS14
    static const uint8_t REG_RESET2        = 0xE5;
    static const uint8_t REG_RESET3        = 0xE2; 
    static const uint8_t REG_AUTO_ON       = 0x93;  // Autosave byte of CMPS14
    static const uint8_t REG_AUTO_OFF      = 0x83;  // Autosave off
    static const uint8_t REG_MASK          = 0x03;  // Mask to read individual calibration status bits for sys, acc, gyr, mag
};
