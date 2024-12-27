#pragma once

#include "subjects.h"

typedef struct cfg_item_t {
    const char     *db_name;
    int             pk;
    subject_t       val;
    struct dirty_t *dirty;
    int (*load)(struct cfg_item_t *item);
    int (*save)(struct cfg_item_t *item);
} cfg_item_t;
