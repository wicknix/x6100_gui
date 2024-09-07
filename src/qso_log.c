/*
 *  SPDX-License-Identifier: LGPL-2.1-or-later
 *
 *  Xiegu X6100 LVGL GUI
 *
 *  Copyright (c) 2024 Georgy Dyuldin aka R2RFE
 */

#include "qso_log.h"

#include "params/db.h"
#include "util.h"

#include <lvgl/src/misc/lv_log.h>
#include <string.h>


static sqlite3_stmt *search_callsign_stmt=NULL;


static inline int bind_optional_text(sqlite3_stmt * stmt, int pos, const char * val) {
    if (!val) {
        return sqlite3_bind_null(stmt, pos);
    } else {
        return sqlite3_bind_text(stmt, pos, val, strlen(val), 0);
    }
}

int qso_log_add_record(const char *local_call, const char *remote_call,
                       time_t time, const char *mode, int rsts, int rstr, float freq_mhz,
                       const char *local_qth, const char *remote_qth, const char *name) {
    sqlite3_stmt    *stmt;
    int             rc;

    if (!local_call) {
        LV_LOG_ERROR("Local callsign is required");
        return -1;
    }
    if (!remote_call) {
        LV_LOG_ERROR("Remote callsign is required");
        return -1;
    }
    if (!mode) {
        LV_LOG_ERROR("Modulation is required");
        return -1;
    }

    rc = sqlite3_prepare_v2(
        db, "INSERT INTO qso_log ("
                "ts, freq, mode, local_callsign, remote_callsign, rsts, rstr, "
                "local_qth, remote_qth, op_name"
            ") VALUES (datetime(?, 'unixepoch'), ?, ?, ?, ?, ?, ?, ?, ?, ?)",
                       -1, &stmt, 0);
    if (rc != SQLITE_OK) {
        return -1;
    }
    rc = sqlite3_bind_int64(stmt, 1, time);
    if (rc != SQLITE_OK) return -1;
    rc = sqlite3_bind_double(stmt, 2, (double) freq_mhz);
    if (rc != SQLITE_OK) return -1;
    rc = sqlite3_bind_text(stmt, 3, mode, strlen(mode), 0);
    if (rc != SQLITE_OK) return -1;
    rc = sqlite3_bind_text(stmt, 4, local_call, strlen(local_call), 0);
    if (rc != SQLITE_OK) return -1;
    rc = sqlite3_bind_text(stmt, 5, remote_call, strlen(remote_call), 0);
    if (rc != SQLITE_OK) return -1;
    rc = sqlite3_bind_int(stmt, 6, rsts);
    if (rc != SQLITE_OK) return -1;
    rc = sqlite3_bind_int(stmt, 7, rstr);
    if (rc != SQLITE_OK) return -1;
    rc = bind_optional_text(stmt, 8, local_qth);
    if (rc != SQLITE_OK) return -1;
    rc = bind_optional_text(stmt, 9, remote_qth);
    if (rc != SQLITE_OK) return -1;
    rc = bind_optional_text(stmt, 10, name);
    if (rc != SQLITE_OK) return -1;

    pthread_mutex_lock(&db_write_mux);
    sqlite3_step(stmt);
    pthread_mutex_unlock(&db_write_mux);

    sqlite3_finalize(stmt);
    return 0;
}


int qso_log_search_remote_callsign(const char *callsign, size_t max_count, qso_log_search_item_t * items) {
    int             rc;
    size_t          cur_item = 0;

    if (!search_callsign_stmt) {
        rc = sqlite3_prepare_v3(db, "SELECT DISTINCT CAST(freq AS INT), mode FROM qso_log WHERE remote_callsign LIKE ?",
                        -1, SQLITE_PREPARE_PERSISTENT, &search_callsign_stmt, 0);
        if (rc != SQLITE_OK) {
            return -1;
        }
    } else {
        sqlite3_reset(search_callsign_stmt);
        sqlite3_clear_bindings(search_callsign_stmt);
    }

    char * canonized_callsign = util_canonize_callsign(callsign, true);
    rc = sqlite3_bind_text(search_callsign_stmt, 1, canonized_callsign, strlen(canonized_callsign), 0);
    if (rc != SQLITE_OK) {
        return -1;
    }

    while (sqlite3_step(search_callsign_stmt) != SQLITE_DONE) {
        items[cur_item].freq_mhz = sqlite3_column_int(search_callsign_stmt, 0);
        strcpy(items[cur_item].mode, sqlite3_column_text(search_callsign_stmt, 1));
        cur_item++;
        if (cur_item >= max_count) {
            break;
        }
    }

    return cur_item;
}
