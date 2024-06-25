/*
 *  SPDX-License-Identifier: LGPL-2.1-or-later
 *
 *  Xiegu X6100 LVGL GUI
 *
 *  Copyright (c) 2022-2023 Belousov Oleg aka R1CBU
 */
#include "cw.h"

#include <math.h>
#include "lvgl/lvgl.h"
#include <pthread.h>

#include "audio.h"
#include "util.h"
#include "params/params.h"
#include "cw_decoder.h"
#include "pannel.h"
#include "meter.h"
#include "util.h"
#include "cw_tune_ui.h"

typedef struct {
    uint16_t    n;
    float       val;
} fft_item_t;

#define NUM_STAGES      6
#define DECIM_FACTOR    (1LL << NUM_STAGES)
#define FFT             128
#define RMS_DELAY       2

static bool             ready = false;

static fft_item_t       fft_items[FFT];

static dds_cccf         ds_dec;
static wrms_t           wrms;
static cbuffercf        input_cbuf;
static cbuffercf        rms_cbuf;

static cbuffercf        fft_cbuf;
static fftplan          fft_plan;
static float complex    *fft_time;
static float complex    *fft_freq;
static float            audio_psd_squared[FFT];

static float            peak_filtered;
static float            noise_filtered;
static float            threshold_pulse;
static float            threshold_silence;
static bool             peak_on = false;

static pthread_mutex_t  cw_mutex = PTHREAD_MUTEX_INITIALIZER;

static void dds_dec_init();

void cw_init() {
    input_cbuf = cbuffercf_create(10000);
    dds_dec_init();
    wrms = wrms_create(16, RMS_DELAY);
    rms_cbuf = cbuffercf_create(4000 / 8 * 2);
    fft_cbuf = cbuffercf_create(4000 / 8 * 2);
    fft_time = malloc(FFT*(sizeof(float complex)));
    fft_freq = malloc(FFT*(sizeof(float complex)));

    fft_plan = fft_create_plan(FFT, fft_time, fft_freq, LIQUID_FFT_FORWARD, 0);

    peak_filtered = S_MIN;
    noise_filtered = S_MIN;

    ready = true;
}

void cw_notify_change_key_tone() {
    dds_dec_init();
}

static void dds_dec_init() {
    pthread_mutex_lock(&cw_mutex);
    if (ds_dec != NULL) {
        dds_cccf_destroy(ds_dec);
    }
    float rel_freq = (float) params.key_tone / AUDIO_CAPTURE_RATE;
    float bw = (float) MAX_CW_BW / AUDIO_CAPTURE_RATE;
    ds_dec = dds_cccf_create(NUM_STAGES, rel_freq, bw, 60.0f);
    pthread_mutex_unlock(&cw_mutex);
}

static int compare_fft_items(const void *p1, const void *p2) {
    fft_item_t *i1 = (fft_item_t *) p1;
    fft_item_t *i2 = (fft_item_t *) p2;

    return (i1->val > i2->val) ? -1 : 1;
}

static void update_thresholds() {
    float noise = 0, noise_db;
    for (uint16_t n = 0; n < FFT; n++) {
        fft_items[n].n = n;
        fft_items[n].val = audio_psd_squared[n];
    }
    qsort(&fft_items, FFT, sizeof(fft_item_t), compare_fft_items);

    // BW for 30 WPM
    uint16_t peak_width = 30 * 4 * FFT * DECIM_FACTOR / AUDIO_CAPTURE_RATE;

    for (uint16_t i = peak_width; i < FFT; i++) {
        noise += fft_items[i].val;
    }
    // Perhaps, should divide to sqrt(FFT)
    noise = sqrtf(noise / (FFT - peak_width)) / sqrt((float)FFT - peak_width);
    noise_db = 10.0f * log10f(noise);
    lpf(&noise_filtered, LV_MIN(-3.0f, noise_db), params.cw_decoder_noise_beta, S_MIN);
    threshold_pulse = params.cw_decoder_snr + noise_filtered;
    threshold_silence = threshold_pulse - params.cw_decoder_snr_gist;
}

static void update_peak_freq(float freq) {
    if (peak_on) {
        cw_tune_set_freq(freq);
    }
}

static bool decode(float rms_db) {
    if (peak_on) {
        if (rms_db < threshold_silence) {
            peak_on = false;
        }
    } else {
        if (rms_db > threshold_pulse) {
            peak_on = true;
        }
    }
    return peak_on;
}


void cw_put_audio_samples(unsigned int n, float complex *samples) {
    if (!ready) {
        return;
    }
    if ((!params.cw_decoder) && (!params.cw_tune)) {
        return;
    }
    float complex sample;
    float rms, rms_db, peak_freq;
    size_t max_pos;

    // fill input buffer
    cbuffercf_write(input_cbuf, samples, n);

    // Downsample and freq shift
    const size_t desired_num = (1<<NUM_STAGES);
    while (cbuffercf_size(input_cbuf) >= desired_num) {
        unsigned int n;
        float complex *buf;
        cbuffercf_read(input_cbuf, desired_num, &buf, &n);
        pthread_mutex_lock(&cw_mutex);
        dds_cccf_decim_execute(ds_dec, buf, &sample);
        pthread_mutex_unlock(&cw_mutex);
        cbuffercf_release(input_cbuf, desired_num);
        cbuffercf_push(rms_cbuf, sample);
        cbuffercf_push(fft_cbuf, sample);
    }
    // Process FFT
    while (cbuffercf_size(fft_cbuf) >= FFT) {
        for (size_t i = 0; i < FFT; i++) {
            cbuffercf_pop(fft_cbuf, &fft_time[i]);
        }
        fft_execute(fft_plan);

        for (size_t i = 0; i < FFT; i++) {
            audio_psd_squared[i] = crealf(fft_freq[i] * conjf(fft_freq[i]));
        }
        update_thresholds();

        max_pos = argmax(audio_psd_squared, FFT);
        // Fix fft order
        peak_freq = (((float) (FFT - (max_pos + FFT / 2) % FFT) / FFT) - 0.5f) * ((float) AUDIO_CAPTURE_RATE / DECIM_FACTOR);
        update_peak_freq(peak_freq);
    }
    // Process RMS
    while (cbuffercf_size(rms_cbuf)) {
        cbuffercf_pop(rms_cbuf, &sample);
        wrms_push(wrms, sample);
        if (wrms_ready(wrms)) {
            rms = wrms_get_val(wrms);
            rms_db = 10 * log10f(rms);
            cw_decoder_signal(decode(rms_db), 1000.0f / AUDIO_CAPTURE_RATE * DECIM_FACTOR * wrms_delay(wrms));
        }
    }
}

bool cw_change_decoder(int16_t df) {
    if (df == 0) {
        return params.cw_decoder;
    }

    params_lock();
    params.cw_decoder = !params.cw_decoder;
    params_unlock(&params.dirty.cw_decoder);

    pannel_visible();

    return params.cw_decoder;
}

float cw_change_snr(int16_t df) {
    if (df == 0) {
        return params.cw_decoder_snr;
    }

    float x = params.cw_decoder_snr + df * 0.1f;

    if (x < 7.0f) {
        x = 7.0f;
    } else if (x > 30.0f) {
        x = 30.0f;
    }

    params_lock();
    params.cw_decoder_snr = x;
    params_unlock(&params.dirty.cw_decoder_snr);

    return params.cw_decoder_snr;
}

float cw_change_peak_beta(int16_t df) {
    if (df == 0) {
        return params.cw_decoder_peak_beta;
    }

    float x = params.cw_decoder_peak_beta + df * 0.01f;

    if (x < 0.1f) {
        x = 0.1f;
    } else if (x > 0.95f) {
        x = 0.95f;
    }

    params_lock();
    params.cw_decoder_peak_beta = x;
    params_unlock(&params.dirty.cw_decoder_peak_beta);

    return params.cw_decoder_peak_beta;
}

float cw_change_noise_beta(int16_t df) {
    if (df == 0) {
        return params.cw_decoder_noise_beta;
    }

    float x = params.cw_decoder_noise_beta + df * 0.01f;

    if (x < 0.1f) {
        x = 0.1f;
    } else if (x > 0.95f) {
        x = 0.95f;
    }

    params_lock();
    params.cw_decoder_noise_beta = x;
    params_unlock(&params.dirty.cw_decoder_noise_beta);

    return params.cw_decoder_noise_beta;
}
