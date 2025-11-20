#pragma once

#include "globals.h"
#include "display.h"

bool read_compass();

bool cmps14_cmd(uint8_t cmd);
bool cmps14_enable_background_cal(bool autosave);
uint8_t cmps14_read_cal_status_byte();
void cmps14_get_cal_status(uint8_t out[4]);
bool cmps14_store_profile();
void cmps14_monitor_and_store(bool save);
bool cmps14_reset();

bool start_calibration_manual_mode();
bool start_calibration_fullauto();
bool start_calibration_semiauto();
bool stop_calibration();

void cmps14_init_with_cal_mode();