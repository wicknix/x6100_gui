/*
 *  SPDX-License-Identifier: LGPL-2.1-or-later
 *
 *  Xiegu X6100 LVGL GUI
 *
 *  Copyright (c) 2024 Georgy Dyuldin aka R2RFE
 */

#include <string.h>
#include <lvgl/src/misc/lv_log.h>
#include "db.h"

#include "../util.h"

sqlite3                 *db = NULL;
pthread_mutex_t         db_write_mux = PTHREAD_MUTEX_INITIALIZER;

static sqlite3_stmt     *write_stmt;


static void errorLogCallback(void *pArg, int iErrCode, const char *zMsg){
    LV_LOG_ERROR("(%d) %s\n", iErrCode, zMsg);
}


bool database_init() {
    sqlite3_config(SQLITE_CONFIG_LOG, errorLogCallback, NULL);

    int rc = sqlite3_open("/mnt/params.db", &db);

    if (rc != SQLITE_OK) {
        LV_LOG_ERROR("Can't open params.db");
        return false;
    }

    rc = sqlite3_prepare_v2(db, "INSERT INTO params(name, val) VALUES(?, ?)", -1, &write_stmt, 0);

    if (rc != SQLITE_OK) {
        LV_LOG_ERROR("Can't prepare insert statement for params");
        return false;
    }
    return true;
}

bool sql_query_exec(const char *sql) {
    char    *err = 0;
    int     rc;

    rc = sqlite3_exec(db, sql, NULL, NULL, &err);

    if (rc != SQLITE_OK) {
        LV_LOG_ERROR(err);
        return false;
    }
    return true;
}


void params_write_int(const char *name, int data, bool *dirty) {
    sqlite3_bind_text(write_stmt, 1, name, strlen(name), 0);
    sqlite3_bind_int(write_stmt, 2, data);
    sqlite3_step(write_stmt);
    sqlite3_reset(write_stmt);
    sqlite3_clear_bindings(write_stmt);

    *dirty = false;
}

void params_write_int64(const char *name, uint64_t data, bool *dirty) {
    sqlite3_bind_text(write_stmt, 1, name, strlen(name), 0);
    sqlite3_bind_int64(write_stmt, 2, data);
    sqlite3_step(write_stmt);
    sqlite3_reset(write_stmt);
    sqlite3_clear_bindings(write_stmt);

    *dirty = false;
}

void params_write_float(const char *name, float data, bool *dirty) {
    sqlite3_bind_text(write_stmt, 1, name, strlen(name), 0);
    sqlite3_bind_double(write_stmt, 2, (double) data);
    sqlite3_step(write_stmt);
    sqlite3_reset(write_stmt);
    sqlite3_clear_bindings(write_stmt);

    *dirty = false;
}

void params_write_text(const char *name, const char *data, bool *dirty) {
    sqlite3_bind_text(write_stmt, 1, name, strlen(name), 0);
    sqlite3_bind_text(write_stmt, 2, data, strlen(data), 0);
    sqlite3_step(write_stmt);
    sqlite3_reset(write_stmt);
    sqlite3_clear_bindings(write_stmt);

    *dirty = false;
}
