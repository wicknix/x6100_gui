/*
 *  SPDX-License-Identifier: LGPL-2.1-or-later
 *
 *  Xiegu X6100 LVGL GUI
 *
 *  Copyright (c) 2024 Georgy Dyuldin aka R2RFE
 */
#include "band.h"

#include <string.h>
#include <lvgl/lvgl.h>

#include "common.h"
#include "types.h"
#include "db.h"

#include "../meter.h"
#include "../util.h"

typedef struct {
    params_uint64_t freq;
    params_bool_t shift;
    params_uint8_t att;
    params_uint8_t pre;
    params_uint8_t mode;
    params_uint8_t agc;
} params_vfo_t;

typedef struct {
    params_uint8_t vfo;
    params_bool_t split;
    params_int16_t grid_min;
    params_int16_t grid_max;
    params_uint16_t rfg;
    struct {char x[64]; bool dirty;} label;

    params_vfo_t    vfo_x[2];
} params_band_t;

static uint16_t band_id;
static params_band_t params_band = {
    .vfo                = {.x=X6100_VFO_A, .dirty=false},

    .vfo_x[X6100_VFO_A] = {
        .freq           = {.x=14000000, .dirty=false},
        .att            = {.x=x6100_att_off, .dirty=false},
        .pre            = {.x=x6100_pre_off, .dirty=false},
        .mode           = {.x=x6100_mode_usb, .dirty=false},
        .agc            = {.x=x6100_agc_fast, .dirty=false}
    },

    .vfo_x[X6100_VFO_B] = {
        .freq           = {.x=14100000, .dirty=false},
        .att            = {.x=x6100_att_off, .dirty=false},
        .pre            = {.x=x6100_pre_off, .dirty=false},
        .mode           = {.x=x6100_mode_usb, .dirty=false},
        .agc            = {.x=x6100_agc_fast, .dirty=false}
    },

    .split              = {.x=false, .dirty=false},
    .grid_min           = {.x=-121, .dirty=false},
    .grid_max           = {.x=-73, .dirty=false},
    .rfg                = {.x=63, .dirty=false},
};

static sqlite3_stmt     *write_mb_stmt;

static void params_mb_save(uint16_t id);
static void params_mb_load(sqlite3_stmt *stmt);

/* Memory/Bands params */

void params_memory_load(uint16_t id) {
    sqlite3_stmt *stmt;

    int rc = sqlite3_prepare_v2(db, "SELECT name,val FROM memory WHERE id = ?", -1, &stmt, 0);

    if (rc != SQLITE_OK) {
        LV_LOG_ERROR("Prepare");
        return;
    }

    sqlite3_bind_int(stmt, 1, id);
    params_mb_load(stmt);
}

void params_band_load(uint16_t id) {
    sqlite3_stmt *stmt;
    band_id = id;

    int rc = sqlite3_prepare_v2(db, "SELECT name,val FROM band_params WHERE bands_id = ?", -1, &stmt, 0);

    if (rc != SQLITE_OK) {
        LV_LOG_ERROR("Prepare");
        return;
    }

    sqlite3_bind_int(stmt, 1, id);
    params_mb_load(stmt);
}

static void params_mb_load(sqlite3_stmt *stmt) {
    bool copy_freq = true;
    bool copy_att = true;
    bool copy_pre = true;
    bool copy_mode = true;
    bool copy_agc = true;

    memset(params_band.label.x, 0, sizeof(params_band.label.x));

    while (sqlite3_step(stmt) != SQLITE_DONE) {
        const char *name = sqlite3_column_text(stmt, 0);

        if (strcmp(name, "vfo") == 0) {
            params_band.vfo.x = sqlite3_column_int(stmt, 1);
        } else if (strcmp(name, "vfoa_freq") == 0) {
            params_band.vfo_x[X6100_VFO_A].freq.x = sqlite3_column_int64(stmt, 1);
        } else if (strcmp(name, "vfoa_att") == 0) {
            params_band.vfo_x[X6100_VFO_A].att.x = sqlite3_column_int(stmt, 1);
        } else if (strcmp(name, "vfoa_pre") == 0) {
            params_band.vfo_x[X6100_VFO_A].pre.x = sqlite3_column_int(stmt, 1);
        } else if (strcmp(name, "vfoa_mode") == 0) {
            params_band.vfo_x[X6100_VFO_A].mode.x = sqlite3_column_int(stmt, 1);
        } else if (strcmp(name, "vfoa_agc") == 0) {
            params_band.vfo_x[X6100_VFO_A].agc.x = sqlite3_column_int(stmt, 1);
        } else if (strcmp(name, "vfob_freq") == 0) {
            params_band.vfo_x[X6100_VFO_B].freq.x = sqlite3_column_int64(stmt, 1);
            copy_freq = false;
        } else if (strcmp(name, "vfob_att") == 0) {
            params_band.vfo_x[X6100_VFO_B].att.x = sqlite3_column_int(stmt, 1);
            copy_att = false;
        } else if (strcmp(name, "vfob_pre") == 0) {
            params_band.vfo_x[X6100_VFO_B].pre.x = sqlite3_column_int(stmt, 1);
            copy_pre = false;
        } else if (strcmp(name, "vfob_mode") == 0) {
            params_band.vfo_x[X6100_VFO_B].mode.x = sqlite3_column_int(stmt, 1);
            copy_mode = false;
        } else if (strcmp(name, "vfob_agc") == 0) {
            params_band.vfo_x[X6100_VFO_B].agc.x = sqlite3_column_int(stmt, 1);
            copy_agc = false;
        } else if (strcmp(name, "split") == 0) {
            params_band.split.x = (bool) sqlite3_column_int(stmt, 1);
        } else if (strcmp(name, "grid_min") == 0) {
            params_band.grid_min.x = sqlite3_column_int(stmt, 1);
        } else if (strcmp(name, "grid_max") == 0) {
            params_band.grid_max.x = sqlite3_column_int(stmt, 1);
        } else if (strcmp(name, "label") == 0) {
            strncpy(params_band.label.x, sqlite3_column_text(stmt, 1), sizeof(params_band.label) - 1);
        } else if (strcmp(name, "rfg") == 0) {
            params_band.rfg.x = sqlite3_column_int64(stmt, 1);
        }
    }

    if (copy_freq)  params_band.vfo_x[X6100_VFO_B].freq.x = params_band.vfo_x[X6100_VFO_A].freq.x;
    if (copy_att)   params_band.vfo_x[X6100_VFO_B].att.x = params_band.vfo_x[X6100_VFO_A].att.x;
    if (copy_pre)   params_band.vfo_x[X6100_VFO_B].pre.x = params_band.vfo_x[X6100_VFO_A].pre.x;
    if (copy_mode)  params_band.vfo_x[X6100_VFO_B].mode.x = params_band.vfo_x[X6100_VFO_A].mode.x;
    if (copy_agc)   params_band.vfo_x[X6100_VFO_B].agc.x = params_band.vfo_x[X6100_VFO_A].agc.x;

    sqlite3_finalize(stmt);
}

static void params_mb_write_int(uint16_t id, const char *name, int data, bool *dirty) {
    sqlite3_bind_int(write_mb_stmt, 1, id);
    sqlite3_bind_text(write_mb_stmt, 2, name, strlen(name), 0);
    sqlite3_bind_int(write_mb_stmt, 3, data);
    sqlite3_step(write_mb_stmt);
    sqlite3_reset(write_mb_stmt);
    sqlite3_clear_bindings(write_mb_stmt);

    *dirty = false;
}

static void params_mb_write_int64(uint16_t id, const char *name, uint64_t data, bool *dirty) {
    sqlite3_bind_int(write_mb_stmt, 1, id);
    sqlite3_bind_text(write_mb_stmt, 2, name, strlen(name), 0);
    sqlite3_bind_int64(write_mb_stmt, 3, data);
    sqlite3_step(write_mb_stmt);
    sqlite3_reset(write_mb_stmt);
    sqlite3_clear_bindings(write_mb_stmt);

    *dirty = false;
}

void params_band_save(uint16_t id) {
    if (!sql_query_exec("BEGIN")) {
        return;
    }

    sqlite3_prepare_v2(db, "INSERT INTO band_params(bands_id, name, val) VALUES(?, ?, ?)", -1, &write_mb_stmt, 0);

    params_mb_save(id);
    sql_query_exec("COMMIT");
    sqlite3_finalize(write_mb_stmt);
}

void params_memory_save(uint16_t id) {
    if (!sql_query_exec("BEGIN")) {
        return;
    }

    sqlite3_prepare_v2(db, "INSERT INTO memory(id, name, val) VALUES(?, ?, ?)", -1, &write_mb_stmt, 0);

    params_band.vfo.dirty = true;

    for (uint8_t i = X6100_VFO_A; i <= X6100_VFO_B; i++) {
        params_band.vfo_x[i].freq.dirty = true;
        params_band.vfo_x[i].att.dirty = true;
        params_band.vfo_x[i].pre.dirty = true;
        params_band.vfo_x[i].mode.dirty = true;
        params_band.vfo_x[i].agc.dirty = true;
    }

    params_band.grid_min.dirty = true;
    params_band.grid_max.dirty = true;
    params_band.rfg.dirty = true;

    params_mb_save(id);
    sql_query_exec("COMMIT");
    sqlite3_finalize(write_mb_stmt);
}

static void params_mb_save(uint16_t id) {
    // TODO: add saving shift
    if (params_band.vfo.dirty)
        params_mb_write_int(id, "vfo", params_band.vfo.x, &params_band.vfo.dirty);

    if (params_band.vfo_x[X6100_VFO_A].freq.dirty)
        params_mb_write_int64(id, "vfoa_freq", params_band.vfo_x[X6100_VFO_A].freq.x, &params_band.vfo_x[X6100_VFO_A].freq.dirty);

    if (params_band.vfo_x[X6100_VFO_A].att.dirty)
        params_mb_write_int(id, "vfoa_att", params_band.vfo_x[X6100_VFO_A].att.x, &params_band.vfo_x[X6100_VFO_A].att.dirty);

    if (params_band.vfo_x[X6100_VFO_A].pre.dirty)
        params_mb_write_int(id, "vfoa_pre", params_band.vfo_x[X6100_VFO_A].pre.x, &params_band.vfo_x[X6100_VFO_A].pre.dirty);

    if (params_band.vfo_x[X6100_VFO_A].mode.dirty)
        params_mb_write_int(id, "vfoa_mode", params_band.vfo_x[X6100_VFO_A].mode.x, &params_band.vfo_x[X6100_VFO_A].mode.dirty);

    if (params_band.vfo_x[X6100_VFO_A].agc.dirty)
        params_mb_write_int(id, "vfoa_agc", params_band.vfo_x[X6100_VFO_A].agc.x, &params_band.vfo_x[X6100_VFO_A].agc.dirty);

    if (params_band.vfo_x[X6100_VFO_B].freq.dirty)
        params_mb_write_int64(id, "vfob_freq", params_band.vfo_x[X6100_VFO_B].freq.x, &params_band.vfo_x[X6100_VFO_B].freq.dirty);

    if (params_band.vfo_x[X6100_VFO_B].att.dirty)
        params_mb_write_int(id, "vfob_att", params_band.vfo_x[X6100_VFO_B].att.x, &params_band.vfo_x[X6100_VFO_B].att.dirty);

    if (params_band.vfo_x[X6100_VFO_B].pre.dirty)
        params_mb_write_int(id, "vfob_pre", params_band.vfo_x[X6100_VFO_B].pre.x, &params_band.vfo_x[X6100_VFO_B].pre.dirty);

    if (params_band.vfo_x[X6100_VFO_B].mode.dirty)
        params_mb_write_int(id, "vfob_mode", params_band.vfo_x[X6100_VFO_B].mode.x, &params_band.vfo_x[X6100_VFO_B].mode.dirty);

    if (params_band.vfo_x[X6100_VFO_B].agc.dirty)
        params_mb_write_int(id, "vfob_agc", params_band.vfo_x[X6100_VFO_B].agc.x, &params_band.vfo_x[X6100_VFO_B].agc.dirty);

    if (params_band.split.dirty)
        params_mb_write_int(id, "split", params_band.split.x, &params_band.split.dirty);

    if (params_band.grid_min.dirty)
        params_mb_write_int(id, "grid_min", params_band.grid_min.x, &params_band.grid_min.dirty);

    if (params_band.grid_max.dirty)
        params_mb_write_int(id, "grid_max", params_band.grid_max.x, &params_band.grid_max.dirty);

    if (params_band.rfg.dirty)
        params_mb_write_int(id, "rfg", params_band.rfg.x, &params_band.rfg.dirty);
}

void params_band_vfo_clone()
{
    params_vfo_t *a = &params_band.vfo_x[X6100_VFO_A];
    params_vfo_t *b = &params_band.vfo_x[X6100_VFO_B];
    params_vfo_t *changed;

    if (params_band.vfo.x == X6100_VFO_A) {
        *b = *a;
        changed = b;
    } else {
        *a = *b;
        changed = a;
    }
    changed->freq.dirty = true;
    changed->att.dirty = true;
    changed->pre.dirty = true;
    changed->mode.dirty = true;
    changed->agc.dirty = true;
}

static params_vfo_t * get_cur_vfo_params() {
    return &params_band.vfo_x[params_band.vfo.x];
}

uint64_t params_band_vfo_freq_get(x6100_vfo_t vfo)
{
    return params_band.vfo_x[vfo].freq.x;
}

uint64_t params_band_vfo_freq_set(x6100_vfo_t vfo, uint64_t freq)
{
    if (params_band.vfo_x[vfo].freq.x != freq) {
        params_lock();
        params_band.vfo_x[vfo].freq.x = freq;
        params_band.vfo_x[vfo].freq.dirty = true;
        params_unlock(NULL);
    }
    return params_band.vfo_x[vfo].freq.x;
}

x6100_mode_t params_band_vfo_mode_get(x6100_vfo_t vfo)
{
    return params_band.vfo_x[vfo].mode.x;
}

x6100_mode_t params_band_vfo_mode_set(x6100_vfo_t vfo, x6100_mode_t mode)
{
    if (params_band.vfo_x[vfo].mode.x != mode) {
        params_lock();
        params_band.vfo_x[vfo].mode.x = mode;
        params_band.vfo_x[vfo].mode.dirty = true;
        params_unlock(NULL);
    }
    return params_band.vfo_x[vfo].mode.x;
}

x6100_agc_t params_band_vfo_agc_get(x6100_vfo_t vfo)
{
    return params_band.vfo_x[vfo].agc.x;
}

x6100_pre_t params_band_vfo_pre_get(x6100_vfo_t vfo)
{
    return params_band.vfo_x[vfo].pre.x;
}

x6100_att_t params_band_vfo_att_get(x6100_vfo_t vfo)
{
    return params_band.vfo_x[vfo].att.x;
}

bool params_band_vfo_shift_set(x6100_vfo_t vfo, bool shift)
{
    if (params_band.vfo_x[vfo].shift.x != shift) {
        params_lock();
        params_band.vfo_x[vfo].shift.x = shift;
        params_band.vfo_x[vfo].shift.dirty = true;
        params_unlock(NULL);
    }
    return params_band.vfo_x[vfo].shift.x;
}

x6100_vfo_t params_band_vfo_get()
{
    return params_band.vfo.x;
}

x6100_vfo_t params_band_vfo_set(x6100_vfo_t vfo)
{
    if (params_band.vfo.x != vfo) {
        params_lock();
        params_band.vfo.x = vfo;
        params_band.vfo.dirty = true;
        params_unlock(NULL);
    }
    return params_band.vfo.x;
}

x6100_mode_t params_band_cur_mode_get()
{
    return get_cur_vfo_params()->mode.x;
}

x6100_agc_t params_band_cur_agc_get()
{
    return get_cur_vfo_params()->agc.x;
}

x6100_agc_t params_band_cur_agc_set(x6100_agc_t agc)
{
    params_vfo_t * cur_param = get_cur_vfo_params();
    if (cur_param->agc.x != agc) {
        params_lock();
        cur_param->agc.x = agc;
        cur_param->agc.dirty = true;
        params_unlock(NULL);
    }
    return cur_param->agc.x;
}

uint64_t params_band_cur_freq_get()
{
    return get_cur_vfo_params()->freq.x;
}

uint64_t params_band_cur_freq_set(uint64_t freq)
{
    params_vfo_t * cur_param = get_cur_vfo_params();
    if (cur_param->freq.x != freq) {
        params_lock();
        cur_param->freq.x = freq;
        cur_param->freq.dirty = true;
        params_unlock(NULL);
    }
    return cur_param->freq.x;
}

bool params_band_cur_shift_get()
{
    return get_cur_vfo_params()->shift.x;
}

bool params_band_cur_shift_set(bool shift)
{
    params_vfo_t * cur_param = get_cur_vfo_params();
    if (cur_param->shift.x != shift) {
        params_lock();
        cur_param->shift.x = shift;
        cur_param->shift.dirty = true;
        params_unlock(NULL);
    }
    return cur_param->shift.x;
}

x6100_pre_t params_band_cur_pre_get()
{
    return get_cur_vfo_params()->pre.x;
}

x6100_pre_t params_band_cur_pre_set(x6100_pre_t pre)
{
    params_vfo_t * cur_param = get_cur_vfo_params();
    if (cur_param->pre.x != pre) {
        params_lock();
        cur_param->pre.x = pre;
        cur_param->pre.dirty = true;
        params_unlock(NULL);
    }
    return cur_param->pre.x;
}

x6100_att_t params_band_cur_att_get()
{
    return get_cur_vfo_params()->att.x;
}

x6100_att_t params_band_cur_att_set(x6100_att_t att)
{
    params_vfo_t * cur_param = get_cur_vfo_params();
    if (cur_param->att.x != att) {
        params_lock();
        cur_param->att.x = att;
        cur_param->att.dirty = true;
        params_unlock(NULL);
    }
    return cur_param->att.x;
}

bool params_band_split_get()
{
    return params_band.split.x;
}

bool params_band_split_set(bool split)
{
    if (params_band.split.x != split) {
        params_lock();
        params_band.split.x = split;
        params_band.split.dirty = true;
        params_unlock(NULL);
    }
    return params_band.split.x;
}

uint16_t params_band_rfg_get()
{
    return params_band.rfg.x;
}

uint16_t params_band_rfg_set(int16_t rfg)
{
    rfg = limit(rfg, 0, 100);
    if (params_band.rfg.x != rfg) {
        params_lock();
        params_band.rfg.x = rfg;
        params_band.rfg.dirty = true;
        params_unlock(NULL);
    }
    return params_band.rfg.x;
    return 0;
}

const char *params_band_label_get()
{
    return params_band.label.x;
}

int16_t params_band_grid_min_get()
{
    return params_band.grid_min.x;
}

int16_t params_band_grid_min_set(int16_t db)
{
    if (db > S7) {
        db = S7;
    } else if (db < S_MIN) {
        db = S_MIN;
    }
    if ((params_band.grid_min.x != db) && (db < params_band.grid_max.x)) {
        params_lock();
        params_band.grid_min.x = db;
        params_band.grid_min.dirty = true;
        params_unlock(NULL);
    }

    return params_band.grid_min.x;
}

int16_t params_band_grid_max_get()
{
    return params_band.grid_max.x;
}

int16_t params_band_grid_max_set(int16_t db)
{
    if (db > S9_40) {
        db = S9_40;
    } else if (db < S8) {
        db = S8;
    }
    if ((params_band.grid_max.x != db) && (db > params_band.grid_min.x)) {
        params_lock();
        params_band.grid_max.x = db;
        params_band.grid_max.dirty = true;
        params_unlock(NULL);
    }

    return params_band.grid_max.x;
}
