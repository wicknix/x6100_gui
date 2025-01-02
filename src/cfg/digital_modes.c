#include "digital_modes.private.h"
#include "cfg.h"

#include "../lvgl/lvgl.h"
#include <pthread.h>
#include <stdlib.h>


static sqlite3        *db;
static sqlite3_stmt   *get_next_stmt;
static sqlite3_stmt   *get_closest_stmt;
static sqlite3_stmt   *get_prev_stmt;
static pthread_mutex_t read_mutex  = PTHREAD_MUTEX_INITIALIZER;

static char *label = NULL;


void cfg_digital_modes_init(sqlite3 *database) {
    db = database;
    int rc;
    rc = sqlite3_prepare_v2(
        db, "SELECT label, freq, mode FROM digital_modes WHERE type = :type AND freq > :freq ORDER BY freq ASC LIMIT 1", -1,
        &get_next_stmt, 0);
    if (rc != SQLITE_OK) {
        LV_LOG_ERROR("Failed prepare get next statement: %s", sqlite3_errmsg(db));
        exit(1);
    }
    rc = sqlite3_prepare_v2(
        db, "SELECT label, freq, mode FROM digital_modes WHERE type = :type ORDER BY ABS(freq - :freq) ASC LIMIT 1", -1,
        &get_closest_stmt, 0);
    if (rc != SQLITE_OK) {
        LV_LOG_ERROR("Failed prepare get closest statement: %s", sqlite3_errmsg(db));
        exit(1);
    }
    rc = sqlite3_prepare_v2(
        db, "SELECT label, freq, mode FROM digital_modes WHERE type = :type AND freq < :freq ORDER BY freq DESC LIMIT 1",
        -1, &get_prev_stmt, 0);
    if (rc != SQLITE_OK) {
        LV_LOG_ERROR("Failed prepare get prev statement: %s", sqlite3_errmsg(db));
        exit(1);
    }
}

bool cfg_digital_load(int8_t dir, cfg_digital_type_t type) {
    sqlite3_stmt *stmt;
    if (dir > 0) {
        stmt = get_next_stmt;
    } else if (dir == 0) {
        stmt = get_closest_stmt;
    } else {
        stmt = get_prev_stmt;
    }
    int rc;
    pthread_mutex_lock(&read_mutex);
    rc = sqlite3_bind_int(stmt, sqlite3_bind_parameter_index(stmt, ":type"), type);
    if (rc != SQLITE_OK) {
        LV_LOG_ERROR("Failed to bind type %i: %s", type, sqlite3_errmsg(db));
        sqlite3_reset(stmt);
        sqlite3_clear_bindings(stmt);
        pthread_mutex_unlock(&read_mutex);
        return false;
    }
    int32_t cur_freq = subject_get_int(cfg_cur.fg_freq);
    rc = sqlite3_bind_int(stmt, sqlite3_bind_parameter_index(stmt, ":freq"), cur_freq);
    if (rc != SQLITE_OK) {
        LV_LOG_ERROR("Failed to bind freq %i: %s", cur_freq, sqlite3_errmsg(db));
        sqlite3_reset(stmt);
        sqlite3_clear_bindings(stmt);
        pthread_mutex_unlock(&read_mutex);
        return false;
    }
    int32_t freq, mode;
    rc = sqlite3_step(stmt);
    if (rc == SQLITE_ROW) {
        if (label != NULL) {
            free(label);
        }
        label = strdup(sqlite3_column_text(stmt, 0));
        freq = sqlite3_column_int(stmt, 1);
        mode = sqlite3_column_int(stmt, 2);
        rc = 0;
    } else {
        LV_LOG_WARN("No results for load from digital_modes with dir=%i, freq=%i, type=%i", dir, cur_freq, type);
        rc = -1;
    }
    sqlite3_reset(stmt);
    sqlite3_clear_bindings(stmt);
    pthread_mutex_unlock(&read_mutex);

    if (rc != 0) {
        return false;
    }
    subject_set_int(cfg_cur.fg_freq, freq);
    subject_set_int(cfg_cur.mode, mode);
    return true;
}

char *cfg_digital_label_get() {
    return label;
}
