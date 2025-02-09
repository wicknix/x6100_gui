/*
 *  SPDX-License-Identifier: LGPL-2.1-or-later
 *
 *  Copyright (c) 2022-2023 Belousov Oleg aka R1CBU
 */

/*********************
 *      INCLUDES
 *********************/

#include "lv_finder.h"

/*********************
 *      DEFINES
 *********************/
#define MY_CLASS &lv_finder_class

#define CURSOR_NO_VALUE -1

/**********************
 *  STATIC PROTOTYPES
 **********************/

static void lv_finder_constructor(const lv_obj_class_t * class_p, lv_obj_t * obj);
static void lv_finder_event(const lv_obj_class_t * class_p, lv_event_t * e);

/**********************
 *  STATIC VARIABLES
 **********************/

const lv_obj_class_t lv_finder_class = {
    .constructor_cb = lv_finder_constructor,
    .base_class = &lv_obj_class,
    .event_cb = lv_finder_event,
    .instance_size = sizeof(lv_finder_t),
};

/**********************
 *   GLOBAL FUNCTIONS
 **********************/

lv_obj_t * lv_finder_create(lv_obj_t * parent) {
    LV_LOG_USER("begin");
    lv_obj_t * obj = lv_obj_class_create_obj(MY_CLASS, parent);
    lv_obj_class_init_obj(obj);

    return obj;
}

/*=====================
 * Setter functions
 *====================*/

void lv_finder_set_range(lv_obj_t * obj, int16_t min, int16_t max) {
    LV_ASSERT_OBJ(obj, MY_CLASS);

    lv_finder_t * finder = (lv_finder_t *)obj;

    finder->range_min = min;
    finder->range_max = max;
}

void lv_finder_set_cursor(lv_obj_t * obj, int16_t value) {
    LV_ASSERT_OBJ(obj, MY_CLASS);

    lv_finder_t * finder = (lv_finder_t *)obj;

    finder->cursor = value;
}

void lv_finder_clear_cursor(lv_obj_t *obj) {
    LV_ASSERT_OBJ(obj, MY_CLASS);

    lv_finder_t * finder = (lv_finder_t *)obj;

    finder->cursor = CURSOR_NO_VALUE;
}

void lv_finder_set_width(lv_obj_t * obj, uint16_t x) {
    LV_ASSERT_OBJ(obj, MY_CLASS);

    lv_finder_t * finder = (lv_finder_t *)obj;

    finder->width = x;
}

void lv_finder_set_value(lv_obj_t * obj, int16_t x) {
    LV_ASSERT_OBJ(obj, MY_CLASS);

    lv_finder_t * finder = (lv_finder_t *)obj;

    finder->value = x;
}

/**********************
 *   STATIC FUNCTIONS
 **********************/

static void lv_finder_constructor(const lv_obj_class_t * class_p, lv_obj_t * obj) {
    LV_UNUSED(class_p);
    LV_TRACE_OBJ_CREATE("begin");

    lv_finder_t * finder = (lv_finder_t *)obj;

    finder->value = 1000;
    finder->width = 50;
    finder->range_min = 50;
    finder->range_max = 3000;
    finder->cursor = CURSOR_NO_VALUE;

    LV_TRACE_OBJ_CREATE("finished");
}

static void lv_finder_event(const lv_obj_class_t * class_p, lv_event_t * e) {
    LV_UNUSED(class_p);

    lv_res_t res = lv_obj_event_base(MY_CLASS, e);

    if (res != LV_RES_OK) return;

    lv_event_code_t code = lv_event_get_code(e);
    lv_obj_t * obj = lv_event_get_target(e);

    if (code == LV_EVENT_DRAW_MAIN_END) {
        lv_finder_t     *finder = (lv_finder_t *) obj;
        lv_draw_ctx_t   *draw_ctx = lv_event_get_draw_ctx(e);
        lv_area_t       area;

        lv_coord_t      x1 = obj->coords.x1;
        lv_coord_t      y1 = obj->coords.y1;
        lv_coord_t      w = lv_obj_get_width(obj);
        lv_coord_t      h = lv_obj_get_height(obj);
        uint16_t        border = lv_obj_get_style_border_width(obj, LV_PART_INDICATOR);

        int32_t         size_hz = finder->range_max - finder->range_min;
        int32_t         f = finder->value - finder->range_min;
        int64_t         f1 = w * f / size_hz;
        int64_t         f2 = w * (f + finder->width) / size_hz;

        area.x1 = x1 + f1;
        area.y1 = y1 + border;
        area.x2 = x1 + f2;
        area.y2 = area.y1 + h - border * 2;

        /* Rectangle */

        lv_draw_rect_dsc_t  draw_dsc;

        lv_draw_rect_dsc_init(&draw_dsc);
        lv_obj_init_draw_rect_dsc(obj, LV_PART_INDICATOR, &draw_dsc);
        draw_dsc.border_color = lv_color_hex(0xFF5050);
        draw_dsc.bg_color = draw_dsc.border_color;
        draw_dsc.bg_opa = LV_OPA_30;
        draw_dsc.border_opa = LV_OPA_70;

        lv_draw_rect(draw_ctx, &draw_dsc, &area);

        /* Cursor */

        if ((finder->cursor != CURSOR_NO_VALUE) && (finder->cursor != finder->value)) {

            draw_dsc.border_color = lv_color_hex(0x50FF50);
            draw_dsc.bg_color = draw_dsc.border_color;

            lv_area_t       cursor_area;

            cursor_area.x1 = x1 + w * (finder->cursor - finder->range_min) / size_hz;
            cursor_area.y1 = area.y1;
            cursor_area.x2 = cursor_area.x1 + w * finder->width / size_hz;
            cursor_area.y2 = area.y2;

            lv_draw_rect(draw_ctx, &draw_dsc, &cursor_area);
        }

    }
}
