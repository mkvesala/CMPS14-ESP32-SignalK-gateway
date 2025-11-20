#pragma once

#include "globals.h"

void build_sk_url();
void build_sk_source();

void setup_websocket_callbacks();
void send_hdg_pitch_roll_delta();
void send_pitch_roll_minmax_delta();