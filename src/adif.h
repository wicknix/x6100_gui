/*
 *  SPDX-License-Identifier: LGPL-2.1-or-later
 *
 *  Xiegu X6100 LVGL GUI
 *
 *  Copyright (c) 2024 Georgy Dyuldin aka R2RFE
 */
#pragma once

#include <time.h>

typedef struct adif_log_s *adif_log;

adif_log  adif_log_init(const char * path);

void adif_log_close(adif_log  l);

void adif_add_qso(adif_log  l, const char * local_call, const char * remote_call,
    time_t time, const char * mode, int rsts, int rstr, float freq_mhz,
    const char * local_grid, const char * remote_grid);
