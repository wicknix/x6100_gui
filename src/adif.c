/*
 *  SPDX-License-Identifier: LGPL-2.1-or-later
 *
 *  Xiegu X6100 LVGL GUI
 *
 *  Copyright (c) 2024 Georgy Dyuldin aka R2RFE
 */
#include "adif.h"

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <stdbool.h>
#include <unistd.h>

struct adif_log_s {
    FILE *fd;
};

static void write_header(FILE *fd);

static void write_str(FILE *fd, const char * key, const char * val);
static void write_int(FILE *fd, const char * key, int val);

static void write_date_time(FILE *fd, time_t time);
static void write_freq_band(FILE *fd, float freq_mhz);


adif_log adif_log_init(const char * path) {
    adif_log log = (adif_log) malloc(sizeof(struct adif_log_s));
    bool new_file = false;
    if (access(path, F_OK) != 0) {
        new_file = true;
    }
    FILE *log_fd = fopen(path, "a");
    if (log_fd == NULL) {
        perror("Unable to open log file:");
        free(log);
        return NULL;
    } else {
        log->fd = log_fd;
        if (new_file) {
            write_header(log->fd);
        }
    }
    return log;
}

void adif_log_close(adif_log l) {
    fclose(l->fd);
}

void adif_add_qso(adif_log l, const char *local_call, const char *remote_call,
    time_t time, const char *mode, int rsts, int rstr, float freq_mhz,
    const char *local_grid, const char *remote_grid)
{
    write_str(l->fd, "STATION_CALLSIGN", local_call);
    write_str(l->fd, "OPERATOR", local_call);
    write_str(l->fd, "CALL", remote_call);
    write_date_time(l->fd, time);
    write_str(l->fd, "MODE", mode);
    write_str(l->fd, "SUBMODE", NULL);
    write_str(l->fd, "NAME", NULL);
    write_str(l->fd, "QTH", NULL);
    write_int(l->fd, "RST_SENT", rsts);
    write_str(l->fd, "STX", NULL);
    write_int(l->fd, "RST_RCVD", rstr);
    write_freq_band(l->fd, freq_mhz);
    write_str(l->fd, "GRIDSQUARE", remote_grid);
    write_str(l->fd, "MY_GRIDSQUARE", local_grid);
    fprintf(l->fd, "<EOR>\r\n");
    fflush(l->fd);
}

static void write_header(FILE *fd) {
    fprintf(fd, "<PROGRAMID:5>X6100\r\n");
    fprintf(fd, "<PROGRAMVERSION:5>1.0.0\r\n");
    fprintf(fd, "<ADIF_VER:4>3.14\r\n");
    fprintf(fd, "<EOH>\r\n");
}

static void write_str(FILE *fd, const char * key, const char * val) {
    if (val == NULL) {
        fprintf(fd, "<%s:0>", key);
    } else {
        size_t l = strlen(val);
        fprintf(fd, "<%s:%i>%s", key, l, val);
    }
}

static void write_int(FILE *fd, const char * key, int val) {
    char str_val[8];
    sprintf(str_val, "%i", val);
    write_str(fd, key, str_val);
}

static void write_date_time(FILE *fd, time_t time) {
    struct tm *ts = localtime(&time);
    fprintf(fd, "<QSO_DATE:8>%04i%02i%02i", ts->tm_year + 1900, ts->tm_mon + 1, ts->tm_mday);
    fprintf(fd, "<QSO_DATE_OFF:8>%04i%02i%02i", ts->tm_year + 1900, ts->tm_mon + 1, ts->tm_mday);
    fprintf(fd, "<TIME_ON:4>%02i%02i", ts->tm_hour, ts->tm_min);
    fprintf(fd, "<TIME_OFF:4>%02i%02i", ts->tm_hour, ts->tm_min);
}

static void write_freq_band(FILE *fd, float freq_mhz) {
    char * band;
    uint16_t freq_khz = freq_mhz * 1000;
    switch (freq_khz)
    {
    case 1800 ... 2000:
        band = "160M";
        break;
    case 3500 ... 4000:
        band = "80M";
        break;
    case 7000 ... 7300:
        band = "40M";
        break;
    case 10100 ... 10150:
        band = "30M";
        break;
    case 14000 ... 14350:
        band = "20M";
        break;
    case 18068 ... 18168:
        band = "17M";
        break;
    case 21000 ... 21450:
        band = "15M";
        break;
    case 24890 ... 24990:
        band = "12M";
        break;
    case 28000 ... 29700:
        band = "10M";
        break;
    case 50000 ... 54000:
        band = "6M";
        break;
    default:
        band = NULL;
        break;
    }

    write_str(fd, "BAND", band);

    char str_freq[8];
    sprintf(str_freq, "%0.4f", freq_mhz);
    write_str(fd, "FREQ", str_freq);
}
