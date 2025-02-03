/*
 *  SPDX-License-Identifier: LGPL-2.1-or-later
 *
 *  Xiegu X6100 LVGL GUI
 *
 *  Copyright (c) 2022-2023 Belousov Oleg aka R1CBU
 */

#pragma once

#include "helpers.h"

#ifdef __cplusplus

#include "cfg/subjects.h"

extern "C" {
#endif

#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <liquid/liquid.h>

#ifdef __cplusplus
}
#endif

#define WATERFALL_NFFT  (RADIO_SAMPLES * 2)
#define SPECTRUM_NFFT   800

#ifdef __cplusplus

class ChunkedSpgram {
    size_t   nfft;
    size_t   chunk_size;
    size_t   buffer_size;
    windowcf buffer;
    fftplan  fft;
    cfloat  *buf_time;
    cfloat  *buf_freq;
    cfloat  *w;
    float   *psd;
    bool     accumulate     = true;
    float    alpha          = 1.0f;
    float    gamma          = 1.0f;
    size_t   num_transforms = 0;
    size_t   num_samples    = 0;

  public:
    ChunkedSpgram(size_t chunk_size, size_t nfft, size_t buffer_size=0);
    ~ChunkedSpgram();
    void set_alpha(float val);
    void clear();
    void reset();
    void execute_block(cfloat *block);
    void get_psd_mag(float *psd);
    void get_psd(float *psd);
};

class Anf {
    size_t              decim_factor;
    size_t              freq_bin;
    size_t              nfft;
    cfloat             *decim_buf;
    firdecim_crcf       decim;
    spgramcf            sg;
    float              *psd;
    uint64_t            last_ts;
    size_t              interval_ms;
    int32_t             freq_from = -3000;
    int32_t             freq_to   = 3000;
    static const size_t hist_size = 3;
    int16_t             freq_hist[hist_size];
    size_t              hist_pos = 0;

  public:
    Anf(size_t decim_factor, size_t chunk_size, size_t nfft, size_t interval_ms, size_t freq_bin);
    void set_freq_from(int32_t freq);
    void set_freq_to(int32_t freq);
    void shift(int32_t freq_diff, bool lower_band);
    void reset();
    void execute_block(cfloat *block, size_t size);
    void update(uint64_t now, bool lower_band);

    SubjectT<int32_t> *notch_freq_subj;
};

extern "C" {
#endif

void dsp_init();
void dsp_samples(cfloat *buf_samples, uint16_t size, bool tx);
void dsp_reset();

float dsp_get_spectrum_beta();
void dsp_set_spectrum_beta(float x);

void dsp_put_audio_samples(size_t nsamples, int16_t *samples);
#ifdef __cplusplus
}
#endif
