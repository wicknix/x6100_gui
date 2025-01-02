#pragma once

#include <stdint.h>
#include <stdbool.h>

typedef enum {
    CFG_DIG_TYPE_FT8,
    CFG_DIG_TYPE_FT4,
} cfg_digital_type_t;

bool cfg_digital_load(int8_t dir, cfg_digital_type_t type);

char * cfg_digital_label_get();
