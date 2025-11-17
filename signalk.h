#pragma once

#include "globals.h"

void build_sk_url();
void make_source_from_mac();
void classify_rssi(int rssi);

void setup_ws_callbacks();
void send_batch_delta_if_needed();
void send_minmax_delta_if_due();