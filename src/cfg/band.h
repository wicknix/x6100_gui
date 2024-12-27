#pragma once

#include "common.h"

typedef struct cfg_item_t cfg_item_t;

struct vfo_params {
    cfg_item_t freq;
    cfg_item_t mode;
    cfg_item_t agc;
    cfg_item_t pre;
    cfg_item_t att;
};

typedef struct {
    struct vfo_params vfo_a;
    struct vfo_params vfo_b;
    cfg_item_t        vfo;
    cfg_item_t        split;
    cfg_item_t        rfg;
} cfg_band_t;


void cfg_band_vfo_copy();
void cfg_band_load_next(bool up);
const char * cfg_band_label_get();
