#include "cmps14.h"

// CMPS14 I2C address and registers
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

// Read values from CMPS14 compass and attitude sensor
bool read_compass(){
  
  Wire.beginTransmission(CMPS14_ADDR);
  Wire.write(REG_ANGLE_16_H);
  if (Wire.endTransmission(false) != 0) return false;

  const uint8_t toRead = 4;
  uint8_t n = Wire.requestFrom(CMPS14_ADDR, toRead);
  if (n != toRead) return false;

  // Raw values from CMPS14 - pich and roll may be negative
  uint8_t hi   = Wire.read();
  uint8_t lo   = Wire.read();
  int8_t pitch = (int8_t)Wire.read();
  int8_t roll  = (int8_t)Wire.read();

  // Raw deg
  uint16_t ang10 = ((uint16_t)hi << 8) | lo;
  float raw_deg = ((float)ang10) / 10.0f; 
  
  // Heading (C)
  raw_deg += installation_offset_deg;
  if (raw_deg >= 360.0f) raw_deg -= 360.0f;
  if (raw_deg <    0.0f) raw_deg += 360.0f;
  if (isnan(compass_deg)) {                   
    compass_deg = raw_deg;
  } else {                                    
    float diff = raw_deg - compass_deg;
    if (diff > 180.0f)  diff -= 360.0f;       
    if (diff < -180.0f) diff += 360.0f;
    compass_deg += HEADING_ALPHA * diff;  
    if (compass_deg >= 360.0f) compass_deg -= 360.0f;
    if (compass_deg < 0.0f)   compass_deg += 360.0f;
  }
 
  // Heading (M)
  dev_deg = deviation_harm_deg(hc, compass_deg);
  heading_deg = compass_deg + dev_deg; 
  if (heading_deg < 0) heading_deg += 360.0f;
  if (heading_deg >= 360.0f) heading_deg -= 360.0f;

  // Heading (T)
  float mv_deg = use_manual_magvar ? magvar_manual_deg : magvar_deg; 
  heading_true_deg = heading_deg + mv_deg;                           
  if (heading_true_deg < 0) heading_true_deg += 360.0f;
  if (heading_true_deg >= 360.0f) heading_true_deg -= 360.0f;

  pitch_deg   = (float)pitch;
  roll_deg    = (float)roll;

  // Radians for SignalK
  heading_rad      = heading_deg * DEG_TO_RAD;
  heading_true_rad = heading_true_deg * DEG_TO_RAD;
  pitch_rad        = pitch_deg * DEG_TO_RAD;
  roll_rad         = roll_deg * DEG_TO_RAD;

  // Update the new maximum values
  if (isnan(pitch_max_rad)) {      
    pitch_max_rad = pitch_rad;
  } else if (pitch_rad > pitch_max_rad) {
    pitch_max_rad = pitch_rad;
  }
  
  if (isnan(pitch_min_rad)) {
    pitch_min_rad = pitch_rad;
  } else if (pitch_rad < pitch_min_rad) {
    pitch_min_rad = pitch_rad;
  }

  if (isnan(roll_max_rad)) {
    roll_max_rad = roll_rad;
  } else if (roll_rad > roll_max_rad) {
    roll_max_rad = roll_rad;
  }

  if (isnan(roll_min_rad)) {
    roll_min_rad = roll_rad;
  } else if (roll_rad < roll_min_rad) {
    roll_min_rad = roll_rad;
  }

  return true;

}

// Send a command to CMPS14
bool cmps14_cmd(uint8_t cmd) {
  Wire.beginTransmission(CMPS14_ADDR);
  Wire.write(REG_CMD);
  Wire.write(cmd);
  if (Wire.endTransmission() != 0) return false;
  delay(23);  // Delay of 20 ms recommended on CMPS14 datasheet

  Wire.requestFrom(CMPS14_ADDR, (uint8_t)1);
  if (Wire.available() < 1) return false;
  uint8_t b = Wire.read();
  if (b == REG_ACK1 || b == REG_ACK2) return true;
  return false;
}

// Enable autocalibration with optional autosave
bool cmps14_enable_background_cal(bool autosave) {
  if (!cmps14_cmd(REG_CAL1)) return false;
  if (!cmps14_cmd(REG_CAL2)) return false;
  if (!cmps14_cmd(REG_CAL3)) return false;
  const uint8_t cfg = autosave ? REG_AUTO_ON : REG_AUTO_OFF;
  if (!cmps14_cmd(cfg)) return false;
  return true;
}

// Read calibration status
uint8_t cmps14_read_cal_status_byte() {
  Wire.beginTransmission(CMPS14_ADDR);
  Wire.write(REG_CAL_STATUS);
  if (Wire.endTransmission(false) != 0) return REG_NACK;
  Wire.requestFrom(CMPS14_ADDR, (uint8_t)1);
  if (Wire.available() < 1) return REG_NACK;
  uint8_t b = Wire.read();
  return b;
}

// Read calibration status byte content
void cmps14_get_cal_status(uint8_t out[4]){
  uint8_t mag = 255, acc = 255, gyr = 255, sys = 255;
  uint8_t byte = cmps14_read_cal_status_byte();
  if (byte != REG_NACK) {
    mag =  (byte     ) & REG_MASK;
    acc =  (byte >> 2) & REG_MASK;
    gyr =  (byte >> 4) & REG_MASK;
    sys =  (byte >> 6) & REG_MASK;
  }
  out[0] = mag;
  out[1] = acc;
  out[2] = gyr;
  out[3] = sys;
}

// Save calibration AND stop calibrating
bool cmps14_store_profile() {
  if (!cmps14_cmd(REG_SAVE1)) return false; 
  if (!cmps14_cmd(REG_SAVE2)) return false;
  if (!cmps14_cmd(REG_SAVE3)) return false;
  if (!cmps14_cmd(REG_USEMODE)) return false; 
  cmps14_cal_profile_stored = true;
  return true;
}

// Monitor and optional storing of the calibration profile
void cmps14_monitor_and_store(bool save) {
  static uint8_t cal_ok_count = 0;
  uint8_t statuses[4];
  cmps14_get_cal_status(statuses);
  uint8_t mag   = statuses[0];
  uint8_t accel = statuses[1];
  uint8_t gyro  = statuses[2];
  uint8_t sys   = statuses[3];

  // Omit gyr, there's a reported firmware bug in CMPS14
  if (sys == 3 && accel == 3 && mag == 3) { 
    if (cal_ok_count < 255) cal_ok_count++;
  } else {
    cal_ok_count = 0;
  }
 
  // AUTO mode saving routine
  if (save && !cmps14_cal_profile_stored && cal_ok_count >= CAL_OK_REQUIRED) { 
    if (cmps14_store_profile()) {
      lcd_show_info("CALIBRATION", "SAVED");
      cal_mode_runtime = CAL_USE;
    } else {
      lcd_show_info("CALIBRATION", "NOT SAVED");
    }
    cal_ok_count = 0;
  }
}

// Reset CMPS14
bool cmps14_reset() {
  if (cmps14_cmd(REG_RESET1) && cmps14_cmd(REG_RESET2) && cmps14_cmd(REG_RESET3)) {
    delay(601);   // Datasheet recommends 300 ms delay
    if (cmps14_cmd(REG_USEMODE)){ 
      return true;
    }
    return false;
  }
  return false;
}

// Start calibration in manual mode
bool start_calibration_manual_mode() {
  if (!cmps14_enable_background_cal(false)) return false;
  cal_mode_runtime = CAL_MANUAL;
  return true;
}

// Start calibration in full auto mode
bool start_calibration_fullauto() {
  if (!cmps14_enable_background_cal(true)) return false;
  cal_mode_runtime = CAL_FULL_AUTO;
  full_auto_left_ms = 0;
  full_auto_start_ms = millis();
  return true;
}

// Start calibration in semi auto mode
bool start_calibration_semiauto(){
  if (!cmps14_enable_background_cal(false)) return false;
  cal_mode_runtime = CAL_SEMI_AUTO;
  return true;
}

// Stop all calibration
bool stop_calibration() {
  if (!cmps14_cmd(REG_USEMODE)) return false; 
  cal_mode_runtime = CAL_USE;
  return true;
}

// Start calibration or use-mode based on preferences, default is use-mode, manual never used at boot
void cmps14_init_with_cal_mode() {
  if (i2c_device_present(CMPS14_ADDR)){
    bool started = false;
    switch (cal_mode_boot){                           
      case CAL_FULL_AUTO:
        started = start_calibration_fullauto();
        break;
      case CAL_SEMI_AUTO:
        started = start_calibration_semiauto();
        break;
      case CAL_MANUAL:
        started = start_calibration_manual_mode();
        break;
      default:
        started = stop_calibration();
        break;
    }
    if (!started) lcd_print_lines("CAL MODE", "START FAILED");
    else lcd_print_lines("CAL MODE", calmode_str(cal_mode_runtime));
  } else lcd_print_lines("CMPS14 N/A", "CHECK WIRING");
}