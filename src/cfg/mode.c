#include "mode.private.h"

#include "subjects.private.h"

#include "../lvgl/lvgl.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

static sqlite3      *db;
static sqlite3_stmt *write_stmt;
static sqlite3_stmt *read_stmt;
static pthread_mutex_t write_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t read_mutex = PTHREAD_MUTEX_INITIALIZER;


void cfg_mode_params_init(sqlite3 *database) {
    db = database;
    int rc;
    rc = sqlite3_prepare_v2(db, "SELECT val FROM mode_params WHERE mode = :id AND name = :name", -1, &read_stmt, 0);
    if (rc != SQLITE_OK) {
        LV_LOG_ERROR("Failed prepare read statement: %s", sqlite3_errmsg(db));
        exit(1);
    }
    rc = sqlite3_prepare_v2(db, "INSERT OR REPLACE INTO mode_params(mode, name, val) VALUES(:id, :name, :val)", -1,
                            &write_stmt, 0);
    if (rc != SQLITE_OK) {
        LV_LOG_ERROR("Failed prepare write statement: %s", sqlite3_errmsg(db));
        exit(1);
    }
}


int cfg_mode_params_load_item(cfg_item_t *item) {
    if (item->pk == MODE_UNDEFINED) {
        LV_LOG_USER("Can't load %s for undefined mode", item->db_name);
        return 0;
    }
    int rc;
    int32_t val;
    sqlite3_stmt *stmt = read_stmt;
    pthread_mutex_lock(&read_mutex);
    rc = sqlite3_bind_text(stmt, sqlite3_bind_parameter_index(stmt, ":name"), item->db_name,
                           strlen(item->db_name), 0);
    if (rc != SQLITE_OK) {
        LV_LOG_ERROR("Failed to bind name %s: %s", item->db_name, sqlite3_errmsg(db));
        sqlite3_reset(stmt);
        sqlite3_clear_bindings(stmt);
        pthread_mutex_unlock(&read_mutex);
        return rc;
    }
    rc = sqlite3_bind_int(stmt, sqlite3_bind_parameter_index(stmt, ":id"), item->pk);
    if (rc != SQLITE_OK) {
        LV_LOG_ERROR("Failed to bind mode %s: %s", item->pk, sqlite3_errmsg(db));
        sqlite3_reset(stmt);
        sqlite3_clear_bindings(stmt);
        pthread_mutex_unlock(&read_mutex);
        return rc;
    }

    rc = sqlite3_step(stmt);
    if (rc == SQLITE_ROW) {
        switch (item->val->dtype) {
        case DTYPE_INT:
            val = sqlite3_column_int(stmt, 0);
            if (val < 0) {
                LV_LOG_WARN("%s can't be negative (%i), ignore DB value", item->db_name, val);
            } else {
                subject_set_int(item->val, val);
            }
            break;
        default:
            LV_LOG_WARN("Unknown item %s dtype: %u, can't load", item->db_name, item->val->dtype);
            sqlite3_reset(stmt);
            sqlite3_clear_bindings(stmt);
            pthread_mutex_unlock(&read_mutex);
            return -1;
        }
        rc = 0;
    } else {
        LV_LOG_WARN("No results for load from mode_params with name: %s and mode: %i", item->db_name, item->pk);
        // Save with default value
        cfg_mode_params_save_item(item);
        rc = -1;
    }
    sqlite3_reset(stmt);
    sqlite3_clear_bindings(stmt);
    pthread_mutex_unlock(&read_mutex);
    return rc;
}

int cfg_mode_params_save_item(cfg_item_t *item) {
    if (item->pk == MODE_UNDEFINED) {
        LV_LOG_USER("Can't save %s for undefined mode", item->db_name);
        return 0;
    }
    int      rc;

    sqlite3_stmt *stmt = write_stmt;
    pthread_mutex_lock(&write_mutex);
    rc = sqlite3_bind_text(stmt, sqlite3_bind_parameter_index(stmt, ":name"), item->db_name,
                           strlen(item->db_name), 0);
    if (rc != SQLITE_OK) {
        LV_LOG_ERROR("Failed to bind name %s: %s", item->db_name, sqlite3_errmsg(db));
        sqlite3_reset(stmt);
        sqlite3_clear_bindings(stmt);
        pthread_mutex_unlock(&write_mutex);
        return rc;
    }
    rc = sqlite3_bind_int(stmt, sqlite3_bind_parameter_index(stmt, ":id"), item->pk);
    if (rc != SQLITE_OK) {
        LV_LOG_ERROR("Failed to bind mode %s: %s", item->pk, sqlite3_errmsg(db));
        sqlite3_reset(stmt);
        sqlite3_clear_bindings(stmt);
        pthread_mutex_unlock(&write_mutex);
        return rc;
    }
    int val_index = sqlite3_bind_parameter_index(stmt, ":val");

    switch (item->val->dtype) {
    case DTYPE_INT:
        if (item->val->int_val < 0) {
            LV_LOG_ERROR("%s can't be negative (%i), will not save", item->db_name, item->val->int_val);
            sqlite3_reset(stmt);
            sqlite3_clear_bindings(stmt);
            pthread_mutex_unlock(&write_mutex);
            return -1;
        } else {
            rc = sqlite3_bind_int(stmt, val_index, item->val->int_val);
        }
        break;
    default:
        LV_LOG_WARN("Unknown item %s dtype: %u, will not save", item->db_name, item->val->dtype);
        rc = -1;
        break;
    }
    if (rc != SQLITE_OK) {
        LV_LOG_ERROR("Failed to bind val for name %s: %s", item->db_name, sqlite3_errmsg(db));
        sqlite3_reset(stmt);
        sqlite3_clear_bindings(stmt);
        pthread_mutex_unlock(&write_mutex);
        return rc;
    }
    rc = sqlite3_step(stmt);
    if (rc != SQLITE_DONE) {
        LV_LOG_ERROR("Failed save item %s: %s", item->db_name, sqlite3_errmsg(db));
    } else {
        rc = 0;
    }
    sqlite3_reset(stmt);
    sqlite3_clear_bindings(stmt);
    pthread_mutex_unlock(&write_mutex);
    return rc;
}


db_mode_t xmode_2_db(x6100_mode_t mode) {
    switch (mode)
    {
    case x6100_mode_lsb:
    case x6100_mode_usb:
        return x6100_mode_lsb;
        break;

    case x6100_mode_lsb_dig:
    case x6100_mode_usb_dig:
        return x6100_mode_lsb_dig;
        break;

    case x6100_mode_cw:
    case x6100_mode_cwr:
        return x6100_mode_cw;
        break;

    case x6100_mode_am:
        return x6100_mode_am;
        break;
    case x6100_mode_nfm:
        return x6100_mode_nfm;
        break;

    default:
        return MODE_UNDEFINED;
        break;
    }
}

bool mode_default_values(db_mode_t mode, uint32_t *low, uint32_t *high, uint32_t *step, uint32_t *zoom) {
    bool result = true;
    switch (mode) {
        case x6100_mode_lsb:
        case x6100_mode_lsb_dig:
            *low = 50;
            *high = 2950;
            *step = 500;
            *zoom = 1;
            break;
        case x6100_mode_cw:
            *low = 0;
            *high = 250;
            *step = 100;
            *zoom = 4;
            break;
        case x6100_mode_am:
        case x6100_mode_nfm:
            *low = 0;
            *high = 4000;
            *step = 1000;
            *zoom = 1;
            break;
        default:
            result = false;
            break;
    }
    return result;
}
