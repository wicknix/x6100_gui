/*
 *  SPDX-License-Identifier: LGPL-2.1-or-later
 *
 *  Xiegu X6100 LVGL GUI
 *
 *  Copyright (c) 2024 Georgy Dyuldin aka R2RFE
 */

#include "dialog_wifi.h"

#include "buttons.h"
#include "events.h"
#include "keyboard.h"
#include "msg.h"
#include "params/params.h"
#include "pubsub_ids.h"
#include "radio.h"
#include "textarea_window.h"
#include "wifi.h"

#include "lvgl/lvgl.h"
#include <stdio.h>
#include <stdlib.h>

#define SIZE_OF_ARRAY(arr) (sizeof(arr) / sizeof(*arr))

static void construct_cb(lv_obj_t *parent);
static void destruct_cb();
static void key_cb(lv_event_t *e);
static void cell_selected_cb(lv_event_t *e);

// button callbacks
static void wifi_bt_toggle_cb(lv_event_t *e);
static void start_scan_cb(lv_event_t *e);
static void connect_cb(lv_event_t *e);
static void con_update_cb(lv_event_t *e);
static void con_delete_cb(lv_event_t *e);
// button label getters
static char *wifi_on_off_label_getter();
static char *wifi_scan_label_getter();
static char *wifi_connected_label_getter();
static char *wifi_con_update_label_getter();
static char *wifi_con_delete_label_getter();

static void start_refresh_ap_list();
static void stop_refresh_ap_list();
static void update_aps_table_cb(lv_timer_t *);
// static void ap_add_cb(wifi_ap_info_t *);
// static void ap_del_cb(wifi_ap_info_t *);

static int compare_aps(const void *a, const void *b);

static void keyboard_open();
static bool keyboard_cancel_cb();
static bool keyboard_ok_cb();
static void keyboard_close_cb();

static void update_status_cb(lv_timer_t *);
static void wifi_state_changed_cb(void *s, lv_msg_t *m);

static button_item_t buttons[] = {
    {.label_type = LABEL_FN, .label_fn = wifi_on_off_label_getter,     .press = wifi_bt_toggle_cb},
    {.label_type = LABEL_FN, .label_fn = wifi_scan_label_getter,       .press = start_scan_cb    },
    {.label_type = LABEL_FN, .label_fn = wifi_connected_label_getter,  .press = connect_cb       },
    {.label_type = LABEL_FN, .label_fn = wifi_con_update_label_getter, .press = con_update_cb    },
    {.label_type = LABEL_FN, .label_fn = wifi_con_delete_label_getter, .press = con_delete_cb    },
};

static lv_obj_t *button_objs[SIZE_OF_ARRAY(buttons)];

static lv_timer_t *timer_refresh_ap = NULL;
static lv_timer_t *timer_status = NULL;

static lv_obj_t *ap_table;
static lv_obj_t *label_status;
static lv_obj_t *label_ip_addr;
static lv_obj_t *label_gateway;
static lv_obj_t *label_status;

static bool disable_buttons = false;
static bool selected_known = false;

static wifi_ap_info_t cur_ap_info;
static char          *cur_password = NULL;

static dialog_t dialog = {
    .run = false,
    .construct_cb = construct_cb,
    .destruct_cb = destruct_cb,
    .audio_cb = NULL,
    .key_cb = key_cb,
};

dialog_t *dialog_wifi = &dialog;

static void construct_cb(lv_obj_t *parent) {
    dialog.obj = dialog_init(parent);

    for (size_t i = 0; i < SIZE_OF_ARRAY(buttons); i++) {
        button_objs[i] = buttons_load(i, &buttons[i]);
    }

    if (params.wifi_enabled.x) {
        start_refresh_ap_list();
    }

#define DIALOG_WIDTH 775
#define DIALOG_HEIGHT 320

#define PARAMS_WIDTH 340

    // Container
    lv_obj_t *cont = lv_obj_create(dialog.obj);
    lv_obj_remove_style(cont, NULL, LV_STATE_ANY | LV_PART_MAIN);
    lv_obj_set_size(cont, DIALOG_WIDTH, DIALOG_HEIGHT);
    lv_obj_center(cont);
    lv_obj_set_flex_flow(cont, LV_FLEX_FLOW_ROW);

    // Params container
    lv_obj_t *param_cont;
    param_cont = lv_obj_create(cont);
    lv_obj_remove_style(param_cont, NULL, LV_STATE_ANY | LV_PART_MAIN);
    lv_obj_set_size(param_cont, PARAMS_WIDTH, DIALOG_HEIGHT);
    lv_obj_set_flex_flow(param_cont, LV_FLEX_FLOW_COLUMN);

    // Params
    static lv_style_t style_val_label;
    lv_style_init(&style_val_label);
    // lv_style_set_bg_color(&style_val_label, lv_color_hex(0x115588));
    // lv_style_set_bg_opa(&style_val_label, LV_OPA_50);
    // lv_style_set_border_width(&style_val_label, 2);
    // lv_style_set_border_color(&style_val_label, lv_color_black());
    lv_style_set_pad_bottom(&style_val_label, 20);
    lv_style_set_pad_left(&style_val_label, 10);

    lv_obj_t *label;
    label = lv_label_create(param_cont);
    lv_label_set_text(label, "Status:");
    label_status = lv_label_create(param_cont);
    lv_label_set_text(label_status, "Unknown");
    lv_obj_add_style(label_status, &style_val_label, 0);

    label = lv_label_create(param_cont);
    lv_label_set_text(label, "IP address:");
    label_ip_addr = lv_label_create(param_cont);
    lv_label_set_text(label_ip_addr, "N/A");
    lv_obj_add_style(label_ip_addr, &style_val_label, 0);

    label = lv_label_create(param_cont);
    lv_label_set_text(label, "Gateway:");
    label_gateway = lv_label_create(param_cont);
    lv_label_set_text(label_gateway, "N/A");
    lv_obj_add_style(label_gateway, &style_val_label, 0);

    // APs table
    ap_table = lv_table_create(cont);
    lv_table_set_col_cnt(ap_table, 1);
    lv_table_set_col_width(ap_table, 0, DIALOG_WIDTH - PARAMS_WIDTH - 2);
    lv_obj_set_height(ap_table, DIALOG_HEIGHT);
    lv_obj_center(ap_table);
    lv_obj_set_flex_grow(ap_table, 1);

    lv_obj_remove_style(ap_table, NULL, LV_PART_MAIN | LV_PART_ITEMS | LV_STATE_ANY);
    lv_obj_set_style_bg_opa(ap_table, LV_OPA_50, LV_PART_MAIN);
    lv_obj_set_style_bg_color(ap_table, lv_color_black(), LV_PART_MAIN);
    lv_obj_set_style_border_width(ap_table, 1, LV_PART_MAIN);
    lv_obj_set_style_border_color(ap_table, lv_color_white(), LV_PART_MAIN);
    lv_obj_set_style_border_opa(ap_table, 128, LV_PART_MAIN);

    lv_obj_set_style_border_width(ap_table, 0, LV_PART_ITEMS);
    lv_obj_set_style_text_color(ap_table, lv_color_white(), LV_PART_ITEMS);
    lv_obj_set_style_bg_color(ap_table, lv_color_white(), LV_PART_ITEMS | LV_STATE_EDITED);
    lv_obj_set_style_bg_opa(ap_table, LV_OPA_30, LV_PART_ITEMS | LV_STATE_EDITED);
    lv_obj_set_style_pad_top(ap_table, 3, LV_PART_ITEMS);
    lv_obj_set_style_pad_bottom(ap_table, 3, LV_PART_ITEMS);
    lv_obj_set_style_pad_left(ap_table, 5, LV_PART_ITEMS);
    lv_obj_set_style_pad_right(ap_table, 0, LV_PART_ITEMS);

    lv_obj_add_event_cb(ap_table, key_cb, LV_EVENT_KEY, NULL);
    lv_obj_add_event_cb(ap_table, cell_selected_cb, LV_EVENT_VALUE_CHANGED, NULL);
    lv_group_add_obj(keyboard_group, ap_table);
    lv_group_set_editing(keyboard_group, true);

    lv_msg_subscribe(MSG_WIFI_STATE_CHANGED, wifi_state_changed_cb, NULL);

    wifi_state_changed_cb(NULL, NULL);

    // Setup wifi callbacks
    // wifi_set_change_ap_callbacks(ap_add_cb, ap_del_cb);
}

static void destruct_cb() {
    // wifi_clear_change_ap_callbacks();
    keyboard_close_cb(NULL);
    stop_refresh_ap_list();
}

static void key_cb(lv_event_t *e) {
    uint32_t key = *((uint32_t *)lv_event_get_param(e));

    switch (key) {
        // case HKEY_FINP:
        //      lv_group_set_editing(keyboard_group, !lv_group_get_editing((const lv_group_t*) keyboard_group));
        //      break;

    case LV_KEY_ESC:
        dialog_destruct(&dialog);
        break;

    case KEY_VOL_LEFT_EDIT:
    case KEY_VOL_LEFT_SELECT:
        radio_change_vol(-1);
        break;

    case KEY_VOL_RIGHT_EDIT:
    case KEY_VOL_RIGHT_SELECT:
        radio_change_vol(1);
        break;
    }
}

static void cell_selected_cb(lv_event_t *e) {
    lv_obj_t *obj = lv_event_get_target(e);
    uint16_t  col;
    uint16_t  row;
    lv_table_get_selected_cell(obj, &row, &col);
    wifi_ap_info_t *ap_info = (wifi_ap_info_t *)lv_table_get_cell_user_data(ap_table, row, col);
    if (ap_info) {
        if (ap_info->known != selected_known) {
            selected_known = ap_info->known;
            lv_msg_send(MSG_WIFI_STATE_CHANGED, NULL);
        }
    }
}

/* Buttons callbacks */

static void wifi_bt_toggle_cb(lv_event_t *e) {
    if (disable_buttons)
        return;
    if (params.wifi_enabled.x) {
        stop_refresh_ap_list();
        wifi_power_off();
        // clear table
        lv_table_set_cell_value(ap_table, 0, 0, "");
        lv_table_set_row_cnt(ap_table, 1);
        lv_event_send(ap_table, LV_EVENT_VALUE_CHANGED, NULL);
    } else {
        wifi_power_on();
        start_refresh_ap_list();
        msg_set_text_fmt("Turning on");
    }
}

static void start_scan_cb(lv_event_t *e) {
    wifi_status_t status;
    if (disable_buttons)
        return;
    status = wifi_get_status();
    if ((status == WIFI_OFF) || (status == WIFI_STARTING))
        return;
    if (wifi_scanning()) {
        msg_set_text_fmt("Already scanning");
    } else {
        msg_set_text_fmt("Start scan");
        wifi_start_scan();
    }
}

static void connect_cb(lv_event_t *e) {
    uint16_t row, col;

    if (disable_buttons)
        return;

    switch (wifi_get_status()) {
    case WIFI_DISCONNECTED:
        lv_table_get_selected_cell(ap_table, &row, &col);
        if ((row == LV_TABLE_CELL_NONE) || (col == LV_TABLE_CELL_NONE)) {
            LV_LOG_USER("Nothing selected, can't connect");
            return;
        }
        wifi_ap_info_t *ap_info = (wifi_ap_info_t *)lv_table_get_cell_user_data(ap_table, row, col);
        if (ap_info->known) {
            wifi_connect(ap_info->ssid);
        } else if (ap_info->password_validator == NULL) {
            wifi_add_connection(ap_info->ssid, NULL);
        } else {
            cur_ap_info = *ap_info;
            keyboard_open();
        }
        break;
    case WIFI_CONNECTED:
        wifi_disconnect();
        break;
    case WIFI_CONNECTING:
        break;
    }
}

static void con_update_cb(lv_event_t *e) {
    uint16_t    row, col;
    const char *con_id;

    lv_table_get_selected_cell(ap_table, &row, &col);
    if ((row == LV_TABLE_CELL_NONE) || (col == LV_TABLE_CELL_NONE)) {
        LV_LOG_USER("Nothing selected");
        return;
    }
    wifi_ap_info_t *ap_info = (wifi_ap_info_t *)lv_table_get_cell_user_data(ap_table, row, col);
    if (ap_info) {
        if (!ap_info->known) {
            msg_set_text_fmt("Can't update new connection");
            return;
        }
        cur_ap_info = *ap_info;
        keyboard_open();
    }
}

static void con_delete_cb(lv_event_t *e) {
    uint16_t    row, col;
    const char *con_id;

    lv_table_get_selected_cell(ap_table, &row, &col);
    if ((row == LV_TABLE_CELL_NONE) || (col == LV_TABLE_CELL_NONE)) {
        LV_LOG_USER("Nothing selected");
        return;
    }
    wifi_ap_info_t *ap_info = (wifi_ap_info_t *)lv_table_get_cell_user_data(ap_table, row, col);
    if (ap_info) {
        wifi_delete_connection(ap_info->ssid);
        msg_set_text_fmt("Connection deleted");
    }
}

/* Buttons labels getters */

static char *wifi_on_off_label_getter() {
    switch (wifi_get_status()) {
    case WIFI_OFF:
        return "Wifi/BT:\nOff";
    case WIFI_STARTING:
        return "Wifi/BT:\nStarting...";
    default:
        return "Wifi/BT:\nOn";
    }
}

static char *wifi_scan_label_getter() {
    wifi_status_t status;
    status = wifi_get_status();
    if ((status == WIFI_OFF) || (status == WIFI_STARTING))
        return "";
    return wifi_scanning() ? "Scanning..." : "Scan";
}

static char *wifi_connected_label_getter() {
    switch (wifi_get_status()) {
    case WIFI_DISCONNECTED:
        return "Connect";
    case WIFI_CONNECTED:
        return "Disconnect";
    case WIFI_CONNECTING:
        return "Connecting...";
    default:
        return "";
    }
}

static char *wifi_con_update_label_getter() {
    switch (wifi_get_status()) {
    case WIFI_DISCONNECTED:
    case WIFI_CONNECTED:
        return selected_known ? "Update" : "";
    default:
        return "";
    }
}

static char *wifi_con_delete_label_getter() {
    switch (wifi_get_status()) {
    case WIFI_DISCONNECTED:
    case WIFI_CONNECTED:
        return selected_known ? "Delete" : "";
    default:
        return "";
    }
}

static void start_refresh_ap_list() {
    timer_refresh_ap = lv_timer_create(update_aps_table_cb, 1000, NULL);
}

static void stop_refresh_ap_list() {
    if (timer_refresh_ap) {
        lv_timer_del(timer_refresh_ap);
        timer_refresh_ap = NULL;
    }
}

static void update_aps_table_cb(lv_timer_t *t) {
    wifi_ap_arr_t aps_info;
    uint16_t      row;
    bool          first_known = true;

    if (!params.wifi_enabled.x) {
        stop_refresh_ap_list();
    }

    aps_info = wifi_get_available_access_points();
    if (aps_info.count) {
        qsort(aps_info.ap_arr, aps_info.count, sizeof(wifi_ap_info_t), compare_aps);
    }
    row = 0;

    for (uint16_t i = 0; i < aps_info.count; i++) {
        lv_table_set_cell_value_fmt(ap_table, row++, 0, "%s (%zu)%s", aps_info.ap_arr[i].ssid,
                                    aps_info.ap_arr[i].strength, aps_info.ap_arr[i].is_connected ? " (*)" : "");
        wifi_ap_info_t *copy = (wifi_ap_info_t *)malloc(sizeof(wifi_ap_info_t));
        *copy = aps_info.ap_arr[i];
        lv_table_set_cell_user_data(ap_table, row - 1, 0, (void *)copy);
    }

    if (row > 0) {
        lv_table_set_row_cnt(ap_table, row);
    } else {
        lv_table_set_cell_value(ap_table, 0, 0, "");
        lv_table_set_row_cnt(ap_table, 1);
    }
    lv_event_send(ap_table, LV_EVENT_VALUE_CHANGED, NULL);
    wifi_aps_info_delete(aps_info);
}

// static void ap_add_cb(wifi_ap_info_t * ap_info) {
//     uint16_t row;
//     row = lv_table_get_row_cnt(ap_table) - 1;
//     lv_table_set_cell_value_fmt(ap_table, row++, 0, "%s (%zu)%s", ap_info->ssid,
//                                 ap_info->strength, ap_info->is_connected ? " (*)" : "");
//     wifi_ap_info_t *copy = (wifi_ap_info_t *)malloc(sizeof(wifi_ap_info_t));
//     *copy = *ap_info;
//     lv_table_set_cell_user_data(ap_table, row - 1, 0, (void *)copy);
//     lv_table_set_row_cnt(ap_table, row);
// }

// static void ap_del_cb(wifi_ap_info_t * ap_info) {
//     uint16_t rows;
//     bool found = false;

//     rows = lv_table_get_row_cnt(ap_table);
//     for (size_t i = 0; i < rows; i++) {
//         wifi_ap_info_t * row_data = (wifi_ap_info_t *) lv_table_get_cell_user_data(ap_table, i, 0);
//         if (row_data){
//             if (!found && (strcmp(row_data->bssid, ap_info->bssid) == 0)) {
//                 found = true;
//             } else if (found) {
//                 const char * cell_value = lv_table_get_cell_value(ap_table, i, 0);
//                 wifi_ap_info_t *copy = (wifi_ap_info_t *)malloc(sizeof(wifi_ap_info_t));
//                 *copy = *ap_info;
//                 lv_table_set_cell_user_data(ap_table, i-1, 0, copy);
//                 lv_table_set_cell_value(ap_table, i - i, 0, cell_value);
//             }
//         }
//     }
//     lv_table_set_row_cnt(ap_table, rows-1);
// }

static int compare_aps(const void *a, const void *b) {
    int score_a = ((wifi_ap_info_t *)a)->is_connected * 2 + ((wifi_ap_info_t *)a)->known;
    int score_b = ((wifi_ap_info_t *)b)->is_connected * 2 + ((wifi_ap_info_t *)b)->known;
    if (score_a == score_b)
        return 0;
    else if (score_a < score_b)
        return 1;
    else
        return -1;
}

static void keyboard_open() {
    lv_group_remove_obj(ap_table);
    textarea_window_open(keyboard_ok_cb, keyboard_cancel_cb);
    if (cur_password) {
        textarea_window_set(cur_password);
    }
    disable_buttons = true;
}

static void keyboard_close_cb() {
    textarea_window_close();
    lv_group_add_obj(keyboard_group, ap_table);
    lv_group_set_editing(keyboard_group, true);
    disable_buttons = false;
}

static bool keyboard_cancel_cb() {
    msg_set_text_fmt("Password is required");
    keyboard_close_cb();
    return true;
}

static bool keyboard_ok_cb() {
    cur_password = (char *)textarea_window_get();

    if (cur_ap_info.password_validator) {
        if (!cur_ap_info.password_validator(cur_password)) {
            msg_set_text_fmt("Incorrect password");
            return false;
        }
    }
    if (cur_ap_info.known) {
        wifi_update_connection(cur_ap_info.ssid, cur_password);
    } else {
        wifi_add_connection(cur_ap_info.ssid, cur_password);
    }
    cur_password = NULL;
    keyboard_close_cb();
    return true;
}

static void update_status_cb(lv_timer_t *t) {
    char  ip_address[16];
    char  gateway[16];
    char *ip_addr_p = ip_address;
    char *gateway_p = gateway;
    bool  res;
    res = wifi_get_ipaddr(&ip_addr_p, &gateway_p);
    if (res == (wifi_get_status() == WIFI_CONNECTED)) {
        if (res) {
            lv_label_set_text(label_ip_addr, ip_address);
            lv_label_set_text(label_gateway, gateway);
        } else {
            lv_label_set_text(label_ip_addr, "N/A");
            lv_label_set_text(label_gateway, "N/A");
        }
        if (timer_status) {
            lv_timer_del(timer_status);
            timer_status = NULL;
        }
    }
}

static void wifi_state_changed_cb(void *s, lv_msg_t *m) {
    const char *status_text;

    for (size_t i = 0; i < SIZE_OF_ARRAY(buttons); i++) {
        lv_obj_t *label = lv_obj_get_user_data(button_objs[i]);
        if (!label)
            continue;
        label_cb_fn label_getter = lv_obj_get_user_data(label);
        if (!label_getter)
            continue;
        lv_label_set_text(label, label_getter());
    }

    switch (wifi_get_status()) {
    case WIFI_CONNECTED:
        status_text = "Connected";
        break;
    case WIFI_CONNECTING:
        status_text = "Connecting...";
        break;
    case WIFI_STARTING:
        status_text = "Starting...";
    default:
        status_text = "Disconnected";
    }
    lv_label_set_text(label_status, status_text);
    if (!timer_status) {
        timer_status = lv_timer_create(update_status_cb, 100, NULL);
    }
}
