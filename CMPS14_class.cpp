#include "CMPS14.h"

// CMPS14 register map
static const uint8_t REG_ANGLE_16_H    = 0x02;  // 16-bit angle * 10 (hi)
static const uint8_t REG_ANGLE_16_L    = 0x03;  // 16-bit angle * 10 (lo)
static const uint8_t REG_PITCH         = 0x04;  // signed degrees
static const uint8_t REG_ROLL          = 0x05;  // signed degrees
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
static const uint8_t REG_ACK1          = 0x55;  // Ack (new firmware)
static const uint8_t REG_ACK2          = 0x07;  // Ack (CMPS12 compliant)
static const uint8_t REG_NACK          = 0xFF;  // Nack
static const uint8_t REG_CMD           = 0x00;  // Command byte, write before sending other commands
static const uint8_t REG_MASK          = 0x03;  // Mask to read individual calibration status bits for sys, acc, gyr, mag

// Constructor
CMPS14::CMPS14(uint8_t i2c_addr) : addr(i2c_addr), wire(&Wire) {}

// Initialize sensor
bool CMPS14::begin(TwoWire &wirePort) {
    wire = &wirePort;
    wire->beginTransmission(addr);
    return (wire->endTransmission() == 0);
}

// Check connection
bool CMPS14::isConnected() const {
    Wire.beginTransmission(addr);
    return (Wire.endTransmission() == 0);
}

// Read current sensor data
bool CMPS14::read() {
    wire->beginTransmission(addr);
    wire->write(REG_ANGLE_16_H);
    if (wire->endTransmission(false) != 0) return false;

    const uint8_t toRead = 4;
    uint8_t n = wire->requestFrom(addr, toRead);
    if (n != toRead) return false;

    uint8_t hi = wire->read();
    uint8_t lo = wire->read();
    int8_t pitch_raw = (int8_t)wire->read();
    int8_t roll_raw  = (int8_t)wire->read();

    uint16_t ang10 = ((uint16_t)hi << 8) | lo;
    float raw_deg = ((float)ang10) / 10.0f;
    smoothHeading(raw_deg);

    // Apply harmonic deviation and variation
    float dev_deg = deviation_harm_deg(hc, compass_deg);
    heading_deg = compass_deg + dev_deg;
    if (heading_deg >= 360.0f) heading_deg -= 360.0f;
    if (heading_deg < 0.0f) heading_deg += 360.0f;

    float mv_deg = use_manual_magvar ? magvar_manual_deg : magvar_deg;
    heading_true_deg = heading_deg + mv_deg;
    if (heading_true_deg >= 360.0f) heading_true_deg -= 360.0f;
    if (heading_true_deg < 0.0f) heading_true_deg += 360.0f;

    pitch_deg = (float)pitch_raw;
    roll_deg  = (float)roll_raw;
    return true;
}

// Smooth heading
void CMPS14::smoothHeading(float raw_deg) {
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
}

// Send command to CMPS14
bool CMPS14::sendCommand(uint8_t cmd) {
    wire->beginTransmission(addr);
    wire->write(REG_CMD);
    wire->write(cmd);
    if (wire->endTransmission() != 0) return false;
    delay(20);
    wire->requestFrom(addr, (uint8_t)1);
    if (!wire->available()) return false;
    uint8_t b = wire->read();
    return (b == REG_ACK1 || b == REG_ACK2);
}

// Reset the CMPS14
bool CMPS14::reset() {
    return sendCommand(REG_RESET1)
        && sendCommand(REG_RESET2)
        && sendCommand(REG_RESET3)
        && sendCommand(0x80); // use mode
}

// Enable background calibration
bool CMPS14::enableBackgroundCal(bool autosave) {
    return sendCommand(REG_CAL1)
        && sendCommand(REG_CAL2)
        && sendCommand(REG_CAL3)
        && sendCommand(autosave ? REG_AUTO_ON : REG_AUTO_OFF);
}

// Start calibration
bool CMPS14::startCalibration(CalMode mode) {
    cal_mode_runtime = mode;
    bool ok = false;
    switch (mode) {
        case CAL_FULL_AUTO: ok = enableBackgroundCal(true); break;
        case CAL_SEMI_AUTO: ok = enableBackgroundCal(false); break;
        case CAL_MANUAL:    ok = enableBackgroundCal(false); break;
        default: ok = sendCommand(0x80); break; // use mode
    }
    return ok;
}

// Stop calibration
bool CMPS14::stopCalibration() {
    cal_mode_runtime = CAL_USE;
    return sendCommand(0x80);
}

// Monitor and store calibration if needed
void CMPS14::monitorAndStore(bool autoSave) {
    uint8_t statuses[4];
    getCalStatus(statuses);
    uint8_t mag = statuses[0], acc = statuses[1], sys = statuses[3];
    if (sys == 3 && acc == 3 && mag == 3) {
        if (cal_ok_count < 255) cal_ok_count++;
    } else cal_ok_count = 0;

    if (autoSave && !cal_profile_stored && cal_ok_count >= CAL_OK_REQUIRED) {
        if (sendCommand(REG_SAVE1) && sendCommand(REG_SAVE2) && sendCommand(REG_SAVE3)) {
            cal_profile_stored = true;
            lcd_show_info("CALIBRATION", "SAVED");
            cal_mode_runtime = CAL_USE;
        } else {
            lcd_show_info("CALIBRATION", "FAILED");
        }
        cal_ok_count = 0;
    }
}

// Read calibration status byte
uint8_t CMPS14::readCalStatusByte() {
    wire->beginTransmission(addr);
    wire->write(REG_CAL_STATUS);
    if (wire->endTransmission(false) != 0) return REG_NACK;
    wire->requestFrom(addr, (uint8_t)1);
    if (!wire->available()) return REG_NACK;
    return wire->read();
}

// Get calibration status
void CMPS14::getCalStatus(uint8_t out[4]) {
    uint8_t byte = readCalStatusByte();
    uint8_t mag = 255, acc = 255, gyr = 255, sys = 255;
    if (byte != REG_NACK) {
        mag = (byte     ) & REG_MASK;
        acc = (byte >> 2) & REG_MASK;
        gyr = (byte >> 4) & REG_MASK;
        sys = (byte >> 6) & REG_MASK;
    }
    out[0] = mag; out[1] = acc; out[2] = gyr; out[3] = sys;
}
