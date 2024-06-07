#pragma once
#include <unistd.h>
#include <stdbool.h>
#include <pthread.h>
#include <stdint.h>


/*********************
 *  DEFINE BASE TYPES
 *********************/

#define TYPEDEF_PARAM_STRUCT(T) typedef struct {\
    T##_t   x; \
    bool    dirty; \
    char    *name; \
    char    *voice; \
} T##_param_t;


TYPEDEF_PARAM_STRUCT(int16)
TYPEDEF_PARAM_STRUCT(uint16)
TYPEDEF_PARAM_STRUCT(int32)
TYPEDEF_PARAM_STRUCT(uint64)
