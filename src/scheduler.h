/*
 *  SPDX-License-Identifier: LGPL-2.1-or-later
 *
 *  Xiegu X6100 LVGL GUI
 *
 *  Copyright (c) 2024 Georgy Dyuldin aka R2RFE
 */

#pragma once

#include <stddef.h>

typedef void (* scheduler_fn_t)(void *);

/**
 * Schedule execution function in main thread
 */
void scheduler_put(scheduler_fn_t fn, void *arg, size_t arg_size);


/**
 * Execute scheduled functions
 */
void scheduler_work();
