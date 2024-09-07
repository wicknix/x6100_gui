/*
 *  SPDX-License-Identifier: LGPL-2.1-or-later
 *
 *  Xiegu X6100 LVGL GUI
 *
 *  Copyright (c) 2024 Georgy Dyuldin aka R2RFE
 */

#pragma once

#include <time.h>
#include <stdint.h>
#include <stdbool.h>

typedef struct {
    uint16_t    freq_mhz;
    char        mode[16];
} qso_log_search_item_t;


int qso_log_add_record(const char * local_call, const char * remote_call,
    time_t time, const char * mode, int rsts, int rstr, float freq_mhz,
    const char * local_qth, const char * remote_qth, const char * name);

/**
 * Search callsign in log.
 * @return count of distinct records
 * @param[in] max_count Max expected results
 * @param[out] items array of `qso_log_search_item_t`
 */
int qso_log_search_remote_callsign(const char *callsign, size_t max_count, qso_log_search_item_t * items);
