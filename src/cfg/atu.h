#pragma once

#include "subjects.h"

typedef struct atu_network_impl_t atu_network_impl_t;

typedef struct {
    Subject *loaded;
    Subject *network;
} atu_network_t;

int cfg_atu_save_network(uint32_t value);
