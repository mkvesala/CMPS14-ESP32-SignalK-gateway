#pragma once

#include "harmonic.h"
#include "display.h"
#include "globals.h"

struct CMPS14Data {
    float heading_rad;
    float heading_true_rad;
    float pitch_rad;
    float roll_rad;
    float pitch_min_rad;
    float pitch_max_rad;
    float roll_min_rad;
    float roll_max_rad;
    bool valid;
};

class CMPS14 {
public:
    explicit CMPS14(uint8_t i2c_addr = CMPS14_ADDR);

    // Basics
    bool begin(TwoWire &wirePort = Wire);
    bool read();
    bool reset();
    bool is_connected() const; 

    // Calibration
    bool start_calibration(CalMode mode);
    bool stop_calibration();
    void monitor_and_store(bool autoSave);

    // Getters
    float get_heading_deg() const { return heading_deg; }
    float get_heading_rad() const { return heading_rad; }
    float get_heading_true_deg() const { return heading_true_deg; }
    float get_heading_true_rad() const { return heading_true_rad; }
    float get_pitch_deg() const { return pitch_deg; }
    float get_pitch_rad() const { return pitch_rad; }
    float get_roll_deg() const { return roll_deg; }
    float get_roll_rad() const { return roll_rad; }
    float get_pitch_min_rad() const { return pitch_min_rad; }
    float get_pitch_max_rad() const { return pitch_max_rad; }
    float get_roll_min_rad() const { return roll_min_rad; }
    float get_roll_max_rad() const { return roll_max_rad; }
    CalMode get_mode() const { return cal_mode_runtime; }
    CMPS14Data get_data() const;

private:
    // Helpers
    bool send_command(uint8_t cmd);
    bool enable_background_cal(bool autosave);
    uint8_t read_cal_status_byte();
    void get_cal_status(uint8_t out[4]);
    
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

    // Harmonic deviation model
    HarmonicCoeffs hc {0,0,0,0,0};
};
