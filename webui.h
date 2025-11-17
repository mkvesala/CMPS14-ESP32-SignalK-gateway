#pragma once

#include "globals.h"
#include "cmps14.h"

void handle_status();
void handle_set_offset();
void handle_dev8_set();
void handle_calmode_set();
void handle_magvar_set();
void handle_heading_mode();
void handle_root();
void handle_deviation_details();
void handle_restart();
void handle_calibrate_on();
void handle_calibrate_off();
void handle_store();
void handle_reset();