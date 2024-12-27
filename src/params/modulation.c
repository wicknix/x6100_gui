// /*
//  *  SPDX-License-Identifier: LGPL-2.1-or-later
//  *
//  *  Xiegu X6100 LVGL GUI
//  *
//  *  Copyright (c) 2024 Georgy Dyuldin aka R2RFE
//  */

// #include "modulation.h"

// #include "common.h"
// #include "types.h"
// #include "db.h"

// #include "../radio.h"
// #include "../pubsub_ids.h"

// #include <string.h>
// #include <stdio.h>
// #include <sqlite3.h>
// #include <lvgl/lvgl.h>
// #include <aether_radio/x6100_control/control.h>

// #define MAX_FILTER_FREQ 10000

// /*********************
//  *  Radio modes params (SSB, CW, etc)
//  *********************/
// typedef struct {
//     params_int32_t         filter_low;
//     params_int32_t         filter_high;
// } params_mode_t;

// // static get_lo_offset_t get_lo_offset = NULL;

// static params_mode_t ssb_params = {
//     .filter_low = {.x = 50, .dirty=false},
//     .filter_high = {.x = 2950, .dirty=false},
// };

// static params_mode_t ssb_dig_params = {
//     .filter_low = {.x = 50, .dirty=false},
//     .filter_high = {.x = 2950, .dirty=false},
// };

// static params_mode_t cw_params = {
//     .filter_low = {.x = 0, .dirty=false},
//     .filter_high = {.x = 250, .dirty=false},
// };

// static params_mode_t am_params = {
//     .filter_low = {.x = 0, .dirty=false},
//     .filter_high = {.x = 4000, .dirty=false},
// };

// static params_mode_t fm_params = {
//     .filter_low = {.x = 0, .dirty=false},
//     .filter_high = {.x = 4000, .dirty=false},
// };

// static const x6100_mode_t db_modes[] = {
//     x6100_mode_lsb,
//     x6100_mode_lsb_dig,
//     x6100_mode_cw,
//     x6100_mode_am,
//     x6100_mode_nfm
// };
// static const size_t db_modes_n = sizeof(db_modes) / sizeof(db_modes[0]);

// static sqlite3_stmt     *write_mode_stmt;

// static void params_mode_load();


// inline static params_mode_t* get_params_by_mode(x6100_mode_t mode) {
//     switch (mode)
//     {
//     case x6100_mode_lsb:
//     case x6100_mode_usb:
//         return &ssb_params;
//         break;

//     case x6100_mode_lsb_dig:
//     case x6100_mode_usb_dig:
//         return &ssb_dig_params;
//         break;

//     case x6100_mode_cw:
//     case x6100_mode_cwr:
//         return &cw_params;
//         break;

//     case x6100_mode_am:
//         return &am_params;
//         break;
//     case x6100_mode_nfm:
//         return &fm_params;
//         break;

//     default:
//         return NULL;
//         break;
//     }

// }

// uint32_t params_mode_filter_high_get(x6100_mode_t mode) {
//     switch (mode) {
//         case x6100_mode_cw:
//         case x6100_mode_cwr:
//             return LV_ABS(get_lo_offset()) + cw_params.filter_high.x / 2;
//         default:
//             return get_params_by_mode(mode)->filter_high.x;
//     }
// }
// uint32_t params_mode_filter_high_set(x6100_mode_t mode, int32_t val) {
//     params_mode_t *mode_params = get_params_by_mode(mode);
//     params_int32_t *param = &mode_params->filter_high;
//     uint32_t lo_offset = LV_ABS(get_lo_offset());
//     switch (mode) {
//         case x6100_mode_cw:
//         case x6100_mode_cwr:
//             val = (val - lo_offset) * 2;
//             val = LV_MIN(val, MAX_CW_BW);
//         default:
//             break;
//     }
//     params_lock();
//     if ((val != param->x) & (val <= MAX_FILTER_FREQ) & (val > mode_params->filter_low.x)) {
//         param->x = val;
//         param->dirty = true;
//         lv_msg_send(MSG_PARAM_CHANGED, NULL);
//     }
//     params_unlock(NULL);
//     return params_mode_filter_high_get(mode);
// }

// uint32_t params_mode_filter_low_get(x6100_mode_t mode) {
//     switch (mode) {
//         case x6100_mode_am:
//         case x6100_mode_nfm:
//             return 0;
//         case x6100_mode_cw:
//         case x6100_mode_cwr:
//             return LV_ABS(get_lo_offset()) - cw_params.filter_high.x / 2;
//         default:
//             return get_params_by_mode(mode)->filter_low.x;
//     }
// }

// uint32_t params_mode_filter_low_set(x6100_mode_t mode, int32_t val) {
//     params_mode_t *mode_params = get_params_by_mode(mode);
//     switch (mode) {
//         case x6100_mode_am:
//         case x6100_mode_nfm:
//             return 0;
//         case x6100_mode_cw:
//         case x6100_mode_cwr:
//             if (val >= 0) {
//                 params_mode_filter_high_set(mode, 2 * LV_ABS(get_lo_offset()) - val);
//             }
//             return params_mode_filter_low_get(mode);
//         default:;
//             params_int32_t *param = &mode_params->filter_low;
//             params_lock();
//             if ((val != param->x) & (val >= 0) & (val < mode_params->filter_high.x)) {
//                 param->x = val;
//                 param->dirty = true;
//                 lv_msg_send(MSG_PARAM_CHANGED, NULL);
//             }
//             params_unlock(NULL);
//             return param->x;
//     }
// }

// // uint32_t params_mode_filter_bw_get(x6100_mode_t mode) {
// //     params_mode_t *params = get_params_by_mode(mode);
// //     return params->filter_high.x - params->filter_low.x;
// // }

// // uint32_t params_mode_filter_bw_set(x6100_mode_t mode, int32_t val) {
// //     int32_t new_low, new_high, change;
// //     params_mode_t *mode_params = get_params_by_mode(mode);
// //     params_int32_t *l_param = &mode_params->filter_low;
// //     params_int32_t *h_param = &mode_params->filter_high;
// //     int32_t cur_bw = h_param->x - l_param->x;
// //     change = (val - cur_bw) / 2;
// //     switch (mode) {
// //         case x6100_mode_lsb:
// //         case x6100_mode_usb:
// //         case x6100_mode_lsb_dig:
// //         case x6100_mode_usb_dig:
// //             new_low = l_param->x - change;
// //             new_high = h_param->x + change;
// //             if (new_low < 0) {
// //                 new_high += -new_low;
// //                 new_low = 0;
// //             }
// //             break;
// //         case x6100_mode_am:
// //         case x6100_mode_nfm:
// //             new_low = 0;
// //             new_high = h_param->x + change * 2;
// //             break;
// //         case x6100_mode_cw:
// //         case x6100_mode_cwr:
// //             new_low = 0;
// //             new_high = LV_MIN(h_param->x + change * 2, MAX_CW_BW);
// //             break;
// //     }
// //     new_high = LV_MIN(new_high, MAX_FILTER_FREQ);
// //     new_high = LV_MAX(new_high, 50);
// //     params_lock();
// //     bool changed = false;
// //     if ((new_low != l_param->x) && (new_low < new_high)) {
// //         l_param->x = new_low;
// //         l_param->dirty = true;
// //         changed = true;
// //     }
// //     if ((new_high != h_param->x) && (new_low < new_high)) {
// //         h_param->x = new_high;
// //         h_param->dirty = true;
// //         changed = true;
// //     }
// //     if (changed) {
// //         lv_msg_send(MSG_PARAM_CHANGED, NULL);
// //     }
// //     params_unlock(NULL);
// //     cur_bw = h_param->x - l_param->x;
// //     return cur_bw;
// // }

// // Current mode functions
// // uint32_t params_current_mode_filter_low_get() {
// //     return params_mode_filter_low_get(radio_current_mode());
// // }

// // uint32_t params_current_mode_filter_low_set(int32_t val) {
// //     return params_mode_filter_low_set(radio_current_mode(), val);
// // }

// // uint32_t params_current_mode_filter_high_get() {
// //     return params_mode_filter_high_get(radio_current_mode());
// // }

// // uint32_t params_current_mode_filter_high_set(int32_t val) {
// //     return params_mode_filter_high_set(radio_current_mode(), val);
// // }

// // uint32_t params_current_mode_filter_bw_get() {
// //     return params_mode_filter_bw_get(radio_current_mode());
// // }

// // uint32_t params_current_mode_filter_bw_set(int32_t val) {
// //     return params_mode_filter_bw_set(radio_current_mode(), val);
// // }

// // void params_current_mode_filter_get(int32_t *low, int32_t *high) {
// //     x6100_mode_t mode = radio_current_mode();
// //     *low = params_mode_filter_low_get(mode);
// //     *high = params_mode_filter_high_get(mode);
// // }

// /* Database operations */

// // void params_modulation_setup(get_lo_offset_t get_lo_offset_fn) {
// //     get_lo_offset = get_lo_offset_fn;
// //     int rc = sqlite3_prepare_v2(db, "INSERT INTO mode_params(mode, name, val) VALUES(?, ?, ?)", -1, &write_mode_stmt, 0);
// //     if (rc != SQLITE_OK) {
// //         LV_LOG_ERROR("Prepare mode write failed: %i", rc);
// //     }
// //     params_mode_load();
// // }


// static void params_mode_load() {
//     sqlite3_stmt    *stmt;
//     int             rc;
//     x6100_mode_t    mode;
//     params_mode_t   *mode_params;

//     char *query;

//     rc = sqlite3_prepare_v2(db, "SELECT mode,name,val FROM mode_params WHERE mode IN (?,?,?,?,?)", -1, &stmt, 0);
//     sqlite3_bind_int(stmt, 1, x6100_mode_lsb);
//     sqlite3_bind_int(stmt, 2, x6100_mode_lsb_dig);
//     sqlite3_bind_int(stmt, 3, x6100_mode_cw);
//     sqlite3_bind_int(stmt, 4, x6100_mode_am);
//     sqlite3_bind_int(stmt, 5, x6100_mode_nfm);

//     if (rc != SQLITE_OK) {
//         LV_LOG_ERROR("Prepare");
//         return;
//     }

//     while (sqlite3_step(stmt) != SQLITE_DONE) {
//         mode = sqlite3_column_int(stmt, 0);
//         mode_params = get_params_by_mode(mode);

//         const char *name = sqlite3_column_text(stmt, 1);

//         if (strcmp(name, "filter_low") == 0) {
//             mode_params->filter_low.x = sqlite3_column_int(stmt, 2);
//         } else if (strcmp(name, "filter_high") == 0) {
//             mode_params->filter_high.x = sqlite3_column_int(stmt, 2);
//         }
//     }
//     sqlite3_finalize(stmt);
//     cw_params.filter_low.x = 0;
//     am_params.filter_low.x = 0;
//     fm_params.filter_low.x = 0;
// }


// static void params_mode_write_int(x6100_mode_t mode, const char *name, int data, bool *dirty) {
//     sqlite3_bind_int(write_mode_stmt, 1, mode);
//     sqlite3_bind_text(write_mode_stmt, 2, name, strlen(name), 0);
//     sqlite3_bind_int(write_mode_stmt, 3, data);
//     sqlite3_step(write_mode_stmt);
//     sqlite3_reset(write_mode_stmt);
//     sqlite3_clear_bindings(write_mode_stmt);

//     *dirty = false;
// }

// void params_mode_save() {
//     if (!sql_query_exec("BEGIN")) {
//         return;
//     }
//     for (size_t i = 0; i < db_modes_n; i++)
//     {
//         x6100_mode_t mode = db_modes[i];
//         params_mode_t *mode_params = get_params_by_mode(mode);
//         if (mode_params->filter_low.dirty)       params_mode_write_int(mode, "filter_low", mode_params->filter_low.x, &mode_params->filter_low.dirty);
//         if (mode_params->filter_high.dirty)      params_mode_write_int(mode, "filter_high", mode_params->filter_high.x, &mode_params->filter_high.dirty);
//     }

//     sql_query_exec("COMMIT");
// }
