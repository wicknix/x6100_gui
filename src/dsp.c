/*
 *  SPDX-License-Identifier: LGPL-2.1-or-later
 *
 *  Xiegu X6100 LVGL GUI
 *
 *  Copyright (c) 2022-2023 Belousov Oleg aka R1CBU
 */

#include <stdlib.h>
#include <stdbool.h>
#include <pthread.h>
#include <math.h>

#include "dsp.h"
#include "spectrum.h"
#include "waterfall.h"
#include "util.h"
#include "radio.h"
#include "meter.h"
#include "audio.h"
#include "cw.h"
#include "rtty.h"
#include "dialog_ft8.h"
#include "dialog_msg_voice.h"
#include "recorder.h"

static int32_t          nfft = 800;
static iirfilt_cccf     dc_block;

static pthread_mutex_t  spectrum_mux = PTHREAD_MUTEX_INITIALIZER;

static uint8_t          spectrum_factor = 1;
static firdecim_crcf    spectrum_decim_rx;
static firdecim_crcf    spectrum_decim_tx;

static spgramcf         spectrum_sg_rx;
static spgramcf         spectrum_sg_tx;
static float            *spectrum_psd;
static float            *spectrum_psd_filtered;
static float            spectrum_beta = 0.7f;
static uint8_t          spectrum_fps_ms = (1000 / 15);
static uint64_t         spectrum_time;
static float complex    *spectrum_dec_buf;

static spgramcf         waterfall_sg_rx;
static spgramcf         waterfall_sg_tx;
static float            *waterfall_psd;
static uint8_t          waterfall_fps_ms = (1000 / 25);
static uint64_t         waterfall_time;

static float complex    buf_filtered[RADIO_SAMPLES];

static uint8_t          psd_delay;
static uint8_t          min_max_delay;

static firhilbf         audio_hilb;
static float complex    *audio;

static bool             ready = false;

static void dsp_update_min_max(float *data_buf, uint16_t size);

/* * */

void dsp_init(uint8_t factor) {
    dc_block = iirfilt_cccf_create_dc_blocker(0.005f);

    spectrum_sg_rx = spgramcf_create(nfft, LIQUID_WINDOW_HANN, nfft, nfft / 4);
    spgramcf_set_alpha(spectrum_sg_rx, 0.2f);
    spectrum_sg_tx = spgramcf_create(nfft, LIQUID_WINDOW_HANN, nfft, nfft / 4);
    spgramcf_set_alpha(spectrum_sg_tx, 0.2f);

    spectrum_psd = malloc(nfft * sizeof(float));
    spectrum_psd_filtered = malloc(nfft * sizeof(float));
    dsp_set_spectrum_factor(factor);

    waterfall_sg_rx = spgramcf_create(nfft, LIQUID_WINDOW_HANN, nfft, nfft / 4);
    spgramcf_set_alpha(waterfall_sg_rx, 0.2f);
    waterfall_sg_tx = spgramcf_create(nfft, LIQUID_WINDOW_HANN, nfft, nfft / 4);
    spgramcf_set_alpha(waterfall_sg_tx, 0.2f);

    waterfall_psd = malloc(nfft * sizeof(float));

    spectrum_time = get_time();
    waterfall_time = get_time();

    psd_delay = 4;

    audio = (float complex *) malloc(AUDIO_CAPTURE_RATE * sizeof(float complex));
    audio_hilb = firhilbf_create(7, 60.0f);

    ready = true;
}

void dsp_reset() {
    psd_delay = 4;

    iirfilt_cccf_reset(dc_block);
    spgramcf_reset(spectrum_sg_rx);
    spgramcf_reset(waterfall_sg_rx);
}

static void process_samples(
    float complex *buf_samples, uint16_t size,
    firdecim_crcf sp_decim, spgramcf sp_sg,
    spgramcf wf_sg
) {
    iirfilt_cccf_execute_block(dc_block, buf_samples, size, buf_filtered);

    if (spectrum_factor > 1) {
        firdecim_crcf_execute_block(sp_decim, buf_filtered, size / spectrum_factor, spectrum_dec_buf);
        spgramcf_write(sp_sg, spectrum_dec_buf, size / spectrum_factor);
    } else {
        spgramcf_write(sp_sg, buf_filtered, size);
    }

    spgramcf_write(wf_sg, buf_filtered, size);
}

static bool update_spectrum(spgramcf sp_sg, uint64_t now, bool tx) {
    if ((now - spectrum_time > spectrum_fps_ms) && (!psd_delay)) {
        spgramcf_get_psd(sp_sg, spectrum_psd);
        liquid_vectorf_addscalar(spectrum_psd, nfft, -30.0f, spectrum_psd);

        lpf_block(spectrum_psd_filtered, spectrum_psd, spectrum_beta, nfft);
        spectrum_data(spectrum_psd_filtered, nfft, tx);
        spectrum_time = now;
        return true;
    }
    return false;
}

static bool update_waterfall(spgramcf wf_sg, uint64_t now, bool tx) {
    if ((now - waterfall_time > waterfall_fps_ms) && (!psd_delay)) {
        spgramcf_get_psd(wf_sg, waterfall_psd);
        liquid_vectorf_addscalar(waterfall_psd, nfft, -30.0f, waterfall_psd);
        waterfall_data(waterfall_psd, nfft, tx);
        waterfall_time = now;
        return true;
    }
    return false;
}

static void update_s_meter(){
    if (dialog_msg_voice_get_state() != MSG_VOICE_RECORD) {
        int32_t filter_from, filter_to;
        int32_t from, to;

        radio_filter_get(&filter_from, &filter_to);

        from = nfft / 2;
        from -= filter_to * nfft / 100000;

        to = nfft / 2;
        to -= filter_from * nfft / 100000;

        int16_t peak_db = -121;

        for (int32_t i = from; i <= to; i++)
            if (waterfall_psd[i] > peak_db)
                peak_db = waterfall_psd[i];

        meter_update(peak_db, 0.8f);
    }
}

void dsp_samples(float complex *buf_samples, uint16_t size, bool tx) {
    firdecim_crcf sp_decim;
    spgramcf sp_sg, wf_sg;
    uint64_t now = get_time();

    if (psd_delay) {
        psd_delay--;
    }

    pthread_mutex_lock(&spectrum_mux);
    if (tx) {
        sp_decim = spectrum_decim_tx;
        sp_sg = spectrum_sg_tx;
        wf_sg = waterfall_sg_tx;
    } else {
        sp_decim = spectrum_decim_rx;
        sp_sg = spectrum_sg_rx;
        wf_sg = waterfall_sg_rx;
    }
    process_samples(buf_samples, size, sp_decim, sp_sg, wf_sg);
    pthread_mutex_unlock(&spectrum_mux);
    update_spectrum(sp_sg, now, tx);
    if (update_waterfall(wf_sg, now, tx)) {
        update_s_meter();
        // TODO: skip on disabled auto min/max
        if (!tx) {
            dsp_update_min_max(waterfall_psd, nfft);
        } else {
            min_max_delay = 2;
        }
    }
}

void dsp_set_spectrum_factor(uint8_t x) {
    if (x == spectrum_factor)
        return;

    pthread_mutex_lock(&spectrum_mux);

    spectrum_factor = x;

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
        spectrum_decim_rx = firdecim_crcf_create_kaiser(spectrum_factor, 16, 40.0f);
        firdecim_crcf_set_scale(spectrum_decim_rx, sqrt(1.0f/(float)spectrum_factor));
        spectrum_decim_tx = firdecim_crcf_create_kaiser(spectrum_factor, 16, 40.0f);
        firdecim_crcf_set_scale(spectrum_decim_tx, sqrt(1.0f/(float)spectrum_factor));
        spectrum_dec_buf = (float complex *) malloc(RADIO_SAMPLES * sizeof(float complex) / spectrum_factor);
    }

    spgramcf_reset(spectrum_sg_rx);
    spgramcf_reset(spectrum_sg_tx);

    for (uint16_t i = 0; i < nfft; i++)
        spectrum_psd_filtered[i] = S_MIN;

    pthread_mutex_unlock(&spectrum_mux);
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

    x6100_mode_t    mode = radio_current_mode();

    if (rtty_get_state() == RTTY_RX) {
        rtty_put_audio_samples(nsamples, audio);
    } else if (mode == x6100_mode_cw || mode == x6100_mode_cwr) {
        cw_put_audio_samples(nsamples, audio);
    } else {
        dialog_audio_samples(nsamples, audio);
    }
}

static int compare_fft(const void *p1, const void *p2) {
    float *i1 = (float *) p1;
    float *i2 = (float *) p2;

    return (*i1 < *i2) ? -1 : 1;
}

static void dsp_update_min_max(float *data_buf, uint16_t size) {
    if (min_max_delay) {
        min_max_delay--;
        return;
    }
    qsort(data_buf, size, sizeof(float), compare_fft);
    uint16_t    min_nth = 15;
    uint16_t    max_nth = 10;

    float       min = data_buf[min_nth];
    float       max = data_buf[size - max_nth - 1];


    if (max > S9_40) {
        max = S9_40;
    } else if (max < S8) {
        max = S8;
    }

    if (min > S7) {
        min = S7;
    } else if (min < S_MIN) {
        min = S_MIN;
    }
    spectrum_update_min(min);
    waterfall_update_min(min);

    spectrum_update_max(max);
    waterfall_update_max(max);
}
