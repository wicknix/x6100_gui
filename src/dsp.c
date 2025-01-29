/*
 *  SPDX-License-Identifier: LGPL-2.1-or-later
 *
 *  Xiegu X6100 LVGL GUI
 *
 *  Copyright (c) 2022-2023 Belousov Oleg aka R1CBU
 */

#include "dsp.h"

#include "cw.h"
#include "util.h"
#include "cfg/subjects.h"
#include "audio.h"
#include "cfg/cfg.h"
#include "dialog_msg_voice.h"
#include "meter.h"
#include "radio.h"
#include "recorder.h"
#include "rtty.h"
#include "spectrum.h"
#include "waterfall.h"

#include <math.h>
#include <pthread.h>
#include <stdlib.h>


#define ANF_DECIM_FACTOR 8 // 100000 -> 12500
#define ANF_STEP 50 // Hz
#define ANF_NFFT 100000 / ANF_DECIM_FACTOR / ANF_STEP
#define ANF_INTERVAL_MS 500
#define ANF_HIST_LEN 3

static iirfilt_cccf dc_block;

static pthread_mutex_t spectrum_mux = PTHREAD_MUTEX_INITIALIZER;

static uint8_t       spectrum_factor = 1;
static firdecim_crcf spectrum_decim_rx;
static firdecim_crcf spectrum_decim_tx;

static spgramcf spectrum_sg_rx;
static spgramcf spectrum_sg_tx;
static float    spectrum_psd[SPECTRUM_NFFT];
static float    spectrum_psd_filtered[SPECTRUM_NFFT];
static float    spectrum_beta   = 0.7f;
static uint8_t  spectrum_fps_ms = (1000 / 15);
static uint64_t spectrum_time;
static cfloat  *spectrum_dec_buf;

static spgramcf waterfall_sg_rx;
static spgramcf waterfall_sg_tx;
static float    waterfall_psd[WATERFALL_NFFT];
static uint8_t  waterfall_fps_ms = (1000 / 25);
static uint64_t waterfall_time;

static bool          anf_enabled = true;
static firdecim_crcf anf_decim;
static cfloat       *anf_dec_buf;
static spgramcf      anf_sg;
static float        *anf_psd;
static uint64_t      anf_time;
static int16_t       anf_freq_hist[ANF_HIST_LEN];
static int16_t       anf_freq_hist_pos;
static Subject      *anf_freq;

static cfloat buf_filtered[RADIO_SAMPLES];

static uint8_t psd_delay;
static uint8_t min_max_delay;

static firhilbf audio_hilb;
static cfloat  *audio;

static bool ready = false;

static int32_t filter_from = 0;
static int32_t filter_to   = 3000;
static x6100_mode_t cur_mode;

static void dsp_update_min_max(float *data_buf, uint16_t size);
static void setup_spectrum_spgram();
static void on_zoom_change(Subject *subj, void *user_data);
static void on_real_filter_from_change(Subject *subj, void *user_data);
static void on_real_filter_to_change(Subject *subj, void *user_data);
static void on_cur_mode_change(Subject *subj, void *user_data);

/* * */

static void on_anf_update(Subject *subj, void *user_data) {
    int32_t new_val = subject_get_int(subj);
    if (new_val == 0) {
        new_val = 3000;
    }
    printf("New ANF freq: %i\n", subject_get_int(subj));
    subject_set_int(cfg.dnf_center.val, new_val);
}

void dsp_init() {
    dc_block = iirfilt_cccf_create_dc_blocker(0.005f);

    setup_spectrum_spgram();

    waterfall_sg_rx = spgramcf_create(WATERFALL_NFFT, LIQUID_WINDOW_HANN, RADIO_SAMPLES, RADIO_SAMPLES);
    spgramcf_set_alpha(waterfall_sg_rx, 0.2f);
    waterfall_sg_tx = spgramcf_create(WATERFALL_NFFT, LIQUID_WINDOW_HANN, RADIO_SAMPLES, RADIO_SAMPLES);
    spgramcf_set_alpha(waterfall_sg_tx, 0.2f);

    spectrum_time  = get_time();
    waterfall_time = get_time();

    // Auto notch filter
    anf_freq = subject_create_int(3000);
    subject_add_observer(anf_freq, on_anf_update, NULL);
    anf_decim = firdecim_crcf_create_kaiser(ANF_DECIM_FACTOR, 8, 60.0f);
    firdecim_crcf_set_scale(anf_decim, 1.0f/(float)ANF_DECIM_FACTOR);
    anf_dec_buf = (cfloat *)malloc(RADIO_SAMPLES * sizeof(cfloat) / ANF_DECIM_FACTOR);
    anf_sg = spgramcf_create(ANF_NFFT, LIQUID_WINDOW_HANN, RADIO_SAMPLES / ANF_DECIM_FACTOR, RADIO_SAMPLES / ANF_DECIM_FACTOR);
    // spgramcf_set_alpha(anf_sg, 0.3f);
    anf_psd = (float *)malloc(ANF_NFFT * sizeof(float));
    anf_time = get_time();

    psd_delay = 4;

    audio      = (cfloat *)malloc(AUDIO_CAPTURE_RATE * sizeof(cfloat));
    audio_hilb = firhilbf_create(7, 60.0f);

    subject_add_observer_and_call(cfg_cur.zoom, on_zoom_change, NULL);
    subject_add_observer_and_call(cfg_cur.filter.real.from, on_real_filter_from_change, NULL);
    subject_add_observer_and_call(cfg_cur.filter.real.to, on_real_filter_to_change, NULL);
    subject_add_observer_and_call(cfg_cur.mode, on_cur_mode_change, NULL);
    ready = true;
}

void dsp_reset() {
    psd_delay = 4;

    iirfilt_cccf_reset(dc_block);
    spgramcf_reset(spectrum_sg_rx);
    spgramcf_reset(waterfall_sg_rx);
}

static void process_samples(cfloat *buf_samples, uint16_t size, firdecim_crcf sp_decim, spgramcf sp_sg,
                            spgramcf wf_sg, bool tx) {
    iirfilt_cccf_execute_block(dc_block, buf_samples, size, buf_filtered);
    // Swap I and Q
    for (size_t i = 0; i < size; i++) {
        buf_filtered[i] = CMPLXF(cimagf(buf_filtered[i]), crealf(buf_filtered[i]));
    }

    if (spectrum_factor > 1) {
        firdecim_crcf_execute_block(sp_decim, buf_filtered, size / spectrum_factor, spectrum_dec_buf);
        spgramcf_write(sp_sg, spectrum_dec_buf, size / spectrum_factor);
    } else {
        spgramcf_write(sp_sg, buf_filtered, size);
    }

    spgramcf_write(wf_sg, buf_filtered, size);

    // if anf enabled and not tx
    if (!tx && anf_enabled) {
        size_t decim_size = size / ANF_DECIM_FACTOR;
        firdecim_crcf_execute_block(anf_decim, buf_filtered, decim_size, anf_dec_buf);
        spgramcf_write(anf_sg, anf_dec_buf, decim_size);
    }
}

static bool update_spectrum(spgramcf sp_sg, uint64_t now, bool tx) {
    if ((now - spectrum_time > spectrum_fps_ms) && (!psd_delay)) {
        spgramcf_get_psd(sp_sg, spectrum_psd);
        liquid_vectorf_addscalar(spectrum_psd, SPECTRUM_NFFT, -30.0f, spectrum_psd);
        // Decrease beta for high zoom
        float new_beta = powf(spectrum_beta, ((float)spectrum_factor - 1.0f) / 2.0f + 1.0f);
        lpf_block(spectrum_psd_filtered, spectrum_psd, new_beta, SPECTRUM_NFFT);
        spectrum_data(spectrum_psd_filtered, SPECTRUM_NFFT, tx);
        spectrum_time = now;
        return true;
    }
    return false;
}

static bool update_waterfall(spgramcf wf_sg, uint64_t now, bool tx) {
    if ((now - waterfall_time > waterfall_fps_ms) && (!psd_delay)) {
        spgramcf_get_psd(wf_sg, waterfall_psd);
        liquid_vectorf_addscalar(waterfall_psd, WATERFALL_NFFT, -30.0f, waterfall_psd);
        waterfall_data(waterfall_psd, WATERFALL_NFFT, tx);
        waterfall_time = now;
        return true;
    }
    return false;
}

static void update_s_meter() {
    if (dialog_msg_voice_get_state() != MSG_VOICE_RECORD) {
        int32_t from, to, center;

        center = WATERFALL_NFFT / 2;
        from = center + filter_from * WATERFALL_NFFT / 100000;
        to = center + filter_to * WATERFALL_NFFT / 100000;

        int16_t peak_db = -121;

        for (int32_t i = from; i <= to; i++)
            if (waterfall_psd[i] > peak_db)
                peak_db = waterfall_psd[i];

        meter_update(peak_db, 0.8f);
    }
}

static bool update_anf(uint64_t now) {
    static float avg[3000 / ANF_STEP];
    if ((now - anf_time > ANF_INTERVAL_MS) && (!psd_delay)) {
        bool lsb = (cur_mode == x6100_mode_lsb);
        spgramcf_get_psd(anf_sg, anf_psd);
        float max = -INFINITY;
        float min = INFINITY;
        float mean = 0;
        size_t max_pos = 0;

        // Search for peak
        size_t center = ANF_NFFT / 2;
        size_t start = center + filter_from / ANF_STEP;
        size_t stop = center + filter_to / ANF_STEP;
        for (size_t i = start; i < stop; i++){
            if (max < anf_psd[i]) {
                max = anf_psd[i];
                max_pos = i;
            }
            if (min > anf_psd[i]) {
                min = anf_psd[i];
            }
            mean += anf_psd[i];
        }
        // Average
        for (size_t i = 0; i < 3000 / ANF_STEP; i++) {
            if (avg[i] < S_MIN - 6.0f) {
                avg[i] = S_MIN;
            } else {
                avg[i] = avg[i] - 6.0f;
            }
        }
        int16_t f_pos = max_pos - center;
        if (f_pos < 0) {
            f_pos = - f_pos;
        }
        // TODO: add +- 1
        avg[f_pos] += max;

        liquid_vectorf_mulscalar(avg, 3000 / ANF_STEP, 0.5f, avg);


        mean -= 3 * max;
        mean /= (stop - start - 3);
        // Adjust pos for USB
        if (!lsb) {
            max_pos--;
        }
        float peak_freq = ((float)max_pos - center) * ANF_STEP;
        printf("ptp: %f, above_mean: %f\n", max - min, max - mean);
        if (max - mean > 6.0f){
            anf_freq_hist[anf_freq_hist_pos] = roundf((peak_freq + ANF_STEP / 2) / 50) * 50;
        } else {
            anf_freq_hist[anf_freq_hist_pos] = 0;
        }
        anf_freq_hist_pos = (anf_freq_hist_pos + 1) % ANF_HIST_LEN;
        // Check all history contains same freq
        bool same_freq_val = true;
        for (size_t i = 1; i < ANF_HIST_LEN; i++) {
            if (anf_freq_hist[i] != anf_freq_hist[0]) {
                same_freq_val = false;
                break;
            }
        }
        if (same_freq_val) {
            int16_t new_freq = anf_freq_hist[0];
            if (lsb) {
                new_freq = - new_freq;
            }
            subject_set_int(anf_freq, new_freq);
        }

        anf_time = now;
        spgramcf_reset(anf_sg);
        return true;
    }
    return false;
}

void dsp_samples(cfloat *buf_samples, uint16_t size, bool tx) {
    firdecim_crcf sp_decim;
    spgramcf      sp_sg, wf_sg;
    uint64_t      now = get_time();

    if (psd_delay) {
        psd_delay--;
    }

    pthread_mutex_lock(&spectrum_mux);
    if (tx) {
        sp_decim = spectrum_decim_tx;
        sp_sg    = spectrum_sg_tx;
        wf_sg    = waterfall_sg_tx;
    } else {
        sp_decim = spectrum_decim_rx;
        sp_sg    = spectrum_sg_rx;
        wf_sg    = waterfall_sg_rx;
    }
    process_samples(buf_samples, size, sp_decim, sp_sg, wf_sg, tx);
    update_spectrum(sp_sg, now, tx);
    pthread_mutex_unlock(&spectrum_mux);
    if (update_waterfall(wf_sg, now, tx)) {
        update_s_meter();
        // TODO: skip on disabled auto min/max
        if (!tx) {
            dsp_update_min_max(waterfall_psd, WATERFALL_NFFT);
        } else {
            min_max_delay = 2;
        }
    }
    if (!tx) {
        update_anf(now);
    }
}

static void on_zoom_change(Subject *subj, void *user_data) {
    int32_t x = subject_get_int(subj);
    if (x == spectrum_factor)
        return;

    pthread_mutex_lock(&spectrum_mux);

    spectrum_factor = x;

    setup_spectrum_spgram();

    if (spectrum_decim_rx) {
        firdecim_crcf_destroy(spectrum_decim_rx);
        spectrum_decim_rx = NULL;
    }

    if (spectrum_decim_tx) {
        firdecim_crcf_destroy(spectrum_decim_tx);
        spectrum_decim_tx = NULL;
    }

    if (spectrum_dec_buf) {
        free(spectrum_dec_buf);
        spectrum_dec_buf = NULL;
    }

    if (spectrum_factor > 1) {
        spectrum_decim_rx = firdecim_crcf_create_kaiser(spectrum_factor, 8, 60.0f);
        firdecim_crcf_set_scale(spectrum_decim_rx, sqrt(1.0f / (float)spectrum_factor));
        spectrum_decim_tx = firdecim_crcf_create_kaiser(spectrum_factor, 8, 60.0f);
        firdecim_crcf_set_scale(spectrum_decim_tx, sqrt(1.0f / (float)spectrum_factor));
        spectrum_dec_buf = (cfloat *)malloc(RADIO_SAMPLES * sizeof(cfloat) / spectrum_factor);
    }

    spgramcf_reset(spectrum_sg_rx);
    spgramcf_reset(spectrum_sg_tx);

    for (uint16_t i = 0; i < SPECTRUM_NFFT; i++)
        spectrum_psd_filtered[i] = S_MIN;

    pthread_mutex_unlock(&spectrum_mux);
}

static void on_real_filter_from_change(Subject *subj, void *user_data) {
    filter_from = subject_get_int(subj);
}

static void on_real_filter_to_change(Subject *subj, void *user_data) {
    filter_to = subject_get_int(subj);
}

static void on_cur_mode_change(Subject *subj, void *user_data) {
    cur_mode = (x6100_mode_t)subject_get_int(subj);
    switch (cur_mode) {
        case x6100_mode_lsb:
        case x6100_mode_usb:
            anf_enabled = true;
            break;
        default:
            anf_enabled = false;
            break;
    }
}

float dsp_get_spectrum_beta() {
    return spectrum_beta;
}

void dsp_set_spectrum_beta(float x) {
    spectrum_beta = x;
}

void dsp_put_audio_samples(size_t nsamples, int16_t *samples) {
    if (!ready) {
        return;
    }

    if (dialog_msg_voice_get_state() == MSG_VOICE_RECORD) {
        dialog_msg_voice_put_audio_samples(nsamples, samples);
        return;
    }

    if (recorder_is_on()) {
        recorder_put_audio_samples(nsamples, samples);
    }

    for (uint16_t i = 0; i < nsamples; i++)
        firhilbf_r2c_execute(audio_hilb, samples[i] / 32768.0f, &audio[i]);

    if (rtty_get_state() == RTTY_RX) {
        rtty_put_audio_samples(nsamples, audio);
    } else if (cur_mode == x6100_mode_cw || cur_mode == x6100_mode_cwr) {
        cw_put_audio_samples(nsamples, audio);
    } else {
        dialog_audio_samples(nsamples, audio);
    }
}

static int compare_fft(const void *p1, const void *p2) {
    float *i1 = (float *)p1;
    float *i2 = (float *)p2;

    return (*i1 < *i2) ? -1 : 1;
}

static void dsp_update_min_max(float *data_buf, uint16_t size) {
    if (min_max_delay) {
        min_max_delay--;
        return;
    }
    qsort(data_buf, size, sizeof(float), compare_fft);
    uint16_t min_nth = 15;
    uint16_t max_nth = 10;

    float min = data_buf[min_nth];
    // float max = data_buf[size - max_nth - 1];


    if (min < S_MIN) {
        min = S_MIN;
    } else if (min > S8) {
        min = S8;
    }
    float max = min + 48;

    spectrum_update_min(min);
    waterfall_update_min(min);

    spectrum_update_max(max);
    waterfall_update_max(max);
}

static void setup_spectrum_spgram() {
    if (spectrum_sg_rx) {
        spgramcf_destroy(spectrum_sg_rx);
    }
    if (spectrum_sg_tx) {
        spgramcf_destroy(spectrum_sg_tx);
    }
    uint16_t window = RADIO_SAMPLES / spectrum_factor;
    spectrum_sg_rx = spgramcf_create(SPECTRUM_NFFT, LIQUID_WINDOW_HANN, window, window);
    spgramcf_set_alpha(spectrum_sg_rx, 0.4f);
    spectrum_sg_tx = spgramcf_create(SPECTRUM_NFFT, LIQUID_WINDOW_HANN, window, window);
    spgramcf_set_alpha(spectrum_sg_tx, 0.4f);
}
