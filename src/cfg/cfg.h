#pragma once

#include "common.h"
#include "atu.h"
#include "band.h"

#include <pthread.h>
#include <sqlite3.h>

typedef enum {
    VOL_VOL = 0,
    VOL_SQL,
    VOL_RFG,
    VOL_FILTER_LOW,
    VOL_FILTER_HIGH,
    VOL_PWR,
    VOL_HMIC,
    VOL_MIC,
    VOL_IMIC,
    VOL_MONI,
    VOL_SPMODE,
    VOL_FILTER_BW = 15,
} cfg_vol_mode_t;


typedef enum {
    MFK_SPECTRUM_FACTOR = 2,

    MFK_KEY_SPEED = 9,
    MFK_KEY_MODE,
    MFK_IAMBIC_MODE,
    MFK_KEY_TONE,
    MFK_KEY_VOL,
    MFK_KEY_TRAIN,
    MFK_QSK_TIME,
    MFK_KEY_RATIO,

    MFK_DNF,
    MFK_DNF_CENTER,
    MFK_DNF_WIDTH,
    MFK_DNF_AUTO,
    MFK_NB,
    MFK_NB_LEVEL,
    MFK_NB_WIDTH,
    MFK_NR,
    MFK_NR_LEVEL,

    MFK_AGC_HANG,
    MFK_AGC_KNEE,
    MFK_AGC_SLOPE,
    MFK_COMP,

    MFK_CW_DECODER,
    MFK_CW_TUNE,
    MFK_CW_DECODER_SNR,
    MFK_CW_DECODER_PEAK_BETA,
    MFK_CW_DECODER_NOISE_BETA,

    MFK_ANT,
    MFK_RIT,
    MFK_XIT,

    /* APPs */

    MFK_RTTY_RATE,
    MFK_RTTY_SHIFT,
    MFK_RTTY_CENTER,
    MFK_RTTY_REVERSE,
} cfg_mfk_mode_t;

extern cfg_vol_mode_t cfg_encoder_vol_modes[12];

extern cfg_mfk_mode_t cfg_encoder_mfk_modes[30];


/* configuration structs. Should contain same types (for correct initialization) */
typedef struct {
    cfg_item_t vol_modes;
    cfg_item_t mfk_modes;

    cfg_item_t vol;
    cfg_item_t sql;
    cfg_item_t pwr;
    cfg_item_t output_gain;

    cfg_item_t key_tone;
    cfg_item_t band_id;
    cfg_item_t ant_id;
    cfg_item_t atu_enabled;
    cfg_item_t comp;
    cfg_item_t comp_threshold_offset;
    cfg_item_t comp_makeup_offset;

    cfg_item_t rit;
    cfg_item_t xit;

    cfg_item_t tx_i_offset;
    cfg_item_t tx_q_offset;

    /* UI */
    cfg_item_t auto_level_enabled;
    cfg_item_t auto_level_offset;

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
    cfg_item_t dnf_auto;

    cfg_item_t nb;
    cfg_item_t nb_level;
    cfg_item_t nb_width;

    cfg_item_t nr;
    cfg_item_t nr_level;

    // SWR scan
    cfg_item_t swrscan_linear;
    cfg_item_t swrscan_span;

    // FT8
    cfg_item_t ft8_show_all;
    cfg_item_t ft8_protocol;
    cfg_item_t ft8_auto;
    cfg_item_t ft8_hold_freq;
    cfg_item_t ft8_max_repeats;
    cfg_item_t ft8_omit_cq_qth;
} cfg_t;
extern cfg_t cfg;

/* Current band/mode params */

typedef struct {
    Subject *fg_freq;
    Subject *bg_freq;
    Subject *lo_offset;
    Subject *freq_shift;
    Subject *mode;
    Subject *agc;
    Subject *att;
    Subject *pre;
    struct {
        Subject *low;
        Subject *high;
        Subject *bw;
        struct {
            Subject *from;
            Subject *to;
        } real;
    } filter;
    Subject       *freq_step;
    Subject       *zoom;
    atu_network_t *atu;
    cfg_band_t    *band;
} cfg_cur_t;

extern cfg_cur_t cfg_cur;

int cfg_init(sqlite3 *db);
