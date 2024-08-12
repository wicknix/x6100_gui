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

lv_obj_t * waterfall_init(lv_obj_t * parent, uint64_t cur_freq);
void waterfall_data(float *data_buf, uint16_t size, bool tx);
void waterfall_set_height(lv_coord_t h);
void waterfall_min_max_reset();

void waterfall_set_max(float db);
void waterfall_set_min(float db);
void waterfall_update_max(float db);
void waterfall_update_min(float db);
void waterfall_set_freq(uint64_t freq);
void waterfall_refresh_reset();
void waterfall_refresh_period_set(uint8_t k);
