/*
 *  SPDX-License-Identifier: LGPL-2.1-or-later
 *
 *  Xiegu X6100 LVGL GUI
 *
 *  Copyright (c) 2022-2023 Belousov Oleg aka R1CBU
 */

#include "gps.h"

#include "lvgl/lvgl.h"
#include "events.h"
#include "dialog_gps.h"

#include <unistd.h>
#include <stdint.h>
#include <stdlib.h>
#include <errno.h>
#include <pthread.h>
#include <math.h>

static struct gps_data_t    gpsdata;
static uint64_t             prev_time = 0;
static gps_status_t         status=GPS_STATUS_WAITING;


static bool connect() {
    if (gps_open("localhost", "2947", &gpsdata) == -1) {
        LV_LOG_ERROR("GPSD open: %s", gps_errstr(errno));
        return false;
    }
    gps_stream(&gpsdata, WATCH_ENABLE | WATCH_JSON, NULL);

    return true;
}

static void data_receive() {
    while (gps_waiting(&gpsdata, 5000000)) {
        usleep(100000);
        if (gps_read(&gpsdata, NULL, 0) != -1) {
            if (MODE_SET != (MODE_SET & gpsdata.set)) {
                // did not even get mode, nothing to see here
                continue;
            }
            status = GPS_STATUS_WORKING;
            if (dialog_gps->run) {
                struct gps_data_t *msg = malloc(sizeof(struct gps_data_t));

                memcpy(msg, &gpsdata, sizeof(*msg));
                event_send(dialog_gps->obj, EVENT_GPS, msg);
            }
        }
    }
}

static void disconnect() {
    gps_stream(&gpsdata, WATCH_DISABLE, NULL);
    gps_close(&gpsdata);

}

static void * gps_thread(void *arg) {
    while (true) {
        status = GPS_STATUS_WAITING;
        if (connect()) {
            data_receive();
            status = GPS_STATUS_RESTARTING;
            disconnect();
        }
        usleep(10000000);
    }
}

void gps_init() {
    pthread_t thread;

    pthread_create(&thread, NULL, gps_thread, NULL);
    pthread_detach(thread);
}


gps_status_t gps_status() {
    return status;
}
