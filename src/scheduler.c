/*
 *  SPDX-License-Identifier: LGPL-2.1-or-later
 *
 *  Xiegu X6100 LVGL GUI
 *
 *  Copyright (c) 2024 Georgy Dyuldin aka R2RFE
 */

#include "scheduler.h"

#include "lvgl/lvgl.h"

#include <pthread.h>
#include <stdlib.h>


#define QUEUE_SIZE  64

typedef struct {
    scheduler_fn_t fn;
    void    *arg;
} item_t;

static item_t queue[QUEUE_SIZE];
static uint8_t queue_read = 0;
static uint8_t queue_write = 0;

static pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;


void scheduler_put(scheduler_fn_t fn, void * arg, size_t arg_size) {
    pthread_mutex_lock(&mutex);
    uint8_t next = (queue_write + 1) % QUEUE_SIZE;

    if (next == queue_read) {
        pthread_mutex_unlock(&mutex);
        LV_LOG_ERROR("Scheduler queue overflow");
        return;
    }

    queue[next].fn = fn;
    if (arg_size) {
        queue[next].arg = malloc(arg_size);
        memcpy(queue[next].arg, arg, arg_size);
    } else {
        queue[next].arg = NULL;
    }
    queue_write = next;
    pthread_mutex_unlock(&mutex);
}

void scheduler_work() {
    while (queue_read != queue_write) {
        pthread_mutex_lock(&mutex);
        queue_read = (queue_read + 1) % QUEUE_SIZE;

        queue[queue_read].fn(queue[queue_read].arg);

        if (queue[queue_read].arg) {
            free(queue[queue_read].arg);
        }
        pthread_mutex_unlock(&mutex);
    }
}
