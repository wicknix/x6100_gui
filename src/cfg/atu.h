#pragma once

#include "subjects.h"

typedef struct atu_network_impl_t atu_network_impl_t;

typedef struct {
    subject_t loaded;
    subject_t network;
} atu_network_t;

int cfg_atu_save_network(uint32_t value);
