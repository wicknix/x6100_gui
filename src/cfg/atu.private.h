#pragma once

#include "cfg.h"

#include <sqlite3.h>

typedef int32_t db_mode_t;

void cfg_atu_init(sqlite3 *database);
bool load_atu_params(int32_t ant_id, int32_t freq, uint32_t *network);
int save_atu_params(int32_t ant_id, int32_t freq, uint32_t network);
