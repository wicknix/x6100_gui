/*
 *  SPDX-License-Identifier: LGPL-2.1-or-later
 *
 *  Xiegu X6100 LVGL GUI
 *
 *  Copyright (c) 2022-2023 Belousov Oleg aka R1CBU
 */
#include "buttons.h"

#include "styles.h"
#include "main_screen.h"
#include "mfk.h"
#include "vol.h"
#include "msg.h"
#include "rtty.h"
#include "pannel.h"
#include "params/params.h"
#include "dialog.h"
#include "dialog_settings.h"
#include "dialog_ft8.h"
#include "dialog_freq.h"
#include "dialog_gps.h"
#include "dialog_recorder.h"
#include "voice.h"
#include "pubsub_ids.h"

#include <stdio.h>

#define ARRAY_SIZE(arr) (sizeof(arr) / sizeof(*arr))

typedef struct {
    lv_obj_t        *label;
    button_item_t   *item;
} button_t;

static uint8_t        btn_height = 62;
static button_t       btn[BUTTONS];
static lv_obj_t      *parent_obj = NULL;
static buttons_page_t *cur_page   = NULL;

static void button_app_page_cb(button_item_t *item);
static void button_vol_update_cb(button_item_t *item);
static void button_mfk_update_cb(button_item_t *item);
static void button_mem_load_cb(button_item_t *item);

static void param_changed_cb(void * s, lv_msg_t * m);
static void label_update_cb(subject_t subj, void *user_data);

static void button_vol_hold_cb(button_item_t *item);
static void button_mfk_hold_cb(button_item_t *item);
static void button_mem_save_cb(button_item_t *item);

// Label getters

static char * vol_label_getter();
static char * sql_label_getter();
static char * rfg_label_getter();
static char * tx_power_label_getter();

static char * filter_low_label_getter();
static char * filter_high_label_getter();
static char * filter_bw_label_getter();
static char * speaker_mode_label_getter();

static char * mic_sel_label_getter();
static char * h_mic_gain_label_getter();
static char * i_mic_gain_label_getter();
static char * moni_level_label_getter();

static char * charger_label_getter();
static char * rit_label_getter();
static char * xit_label_getter();

static char * agc_hang_label_getter();
static char * agc_knee_label_getter();
static char * agc_slope_label_getter();

static char * key_speed_label_getter();
static char * key_volume_label_getter();
static char * key_train_label_getter();
static char * key_tone_label_getter();

static char * key_mode_label_getter();
static char * iambic_mode_label_getter();
static char * qsk_time_label_getter();
static char * key_ratio_label_getter();

static char * cw_decoder_label_getter();
static char * cw_tuner_label_getter();
static char * cw_snr_label_getter();

static char * cw_peak_beta_label_getter();
static char * cw_noise_beta_label_getter();

static char * dnf_label_getter();
static char * dnf_center_label_getter();
static char * dnf_width_label_getter();

static char * nb_label_getter();
static char * nb_level_label_getter();
static char * nb_width_label_getter();

static char * nr_label_getter();
static char * nr_level_label_getter();

static void button_action_cb(button_item_t *item);

/* VOL page 1 */

static button_item_t btn_vol = {
    .type     = BTN_TEXT_FN,
    .label_fn = vol_label_getter,
    .press    = button_vol_update_cb,
    .data     = VOL_VOL,
};
static button_item_t btn_sql = {
    .type     = BTN_TEXT_FN,
    .label_fn = sql_label_getter,
    .press    = button_vol_update_cb,
    .hold     = button_vol_hold_cb,
    .data     = VOL_SQL,
};
static button_item_t btn_rfg = {
    .type     = BTN_TEXT_FN,
    .label_fn = rfg_label_getter,
    .press    = button_vol_update_cb,
    .hold     = button_vol_hold_cb,
    .data     = VOL_RFG,
};
static button_item_t btn_tx_pwr = {
    .type     = BTN_TEXT_FN,
    .label_fn = tx_power_label_getter,
    .press    = button_vol_update_cb,
    .hold     = button_vol_hold_cb,
    .data     = VOL_PWR,
};

/* VOL page 2 */

static button_item_t btn_flt_low = {
    .type     = BTN_TEXT_FN,
    .label_fn = filter_low_label_getter,
    .press    = button_vol_update_cb,
    .hold     = button_vol_hold_cb,
    .data     = VOL_FILTER_LOW,
};
static button_item_t btn_flt_high = {
    .type     = BTN_TEXT_FN,
    .label_fn = filter_high_label_getter,
    .press    = button_vol_update_cb,
    .hold     = button_vol_hold_cb,
    .data     = VOL_FILTER_HIGH,
};
static button_item_t btn_flt_bw = {
    .type     = BTN_TEXT_FN,
    .label_fn = filter_bw_label_getter,
    .press    = button_vol_update_cb,
    .hold     = button_vol_hold_cb,
    .data     = VOL_FILTER_BW,
};
static button_item_t btn_sp_mode = {
    .type     = BTN_TEXT_FN,
    .label_fn = speaker_mode_label_getter,
    .press    = button_vol_update_cb,
    .hold     = button_vol_hold_cb,
    .data     = VOL_SPMODE,
};

/* VOL page 3 */

static button_item_t btn_mic_sel = {
    .type     = BTN_TEXT_FN,
    .label_fn = mic_sel_label_getter,
    .press    = button_vol_update_cb,
    .hold     = button_vol_hold_cb,
    .data     = VOL_MIC,
};
static button_item_t btn_hmic_gain = {
    .type     = BTN_TEXT_FN,
    .label_fn = h_mic_gain_label_getter,
    .press    = button_vol_update_cb,
    .hold     = button_vol_hold_cb,
    .data     = VOL_HMIC,
};
static button_item_t btn_imic_hain = {
    .type     = BTN_TEXT_FN,
    .label_fn = i_mic_gain_label_getter,
    .press    = button_vol_update_cb,
    .hold     = button_vol_hold_cb,
    .data     = VOL_IMIC,
};
static button_item_t btn_moni_lvl = {
    .type     = BTN_TEXT_FN,
    .label_fn = moni_level_label_getter,
    .press    = button_vol_update_cb,
    .hold     = button_vol_hold_cb,
    .data     = VOL_MONI,
};


/* VOL page 4 */

static button_item_t btn_voice = {
    .type  = BTN_TEXT,
    .label = "Voice",
    .press = button_vol_update_cb,
    .hold  = button_vol_hold_cb,
    .data  = VOL_VOICE_LANG,
};
static button_item_t btn_voice_rate = {
    .type  = BTN_TEXT,
    .label = "Voice\nRate",
    .press = button_vol_update_cb,
    .hold  = button_vol_hold_cb,
    .data  = VOL_VOICE_RATE,
};
static button_item_t btn_voice_pitch = {
    .type  = BTN_TEXT,
    .label = "Voice\nPitch",
    .press = button_vol_update_cb,
    .hold  = button_vol_hold_cb,
    .data  = VOL_VOICE_PITCH,
};
static button_item_t btn_voice_vol = {
    .type  = BTN_TEXT,
    .label = "Voice\nVolume",
    .press = button_vol_update_cb,
    .hold  = button_vol_hold_cb,
    .data  = VOL_VOICE_VOLUME,
};


/* MFK page 1 */

static button_item_t btn_spectrum_min_level = {
    .type  = BTN_TEXT,
    .label = "Min\nLevel",
    .press = button_mfk_update_cb,
    .hold  = button_mfk_hold_cb,
    .data  = MFK_MIN_LEVEL,
};
static button_item_t btn_spectrum_max_level = {
    .type  = BTN_TEXT,
    .label = "Max\nLevel",
    .press = button_mfk_update_cb,
    .hold  = button_mfk_hold_cb,
    .data  = MFK_MAX_LEVEL,
};
static button_item_t btn_zoom = {
    .type  = BTN_TEXT,
    .label = "Spectrum\nZoom",
    .press = button_mfk_update_cb,
    .hold  = button_mfk_hold_cb,
    .data  = MFK_SPECTRUM_FACTOR,
};
static button_item_t btn_spectrum_beta = {
    .type  = BTN_TEXT,
    .label = "Spectrum\nBeta",
    .press = button_mfk_update_cb,
    .hold  = button_mfk_hold_cb,
    .data  = MFK_SPECTRUM_BETA,
};


/* MFK page 2 */

static button_item_t btn_spectrum_fill = {
    .type  = BTN_TEXT,
    .label = "Spectrum\nFill",
    .press = button_mfk_update_cb,
    .hold  = button_mfk_hold_cb,
    .data  = MFK_SPECTRUM_FILL,
};
static button_item_t btn_spectrum_peak = {
    .type  = BTN_TEXT,
    .label = "Spectrum\nPeak",
    .press = button_mfk_update_cb,
    .hold  = button_mfk_hold_cb,
    .data  = MFK_SPECTRUM_PEAK,
};
static button_item_t btn_spectrum_hold = {
    .type  = BTN_TEXT,
    .label = "Peaks\nHold",
    .press = button_mfk_update_cb,
    .hold  = button_mfk_hold_cb,
    .data  = MFK_PEAK_HOLD,
};
static button_item_t btn_spectrum_speed = {
    .type  = BTN_TEXT,
    .label = "Peaks\nSpeed",
    .press = button_mfk_update_cb,
    .hold  = button_mfk_hold_cb,
    .data  = MFK_PEAK_SPEED,
};


/* MFK page 3 */
static button_item_t btn_charger = {
    .type     = BTN_TEXT_FN,
    .label_fn = charger_label_getter,
    .press    = button_mfk_update_cb,
    .hold     = button_mfk_hold_cb,
    .data     = MFK_CHARGER,
};
static button_item_t btn_ant = {
    .type  = BTN_TEXT,
    .label = "Antenna",
    .press = button_mfk_update_cb,
    .hold  = button_mfk_hold_cb,
    .data  = MFK_ANT,
};
static button_item_t btn_rit = {
    .type     = BTN_TEXT_FN,
    .label_fn = rit_label_getter,
    .press    = button_mfk_update_cb,
    .hold     = button_mfk_hold_cb,
    .data     = MFK_RIT,
};
static button_item_t btn_xit = {
    .type     = BTN_TEXT_FN,
    .label_fn = xit_label_getter,
    .press    = button_mfk_update_cb,
    .hold     = button_mfk_hold_cb,
    .data     = MFK_XIT,
};


/* MFK page 4 */

static button_item_t btn_agc_hang = {
    .type     = BTN_TEXT_FN,
    .label_fn = agc_hang_label_getter,
    .press    = button_mfk_update_cb,
    .hold     = button_mfk_hold_cb,
    .data     = MFK_AGC_HANG,
};
static button_item_t btn_agc_knee = {
    .type     = BTN_TEXT_FN,
    .label_fn = agc_knee_label_getter,
    .press    = button_mfk_update_cb,
    .hold     = button_mfk_hold_cb,
    .data     = MFK_AGC_KNEE,
};
static button_item_t btn_agc_slope = {
    .type     = BTN_TEXT_FN,
    .label_fn = agc_slope_label_getter,
    .press    = button_mfk_update_cb,
    .hold     = button_mfk_hold_cb,
    .data     = MFK_AGC_SLOPE,
};

/* MEM page 1 */

static button_item_t btn_mem_1 = {
    .type  = BTN_TEXT,
    .label = "Set 1",
    .press = button_mem_load_cb,
    .hold  = button_mem_save_cb,
    .data  = 1,
};
static button_item_t btn_mem_2 = {
    .type  = BTN_TEXT,
    .label = "Set 2",
    .press = button_mem_load_cb,
    .hold  = button_mem_save_cb,
    .data  = 2,
};
static button_item_t btn_mem_3 = {
    .type  = BTN_TEXT,
    .label = "Set 3",
    .press = button_mem_load_cb,
    .hold  = button_mem_save_cb,
    .data  = 3,
};
static button_item_t btn_mem_4 = {
    .type  = BTN_TEXT,
    .label = "Set 4",
    .press = button_mem_load_cb,
    .hold  = button_mem_save_cb,
    .data  = 4,
};

/* MEM page 2 */

static button_item_t btn_mem_5 = {
    .type  = BTN_TEXT,
    .label = "Set 5",
    .press = button_mem_load_cb,
    .hold  = button_mem_save_cb,
    .data  = 5,
};
static button_item_t btn_mem_6 = {
    .type  = BTN_TEXT,
    .label = "Set 6",
    .press = button_mem_load_cb,
    .hold  = button_mem_save_cb,
    .data  = 6,
};
static button_item_t btn_mem_7 = {
    .type  = BTN_TEXT,
    .label = "Set 7",
    .press = button_mem_load_cb,
    .hold  = button_mem_save_cb,
    .data  = 7,
};
static button_item_t btn_mem_8 = {
    .type  = BTN_TEXT,
    .label = "Set 8",
    .press = button_mem_load_cb,
    .hold  = button_mem_save_cb,
    .data  = 8,
};

/* CW */

static button_item_t btn_key_speed = {
    .type     = BTN_TEXT_FN,
    .label_fn = key_speed_label_getter,
    .press    = button_mfk_update_cb,
    .hold     = button_mfk_hold_cb,
    .data     = MFK_KEY_SPEED,
};
static button_item_t btn_key_volume = {
    .type     = BTN_TEXT_FN,
    .label_fn = key_volume_label_getter,
    .press    = button_mfk_update_cb,
    .hold     = button_mfk_hold_cb,
    .data     = MFK_KEY_VOL,
};
static button_item_t btn_key_train = {
    .type     = BTN_TEXT_FN,
    .label_fn = key_train_label_getter,
    .press    = button_mfk_update_cb,
    .hold     = button_mfk_hold_cb,
    .data     = MFK_KEY_TRAIN,
};
static button_item_t btn_key_tone = {
    .type     = BTN_TEXT_FN,
    .label_fn = key_tone_label_getter,
    .press    = button_mfk_update_cb,
    .hold     = button_mfk_hold_cb,
    .data     = MFK_KEY_TONE,
};

static button_item_t btn_key_mode = {
    .type     = BTN_TEXT_FN,
    .label_fn = key_mode_label_getter,
    .press    = button_mfk_update_cb,
    .hold     = button_mfk_hold_cb,
    .data     = MFK_KEY_MODE,
};
static button_item_t btn_key_iambic_mode = {
    .type     = BTN_TEXT_FN,
    .label_fn = iambic_mode_label_getter,
    .press    = button_mfk_update_cb,
    .hold     = button_mfk_hold_cb,
    .data     = MFK_IAMBIC_MODE,
};
static button_item_t btn_key_qsk_time = {
    .type     = BTN_TEXT_FN,
    .label_fn = qsk_time_label_getter,
    .press    = button_mfk_update_cb,
    .hold     = button_mfk_hold_cb,
    .data     = MFK_QSK_TIME,
};
static button_item_t btn_key_ratio = {
    .type     = BTN_TEXT_FN,
    .label_fn = key_ratio_label_getter,
    .press    = button_mfk_update_cb,
    .hold     = button_mfk_hold_cb,
    .data     = MFK_KEY_RATIO,
};

static button_item_t btn_cw_decoder = {
    .type     = BTN_TEXT_FN,
    .label_fn = cw_decoder_label_getter,
    .press    = button_mfk_update_cb,
    .hold     = button_mfk_hold_cb,
    .data     = MFK_CW_DECODER,
};
static button_item_t btn_cw_tuner = {
    .type     = BTN_TEXT_FN,
    .label_fn = cw_tuner_label_getter,
    .press    = button_mfk_update_cb,
    .hold     = button_mfk_hold_cb,
    .data     = MFK_CW_TUNE,
};
static button_item_t btn_cw_snr = {
    .type     = BTN_TEXT_FN,
    .label_fn = cw_snr_label_getter,
    .press    = button_mfk_update_cb,
    .hold     = button_mfk_hold_cb,
    .data     = MFK_CW_DECODER_SNR,
};
static button_item_t btn_cw_peak_beta = {
    .type     = BTN_TEXT_FN,
    .label_fn = cw_peak_beta_label_getter,
    .press    = button_mfk_update_cb,
    .hold     = button_mfk_hold_cb,
    .data     = MFK_CW_DECODER_PEAK_BETA,
};
static button_item_t btn_cw_noise_beta = {
    .type     = BTN_TEXT_FN,
    .label_fn = cw_noise_beta_label_getter,
    .press    = button_mfk_update_cb,
    .hold     = button_mfk_hold_cb,
    .data     = MFK_CW_DECODER_NOISE_BETA,
};

/* DSP */

static button_item_t btn_dnf = {
    .type     = BTN_TEXT_FN,
    .label_fn = dnf_label_getter,
    .press    = button_mfk_update_cb,
    .hold     = button_mfk_hold_cb,
    .data     = MFK_DNF,
};
static button_item_t btn_dnf_center = {
    .type     = BTN_TEXT_FN,
    .label_fn = dnf_center_label_getter,
    .press    = button_mfk_update_cb,
    .hold     = button_mfk_hold_cb,
    .data     = MFK_DNF_CENTER,
};
static button_item_t btn_dnf_width = {
    .type     = BTN_TEXT_FN,
    .label_fn = dnf_width_label_getter,
    .press    = button_mfk_update_cb,
    .hold     = button_mfk_hold_cb,
    .data     = MFK_DNF_WIDTH,
};

static button_item_t btn_nb = {
    .type     = BTN_TEXT_FN,
    .label_fn = nb_label_getter,
    .press    = button_mfk_update_cb,
    .hold     = button_mfk_hold_cb,
    .data     = MFK_NB,
};
static button_item_t btn_nb_level = {
    .type     = BTN_TEXT_FN,
    .label_fn = nb_level_label_getter,
    .press    = button_mfk_update_cb,
    .hold     = button_mfk_hold_cb,
    .data     = MFK_NB_LEVEL,
};
static button_item_t btn_nb_width = {
    .type     = BTN_TEXT_FN,
    .label_fn = nb_width_label_getter,
    .press    = button_mfk_update_cb,
    .hold     = button_mfk_hold_cb,
    .data     = MFK_NB_WIDTH,
};

static button_item_t btn_nr = {
    .type     = BTN_TEXT_FN,
    .label_fn = nr_label_getter,
    .press    = button_mfk_update_cb,
    .hold     = button_mfk_hold_cb,
    .data     = MFK_NR,
};
static button_item_t btn_nr_level = {
    .type     = BTN_TEXT_FN,
    .label_fn = nr_level_label_getter,
    .press    = button_mfk_update_cb,
    .hold     = button_mfk_hold_cb,
    .data     = MFK_NR_LEVEL,
};

/* APP */

static button_item_t btn_rtty = {
    .type  = BTN_TEXT,
    .label = "RTTY",
    .press = button_app_page_cb,
    .data  = ACTION_APP_RTTY,
};
static button_item_t btn_ft8 = {
    .type  = BTN_TEXT,
    .label = "FT8",
    .press = button_app_page_cb,
    .data  = ACTION_APP_FT8,
};
static button_item_t btn_swr = {
    .type  = BTN_TEXT,
    .label = "SWR\nScan",
    .press = button_app_page_cb,
    .data  = ACTION_APP_SWRSCAN,
};
static button_item_t btn_gps = {
    .type  = BTN_TEXT,
    .label = "GPS",
    .press = button_app_page_cb,
    .data  = ACTION_APP_GPS,
};

static button_item_t btn_rec = {
    .type  = BTN_TEXT,
    .label = "Recorder",
    .press = button_app_page_cb,
    .data  = ACTION_APP_RECORDER,
};
static button_item_t btn_qth = {
    .type  = BTN_TEXT,
    .label = "QTH",
    .press = button_action_cb,
    .data  = ACTION_APP_QTH,
};
static button_item_t btn_callsign = {
    .type  = BTN_TEXT,
    .label = "Callsign",
    .press = button_action_cb,
    .data  = ACTION_APP_CALLSIGN,
};
static button_item_t btn_settings = {
    .type  = BTN_TEXT,
    .label = "Settings",
    .press = button_app_page_cb,
    .data  = ACTION_APP_SETTINGS,
};


static button_item_t btn_app_p3 = {
    .type  = BTN_TEXT,
    .label = "(APP 3:3)",
    .press = button_next_page_cb,
    .hold  = button_prev_page_cb,
    .voice = "Application|page 2",
};
static button_item_t btn_wifi = {
    .type  = BTN_TEXT,
    .label = "WiFi",
    .press = button_app_page_cb,
    .data  = ACTION_APP_WIFI,
};
static buttons_page_t page_app_3 = {
    {
     &btn_app_p3,
     &btn_wifi,
     }
};

/* RTTY */
static button_item_t btn_rtty_p1 = {
    .type  = BTN_TEXT,
    .label = "(RTTY 1:1)",
    .press = NULL,
};
static button_item_t btn_rtty_rate = {
    .type  = BTN_TEXT,
    .label = "Rate",
    .press = button_mfk_update_cb,
    .data  = MFK_RTTY_RATE,
};
static button_item_t btn_rtty_shift = {
    .type  = BTN_TEXT,
    .label = "Shift",
    .press = button_mfk_update_cb,
    .data  = MFK_RTTY_SHIFT,
};
static button_item_t btn_rtty_center = {
    .type  = BTN_TEXT,
    .label = "Center",
    .press = button_mfk_update_cb,
    .data  = MFK_RTTY_CENTER,
};
static button_item_t btn_rtty_reverse = {
    .type  = BTN_TEXT,
    .label = "Reverse",
    .press = button_mfk_update_cb,
    .data  = MFK_RTTY_REVERSE,
};


/* VOL pages */
static button_item_t btn_vol_p1 = {.type  = BTN_TEXT,
                                   .label = "(VOL 1:4)",
                                   .press = button_next_page_cb,
                                   .hold  = button_prev_page_cb,
                                   .voice = "Volume|page 1"};
static button_item_t  btn_vol_p2 = {.type  = BTN_TEXT,
                                    .label = "(VOL 2:4)",
                                    .press = button_next_page_cb,
                                    .hold  = button_prev_page_cb,
                                    .voice = "Volume|page 2"};
static button_item_t  btn_vol_p3 = {.type  = BTN_TEXT,
                                    .label = "(VOL 3:4)",
                                    .press = button_next_page_cb,
                                    .hold  = button_prev_page_cb,
                                    .voice = "Volume|page 3"};
static button_item_t  btn_vol_p4 = {.type  = BTN_TEXT,
                                    .label = "(VOL 4:4)",
                                    .press = button_next_page_cb,
                                    .hold  = button_prev_page_cb,
                                    .voice = "Volume|page 4"};
buttons_page_t buttons_page_vol_1 = {
    {&btn_vol_p1, &btn_vol, &btn_sql, &btn_rfg, &btn_tx_pwr}
};
static buttons_page_t page_vol_2 = {
    {&btn_vol_p2, &btn_mic_sel, &btn_hmic_gain, &btn_imic_hain, &btn_moni_lvl}
};
static buttons_page_t page_vol_3 = {
    {&btn_vol_p3, &btn_sp_mode}
};
static buttons_page_t page_vol_4 = {
    {&btn_vol_p4, &btn_voice, &btn_voice_rate, &btn_voice_pitch, &btn_voice_vol}
};

/* MFK pages */
static button_item_t  btn_mfk_p1 = {.type  = BTN_TEXT,
                                    .label = "(MFK 1:4)",
                                    .press = button_next_page_cb,
                                    .hold  = button_prev_page_cb,
                                    .voice = "MFK|page 1"};
static button_item_t  btn_mfk_p2 = {.type  = BTN_TEXT,
                                    .label = "(MFK 2:4)",
                                    .press = button_next_page_cb,
                                    .hold  = button_prev_page_cb,
                                    .voice = "MFK|page 2"};
static button_item_t  btn_mfk_p3 = {.type  = BTN_TEXT,
                                    .label = "(MFK 3:4)",
                                    .press = button_next_page_cb,
                                    .hold  = button_prev_page_cb,
                                    .voice = "MFK|page 3"};
static button_item_t  btn_mfk_p4 = {.type  = BTN_TEXT,
                                    .label = "(MFK 4:4)",
                                    .press = button_next_page_cb,
                                    .hold  = button_prev_page_cb,
                                    .voice = "MFK|page 4"};
static buttons_page_t page_mfk_1 = {
    {&btn_mfk_p1, &btn_spectrum_min_level, &btn_spectrum_max_level, &btn_zoom, &btn_spectrum_beta}
};
static buttons_page_t page_mfk_2 = {
    {&btn_mfk_p2, &btn_spectrum_fill, &btn_spectrum_peak, &btn_spectrum_hold, &btn_spectrum_speed}
};
static buttons_page_t page_mfk_3 = {
    {&btn_mfk_p3, &btn_charger, &btn_ant, &btn_rit, &btn_xit}
};
static buttons_page_t page_mfk_4 = {
    {&btn_mfk_p4, &btn_agc_hang, &btn_agc_knee, &btn_agc_slope}
};

/* MEM pages */

static button_item_t  btn_mem_p1 = {.type  = BTN_TEXT,
                                    .label = "(MEM 1:2)",
                                    .press = button_next_page_cb,
                                    .hold  = button_prev_page_cb,
                                    .voice = "Memory|page 1"};
static button_item_t  btn_mem_p2 = {.type  = BTN_TEXT,
                                    .label = "(MEM 2:2)",
                                    .press = button_next_page_cb,
                                    .hold  = button_prev_page_cb,
                                    .voice = "Memory|page 2"};
static buttons_page_t page_mem_1 = {
    {&btn_mem_p1, &btn_mem_1, &btn_mem_2, &btn_mem_3, &btn_mem_4}
};
static buttons_page_t page_mem_2 = {
    {&btn_mem_p2, &btn_mem_5, &btn_mem_6, &btn_mem_7, &btn_mem_8}
};

/* KEY pages */
static button_item_t  btn_key_p1 = {.type  = BTN_TEXT,
                                    .label = "(KEY 1:2)",
                                    .press = button_next_page_cb,
                                    .hold  = button_prev_page_cb,
                                    .voice = "Key|page 1"};
static button_item_t  btn_key_p2 = {.type  = BTN_TEXT,
                                    .label = "(KEY 2:2)",
                                    .press = button_next_page_cb,
                                    .hold  = button_prev_page_cb,
                                    .voice = "Key|page 2"};
static button_item_t  btn_cw_p1  = {.type  = BTN_TEXT,
                                    .label = "(CW 1:2)",
                                    .press = button_next_page_cb,
                                    .hold  = button_prev_page_cb,
                                    .voice = "CW|page 1"};
static button_item_t  btn_cw_p2  = {.type  = BTN_TEXT,
                                    .label = "(CW 2:2)",
                                    .press = button_next_page_cb,
                                    .hold  = button_prev_page_cb,
                                    .voice = "CW|page 2"};
static buttons_page_t page_key_1 = {
    {&btn_key_p1, &btn_key_speed, &btn_key_volume, &btn_key_train, &btn_key_tone}
};
static buttons_page_t page_key_2 = {
    {&btn_key_p2, &btn_key_mode, &btn_key_iambic_mode, &btn_key_qsk_time, &btn_key_ratio}
};
static buttons_page_t page_cw_decoder_1 = {
    {&btn_cw_p1, &btn_cw_decoder, &btn_cw_tuner, &btn_cw_snr}
};
static buttons_page_t page_cw_decoder_2 = {
    {&btn_cw_p2, &btn_cw_peak_beta, &btn_cw_noise_beta}
};

/* DFN pages */
static button_item_t btn_dfn_p1 = {.type  = BTN_TEXT,
                                   .label = "(DFN 1:3)",
                                   .press = button_next_page_cb,
                                   .hold  = button_prev_page_cb,
                                   .voice = "DNF page"};
static button_item_t btn_dfn_p2 = {.type  = BTN_TEXT,
                                   .label = "(DFN 2:3)",
                                   .press = button_next_page_cb,
                                   .hold  = button_prev_page_cb,
                                   .voice = "NB page"};
static button_item_t btn_dfn_p3 = {.type  = BTN_TEXT,
                                   .label = "(DFN 3:3)",
                                   .press = button_next_page_cb,
                                   .hold  = button_prev_page_cb,
                                   .voice = "NR page"};
static buttons_page_t page_dfn_1 = {
    {&btn_dfn_p1, &btn_dnf, &btn_dnf_center, &btn_dnf_width}
};
static buttons_page_t page_dfn_2 = {
    {&btn_dfn_p2, &btn_nb, &btn_nb_level, &btn_nb_width}
};
static buttons_page_t page_dfn_3 = {
    {&btn_dfn_p3, &btn_nr, &btn_nr_level}
};

/* DFL pages */
static buttons_page_t page_dfl_1 = {
    {&btn_flt_low, &btn_flt_high, &btn_flt_bw}
};

/* App pages */
static button_item_t  btn_app_p1 = {.type  = BTN_TEXT,
                                    .label = "(APP 1:3)",
                                    .press = button_next_page_cb,
                                    .hold  = button_prev_page_cb,
                                    .voice = "Application|page 1"};
static button_item_t  btn_app_p2 = {.type  = BTN_TEXT,
                                    .label = "(APP 2:3)",
                                    .press = button_next_page_cb,
                                    .hold  = button_prev_page_cb,
                                    .voice = "Application|page 2"};
static buttons_page_t page_app_1 = {
    {&btn_app_p1, &btn_rtty, &btn_ft8, &btn_swr, &btn_gps}
};
static buttons_page_t page_app_2 = {
    {&btn_app_p2, &btn_rec, &btn_qth, &btn_callsign, &btn_settings}
};

/* RTTY */

buttons_page_t buttons_page_rtty = {
    {&btn_rtty_p1, &btn_rtty_rate, &btn_rtty_shift, &btn_rtty_center, &btn_rtty_reverse}
};

buttons_group_t buttons_group_gen = {
    &buttons_page_vol_1,
    &page_vol_2,
    &page_vol_3,
    &page_vol_4,
    &page_mfk_1,
    &page_mfk_2,
    &page_mfk_3,
    &page_mfk_4,
};

buttons_group_t buttons_group_app = {
    &page_app_1,
    &page_app_2,
    &page_app_3,
};

buttons_group_t buttons_group_key = {
    &page_key_1,
    &page_key_2,
    &page_cw_decoder_1,
    &page_cw_decoder_2,
};

buttons_group_t buttons_group_dfn = {
    &page_dfn_1,
    &page_dfn_2,
    &page_dfn_3,
};

buttons_group_t buttons_group_dfl = {
    &page_dfl_1,
};

buttons_group_t buttons_group_vm = {
    &page_mem_1,
    &page_mem_2,
};

static struct {
    buttons_page_t **group;
    size_t           size;
} groups[] = {
    {buttons_group_gen, ARRAY_SIZE(buttons_group_gen)},
    {buttons_group_app, ARRAY_SIZE(buttons_group_app)},
    {buttons_group_key, ARRAY_SIZE(buttons_group_key)},
    {buttons_group_dfn, ARRAY_SIZE(buttons_group_dfn)},
    {buttons_group_dfl, ARRAY_SIZE(buttons_group_dfl)},
    {buttons_group_vm,  ARRAY_SIZE(buttons_group_vm) },
};

void buttons_init(lv_obj_t *parent) {

    /* Fill prev/next pointers */
    for (size_t i = 0; i < ARRAY_SIZE(groups); i++) {
        buttons_page_t **group = groups[i].group;
        for (ssize_t j = 0; j < groups[i].size; j++) {
            if (group[j]->items[0]->press == button_next_page_cb) {
                uint16_t next_id = (j + 1) % groups[i].size;
                group[j]->items[0]->next = group[next_id];
            } else {
                LV_LOG_WARN("First button in page=%u, group=%u press cb is wrong", j, i);
            }
            if (group[j]->items[0]->hold == button_prev_page_cb) {
                uint16_t prev_id = (j - 1) % groups[i].size;
                group[j]->items[0]->prev = group[prev_id];
            } else {
                LV_LOG_WARN("First button in page=%u, group=%u hold cb is wrong", j, i);
            }
        }
    }

    btn_flt_low.subj = cfg_cur.filter.low;
    btn_flt_high.subj = cfg_cur.filter.high;
    btn_flt_bw.subj = cfg_cur.filter.bw;

    uint16_t y = 480 - btn_height;
    uint16_t x = 0;
    uint16_t width = 800 / 5;

    for (uint8_t i = 0; i < 5; i++) {
        lv_obj_t *f = lv_obj_create(parent);

        lv_obj_remove_style_all(f);
        lv_obj_add_style(f, &btn_style, 0);

        lv_obj_set_pos(f, x, y);
        lv_obj_set_size(f, width, btn_height);
        x += width;

        lv_obj_t *label = lv_label_create(f);

        lv_obj_center(label);
        lv_obj_set_user_data(f, label);
        lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);

        btn[i].label = label;
    }

    parent_obj = parent;
    lv_msg_subscribe(MSG_PARAM_CHANGED, param_changed_cb, NULL);
}

void buttons_load(uint8_t n, button_item_t *item) {
    button_item_t *prev_item = btn[n].item;
    if (prev_item) {
        prev_item->label_obj = NULL;
        if (item->observer) {
            observer_del(item->observer);
        }
    }

    lv_obj_t *label = btn[n].label;
    if (item) {
        if (item->type == BTN_TEXT) {
            lv_label_set_text(label, item->label);
        } else if (item->type == BTN_TEXT_FN) {
            lv_label_set_text(label, item->label_fn());
            lv_obj_set_user_data(label, item->label_fn);
            if (item->subj) {
                item->observer = subject_add_observer(item->subj, label_update_cb, item);
            }
        } else {
            lv_label_set_text(label, "");
        }
        item->label_obj = label;
    } else {
        lv_label_set_text(label, "");
    }
    btn[n].item = item;
}

void buttons_load_page(buttons_page_t *page) {
    if (!page) {
        LV_LOG_ERROR("NULL pointer to buttons page");
        return;
    }
    if (cur_page) {
        buttons_unload_page();
    }
    cur_page = page;
    for (uint8_t i = 0; i < BUTTONS; i++) {
        buttons_load(i, page->items[i]);
    }
}

void buttons_unload_page() {
    cur_page = NULL;
    for (uint8_t i = 0; i < BUTTONS; i++) {
        lv_obj_t        *label = btn[i].label;
        lv_label_set_text(label, "");
        lv_obj_set_user_data(label, NULL);
        if (btn[i].item) {
            btn[i].item->label_obj = NULL;
            btn[i].item = NULL;
        }
    }
}

void button_next_page_cb(button_item_t *item) {
    buttons_unload_page();
    buttons_load_page(item->next);

    char *voice = item->next->items[0]->voice;
    if (voice) {
        voice_say_text_fmt("%s", voice);
    }
}

void button_prev_page_cb(button_item_t *item) {
    buttons_unload_page();
    buttons_load_page(item->prev);

    char *voice = item->prev->items[0]->voice;
    if (voice) {
        voice_say_text_fmt("%s", voice);
    }
}

static void button_app_page_cb(button_item_t *item) {
    main_screen_start_app(item->data);
}

static void button_action_cb(button_item_t *item) {
    main_screen_action(item->data);
}

static void button_vol_update_cb(button_item_t *item) {
    vol_set_mode(item->data);
    vol_update(0, true);
}

static void button_mfk_update_cb(button_item_t *item) {
    mfk_set_mode(item->data);
    mfk_update(0, true);
}

static void button_vol_hold_cb(button_item_t *item) {
    uint64_t        mask = (uint64_t) 1L << item->data;

    if (params.vol_modes ^ mask) {
        params_lock();
        params.vol_modes ^= mask;
        params_unlock(&params.dirty.vol_modes);

        if (params.vol_modes & mask) {
            msg_update_text_fmt("Added to VOL encoder");
            voice_say_text_fmt("Added to volume encoder");
        } else {
            msg_update_text_fmt("Removed from VOL encoder");
            voice_say_text_fmt("Removed from volume encoder");
        }
    }
}

static void button_mfk_hold_cb(button_item_t *item) {
    uint64_t        mask = (uint64_t) 1L << item->data;

    if (params.mfk_modes ^ mask) {
        params_lock();
        params.mfk_modes ^= mask;
        params_unlock(&params.dirty.mfk_modes);

        if (params.mfk_modes & mask) {
            msg_update_text_fmt("Added to MFK encoder");
            voice_say_text_fmt("Added to MFK encoder");
        } else {
            msg_update_text_fmt("Removed from MFK encoder");
            voice_say_text_fmt("Removed from MFK encoder");
        }
    }
}

static void button_mem_load_cb(button_item_t *item) {
    mem_load(item->data);
    voice_say_text_fmt("Memory %i loaded", item->data);
}

static void button_mem_save_cb(button_item_t *item) {
    mem_save(item->data);
    voice_say_text_fmt("Memory %i stored", item->data);
}

void buttons_press(uint8_t n, bool hold) {
    button_item_t *item = btn[n].item;
    if (item == NULL) {
        LV_LOG_WARN("Button %u is NULL", n);
    }
    if (hold) {
        if (item->hold) {
            item->hold(item);
        } else {
            LV_LOG_USER("Button %u hold action is NULL", n);
        }
    } else {
        if (item->press) {
            item->press(item);
        } else {
            LV_LOG_USER("Button %u press action is NULL", n);
        }
    }
}

void buttons_load_page_group(buttons_group_t group) {
    size_t group_size = 0;
    for (size_t i = 0; i < ARRAY_SIZE(groups); i++) {
        if (groups[i].group == group) {
            group_size = groups[i].size;
            break;
        }
    }
    if (group_size <= 0) {
        return;
    }
    for (size_t i = 0; i < group_size; i++) {
        if (group[i] == cur_page) {
            // load next
            cur_page->items[0]->press(cur_page->items[0]);
            return;
        }
    }
    // Load first
    buttons_load_page(group[0]);
}

buttons_page_t *buttons_get_cur_page() {
    return cur_page;
}

static char * vol_label_getter() {
    static char buf[16];
    sprintf(buf, "Volume:\n%zi", subject_get_int(cfg.vol.val));
    return buf;
}

static char * sql_label_getter() {
    static char buf[16];
    sprintf(buf, "Squelch:\n%zu", params.sql);
    return buf;
}

static char * rfg_label_getter() {
    static char buf[16];
    sprintf(buf, "RF gain:\n%zu", subject_get_int(cfg_cur.band->rfg.val));
    return buf;
}

static char * tx_power_label_getter() {
    static char buf[20];
    sprintf(buf, "TX power:\n%0.1f W", params.pwr);
    return buf;
}

static char * filter_low_label_getter() {
    static char buf[22];
    sprintf(buf, "Filter low:\n%zu Hz", subject_get_int(cfg_cur.filter.low));
    return buf;
}
static char * filter_high_label_getter() {
    static char buf[22];
    sprintf(buf, "Filter high:\n%zu Hz", subject_get_int(cfg_cur.filter.high));
    return buf;
}

static char * filter_bw_label_getter() {
    static char buf[22];
    sprintf(buf, "Filter BW:\n%i Hz", subject_get_int(cfg_cur.filter.bw));
    return buf;
}


static char * speaker_mode_label_getter() {
    static char buf[22];
    sprintf(buf, "SP mode:\n%s", params.spmode.x ? "Speaker": "Phones");
    return buf;
}


static char * mic_sel_label_getter() {
    static char buf[22];
    sprintf(buf, "MIC Sel:\n%s", params_mic_str_get(params.mic));
    return buf;
}


static char * h_mic_gain_label_getter() {
    static char buf[22];
    sprintf(buf, "H-Mic gain:\n%zu", params.hmic);
    return buf;
}

static char * i_mic_gain_label_getter() {
    static char buf[22];
    sprintf(buf, "I-Mic gain:\n%zu", params.imic);
    return buf;
}

static char * moni_level_label_getter() {
    static char buf[22];
    sprintf(buf, "Moni level:\n%zu", params.moni);
    return buf;
}

static char * charger_label_getter() {
    static char buf[22];
    sprintf(buf, "Charger:\n%s", params_charger_str_get(params.charger));
    return buf;
}


static char * rit_label_getter() {
    static char buf[22];
    sprintf(buf, "RIT:\n%+zi", params.rit);
    return buf;
}

static char * xit_label_getter() {
    static char buf[22];
    sprintf(buf, "XIT:\n%+zi", params.xit);
    return buf;
}

static char * agc_hang_label_getter() {
    static char buf[22];
    sprintf(buf, "AGC hang:\n%s", params.agc_hang ? "On": "Off");
    return buf;
}

static char * agc_knee_label_getter() {
    static char buf[22];
    sprintf(buf, "AGC knee:\n%zi dB", params.agc_knee);
    return buf;
}

static char * agc_slope_label_getter() {
    static char buf[22];
    sprintf(buf, "AGC slope:\n%zu dB", params.agc_slope);
    return buf;
}

static char * key_speed_label_getter() {
    static char buf[22];
    sprintf(buf, "Speed:\n%zu wpm", params.key_speed);
    return buf;
}

static char * key_volume_label_getter() {
    static char buf[22];
    sprintf(buf, "Volume:\n%zu", params.key_vol);
    return buf;
}

static char * key_train_label_getter() {
    static char buf[22];
    sprintf(buf, "Train:\n%s", params.key_train ? "On": "Off");
    return buf;
}

static char * key_tone_label_getter() {
    static char buf[22];
    sprintf(buf, "Tone:\n%zu Hz", subject_get_int(cfg.key_tone.val));
    return buf;
}

static char * key_mode_label_getter() {
    static char buf[22];
    sprintf(buf, "Mode:\n%s", params_key_mode_str_get(params.key_mode));
    return buf;
}

static char * iambic_mode_label_getter() {
    static char buf[22];
    sprintf(buf, "Iambic:\n%s mode", params_iambic_mode_str_ger(params.iambic_mode));
    return buf;
}

static char * qsk_time_label_getter() {
    static char buf[22];
    sprintf(buf, "QSK time:\n%zu ms", params.qsk_time);
    return buf;
}

static char * key_ratio_label_getter() {
    static char buf[22];
    sprintf(buf, "Ratio:\n%0.1f", (float)params.key_ratio / 10.0f);
    return buf;
}

static char * cw_decoder_label_getter() {
    static char buf[22];
    sprintf(buf, "Decoder:\n%s", params.cw_decoder ? "On": "Off");
    return buf;
}

static char * cw_tuner_label_getter() {
    static char buf[22];
    sprintf(buf, "Tuner:\n%s", params.cw_tune ? "On": "Off");
    return buf;
}

static char * cw_snr_label_getter() {
    static char buf[22];
    sprintf(buf, "Dec SNR:\n%0.1f dB", params.cw_decoder_snr);
    return buf;
}

static char * cw_peak_beta_label_getter() {
    static char buf[22];
    sprintf(buf, "Peak beta:\n%0.2f", params.cw_decoder_peak_beta);
    return buf;
}

static char * cw_noise_beta_label_getter() {
    static char buf[22];
    sprintf(buf, "Noise beta:\n%0.2f", params.cw_decoder_noise_beta);
    return buf;
}

static char * dnf_label_getter() {
    static char buf[22];
    sprintf(buf, "DNF:\n%s", subject_get_int(cfg.dnf.val) ? "On": "Off");
    return buf;
}

static char * dnf_center_label_getter() {
    static char buf[22];
    sprintf(buf, "DNF freq:\n%zu Hz", subject_get_int(cfg.dnf_center.val));
    return buf;
}

static char * dnf_width_label_getter() {
    static char buf[22];
    sprintf(buf, "DNF width:\n%zu Hz", subject_get_int(cfg.dnf_width.val));
    return buf;
}

static char * nb_label_getter() {
    static char buf[22];
    sprintf(buf, "NB:\n%s", subject_get_int(cfg.nb.val) ? "On": "Off");
    return buf;
}

static char * nb_level_label_getter() {
    static char buf[22];
    sprintf(buf, "NB level:\n%zu", subject_get_int(cfg.nb_level.val));
    return buf;
}

static char * nb_width_label_getter() {
    static char buf[22];
    sprintf(buf, "NB width:\n%zu Hz", subject_get_int(cfg.nb_width.val));
    return buf;
}

static char * nr_label_getter() {
    static char buf[22];
    sprintf(buf, "NR:\n%s", subject_get_int(cfg.nr.val) ? "On": "Off");
    return buf;
}

static char * nr_level_label_getter() {
    static char buf[22];
    sprintf(buf, "NR level:\n%zu", subject_get_int(cfg.nr_level.val));
    return buf;
}


static void param_changed_cb(void * s, lv_msg_t * m) {
    for (size_t i = 0; i < BUTTONS; i++) {
        lv_obj_t  *label = btn[i].label;
        if (!label) continue;
        char *(*label_getter)() = lv_obj_get_user_data(label);
        if (!label_getter) continue;
        lv_label_set_text(label, label_getter());
    }
}


static void label_update_cb(subject_t subj, void *user_data) {
    button_item_t *item = (button_item_t*)user_data;
    if (item->label_obj) {
        lv_label_set_text(item->label_obj, item->label_fn());
    } else {
        LV_LOG_WARN("Can't update label: it's NULL");
    }
}
