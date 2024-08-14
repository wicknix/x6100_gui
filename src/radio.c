/*
 *  SPDX-License-Identifier: LGPL-2.1-or-later
 *
 *  Xiegu X6100 LVGL GUI
 *
 *  Copyright (c) 2022-2023 Belousov Oleg aka R1CBU
 */

#include <unistd.h>
#include <stdio.h>
#include <pthread.h>
#include <string.h>

#include <aether_radio/x6100_control/low/flow.h>
#include <aether_radio/x6100_control/low/gpio.h>

#include "util.h"
#include "radio.h"
#include "dsp.h"
#include "params/params.h"
#include "hkey.h"
#include "tx_info.h"
#include "info.h"
#include "dialog_swrscan.h"
#include "cw.h"

#define FLOW_RESTART_TIMEOUT 300
#define IDLE_TIMEOUT        (3 * 1000)

static radio_state_change_t notify_tx;
static radio_state_change_t notify_rx;
static radio_state_change_t notify_atu_update;

static pthread_mutex_t  control_mux;

static x6100_flow_t     *pack;

static radio_state_t    state = RADIO_RX;
static uint64_t         now_time;
static uint64_t         prev_time;
static uint64_t         idle_time;
static bool             mute = false;

static void update_agc_time();

static void radio_lock() {
    pthread_mutex_lock(&control_mux);
}

static void radio_unlock() {
    idle_time = get_time();
    pthread_mutex_unlock(&control_mux);
}

bool radio_tick() {
    if (now_time < prev_time) {
        prev_time = now_time;
    }

    int32_t d = now_time - prev_time;

    if (x6100_flow_read(pack)) {
        prev_time = now_time;

        static uint8_t delay = 0;

        if (delay++ > 10) {
            delay = 0;
            clock_update_power(pack->vext * 0.1f, pack->vbat*0.1f, pack->batcap, pack->flag.charging);
        }
        dsp_samples(pack->samples, RADIO_SAMPLES, pack->flag.tx);

        switch (state) {
            case RADIO_RX:
                if (pack->flag.tx) {
                    state = RADIO_TX;
                    notify_tx();
                }
                break;

            case RADIO_TX:
                if (!pack->flag.tx) {
                    state = RADIO_RX;
                    notify_rx();
                } else {
                    tx_info_update(pack->tx_power * 0.1f, pack->vswr * 0.1f, pack->alc_level * 0.1f);
                }
                break;

            case RADIO_ATU_START:
                radio_lock();
                x6100_control_atu_tune(true);
                radio_unlock();
                state = RADIO_ATU_WAIT;
                break;

            case RADIO_ATU_WAIT:
                if (pack->flag.tx) {
                    notify_tx();
                    state = RADIO_ATU_RUN;
                }
                break;

            case RADIO_ATU_RUN:
                if (pack->flag.atu_status && !pack->flag.tx) {
                    params_atu_save(pack->atu_params);
                    radio_lock();
                    x6100_control_atu_tune(false);
                    radio_unlock();
                    notify_rx();

                    if (params.atu) {
                        radio_lock();
                        x6100_control_cmd(x6100_atu_network, pack->atu_params);
                        radio_unlock();
                        params.atu_loaded = true;
                        notify_atu_update();
                    }
                    state = RADIO_RX;
                } else if (pack->flag.tx) {
                    tx_info_update(pack->tx_power * 0.1f, pack->vswr * 0.1f, pack->alc_level * 0.1f);
                }
                break;

            case RADIO_SWRSCAN:
                dialog_swrscan_update(pack->vswr * 0.1f);
                break;

            case RADIO_POWEROFF:
                x6100_control_poweroff();
                state = RADIO_OFF;
                break;

            case RADIO_OFF:
                break;
        }

        hkey_put(pack->hkey);
    } else {
        if (d > FLOW_RESTART_TIMEOUT) {
            LV_LOG_WARN("Flow reset");
            prev_time = now_time;
            x6100_flow_restart();
            dsp_reset();
        }
        return true;
    }
    return false;
}

static void * radio_thread(void *arg) {
    while (true) {
        now_time = get_time();

        if (radio_tick()) {
            usleep(15000);
        }

        int32_t idle = now_time - idle_time;

        if (idle > IDLE_TIMEOUT && state == RADIO_RX) {
            radio_lock();
            x6100_control_idle();
            radio_unlock();

            idle_time = now_time;
        }
    }
}

void radio_vfo_set() {
    uint64_t shift, vfo_freq;

    radio_lock();

    for (int i = 0; i < 2; i++) {
        x6100_control_vfo_mode_set(i, params_band_vfo_mode_get(i));
        x6100_control_vfo_agc_set(i, params_band_vfo_agc_get(i));
        x6100_control_vfo_pre_set(i, params_band_vfo_pre_get(i));
        x6100_control_vfo_att_set(i, params_band_vfo_att_get(i));

        vfo_freq = params_band_vfo_freq_get(i);
        radio_check_freq(vfo_freq, &shift);
        x6100_control_vfo_freq_set(i, vfo_freq - shift);
        params_band_vfo_shift_set(i, shift != 0);
    }

    x6100_control_vfo_set(params_band_vfo_get());
    x6100_control_split_set(params_band_split_get());
    x6100_control_rfg_set(params_band_rfg_get());
    radio_unlock();
    lv_msg_send(RADIO_MSG_MODE_CHANGED, NULL);

    params_bands_find(params_band_cur_freq_get(), &params.freq_band);
}

/**
 * Set radio BB filters
 */
static void radio_filter_set(int32_t * low, int32_t * high) {
    x6100_mode_t    mode = radio_current_mode();
    radio_lock();
    switch (mode) {
        case x6100_mode_am:
        case x6100_mode_nfm:
            if (high != NULL) {
                x6100_control_cmd(x6100_filter1_low, -*high);
                x6100_control_cmd(x6100_filter2_low, -*high);
                x6100_control_cmd(x6100_filter1_high, *high);
                x6100_control_cmd(x6100_filter2_high, *high);
            }
            break;

        default:
            if (low != NULL) {
                x6100_control_cmd(x6100_filter1_low, *low);
                x6100_control_cmd(x6100_filter2_low, *low);
            }
            if (high != NULL) {
                x6100_control_cmd(x6100_filter1_high, *high);
                x6100_control_cmd(x6100_filter2_high, *high);
            }
            break;
    }
    radio_unlock();
}

void radio_filters_setup() {

    int32_t low, high;
    params_current_mode_filter_get(&low, &high);
    radio_filter_set(&low, &high);
    update_agc_time();
}

void radio_bb_reset() {
    x6100_gpio_set(x6100_pin_bb_reset, 1);
    usleep(100000);
    x6100_gpio_set(x6100_pin_bb_reset, 0);
    usleep(6000000);
    radio_lock();
    x6100_control_idle();
    radio_unlock();
    x6100_flow_restart();
}

void radio_init(radio_state_change_t tx_cb, radio_state_change_t rx_cb, radio_state_change_t atu_update_cb) {
    if (!x6100_gpio_init())
        return;

    while (!x6100_control_init()) {
        usleep(100000);
    }

    if (!x6100_flow_init())
        return;

    x6100_gpio_set(x6100_pin_wifi, 1);          /* WiFi off */
    x6100_gpio_set(x6100_pin_morse_key, 1);     /* Morse key off */

    notify_tx = tx_cb;
    notify_rx = rx_cb;
    notify_atu_update = atu_update_cb;

    pack = malloc(sizeof(x6100_flow_t));

    radio_vfo_set();
    radio_filters_setup();
    radio_load_atu();

    x6100_control_rxvol_set(params.vol);
    x6100_control_rfg_set(params_band_rfg_get());
    x6100_control_sql_set(params.sql);
    x6100_control_atu_set(params.atu);
    x6100_control_txpwr_set(params.pwr);
    x6100_control_charger_set(params.charger == RADIO_CHARGER_ON);
    x6100_control_bias_drive_set(params.bias_drive);
    x6100_control_bias_final_set(params.bias_final);

    x6100_control_key_speed_set(params.key_speed);
    x6100_control_key_mode_set(params.key_mode);
    x6100_control_iambic_mode_set(params.iambic_mode);
    x6100_control_key_tone_set(params.key_tone);
    x6100_control_key_vol_set(params.key_vol);
    x6100_control_key_train_set(params.key_train);
    x6100_control_qsk_time_set(params.qsk_time);
    x6100_control_key_ratio_set(params.key_ratio * 0.1f);

    x6100_control_mic_set(params.mic);
    x6100_control_hmic_set(params.hmic);
    x6100_control_imic_set(params.imic);
    x6100_control_spmode_set(params.spmode.x);

    x6100_control_dnf_set(params.dnf);
    x6100_control_dnf_center_set(params.dnf_center);
    x6100_control_dnf_width_set(params.dnf_width);
    x6100_control_nb_set(params.nb);
    x6100_control_nb_level_set(params.nb_level);
    x6100_control_nb_width_set(params.nb_width);
    x6100_control_nr_set(params.nr);
    x6100_control_nr_level_set(params.nr_level);

    x6100_control_agc_hang_set(params.agc_hang);
    x6100_control_agc_knee_set(params.agc_knee);
    x6100_control_agc_slope_set(params.agc_slope);

    x6100_control_vox_set(params.vox);
    x6100_control_vox_ag_set(params.vox_ag);
    x6100_control_vox_delay_set(params.vox_delay);
    x6100_control_vox_gain_set(params.vox_gain);

    x6100_control_cmd(x6100_rit, params.rit);
    x6100_control_cmd(x6100_xit, params.xit);
    x6100_control_linein_set(params.line_in);
    x6100_control_lineout_set(params.line_out);
    x6100_control_cmd(x6100_monilevel, params.moni);

    prev_time = get_time();
    idle_time = prev_time;

    pthread_mutex_init(&control_mux, NULL);

    pthread_t thread;

    pthread_create(&thread, NULL, radio_thread, NULL);
    pthread_detach(thread);
}

radio_state_t radio_get_state() {
    return state;
}

void radio_set_freq(uint64_t freq) {
    uint64_t shift = 0;

    if (!radio_check_freq(freq, &shift)) {
        LV_LOG_ERROR("Freq %llu incorrect", freq);
        return;
    }

    params_band_cur_freq_set(freq);
    params_band_cur_shift_set(shift != 0);

    radio_lock();
    x6100_control_vfo_freq_set(params_band_vfo_get(), freq - shift);
    radio_unlock();

    radio_load_atu();
}

bool radio_check_freq(uint64_t freq, uint64_t *shift) {
    if (freq >= 500000 && freq <= 55000000) {
        if (shift != NULL) {
            *shift = 0;
        }
        return true;
    }

    for (uint8_t i = 0; i < TRANSVERTER_NUM; i++)
        if (freq >= params_transverter[i].from && freq <= params_transverter[i].to) {
            if (shift != NULL) {
                *shift = params_transverter[i].shift;
            }
            return true;
        }

    return false;
}

uint64_t radio_change_freq(int32_t df, uint64_t *prev_freq) {
    *prev_freq = params_band_cur_freq_get();

    radio_set_freq(align_long(*prev_freq + df, abs(df)));

    return params_band_cur_freq_get();
}

uint16_t radio_change_vol(int16_t df) {
    if (df == 0) {
        return params.vol;
    }

    mute = false;

    params_lock();
    params.vol = limit(params.vol + df, 0, 55);
    params_unlock(&params.dirty.vol);

    radio_lock();
    x6100_control_rxvol_set(params.vol);
    radio_unlock();

    return params.vol;
}

void radio_change_mute() {
    mute = !mute;
    x6100_control_rxvol_set(mute ? 0 : params.vol);
}

uint16_t radio_change_moni(int16_t df) {
    if (df == 0) {
        return params.moni;
    }

    params_lock();
    params.moni = limit(params.moni + df, 0, 100);
    params_unlock(&params.dirty.moni);

    radio_lock();
    x6100_control_cmd(x6100_monilevel, params.moni);
    radio_unlock();

    return params.moni;
}

bool radio_change_spmode(int16_t df) {
    if (df == 0) {
        return params.spmode.x;
    }

    params_bool_set(&params.spmode, df > 0);

    radio_lock();
    x6100_control_spmode_set(params.spmode.x);
    radio_unlock();

    return params.spmode.x;
}

uint16_t radio_change_rfg(int16_t df) {
    uint16_t rfg = params_band_rfg_get();
    if (df == 0) {
        return rfg;
    }
    rfg = params_band_rfg_set(rfg + df);

    radio_lock();
    x6100_control_rfg_set(rfg);
    radio_unlock();

    return rfg;
}

uint16_t radio_change_sql(int16_t df) {
    if (df == 0) {
        return params.sql;
    }

    params_lock();
    params.sql = limit(params.sql + df, 0, 100);
    params_unlock(&params.dirty.sql);

    radio_lock();
    x6100_control_sql_set(params.sql);
    radio_unlock();

    return params.sql;
}

bool radio_change_pre() {
    x6100_vfo_t cur_vfo = params_band_vfo_get();
    x6100_pre_t pre = params_band_cur_pre_get();

    pre = params_band_cur_pre_set(!pre);

    radio_lock();
    x6100_control_vfo_pre_set(cur_vfo, pre);
    radio_unlock();

    voice_say_text_fmt("Preamplifier %s", pre ? "On" : "Off");
    return pre;
}

bool radio_change_att() {
    x6100_vfo_t cur_vfo = params_band_vfo_get();
    x6100_att_t att = params_band_cur_att_get();

    att = params_band_cur_att_set(!att);

    radio_lock();
    x6100_control_vfo_att_set(cur_vfo, att);
    radio_unlock();

    voice_say_text_fmt("Attenuator %s", att ? "On" : "Off");
    return att;
}

/**
 * get frequencies for display and dsp (with negative numbers)
*/
void radio_filter_get(int32_t *from_freq, int32_t *to_freq) {
    int32_t         low, high;
    x6100_mode_t    mode = radio_current_mode();
    params_current_mode_filter_get(&low, &high);

    switch (mode) {
        case x6100_mode_lsb:
        case x6100_mode_lsb_dig:
        case x6100_mode_cwr:
            *from_freq = -high;
            *to_freq = -low;
            break;

        case x6100_mode_usb:
        case x6100_mode_usb_dig:
        case x6100_mode_cw:
            *from_freq = low;
            *to_freq = high;
            break;

        case x6100_mode_am:
        case x6100_mode_nfm:
            *from_freq = -high;
            *to_freq = high;
            break;

        default:
            *from_freq = 0;
            *to_freq = 0;
    }
}

void radio_set_mode(x6100_vfo_t vfo, x6100_mode_t mode) {
    params_band_vfo_mode_set(vfo, mode);

    radio_lock();
    x6100_control_vfo_mode_set(vfo, mode);
    radio_unlock();
    lv_msg_send(RADIO_MSG_MODE_CHANGED, NULL);
}

void radio_set_cur_mode(x6100_mode_t mode) {
    x6100_vfo_t vfo = params_band_vfo_get();
    radio_set_mode(vfo, mode);
}

x6100_mode_t radio_current_mode() {
    return params_band_cur_mode_get();
}

uint32_t radio_change_filter_low(int32_t freq) {
    if (freq == params_current_mode_filter_low_get()){
        return freq;
    }
    int32_t new_freq = params_current_mode_filter_low_set(freq);
    radio_filter_set(&new_freq, NULL);

    return new_freq;
}

uint32_t radio_change_filter_high(int32_t freq) {
    if (freq == params_current_mode_filter_high_get()){
        return freq;
    }
    int32_t new_freq = params_current_mode_filter_high_set(freq);

    radio_filter_set(NULL, &new_freq);

    return new_freq;
}

uint32_t radio_change_filter_bw(int32_t bw) {
    if (bw == params_current_mode_filter_bw_get()){
        return bw;
    }
    uint32_t new_bw = params_current_mode_filter_bw_set(bw);
    uint32_t low_freq = params_current_mode_filter_low_get();
    uint32_t high_freq = params_current_mode_filter_high_get();

    radio_filter_set(&low_freq, &high_freq);

    return new_bw;
}


static void update_agc_time() {
    x6100_agc_t     agc = params_band_cur_agc_get();
    x6100_mode_t    mode = radio_current_mode();
    uint16_t        agc_time = 500;

    switch (agc) {
        case x6100_agc_off:
            agc_time = 1000;
            break;

        case x6100_agc_slow:
            agc_time = 1000;
            break;

        case x6100_agc_fast:
            agc_time = 100;
            break;

        case x6100_agc_auto:
            switch (mode) {
                case x6100_mode_lsb:
                case x6100_mode_lsb_dig:
                case x6100_mode_usb:
                case x6100_mode_usb_dig:
                    agc_time = 500;
                    break;

                case x6100_mode_cw:
                case x6100_mode_cwr:
                    agc_time = 100;
                    break;

                case x6100_mode_am:
                case x6100_mode_nfm:
                    agc_time = 1000;
                    break;
            }
            break;
    }

    radio_lock();
    x6100_control_agc_time_set(agc_time);
    radio_unlock();
}

void radio_change_agc() {
    x6100_agc_t     agc = params_band_cur_agc_get();

    switch (agc) {
        case x6100_agc_off:
            agc = x6100_agc_slow;
            voice_say_text_fmt("Auto gain slow mode");
            break;

        case x6100_agc_slow:
            agc = x6100_agc_fast;
            voice_say_text_fmt("Auto gain fast mode");
            break;

        case x6100_agc_fast:
            agc = x6100_agc_auto;
            voice_say_text_fmt("Auto gain auto mode");
            break;

        case x6100_agc_auto:
            agc = x6100_agc_off;
            voice_say_text_fmt("Auto gain off");
            break;
    }

    update_agc_time();

    agc = params_band_cur_agc_set(agc);

    radio_lock();
    x6100_control_vfo_agc_set(params_band_vfo_get(), agc);
    radio_unlock();
}

void radio_change_atu() {
    params_lock();
    params.atu = !params.atu;
    params_unlock(&params.dirty.atu);

    radio_lock();
    x6100_control_atu_set(params.atu);
    radio_unlock();

    radio_load_atu();
    voice_say_text_fmt("Auto tuner %s", params.atu ? "On" : "Off");
}

void radio_start_atu() {
    if (state == RADIO_RX) {
        state = RADIO_ATU_START;
    }
}

bool radio_start_swrscan() {
    if (state != RADIO_RX) {
        return false;
    }

    state = RADIO_SWRSCAN;

    x6100_control_vfo_mode_set(params_band_vfo_get(), x6100_mode_am);
    x6100_control_txpwr_set(5.0f);
    x6100_control_swrscan_set(true);
    lv_msg_send(RADIO_MSG_MODE_CHANGED, NULL);

    return true;
}

void radio_stop_swrscan() {
    if (state == RADIO_SWRSCAN) {
        x6100_control_swrscan_set(false);
        x6100_control_txpwr_set(params.pwr);
        state = RADIO_RX;
    }
}

void radio_load_atu() {
    if (params.atu) {
        if (params_band_cur_shift_get()) {
            info_atu_update();

            radio_lock();
            x6100_control_atu_set(false);
            radio_unlock();

            return;
        }

        uint32_t atu = params_atu_load(&params.atu_loaded);

        radio_lock();
        x6100_control_atu_set(true);
        x6100_control_cmd(x6100_atu_network, atu);
        radio_unlock();

        if (state != RADIO_SWRSCAN) {
            info_atu_update();
        }
    }
}

float radio_change_pwr(int16_t d) {
    if (d == 0) {
        return params.pwr;
    }

    params_lock();
    params.pwr += d * 0.1f;

    if (params.pwr > 10.0f) {
        params.pwr = 10.0f;
    } else if (params.pwr < 0.1f) {
        params.pwr = 0.1f;
    }

    params_unlock(&params.dirty.pwr);

    radio_lock();
    x6100_control_txpwr_set(params.pwr);
    radio_unlock();

    return params.pwr;
}

void radio_set_pwr(float d) {
    radio_lock();
    x6100_control_txpwr_set(d);
    radio_unlock();
}

uint16_t radio_change_key_speed(int16_t d) {
    if (d == 0) {
        return params.key_speed;
    }

    params_lock();
    params.key_speed = limit(params.key_speed + d, 5, 50);
    params_unlock(&params.dirty.key_speed);

    radio_lock();
    x6100_control_key_speed_set(params.key_speed);
    radio_unlock();

    return params.key_speed;
}

x6100_key_mode_t radio_change_key_mode(int16_t d) {
    if (d == 0) {
        return params.key_mode;
    }

    params_lock();

    switch (params.key_mode) {
        case x6100_key_manual:
            params.key_mode = d > 0 ? x6100_key_auto_left : x6100_key_auto_right;
            break;

        case x6100_key_auto_left:
            params.key_mode = d > 0 ? x6100_key_auto_right : x6100_key_manual;
            break;

        case x6100_key_auto_right:
            params.key_mode = d > 0 ? x6100_key_manual : x6100_key_auto_left;
            break;
    }

    params_unlock(&params.dirty.key_mode);

    radio_lock();
    x6100_control_key_mode_set(params.key_mode);
    radio_unlock();

    return params.key_mode;
}

x6100_iambic_mode_t radio_change_iambic_mode(int16_t d) {
    if (d == 0) {
        return params.iambic_mode;
    }

    params_lock();

    params.iambic_mode = (params.iambic_mode == x6100_iambic_a) ? x6100_iambic_b : x6100_iambic_a;

    params_unlock(&params.dirty.iambic_mode);

    radio_lock();
    x6100_control_iambic_mode_set(params.iambic_mode);
    radio_unlock();

    return params.iambic_mode;
}

uint16_t radio_change_key_tone(int16_t d) {
    if (d == 0) {
        return params.key_tone;
    }

    params_lock();

    params.key_tone += (d > 0) ? 10 : -10;

    if (params.key_tone < 400) {
        params.key_tone = 400;
    } else if (params.key_tone > 1200) {
        params.key_tone = 1200;
    }

    params_unlock(&params.dirty.key_tone);

    radio_lock();
    x6100_control_key_tone_set(params.key_tone);
    radio_unlock();
    cw_notify_change_key_tone();

    return params.key_tone;
}

uint16_t radio_change_key_vol(int16_t d) {
    if (d == 0) {
        return params.key_vol;
    }

    params_lock();

    params.key_vol = limit(params.key_vol + d, 0, 32);
    params_unlock(&params.dirty.key_vol);

    radio_lock();
    x6100_control_key_vol_set(params.key_vol);
    radio_unlock();

    return params.key_vol;
}

bool radio_change_key_train(int16_t d) {
    if (d == 0) {
        return params.key_train;
    }

    params_lock();
    params.key_train = !params.key_train;
    params_unlock(&params.dirty.key_train);

    radio_lock();
    x6100_control_key_train_set(params.key_train);
    radio_unlock();

    return params.key_train;
}

uint16_t radio_change_qsk_time(int16_t d) {
    if (d == 0) {
        return params.qsk_time;
    }

    params_lock();

    int16_t x = params.qsk_time;

    if (d > 0) {
        x += 10;
    } else {
        x -= 10;
    }

    params.qsk_time = limit(x, 0, 1000);
    params_unlock(&params.dirty.qsk_time);

    radio_lock();
    x6100_control_qsk_time_set(params.qsk_time);
    radio_unlock();

    return params.qsk_time;
}

uint8_t radio_change_key_ratio(int16_t d) {
    if (d == 0) {
        return params.key_ratio;
    }

    params_lock();

    int16_t x = params.key_ratio;

    if (d > 0) {
        x += 5;
    } else {
        x -= 5;
    }

    params.key_ratio = limit(x, 25, 45);
    params_unlock(&params.dirty.key_ratio);

    radio_lock();
    x6100_control_key_ratio_set(params.key_ratio * 0.1f);
    radio_unlock();

    return params.key_ratio;
}

x6100_mic_sel_t radio_change_mic(int16_t d) {
    if (d == 0) {
        return params.mic;
    }

    params_lock();

    switch (params.mic) {
        case x6100_mic_builtin:
            params.mic = d > 0 ? x6100_mic_handle : x6100_mic_auto;
            break;

        case x6100_mic_handle:
            params.mic = d > 0 ? x6100_mic_auto : x6100_mic_builtin;
            break;

        case x6100_mic_auto:
            params.mic = d > 0 ? x6100_mic_builtin : x6100_mic_handle;
            break;
    }

    params_unlock(&params.dirty.mic);

    radio_lock();
    x6100_control_mic_set(params.mic);
    radio_unlock();

    return params.mic;
}

uint8_t radio_change_hmic(int16_t d) {
    if (d == 0) {
        return params.hmic;
    }

    params_lock();
    params.hmic = limit(params.hmic + d, 0, 50);
    params_unlock(&params.dirty.hmic);

    radio_lock();
    x6100_control_hmic_set(params.hmic);
    radio_unlock();

    return params.hmic;
}

uint8_t radio_change_imic(int16_t d) {
    if (d == 0) {
        return params.imic;
    }

    params_lock();
    params.imic = limit(params.imic + d, 0, 35);
    params_unlock(&params.dirty.imic);

    radio_lock();
    x6100_control_imic_set(params.imic);
    radio_unlock();

    return params.imic;
}

x6100_vfo_t radio_set_vfo(x6100_vfo_t vfo) {
    params_band_vfo_set(vfo);

    radio_lock();
    x6100_control_vfo_set(vfo);
    radio_unlock();
    lv_msg_send(RADIO_MSG_MODE_CHANGED, NULL);
}

x6100_vfo_t radio_toggle_vfo() {
    x6100_vfo_t new_vfo = (params_band_vfo_get() == X6100_VFO_A) ? X6100_VFO_B : X6100_VFO_A;

    radio_set_vfo(new_vfo);
    voice_say_text_fmt("V F O %s", (new_vfo == X6100_VFO_A) ? "A" : "B");

    return new_vfo;
}

void radio_toggle_split() {
    bool split = params_band_split_get();
    split = params_band_split_set(!split);

    radio_lock();
    x6100_control_split_set(split);
    radio_unlock();
    voice_say_text_fmt("Split %s", split ? "On" : "Off");
}

void radio_poweroff() {
    if (params.charger == RADIO_CHARGER_SHADOW) {
        radio_lock();
        x6100_control_charger_set(true);
        radio_unlock();
    }

    state = RADIO_POWEROFF;
}

radio_charger_t radio_change_charger(int16_t d) {
    if (d == 0) {
        return params.charger;
    }

    params_lock();

    switch (params.charger) {
        case RADIO_CHARGER_OFF:
            params.charger = d > 0 ? RADIO_CHARGER_ON : RADIO_CHARGER_SHADOW;
            break;

        case RADIO_CHARGER_ON:
            params.charger = d > 0 ? RADIO_CHARGER_SHADOW : RADIO_CHARGER_OFF;
            break;

        case RADIO_CHARGER_SHADOW:
            params.charger = d > 0 ? RADIO_CHARGER_OFF : RADIO_CHARGER_ON;
            break;
    }

    params_unlock(&params.dirty.charger);

    radio_lock();
    x6100_control_charger_set(params.charger == RADIO_CHARGER_ON);
    radio_unlock();

    return params.charger;
}

bool radio_change_dnf(int16_t d) {
    if (d == 0) {
        return params.dnf;
    }

    params_lock();
    params.dnf = !params.dnf;
    params_unlock(&params.dirty.dnf);

    radio_lock();
    x6100_control_dnf_set(params.dnf);
    radio_unlock();

    return params.dnf;
}

uint16_t radio_change_dnf_center(int16_t d) {
    if (d == 0) {
        return params.dnf_center;
    }

    params_lock();
    params.dnf_center = limit(params.dnf_center + d * 50, 100, 3000);
    params_unlock(&params.dirty.dnf_center);

    radio_lock();
    x6100_control_dnf_center_set(params.dnf_center);
    radio_unlock();

    return params.dnf_center;
}

uint16_t radio_change_dnf_width(int16_t d) {
    if (d == 0) {
        return params.dnf_width;
    }

    params_lock();
    params.dnf_width = limit(params.dnf_width + d * 5, 10, 100);
    params_unlock(&params.dirty.dnf_width);

    radio_lock();
    x6100_control_dnf_width_set(params.dnf_width);
    radio_unlock();

    return params.dnf_width;
}

bool radio_change_nb(int16_t d) {
    if (d == 0) {
        return params.nb;
    }

    params_lock();
    params.nb = !params.nb;
    params_unlock(&params.dirty.nb);

    radio_lock();
    x6100_control_nb_set(params.nb);
    radio_unlock();

    return params.nb;
}

uint8_t radio_change_nb_level(int16_t d) {
    if (d == 0) {
        return params.nb_level;
    }

    params_lock();
    params.nb_level = limit(params.nb_level + d * 5, 0, 100);
    params_unlock(&params.dirty.nb_level);

    radio_lock();
    x6100_control_nb_level_set(params.nb_level);
    radio_unlock();

    return params.nb_level;
}

uint8_t radio_change_nb_width(int16_t d) {
    if (d == 0) {
        return params.nb_width;
    }

    params_lock();
    params.nb_width = limit(params.nb_width + d * 5, 0, 100);
    params_unlock(&params.dirty.nb_width);

    radio_lock();
    x6100_control_nb_width_set(params.nb_width);
    radio_unlock();

    return params.nb_width;
}

bool radio_change_nr(int16_t d) {
    if (d == 0) {
        return params.nr;
    }

    params_lock();
    params.nr = !params.nr;
    params_unlock(&params.dirty.nr);

    radio_lock();
    x6100_control_nr_set(params.nr);
    radio_unlock();

    return params.nr;
}

uint8_t radio_change_nr_level(int16_t d) {
    if (d == 0) {
        return params.nr_level;
    }

    params_lock();
    params.nr_level = limit(params.nr_level + d * 5, 0, 60);
    params_unlock(&params.dirty.nr_level);

    radio_lock();
    x6100_control_nr_level_set(params.nr_level);
    radio_unlock();

    return params.nr_level;
}

bool radio_change_agc_hang(int16_t d) {
    if (d == 0) {
        return params.agc_hang;
    }

    params_lock();
    params.agc_hang = !params.agc_hang;
    params_unlock(&params.dirty.agc_hang);

    radio_lock();
    x6100_control_agc_hang_set(params.agc_hang);
    radio_unlock();

    return params.agc_hang;
}

int8_t radio_change_agc_knee(int16_t d) {
    if (d == 0) {
        return params.agc_knee;
    }

    params_lock();
    params.agc_knee = limit(params.agc_knee + d, -100, 0);
    params_unlock(&params.dirty.agc_knee);

    radio_lock();
    x6100_control_agc_knee_set(params.agc_knee);
    radio_unlock();

    return params.agc_knee;
}

uint8_t radio_change_agc_slope(int16_t d) {
    if (d == 0) {
        return params.agc_slope;
    }

    params_lock();
    params.agc_slope = limit(params.agc_slope + d, 0, 10);
    params_unlock(&params.dirty.agc_slope);

    radio_lock();
    x6100_control_agc_slope_set(params.agc_slope);
    radio_unlock();

    return params.agc_slope;
}

void radio_set_ptt(bool tx) {
    radio_lock();
    x6100_control_ptt_set(tx);
    radio_unlock();
}

void radio_set_modem(bool tx) {
    radio_lock();
    x6100_control_modem_set(tx);
    radio_unlock();
}

int16_t radio_change_rit(int16_t d) {
    if (d == 0) {
        return params.rit;
    }

    params_lock();
    params.rit = limit(align_int(params.rit + d * 10, 10), -1500, +1500);
    params_unlock(&params.dirty.rit);

    radio_lock();
    x6100_control_cmd(x6100_rit, params.rit);
    radio_unlock();

    return params.rit;
}

int16_t radio_change_xit(int16_t d) {
    if (d == 0) {
        return params.xit;
    }

    params_lock();
    params.xit = limit(align_int(params.xit + d * 10, 10), -1500, +1500);
    params_unlock(&params.dirty.xit);

    radio_lock();
    x6100_control_cmd(x6100_xit, params.xit);
    radio_unlock();

    return params.xit;
}

void radio_set_line_in(uint8_t d) {
    params_lock();
    params.line_in = d;
    params_unlock(&params.dirty.line_in);

    radio_lock();
    x6100_control_linein_set(d);
    radio_unlock();
}

void radio_set_line_out(uint8_t d) {
    params_lock();
    params.line_out = d;
    params_unlock(&params.dirty.line_out);

    radio_lock();
    x6100_control_lineout_set(d);
    radio_unlock();
}

void radio_set_morse_key(bool on) {
    x6100_gpio_set(x6100_pin_morse_key, on ? 0 : 1);
}
