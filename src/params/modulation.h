/*
 *  SPDX-License-Identifier: LGPL-2.1-or-later
 *
 *  Xiegu X6100 LVGL GUI
 *
 *  Copyright (c) 2024 Georgy Dyuldin aka R2RFE
 */

#pragma once

#include <stdint.h>
#include <stdbool.h>

#define MAX_CW_BW 500

typedef int32_t (* get_lo_offset_t)();

uint32_t params_current_mode_filter_high_get();
uint32_t params_current_mode_filter_high_set(int32_t val);
uint32_t params_current_mode_filter_low_get();
uint32_t params_current_mode_filter_low_set(int32_t val);
uint32_t params_current_mode_filter_bw_get();
uint32_t params_current_mode_filter_bw_set(int32_t val);

int16_t params_current_mode_spectrum_factor_get();
int16_t params_current_mode_spectrum_factor_set(int16_t val);

void params_current_mode_filter_get(int32_t *low, int32_t *high);

uint16_t params_current_mode_freq_step_change(bool up);

uint16_t params_current_mode_freq_step_get();

void params_modulation_setup(get_lo_offset_t fn);
void params_mode_save();
