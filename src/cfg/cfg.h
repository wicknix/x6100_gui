#pragma once

#include "common.h"
#include "atu.h"
#include "band.h"

#include <pthread.h>
#include <sqlite3.h>


/* configuration structs. Should contain same types (for correct initialization) */
typedef struct {
    cfg_item_t key_tone;
    cfg_item_t vol;
    cfg_item_t band_id;
    cfg_item_t ant_id;
    cfg_item_t atu_enabled;

    /* key */
    cfg_item_t key_speed;
    cfg_item_t key_mode;
    cfg_item_t iambic_mode;
    cfg_item_t key_vol;
    cfg_item_t key_train;
    cfg_item_t qsk_time;
    cfg_item_t key_ratio;

    /* CW decoder */
    cfg_item_t cw_decoder;
    cfg_item_t cw_tune;
    cfg_item_t cw_decoder_snr;
    cfg_item_t cw_decoder_snr_gist;
    cfg_item_t cw_decoder_peak_beta;
    cfg_item_t cw_decoder_noise_beta;

    cfg_item_t agc_hang;
    cfg_item_t agc_knee;
    cfg_item_t agc_slope;

    // DSP
    cfg_item_t dnf;
    cfg_item_t dnf_center;
    cfg_item_t dnf_width;

    cfg_item_t nb;
    cfg_item_t nb_level;
    cfg_item_t nb_width;

    cfg_item_t nr;
    cfg_item_t nr_level;
} cfg_t;
extern cfg_t cfg;

/* Current band/mode params */

typedef struct {
    subject_t      fg_freq;
    subject_t      bg_freq;
    subject_t      lo_offset;
    subject_t      freq_shift;
    subject_t      mode;
    subject_t      agc;
    subject_t      att;
    subject_t      pre;
    struct {
        subject_t      low;
        subject_t      high;
        subject_t      bw;
        struct {
            subject_t      from;
            subject_t      to;
        } real;
    } filter;
    subject_t      freq_step;
    subject_t      zoom;
    atu_network_t *atu;
    cfg_band_t    *band;
} cfg_cur_t;

extern cfg_cur_t cfg_cur;

int cfg_init(sqlite3 *db);
