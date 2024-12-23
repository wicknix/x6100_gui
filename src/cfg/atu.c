#include "atu.private.h"

#include "subjects.private.h"

#include "../lvgl/lvgl.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#define STR_INDIR(x) #x
#define STR(x) STR_INDIR(x)

#define ATU_SAVE_STEP 50000

static sqlite3      *db;
static sqlite3_stmt *write_stmt;
static sqlite3_stmt *read_stmt;
static pthread_mutex_t write_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t read_mutex = PTHREAD_MUTEX_INITIALIZER;

static struct {
    int32_t freq;
    uint32_t network;
} _atu_cache;


void cfg_atu_init(sqlite3 *database) {
    db = database;
    int rc;
    rc = sqlite3_prepare_v2(db, "SELECT freq, val FROM atu WHERE ant = :ant AND ABS(freq - :freq) < " STR(ATU_SAVE_STEP), -1, &read_stmt, 0);
    if (rc != SQLITE_OK) {
        LV_LOG_ERROR("Failed prepare read statement: %s", sqlite3_errmsg(db));
        exit(1);
    }
    rc = sqlite3_prepare_v2(db, "INSERT OR REPLACE INTO atu(ant, freq, val) VALUES(:ant, :freq, :val)", -1,
                            &write_stmt, 0);
    if (rc != SQLITE_OK) {
        LV_LOG_ERROR("Failed prepare write statement: %s", sqlite3_errmsg(db));
        exit(1);
    }
}


bool load_atu_params(int32_t ant_id, int32_t freq, uint32_t *network) {
    if (abs(freq - _atu_cache.freq) < ATU_SAVE_STEP) {
        *network = _atu_cache.network;
        return true;
    }
    int rc;
    sqlite3_stmt *stmt = read_stmt;
    pthread_mutex_lock(&read_mutex);
    rc = sqlite3_bind_int(stmt, sqlite3_bind_parameter_index(stmt, ":ant"), ant_id);
    if (rc != SQLITE_OK) {
        LV_LOG_ERROR("Failed to bind ant_id %i: %s", ant_id, sqlite3_errmsg(db));
        sqlite3_reset(stmt);
        sqlite3_clear_bindings(stmt);
        pthread_mutex_unlock(&read_mutex);
        return false;
    }
    rc = sqlite3_bind_int(stmt, sqlite3_bind_parameter_index(stmt, ":freq"), freq);
    if (rc != SQLITE_OK) {
        LV_LOG_ERROR("Failed to bind freq %i: %s", freq, sqlite3_errmsg(db));
        sqlite3_reset(stmt);
        sqlite3_clear_bindings(stmt);
        pthread_mutex_unlock(&read_mutex);
        return false;
    }

    rc = sqlite3_step(stmt);
    if (rc == SQLITE_ROW) {
        _atu_cache.freq = sqlite3_column_int(stmt, 0);
        _atu_cache.network = sqlite3_column_int(stmt, 1);
        *network = _atu_cache.network;
        rc = 0;
    } else {
        LV_LOG_WARN("No results for load atu for freq: %i and ant: %i", freq, ant_id);
        rc = -1;
    }
    sqlite3_reset(stmt);
    sqlite3_clear_bindings(stmt);
    pthread_mutex_unlock(&read_mutex);
    return rc == 0;
}

int save_atu_params(int32_t ant_id, int32_t freq, uint32_t network) {
    int      rc;

    sqlite3_stmt *stmt = write_stmt;
    pthread_mutex_lock(&write_mutex);
    rc = sqlite3_bind_int(stmt, sqlite3_bind_parameter_index(stmt, ":ant"), ant_id);
    if (rc != SQLITE_OK) {
        LV_LOG_ERROR("Failed to bind ant_id %i: %s", ant_id, sqlite3_errmsg(db));
        sqlite3_reset(stmt);
        sqlite3_clear_bindings(stmt);
        pthread_mutex_unlock(&write_mutex);
        return rc;
    }
    rc = sqlite3_bind_int(stmt, sqlite3_bind_parameter_index(stmt, ":freq"), freq);
    if (rc != SQLITE_OK) {
        LV_LOG_ERROR("Failed to bind freq %i: %s", freq, sqlite3_errmsg(db));
        sqlite3_reset(stmt);
        sqlite3_clear_bindings(stmt);
        pthread_mutex_unlock(&write_mutex);
        return rc;
    }
    rc = sqlite3_bind_int(stmt, sqlite3_bind_parameter_index(stmt, ":val"), network);
    if (rc != SQLITE_OK) {
        LV_LOG_ERROR("Failed to bind network %i: %s", network, sqlite3_errmsg(db));
        sqlite3_reset(stmt);
        sqlite3_clear_bindings(stmt);
        pthread_mutex_unlock(&write_mutex);
        return rc;
    }

    rc = sqlite3_step(stmt);
    if (rc != SQLITE_DONE) {
        LV_LOG_ERROR("Failed save atu_params: %s", sqlite3_errmsg(db));
    } else {
        rc = 0;
        _atu_cache.freq = freq;
        _atu_cache.network = network;
    }
    sqlite3_reset(stmt);
    sqlite3_clear_bindings(stmt);
    pthread_mutex_unlock(&write_mutex);
    return rc;
}
