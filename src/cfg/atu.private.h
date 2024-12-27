#pragma once

#include "atu.h"
#include "stdbool.h"

#include <sqlite3.h>

extern atu_network_t atu_network;

void cfg_atu_init(sqlite3 *database);
