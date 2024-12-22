#pragma once

#include "cfg.h"

#include <pthread.h>
#include <sqlite3.h>

struct dirty_t {
    bool            val;
    pthread_mutex_t mux;
};

typedef struct {
    cfg_item_t  filter_low;
    cfg_item_t  filter_high;
    cfg_item_t  freq_step;
    cfg_item_t  zoom;
} cfg_mode_t;
