#pragma once

#include "globals.h"

void lcd_init_safe();
void lcd_print_lines(const char* l1, const char* l2);
void lcd_show_info(const char* l1, const char* l2);

void led_update_by_cal_mode();
void led_update_by_conn_status();