/*
 *  SPDX-License-Identifier: LGPL-2.1-or-later
 *
 *  Xiegu X6100 LVGL GUI
 *
 *  Copyright (c) 2022-2023 Belousov Oleg aka R1CBU
 */

#pragma once

#include "cfg/subjects.h"
#include "lvgl/lvgl.h"

#define BUTTONS 5

// typedef enum {
//     PAGE_VOL_1 = 0,
//     PAGE_VOL_2,
//     PAGE_VOL_3,
//     PAGE_VOL_4,

//     PAGE_MFK_1,
//     PAGE_MFK_2,
//     PAGE_MFK_3,
//     PAGE_MFK_4,

//     PAGE_MEM_1,
//     PAGE_MEM_2,

//     PAGE_KEY_1,
//     PAGE_KEY_2,

//     PAGE_CW_DECODER_1,
//     PAGE_CW_DECODER_2,

//     PAGE_DFN_1,
//     PAGE_DFN_2,
//     PAGE_DFN_3,

//     PAGE_APP_1,
//     PAGE_APP_2,
//     PAGE_APP_3,

//     PAGE_RTTY,
//     PAGE_SETTINGS,
//     PAGE_SWRSCAN,
//     PAGE_FT8,
//     PAGE_GPS,
//     PAGE_MSG_CW_1,
//     PAGE_MSG_CW_2,
//     PAGE_MSG_VOICE_1,
//     PAGE_MSG_VOICE_2,
//     PAGE_RECORDER,
//     PAGE_WIFI,
// } button_page_id_t;

// typedef enum {
//     GROUP_GEN,
//     GROUP_APP,
//     GROUP_KEY,
//     GROUP_MSG_CW,
//     GROUP_MSG_VOICE,
//     GROUP_DFN

// } buttons_group_t;

typedef enum {
    BTN_EMPTY,
    BTN_TEXT,
    BTN_TEXT_FN,
} btn_type_t;

typedef struct button_item_t {
    btn_type_t type;
    union {
        char *label;
        char *(*label_fn)();
    };
    char *voice;
    void (*press)(struct button_item_t *);
    void (*hold)(struct button_item_t *);
    // next/prev page
    struct buttons_page_t *next;
    struct buttons_page_t *prev;
    int32_t                data;
    lv_obj_t              *label_obj;
    subject_t              subj;
    observer_t             observer;
} button_item_t;

typedef struct buttons_page_t {
    button_item_t *items[BUTTONS];
} buttons_page_t;

typedef buttons_page_t *buttons_group_t[];

extern buttons_page_t buttons_page_vol_1;

extern buttons_page_t buttons_page_msg_cw_1;
extern buttons_page_t buttons_page_msg_cw_2;

extern buttons_page_t buttons_page_rtty;

extern buttons_group_t buttons_group_gen;
extern buttons_group_t buttons_group_app;
extern buttons_group_t buttons_group_key;
extern buttons_group_t buttons_group_dfn;
extern buttons_group_t buttons_group_dfl;
extern buttons_group_t buttons_group_vm;

// TODO: move to applications
extern buttons_group_t buttons_group_msg_cw;
extern buttons_group_t buttons_group_msg_voice;

void            buttons_init(lv_obj_t *parent);
void            buttons_load(uint8_t n, button_item_t *item);
void            buttons_load_page(buttons_page_t *page);
void            buttons_unload_page();
void            button_next_page_cb(button_item_t *item);
void            button_prev_page_cb(button_item_t *item);
void            buttons_press(uint8_t n, bool hold);
void            buttons_load_page_group(buttons_group_t group);
buttons_page_t *buttons_get_cur_page();
