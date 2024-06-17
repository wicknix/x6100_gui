/*
 *  SPDX-License-Identifier: LGPL-2.1-or-later
 *
 *  Xiegu X6100 LVGL GUI
 *
 *  Copyright (c) 2024 Georgy Dyuldin aka R2RFE
 */
#pragma once

#include <stdbool.h>
#include <aether_radio/x6100_control/control.h>

void params_memory_load(uint16_t id);
void params_memory_save(uint16_t id);

void params_band_load(uint16_t id);
void params_band_save(uint16_t id);


/* Custom vfo params get/set */

uint64_t params_band_vfo_freq_get(x6100_vfo_t vfo);
uint64_t params_band_vfo_freq_set(x6100_vfo_t vfo, uint64_t freq);

x6100_mode_t params_band_vfo_mode_get(x6100_vfo_t vfo);
x6100_mode_t params_band_vfo_mode_set(x6100_vfo_t vfo, x6100_mode_t mode);
x6100_agc_t params_band_vfo_agc_get(x6100_vfo_t vfo);
x6100_pre_t params_band_vfo_pre_get(x6100_vfo_t vfo);
x6100_att_t params_band_vfo_att_get(x6100_vfo_t vfo);

bool params_band_vfo_shift_set(x6100_vfo_t vfo, bool shift);

x6100_vfo_t params_band_vfo_get();
x6100_vfo_t params_band_vfo_set(x6100_vfo_t vfo);

/* Current vfo params get-set*/
uint64_t params_band_cur_freq_get();
uint64_t params_band_cur_freq_set(uint64_t freq);

bool params_band_cur_shift_get();
bool params_band_cur_shift_set(bool shift);

x6100_pre_t params_band_cur_pre_get();
x6100_pre_t params_band_cur_pre_set(x6100_pre_t pre);

x6100_att_t params_band_cur_att_get();
x6100_att_t params_band_cur_att_set(x6100_att_t att);

x6100_mode_t params_band_cur_mode_get();

x6100_agc_t params_band_cur_agc_get();
x6100_agc_t params_band_cur_agc_set(x6100_agc_t agc);

/* Band params get/set */

bool params_band_split_get();
bool params_band_split_set(bool split);

uint16_t params_band_rfg_get();
uint16_t params_band_rfg_set(int16_t rfg);

const char * params_band_label_get();

int16_t params_band_grid_min_get();
int16_t params_band_grid_min_set(int16_t db);
int16_t params_band_grid_max_get();
int16_t params_band_grid_max_set(int16_t db);


void params_band_vfo_clone();
