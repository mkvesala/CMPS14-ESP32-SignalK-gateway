#include "CMPS14Processor.h"

// Constructor, takes CMPS14Sensor as parameter
CMPS14Processor::CMPS14Processor(CMPS14Sensor &cmps14Sensor) : sensor(cmps14Sensor), wire(nullptr) {}

// Begin, calls CMPSSensor.begin()
bool CMPS14Processor::begin(TwoWire &wirePort) {
    wire = &wirePort;
    return sensor.begin(wirePort);
}

// Update global shared variables, interim solution, globals to be replaced by data struct of CMPS14Processor
bool CMPS14Processor::update() {
    float raw_deg, pitch_raw, roll_raw;

    if (!sensor.read(raw_deg, pitch_raw, roll_raw)) return false;
    
    // Heading (C)
    raw_deg += installation_offset_deg;
    if (raw_deg >= 360.0f) raw_deg -= 360.0f;
    if (raw_deg < 0.0f) raw_deg += 360.0f;
    if (isnan(compass_deg)) {
        compass_deg = raw_deg;
    } else {
        float diff = raw_deg - compass_deg;
        if (diff > 180.0f) diff -= 360.0f;
        if (diff < -180.0f) diff += 360.0f;
        compass_deg += HEADING_ALPHA * diff;
        if (compass_deg >= 360.0f) compass_deg -= 360.0f;
        if (compass_deg < 0.0f) compass_deg += 360.0f;
    }

    // Heading (M)
    float dev_deg = deviation_harm_deg(hc, compass_deg);
    heading_deg = compass_deg + dev_deg;
    if (heading_deg >= 360.0f) heading_deg -= 360.0f;
    if (heading_deg < 0.0f) heading_deg += 360.0f;

    // Heading (T)
    float mv_deg = use_manual_magvar ? magvar_manual_deg : magvar_deg;
    heading_true_deg = heading_deg + mv_deg;
    if (heading_true_deg >= 360.0f) heading_true_deg -= 360.0f;
    if (heading_true_deg < 0.0f) heading_true_deg += 360.0f;

    pitch_deg = pitch_raw;
    roll_deg  = roll_raw;

    // Radians for SignalK
    heading_rad      = heading_deg * DEG_TO_RAD;
    heading_true_rad = heading_true_deg * DEG_TO_RAD;
    pitch_rad        = pitch_deg * DEG_TO_RAD;
    roll_rad         = roll_deg * DEG_TO_RAD;

    // Update the new maximum values
    if (isnan(pitch_max_rad)) pitch_max_rad = pitch_rad;
    else if (pitch_rad > pitch_max_rad) pitch_max_rad = pitch_rad;
    
    if (isnan(pitch_min_rad)) pitch_min_rad = pitch_rad;
    else if (pitch_rad < pitch_min_rad) pitch_min_rad = pitch_rad;

    if (isnan(roll_max_rad)) roll_max_rad = roll_rad;
    else if (roll_rad > roll_max_rad) roll_max_rad = roll_rad;

    if (isnan(roll_min_rad)) roll_min_rad = roll_rad;
    else if (roll_rad < roll_min_rad) roll_min_rad = roll_rad;

    return true;
}

// Reset CMPS14Sensor
bool CMPS14Processor::reset() {
    bool ok =
        sensor.sendCommand(REG_RESET1) &&
        sensor.sendCommand(REG_RESET2) &&
        sensor.sendCommand(REG_RESET3);
    if (!ok) return false;
    delay(599);  // Datasheet recommends delay of 300 ms here
    if (sensor.sendCommand(REG_USEMODE)) {
        cal_mode_runtime = CAL_USE;
        cal_mode_boot = CAL_USE;
        cmps14_cal_profile_stored = false;
        cal_profile_stored = false;
        return true;
    }
    return false;
}

// Enable calibration with optional autosave (built in autosave of CMPS14)
bool CMPS14Processor::enableBackgroundCal(bool autosave) {
    return sensor.sendCommand(REG_CAL1)
        && sensor.sendCommand(REG_CAL2)
        && sensor.sendCommand(REG_CAL3)
        && sensor.sendCommand(autosave ? REG_AUTO_ON : REG_AUTO_OFF);
}

// Start calibration with desired calibration mode
bool CMPS14Processor::startCalibration(CalMode mode) {
    cal_mode_runtime = mode;
    bool ok = false;
    switch (mode) {
        case CAL_FULL_AUTO: {
            ok = enableBackgroundCal(true);
            full_auto_left_ms = 0;
            full_auto_start_ms = millis();
        } break;
        case CAL_SEMI_AUTO: ok = enableBackgroundCal(false); break;
        case CAL_MANUAL:    ok = enableBackgroundCal(false); break;
        default:            ok = sensor.sendCommand(REG_USEMODE); break;
    }
    return ok;
}

// Stop calibration and return to use-mode
bool CMPS14Processor::stopCalibration() {
    cal_mode_runtime = CAL_USE;
    return sensor.sendCommand(REG_USEMODE);
}

// Read calibration status byte
uint8_t CMPS14Processor::readCalStatusByte() {
    return sensor.readRegister(REG_CAL_STATUS);
}

// Get calibration status by checking the contents of the calibration byte
void CMPS14Processor::getCalStatus(uint8_t out[4]) {
    uint8_t byte = readCalStatusByte();
    uint8_t mag = 255, acc = 255, gyr = 255, sys = 255;
    if (!sensor.isNack(byte)) {
        mag = (byte     ) & REG_MASK;
        acc = (byte >> 2) & REG_MASK;
        gyr = (byte >> 4) & REG_MASK;
        sys = (byte >> 6) & REG_MASK;
    }
    out[0] = mag; out[1] = acc; out[2] = gyr; out[3] = sys;
}

// Monitor calibration and optionally save calibration profile
void CMPS14Processor::monitorCalibration(bool autosave) {
    uint8_t statuses[4];
    getCalStatus(statuses);
    uint8_t mag = statuses[0], acc = statuses[1], sys = statuses[3];
    if (sys == 3 && acc == 3 && mag == 3) {
        if (cal_ok_count < 255) cal_ok_count++;
    } else cal_ok_count = 0;

    if (autosave && !cal_profile_stored && cal_ok_count >= CAL_OK_REQUIRED) {
        bool ok = 
            sensor.sendCommand(REG_SAVE1) &&
            sensor.sendCommand(REG_SAVE2) && 
            sensor.sendCommand(REG_SAVE3) &&
            sensor.sendCommand(REG_USEMODE);
        if (ok) {
            cal_profile_stored = true;
            cmps14_cal_profile_stored = true;
            updateLCD("CALIBRATION", "SAVED", true);
            cal_mode_runtime = CAL_USE;
        } else {
            updateLCD("CALIBRATION", "FAILED", true);
        }
        cal_ok_count = 0;
    }
}

// Save calibration profile
bool CMPS14Processor::saveCalibrationProfile() {
    bool ok = 
        sensor.sendCommand(REG_SAVE1) &&
        sensor.sendCommand(REG_SAVE2) &&
        sensor.sendCommand(REG_SAVE3) &&
        sensor.sendCommand(REG_USEMODE);
    if (ok) {
        cal_profile_stored = true;
        cmps14_cal_profile_stored = true;
        cal_mode_runtime = CAL_USE;
    }
    return ok;
}

// Start calibration mode or use-mode, default is use-mode, manual never used at boot
bool CMPS14Processor::initCalibrationModeBoot(CalMode mode_boot) {
    bool started = false;
    if (!sensor.available()) {
        updateLCD("CMPS14 N/A", "CHECK WIRING!");
        cal_mode_runtime = CAL_USE;
        return started;
    }
    switch (mode_boot) {
        case CAL_FULL_AUTO:     started = startCalibration(CAL_FULL_AUTO); break;
        case CAL_SEMI_AUTO:     started = startCalibration(CAL_SEMI_AUTO); break;
        case CAL_MANUAL:        started = startCalibration(CAL_MANUAL); break;
        default:                started = stopCalibration(); break;
    }
    if (!started) updateLCD("CAL MODE", "START FAILED");
    else updateLCD("CAL MODE", calmode_str(cal_mode_runtime));
    return started;
}