/*
 *  SPDX-License-Identifier: LGPL-2.1-or-later
 *
 *  Xiegu X6100 LVGL GUI
 *
 *  Copyright (c) 2022-2023 Belousov Oleg aka R1CBU
 */

#pragma once

#include "cfg/subjects.h"

#ifdef __cplusplus
extern "C" {
#endif

#include "cfg/cfg.h"

#include <stdint.h>
#include <stdbool.h>

void vol_update(int16_t diff, bool voice);
void vol_change_mode(int16_t dir);
void vol_set_mode(cfg_vol_mode_t mode);

#ifdef __cplusplus
}
#endif
