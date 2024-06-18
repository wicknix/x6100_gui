/*
 *  SPDX-License-Identifier: LGPL-2.1-or-later
 *
 *  Xiegu X6100 LVGL GUI
 *
 *  Copyright (c) 2022-2023 Belousov Oleg aka R1CBU
 */

#pragma once

#include <unistd.h>
#include <stdint.h>

#include "lvgl/lvgl.h"

lv_obj_t * spectrum_init(lv_obj_t * parent, uint8_t factor);
void spectrum_data(float *data_buf, uint16_t size, bool tx);
void spectrum_min_max_reset();
void spectrum_zoom_factor_set(uint8_t val);

float spectrum_get_min();
void spectrum_set_max(float db);
void spectrum_set_min(float db);
void spectrum_update_max(float db);
void spectrum_update_min(float db);
void spectrum_change_freq(int16_t df);
void spectrum_clear();
void spectrum_update_filters();
void spectrum_update_factor();
