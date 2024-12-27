#pragma once

#include "transverter.h"

#include "cfg.h"

#include <sqlite3.h>
#define TRANSVERTER_UNKNOWN_ID -1

typedef struct {
    int32_t from;
    int32_t to;
    int32_t shift;
    int32_t id;
} transverter_info_t;

void cfg_transverter_init(sqlite3 *database);
