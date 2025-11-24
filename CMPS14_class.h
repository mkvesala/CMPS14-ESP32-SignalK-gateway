#pragma once

#include <Arduino.h>
#include <Wire.h>
#include "harmonic.h"
#include "globals.h"

class CMPS14 {
public:
    explicit CMPS14(uint8_t i2c_addr = 0x60);

    // Basics
    bool begin(TwoWire &wirePort = Wire);
    bool read();                 // Lue anturidata
    bool reset();                // Reset CMPS14
    bool isConnected() const;    // Tarkista I²C-vastaus

    // Calibration
    bool startCalibration(CalMode mode);
    bool stopCalibration();
    void monitorAndStore(bool autoSave);

    // Getters
    float heading() const { return heading_deg; }
    float headingTrue() const { return heading_true_deg; }
    float pitch() const { return pitch_deg; }
    float roll() const { return roll_deg; }
    CalMode mode() const { return cal_mode_runtime; }

private:
    // Helpers
    bool sendCommand(uint8_t cmd);
    bool enableBackgroundCal(bool autosave);
    uint8_t readCalStatusByte();
    void getCalStatus(uint8_t out[4]);
    void smoothHeading(float raw_deg);

    // State etc.
    uint8_t addr;
    TwoWire *wire;
    CalMode cal_mode_runtime = CAL_USE;

    // Compass and attitude in degrees
    float compass_deg = NAN;
    float heading_deg = NAN;
    float heading_true_deg = NAN;
    float pitch_deg = NAN;
    float roll_deg = NAN;

    // Compass and attitude in radians
    float pitch_min_rad = NAN;
    float pitch_max_rad = NAN;
    float roll_min_rad = NAN;
    float roll_max_rad = NAN;

    // Monitor calibration
    uint8_t cal_ok_count = 0;
    bool cal_profile_stored = false;

    // === Harmonic deviation model ===
    HarmonicCoeffs hc {0,0,0,0,0};
};
