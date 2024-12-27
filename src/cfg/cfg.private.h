#pragma once

#include "cfg.h"

#include <pthread.h>
#include <sqlite3.h>

enum item_state_t {
    ITEM_STATE_CLEAN,
    ITEM_STATE_CHANGED,
    ITEM_STATE_LOADING,
};

struct dirty_t {
    enum item_state_t val;
    pthread_mutex_t   mux;
};

void init_items(cfg_item_t *cfg_arr, uint32_t count, int (*load)(struct cfg_item_t *item),
                int (*save)(struct cfg_item_t *item));
int  load_items_from_db(cfg_item_t *cfg_arr, uint32_t count);
void save_item_to_db(cfg_item_t *item, bool force);
