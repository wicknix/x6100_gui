/*
 *  SPDX-License-Identifier: LGPL-2.1-or-later
 *
 *  Xiegu X6100 LVGL GUI
 *
 *  Copyright (c) 2024 Georgy Dyuldin aka R2RFE
 */

#include <stdint.h>
#include <string.h>
#include <stdio.h>
#include <sqlite3.h>
#include <lvgl/lvgl.h>
#include <aether_radio/x6100_control/control.h>

#include "common.h"
#include "types.h"
#include "db.h"

#include "../radio.h"

#include "modulation.h"

#define MAX_FILTER_FREQ 10000

/*********************
 *  Radio modes params (SSB, CW, etc)
 *********************/
typedef struct {
    int32_param_t         filter_low;
    int32_param_t         filter_high;

    uint16_param_t        freq_step;
    int16_param_t         spectrum_factor;
} params_mode_t;


static params_mode_t ssb_params = {
    .filter_low = {.x = 50, .dirty=false},
    .filter_high = {.x = 2950, .dirty=false},
    .freq_step = {.x = 500, .dirty=false},
    .spectrum_factor = {.x = 1, .dirty=false},
};

static params_mode_t ssb_dig_params = {
    .filter_low = {.x = 50, .dirty=false},
    .filter_high = {.x = 2950, .dirty=false},
    .freq_step = {.x = 500, .dirty=false},
    .spectrum_factor = {.x = 1, .dirty=false},
};

static params_mode_t cw_params = {
    .filter_low = {.x = 500, .dirty=false},
    .filter_high = {.x = 900, .dirty=false},
    .freq_step = {.x = 100, .dirty=false},
    .spectrum_factor = {.x = 4, .dirty=false},
};

static params_mode_t am_params = {
    .filter_low = {.x = 0, .dirty=false},
    .filter_high = {.x = 4000, .dirty=false},
    .freq_step = {.x = 1000, .dirty=false},
    .spectrum_factor = {.x = 1, .dirty=false},
};

static params_mode_t fm_params = {
    .filter_low = {.x = 0, .dirty=false},
    .filter_high = {.x = 4000, .dirty=false},
    .freq_step = {.x = 1000, .dirty=false},
    .spectrum_factor = {.x = 1, .dirty=false},
};

static const uint16_t freq_steps[] = {10, 100, 500, 1000, 5000};
static const size_t freq_steps_n = sizeof(freq_steps) / sizeof(freq_steps[0]);

static const x6100_mode_t db_modes[] = {
    x6100_mode_lsb,
    x6100_mode_lsb_dig,
    x6100_mode_cw,
    x6100_mode_am,
    x6100_mode_nfm
};
static const size_t db_modes_n = sizeof(db_modes) / sizeof(db_modes[0]);

static sqlite3_stmt     *write_mode_stmt;

static void params_mode_load();


inline static params_mode_t* get_params_by_mode(x6100_mode_t mode) {
    switch (mode)
    {
    case x6100_mode_lsb:
    case x6100_mode_usb:
        return &ssb_params;
        break;

    case x6100_mode_lsb_dig:
    case x6100_mode_usb_dig:
        return &ssb_dig_params;
        break;

    case x6100_mode_cw:
    case x6100_mode_cwr:
        return &cw_params;
        break;

    case x6100_mode_am:
        return &am_params;
        break;
    case x6100_mode_nfm:
        return &fm_params;
        break;

    default:
        return NULL;
        break;
    }

}

uint32_t params_mode_filter_low_get(x6100_mode_t mode) {
    if ((mode == x6100_mode_am) || (mode == x6100_mode_nfm)) {
        return 0;
    }
    return get_params_by_mode(mode)->filter_low.x;
}

uint32_t params_mode_filter_low_set(x6100_mode_t mode, int32_t val) {
    if ((mode == x6100_mode_am) || (mode == x6100_mode_nfm)) {
        return 0;
    }
    params_mode_t *mode_params = get_params_by_mode(mode);
    int32_param_t *param = &mode_params->filter_low;
    params_lock();
    if ((val != param->x) & (val >= 0) & (val < mode_params->filter_high.x)) {
        param->x = val;
        param->dirty = true;
    }
    params_unlock(NULL);
    return param->x;
}

uint32_t params_mode_filter_high_get(x6100_mode_t mode) {
    return get_params_by_mode(mode)->filter_high.x;
}
uint32_t params_mode_filter_high_set(x6100_mode_t mode, int32_t val) {
    params_mode_t *mode_params = get_params_by_mode(mode);
    int32_param_t *param = &mode_params->filter_high;
    params_lock();
    if ((val != param->x) & (val < MAX_FILTER_FREQ) & (val > mode_params->filter_low.x)) {
        param->x = val;
        param->dirty = true;
    }
    params_unlock(NULL);
    return param->x;
}

uint32_t params_mode_filter_bw_get(x6100_mode_t mode) {
    params_mode_t *params = get_params_by_mode(mode);
    return params->filter_high.x - params->filter_low.x;
}
uint32_t params_mode_filter_bw_set(x6100_mode_t mode, int32_t val) {
    int32_t change;
    params_mode_t *mode_params = get_params_by_mode(mode);
    int32_param_t *l_param = &mode_params->filter_low;
    int32_param_t *h_param = &mode_params->filter_high;
    params_lock();
    int32_t cur_bw = h_param->x - l_param->x;
    change = (val - cur_bw) / 2;
    if (h_param->x + change > MAX_FILTER_FREQ) {
        change = MAX_FILTER_FREQ - h_param->x;
    }
    if (l_param->x - change < 0) {
        change = LV_MIN(change, l_param->x);
    }
    if (cur_bw + 2 * change <= 20) {
        change = 0;
    }
    if (change != 0) {
        l_param->x -= change;
        h_param->x += change;
        l_param->dirty = true;
        h_param->dirty = true;
        cur_bw = h_param->x - l_param->x;
    }
    params_unlock(NULL);
    return cur_bw;
}

uint16_t params_mode_freq_step_get(x6100_mode_t mode) {
    return get_params_by_mode(mode)->freq_step.x;
}
uint16_t params_mode_freq_step_set(x6100_mode_t mode, uint16_t val) {
    uint16_param_t *param = &get_params_by_mode(mode)->freq_step;
    params_lock();
    if (val != param->x) {
        param->x = val;
        param->dirty = true;
    }
    params_unlock(NULL);
    return param->x;
}

int16_t params_mode_spectrum_factor_get(x6100_mode_t mode) {
    return get_params_by_mode(mode)->spectrum_factor.x;
}

int16_t params_mode_spectrum_factor_set(x6100_mode_t mode, int16_t val) {
    int16_param_t *param = &get_params_by_mode(mode)->spectrum_factor;
    params_lock();
    if ((val != param->x) & (val >= 1) & (val <= 4)) {
        param->x = val;
        param->dirty = true;
    }
    params_unlock(NULL);
    return param->x;
}

// Current mode functions
uint32_t params_current_mode_filter_low_get() {
    return params_mode_filter_low_get(radio_current_mode());
}

uint32_t params_current_mode_filter_low_set(int32_t val) {
    return params_mode_filter_low_set(radio_current_mode(), val);
}

uint32_t params_current_mode_filter_high_get() {
    return params_mode_filter_high_get(radio_current_mode());
}

uint32_t params_current_mode_filter_high_set(int32_t val) {
    return params_mode_filter_high_set(radio_current_mode(), val);
}

uint32_t params_current_mode_filter_bw_get() {
    return params_mode_filter_bw_get(radio_current_mode());
}

uint32_t params_current_mode_filter_bw_set(int32_t val) {
    return params_mode_filter_bw_set(radio_current_mode(), val);
}

uint16_t params_current_mode_freq_step_get() {
    return params_mode_freq_step_get(radio_current_mode());
}
uint16_t params_current_mode_freq_step_set(uint16_t val) {
    return params_mode_freq_step_set(radio_current_mode(), val);
}

int16_t params_current_mode_spectrum_factor_get() {
    return params_mode_spectrum_factor_get(radio_current_mode());
}
int16_t params_current_mode_spectrum_factor_set(int16_t val) {
    return params_mode_spectrum_factor_set(radio_current_mode(), val);
}


void params_current_mode_filter_get(int32_t *low, int32_t *high) {
    x6100_mode_t mode = radio_current_mode();
    params_mode_t *param = get_params_by_mode(mode);
    *low = param->filter_low.x;
    *high = param->filter_high.x;
}


uint16_t params_current_mode_freq_step_change(bool up) {
    x6100_mode_t mode = radio_current_mode();
    params_mode_t *param = get_params_by_mode(mode);
    size_t i;

    params_lock();
    for (i = 0; i < freq_steps_n; i++)
    {
        if (param->freq_step.x == freq_steps[i])
        {
            break;
        }
    }
    i = (i + (up ? 1 : -1) + freq_steps_n) % freq_steps_n;
    param->freq_step.x = freq_steps[i];
    param->freq_step.dirty = true;
    params_unlock(NULL);
    return param->freq_step.x;
}


uint32_t params_current_mode_filter_bw() {
    x6100_mode_t mode = radio_current_mode();
    params_mode_t *mode_params = get_params_by_mode(mode);

    return mode_params->filter_high.x - mode_params->filter_low.x;
}

int16_t params_current_mode_spectrum_factor_add(int16_t diff) {
    return params_current_mode_spectrum_factor_set(params_current_mode_spectrum_factor_get() + diff);
}

/* Database operations */

void params_modulation_setup() {
    int rc = sqlite3_prepare_v2(db, "INSERT INTO mode_params(mode, name, val) VALUES(?, ?, ?)", -1, &write_mode_stmt, 0);
    if (rc != SQLITE_OK) {
        LV_LOG_ERROR("Prepare mode write failed: %i", rc);
    }
    params_mode_load();
}


static void params_mode_load() {
    sqlite3_stmt    *stmt;
    int             rc;
    x6100_mode_t    mode;
    params_mode_t   *mode_params;

    char *query;

    rc = sqlite3_prepare_v2(db, "SELECT mode,name,val FROM mode_params WHERE mode IN (?,?,?,?,?)", -1, &stmt, 0);
    sqlite3_bind_int(stmt, 1, x6100_mode_lsb);
    sqlite3_bind_int(stmt, 2, x6100_mode_lsb_dig);
    sqlite3_bind_int(stmt, 3, x6100_mode_cw);
    sqlite3_bind_int(stmt, 4, x6100_mode_am);
    sqlite3_bind_int(stmt, 5, x6100_mode_nfm);

    if (rc != SQLITE_OK) {
        LV_LOG_ERROR("Prepare");
        return;
    }

    while (sqlite3_step(stmt) != SQLITE_DONE) {
        mode = sqlite3_column_int(stmt, 0);
        mode_params = get_params_by_mode(mode);

        const char *name = sqlite3_column_text(stmt, 1);

        if (strcmp(name, "filter_low") == 0) {
            mode_params->filter_low.x = sqlite3_column_int(stmt, 2);
        } else if (strcmp(name, "filter_high") == 0) {
            mode_params->filter_high.x = sqlite3_column_int(stmt, 2);
        } else if (strcmp(name, "freq_step") == 0) {
            mode_params->freq_step.x = sqlite3_column_int(stmt, 2);
        } else if (strcmp(name, "spectrum_factor") == 0) {
            mode_params->spectrum_factor.x = sqlite3_column_int(stmt, 2);
        }
    }
    sqlite3_finalize(stmt);
}


static void params_mode_write_int(x6100_mode_t mode, const char *name, int data, bool *dirty) {
    sqlite3_bind_int(write_mode_stmt, 1, mode);
    sqlite3_bind_text(write_mode_stmt, 2, name, strlen(name), 0);
    sqlite3_bind_int(write_mode_stmt, 3, data);
    sqlite3_step(write_mode_stmt);
    sqlite3_reset(write_mode_stmt);
    sqlite3_clear_bindings(write_mode_stmt);

    *dirty = false;
}

void params_mode_save() {
    pthread_mutex_lock(&db_write_mux);
    if (!sql_query_exec("BEGIN")) {
        pthread_mutex_unlock(&db_write_mux);
        return;
    }
    for (size_t i = 0; i < db_modes_n; i++)
    {
        x6100_mode_t mode = db_modes[i];
        params_mode_t *mode_params = get_params_by_mode(mode);
        if (mode_params->filter_low.dirty)       params_mode_write_int(mode, "filter_low", mode_params->filter_low.x, &mode_params->filter_low.dirty);
        if (mode_params->filter_high.dirty)      params_mode_write_int(mode, "filter_high", mode_params->filter_high.x, &mode_params->filter_high.dirty);
        if (mode_params->freq_step.dirty)        params_mode_write_int(mode, "freq_step", mode_params->freq_step.x, &mode_params->freq_step.dirty);
        if (mode_params->spectrum_factor.dirty)  params_mode_write_int(mode, "spectrum_factor", mode_params->spectrum_factor.x, &mode_params->spectrum_factor.dirty);
    }

    sql_query_exec("COMMIT");
    pthread_mutex_unlock(&db_write_mux);
}
