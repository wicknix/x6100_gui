/*
 *  SPDX-License-Identifier: LGPL-2.1-or-later
 *
 *  Xiegu X6100 LVGL GUI
 *
 *  Copyright (c) 2022-2023 Belousov Oleg aka R1CBU
 */

#include <stdlib.h>

#include "waterfall.h"
#include "styles.h"
#include "radio.h"
#include "events.h"
#include "params/params.h"
#include "bands.h"
#include "band_info.h"
#include "meter.h"
#include "backlight.h"
#include "dsp.h"
#include "util.h"

#define PX_BYTES    4
#define DEFAULT_MIN S4
#define DEFAULT_MAX S9_20

static lv_obj_t         *obj;
static lv_obj_t         *img;

static lv_style_t       middle_line_style;
static lv_obj_t         *middle_line;
static lv_point_t       middle_line_points[] = { {0, 0}, {0, 0} };

static lv_coord_t       width;
static lv_coord_t       height;
static int32_t          width_hz = 100000;

static float            grid_min = DEFAULT_MIN;
static float            grid_max = DEFAULT_MAX;

static lv_img_dsc_t     *frame;
static lv_color_t       palette[256];
static uint8_t          delay = 0;

static uint32_t         *x_offsets;
static uint16_t         last_row_id;
static uint8_t          *waterfall_cache;

static uint64_t         radio_center_freq = 0;
static uint64_t         wf_center_freq = 0;

static uint8_t          refresh_period = 1;
static uint8_t          refresh_counter = 0;

static void refresh_waterfall();
static void draw_middle_line();
static void redraw_cb(lv_event_t * e);


lv_obj_t * waterfall_init(lv_obj_t * parent, uint64_t cur_freq) {
    radio_center_freq = cur_freq;
    wf_center_freq = cur_freq;

    obj = lv_obj_create(parent);

    lv_obj_add_style(obj, &waterfall_style, 0);
    lv_obj_clear_flag(obj, LV_OBJ_FLAG_SCROLLABLE);

    // Middle line style
    lv_style_init(&middle_line_style);
    lv_style_set_line_width(&middle_line_style, 1);
    lv_style_set_line_color(&middle_line_style, lv_color_hex(0xAAAAAA));
    lv_style_set_line_opa(&middle_line_style, LV_OPA_60);

    return obj;
}

static void scroll_down() {
    last_row_id = (last_row_id + 1) % height;
}

void waterfall_data(float *data_buf, uint16_t size, bool tx) {
    if (delay)
    {
        delay--;
        return;
    }
    scroll_down();

    float min, max;
    if (tx) {
        min = DEFAULT_MIN;
        max = DEFAULT_MAX;
    } else {
        min = grid_min;
        max = grid_max;
    }

    x_offsets[last_row_id] = (radio_center_freq + params_lo_offset_get()) * width  / width_hz;

    for (int x = 0; x < width; x++) {
        uint16_t    index = x * size / width;
        float       v = (data_buf[index] - min) / (max - min);

        if (v < 0.0f) {
            v = 0.0f;
        } else if (v > 1.0f) {
            v = 1.0f;
        }

        uint8_t id = v * 254 + 1;
        memcpy(&waterfall_cache[(last_row_id * width + width - 1 - x) * PX_BYTES], &palette[id], PX_BYTES);
    }
    refresh_waterfall();
}

static void do_scroll_cb(lv_event_t * event) {
    if (wf_center_freq == radio_center_freq) {
        return;
    }
    if (params.waterfall_smooth_scroll.x) {
        wf_center_freq += ((int64_t)radio_center_freq - (int64_t)wf_center_freq) / 10 + 1;
    } else {
        wf_center_freq = radio_center_freq;
    }
    refresh_waterfall();
}

void waterfall_set_height(lv_coord_t h) {
    lv_obj_set_height(obj, h);
    lv_obj_update_layout(obj);

    /* For more accurate horizontal scroll, it should be a "multiple of 500Hz" */
    /* 800 * 500Hz / 100000Hz = 4.0px */

    width = 800;
    height = lv_obj_get_height(obj);

    frame = lv_img_buf_alloc(width, height, LV_IMG_CF_TRUE_COLOR);

    styles_waterfall_palette(palette, 256);

    img = lv_img_create(obj);
    lv_obj_align(img, LV_ALIGN_CENTER, 0, 0);
    lv_img_set_src(img, frame);

    x_offsets = malloc(height * sizeof(*x_offsets));
    uint32_t initial_offset = (radio_center_freq + params_lo_offset_get()) * width  / width_hz;
    for (size_t i = 0; i < height; i++) {
        x_offsets[i] = initial_offset;
    }
    last_row_id = 0;
    waterfall_cache = malloc(frame->data_size);
    memset(waterfall_cache, 0, frame->data_size);

    lv_obj_add_event_cb(img, do_scroll_cb, LV_EVENT_DRAW_POST_END, NULL);
    lv_obj_add_event_cb(img, redraw_cb, LV_EVENT_DRAW_MAIN_BEGIN, NULL);

    waterfall_min_max_reset();
    band_info_init(obj);
    draw_middle_line();
}

static void middle_line_cb(lv_event_t * event) {
    if (params.waterfall_center_line.x && lv_obj_has_flag(middle_line, LV_OBJ_FLAG_HIDDEN)) {
        lv_obj_clear_flag(middle_line, LV_OBJ_FLAG_HIDDEN);
        return;
    }
    if (!params.waterfall_center_line.x && !lv_obj_has_flag(middle_line, LV_OBJ_FLAG_HIDDEN)) {
        lv_obj_add_flag(middle_line, LV_OBJ_FLAG_HIDDEN);
        return;
    }
}

static void draw_middle_line() {
    middle_line_points[1].y = height;
    middle_line = lv_line_create(obj);
    lv_line_set_points(middle_line, middle_line_points, 2);
    lv_obj_add_style(middle_line, &middle_line_style, 0);
    lv_obj_center(middle_line);
    lv_obj_add_event_cb(obj, middle_line_cb, LV_EVENT_DRAW_POST_END, NULL);
}

void waterfall_min_max_reset() {
    if (params.waterfall_auto_min.x) {
        grid_min = DEFAULT_MIN;
    } else {
        grid_min = params_band_grid_min_get();
    }
    if (params.waterfall_auto_max.x) {
        grid_max = DEFAULT_MAX;
    } else {
        grid_max = params_band_grid_max_get();
    }
}

void waterfall_set_max(float db) {
    if (!params.waterfall_auto_max.x) {
        grid_max = db;
    }
}

void waterfall_set_min(float db) {
    if (!params.waterfall_auto_min.x) {
        grid_min = db;
    }
}

void waterfall_update_max(float db) {
    if (params.waterfall_auto_max.x) {
        lpf(&grid_max, db + 3.0f, 0.85f, DEFAULT_MAX);
    } else {
        // TODO: set min/max at param change
        grid_max = params_band_grid_max_get();
    }
}

void waterfall_update_min(float db) {
    if (params.waterfall_auto_min.x) {
        lpf(&grid_min, db + 3.0f, 0.95f, DEFAULT_MIN);
    } else {
        grid_min = params_band_grid_min_get();
    }
}

void waterfall_set_freq(uint64_t freq) {
    delay = 2;
    radio_center_freq = freq;
}

void waterfall_refresh_reset() {
    refresh_period = 1;
}

void waterfall_refresh_period_set(uint8_t k) {
    if (k == 0) {
        return;
    }
    refresh_period = k;
}

static void redraw_cb(lv_event_t * e) {
    int32_t x_offset, w=width;
    size_t copy_n, copy_src, copy_dst;
    size_t clean_n, clean_from;

    uint8_t * temp_buf = frame->data;

    uint32_t cur_freq_px = wf_center_freq * width / width_hz;

    for (size_t i = 0; i < height; i++) {
        x_offset = (int32_t)x_offsets[i] - cur_freq_px;
        if (x_offset > 0) {
            copy_n = LV_MAX(w - x_offset, 0);
            copy_src = 0;
            copy_dst = x_offset;

            clean_n = LV_MIN(x_offset, width);
            clean_from = 0;
        } else if (x_offset < 0) {
            copy_n = LV_MAX(w + x_offset, 0);
            copy_src = -x_offset;
            copy_dst = 0;

            clean_n = LV_MIN(-x_offset, width);
            clean_from = LV_MAX(w + x_offset, 0);
        } else {
            copy_n = width;
            copy_src = 0;
            copy_dst = 0;
            clean_n = 0;
            clean_from = 0;
        }
        copy_src += i * width;
        copy_dst += ((height - i + last_row_id) % height) * width;
        clean_from += ((height - i + last_row_id) % height) * width;
        if (clean_n > 0){
            memset(temp_buf + clean_from * PX_BYTES, 0, clean_n * PX_BYTES);
        }
        if (copy_n > 0){
            memcpy(
                temp_buf + copy_dst * PX_BYTES,
                waterfall_cache + copy_src * PX_BYTES,
                copy_n * PX_BYTES
            );
        }
    }
}

static void refresh_waterfall() {
    refresh_counter++;
    if (refresh_counter >= refresh_period) {
        refresh_counter = 0;
        event_send(img, LV_EVENT_REFRESH, NULL);
    }
}
