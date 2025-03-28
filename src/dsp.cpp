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
#include "buttons.h"
#include "cfg/subjects.h"

#include <algorithm>
#include <numeric>

extern "C" {
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
}


#define ANF_DECIM_FACTOR 8 // 100000 -> 12500
#define ANF_STEP 25 // Hz
#define ANF_NFFT 100000 / ANF_DECIM_FACTOR / ANF_STEP
#define ANF_INTERVAL_MS 500
#define ANF_HIST_LEN 3

static iirfilt_cccf dc_block;

static pthread_mutex_t spectrum_mux = PTHREAD_MUTEX_INITIALIZER;

static uint8_t       spectrum_factor = 1;
static firdecim_crcf spectrum_decim_rx;
static firdecim_crcf spectrum_decim_tx;

static ChunkedSpgram *spectrum_sg_rx;
static ChunkedSpgram *spectrum_sg_tx;
static float          spectrum_psd[SPECTRUM_NFFT];
static float          spectrum_psd_filtered[SPECTRUM_NFFT];
static float          spectrum_beta   = 0.7f;
static uint8_t        spectrum_fps_ms = (1000 / 15);
static uint64_t       spectrum_time;
static cfloat         spectrum_dec_buf[SPECTRUM_NFFT / 2];

static ChunkedSpgram *waterfall_sg_rx;
static ChunkedSpgram *waterfall_sg_tx;
static float          waterfall_psd[WATERFALL_NFFT];
static uint8_t        waterfall_fps_ms = (1000 / 25);
static uint64_t       waterfall_time;

static Anf *anf;
static bool anf_enabled = true;

static cfloat buf_filtered[RADIO_SAMPLES];

static uint32_t cur_freq;
static uint8_t  psd_delay;
static uint8_t  min_max_delay;

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
static void update_dnf_enabled(Subject *subj, void *user_data);
static void on_cur_freq_change(Subject *subj, void *user_data);


/* Chunked spectrum periodogram class */

ChunkedSpgram::ChunkedSpgram(size_t chunk_size, size_t nfft, size_t buffer_size) {
    this->chunk_size = chunk_size;
    this->nfft = nfft;
    if (buffer_size > 0) {
        this->buffer_size = buffer_size;
    } else {
        this->buffer_size = nfft - nfft % chunk_size;
    }
    this->buffer = windowcf_create(this->buffer_size);
    this->buf_time = (cfloat *) calloc(sizeof(cfloat), nfft);
    this->buf_freq = (cfloat *) calloc(sizeof(cfloat), nfft);
    this->psd = (float *) calloc(sizeof(float), nfft);
    this->fft = fft_create_plan(nfft, buf_time, buf_freq, LIQUID_FFT_FORWARD, 0);
    this->w = (cfloat *) calloc(sizeof(cfloat), chunk_size);

    size_t i;
    for (i = 0; i < chunk_size; i++) {
        this->w[i] = liquid_kaiser(i, chunk_size, 5.0f);
        // this->w[i] = liquid_hann(i, chunk_size);
    }
    // scale by window magnitude
    float g = 0.0f;
    for (i=0; i<chunk_size; i++)
        g += std::norm(this->w[i]);
    g = 1.0f / sqrtf(g * nfft / chunk_size);

    // scale window and copy
    for (i=0; i<chunk_size; i++)
        this->w[i] *= g;

}

ChunkedSpgram::~ChunkedSpgram() {
    free(this->buf_time);
    free(this->buf_freq);
    free(this->psd);
    free(this->w);

    windowcf_destroy(buffer);
    fft_destroy_plan(fft);
}

void ChunkedSpgram::set_alpha(float val) {
    // validate input
    if (val != -1 && (val < 0.0f || val > 1.0f)) {
        printf("set_alpha(), alpha must be in {-1,[0,1]}");
        return;
    }

    // set accumulation flag appropriately
    accumulate = (val == -1.0f) ? true : false;

    if (accumulate) {
        this->alpha = 1.0f;
        this->gamma = 1.0f;
    } else {
        this->alpha = val;
        this->gamma = 1.0f - val;
    }
}

void ChunkedSpgram::clear() {
    num_transforms = 0;
    num_samples = 0;
    for (size_t i = 0; i < nfft; i++) {
        psd[i] = 0.0f;
        buf_time[i] = 0.0f;
    }
}

void ChunkedSpgram::reset() {
    clear();
    windowcf_reset(buffer);
}

void ChunkedSpgram::execute_block(cfloat *chunk) {
    cfloat val;
    for (size_t i=0; i<chunk_size; i++) {
        val = chunk[i] * w[i];
        windowcf_push(buffer, val);
    }
    num_samples += chunk_size;
    if (num_samples >= buffer_size){}
    cfloat *rc;
    windowcf_read(buffer, &rc);
    memcpy(buf_time, rc, sizeof(cfloat) * buffer_size);
    fft_execute(fft);

    // accumulate output
    // TODO: vectorize this operation
    for (size_t i=0; i<nfft; i++) {
        float v = std::norm(buf_freq[i]);
        if (num_transforms == 0)
            psd[i] = v;
        else
            psd[i] = gamma*psd[i] + alpha*v;
    }
    num_transforms++;
}

void ChunkedSpgram::get_psd_mag(float *psd) {
    // compute magnitude (linear) and run FFT shift
    unsigned int i;
    unsigned int nfft_2 = nfft / 2;
    float scale = accumulate ? 1.0f / std::max((size_t)1, num_transforms) : 1.0f;
    for (i=0; i<nfft; i++) {
        unsigned int k = (i + nfft_2) % nfft;
        psd[i] = std::max((float)LIQUID_SPGRAM_PSD_MIN, this->psd[k]) * scale;
    }
    if (accumulate) {
        clear();
    }
}

void ChunkedSpgram::get_psd(float *psd) {
    // compute magnitude, linear
    get_psd_mag(psd);
    // convert to dB
    unsigned int i;
    for (i=0; i<nfft; i++)
        psd[i] = 10*log10f(psd[i]);
}

/* Anf class */

Anf::Anf(size_t decim_factor, size_t chunk_size, size_t nfft, size_t interval_ms, size_t freq_bin) {
    this->decim_factor = decim_factor;
    this->interval_ms = interval_ms;
    this->freq_bin = freq_bin;
    this->nfft = nfft;

    decim_buf = (cfloat *)calloc(chunk_size / decim_factor, sizeof(cfloat));
    psd = (float *)calloc(nfft, sizeof(float));

    decim = firdecim_crcf_create_kaiser(this->decim_factor, 8, 60.0f);
    firdecim_crcf_set_scale(decim, 1.0f/(float)this->decim_factor);
    sg = spgramcf_create(nfft, LIQUID_WINDOW_HANN, chunk_size / this->decim_factor, chunk_size / this->decim_factor);
    last_ts = get_time();
    notch_freq_subj = new SubjectT(0);
}

void Anf::set_freq_from(int32_t freq) {
    freq_from = freq;
}

void Anf::set_freq_to(int32_t freq) {
    freq_to = freq;
}

void Anf::shift(int32_t freq_diff, bool lower_band) {
    if (lower_band)
        freq_diff = -freq_diff;

    // Shift history
    for (size_t i = 0; i < hist_size; i++){
        freq_hist[i] -= freq_diff;
    }
    // Shift detected val
    int32_t notch_freq = notch_freq_subj->get();
    if (notch_freq > 0) {
        notch_freq -= freq_diff;
        notch_freq_subj->set(notch_freq);
    }
    firdecim_crcf_reset(decim);
    spgramcf_reset(sg);
}

void Anf::reset() {
    for (size_t i = 0; i < hist_size; i++){
        freq_hist[i] = 0;
    }
    notch_freq_subj->set(0);
    firdecim_crcf_reset(decim);
    spgramcf_reset(sg);
}

void Anf::execute_block(cfloat *block, size_t size) {
    size_t decim_size = size / decim_factor;
    firdecim_crcf_execute_block(decim, block, decim_size, decim_buf);
    spgramcf_write(sg, decim_buf, decim_size);
}

void Anf::update(uint64_t now, bool lower_band) {
    if ((now - last_ts > interval_ms) && (spgramcf_get_num_transforms(sg) > 5)) {
        spgramcf_get_psd(sg, psd);
        float max = -INFINITY;
        float min = INFINITY;
        float mean = 0;
        int32_t max_pos = 0;

        // Search for peak
        size_t center = nfft / 2;
        size_t start = center + freq_from / (int32_t)freq_bin;
        size_t stop = center + freq_to / (int32_t)freq_bin;
        for (size_t i = start; i < stop; i++){
            if (max < psd[i]) {
                max = psd[i];
                max_pos = i;
            }
            if (min > psd[i]) {
                min = psd[i];
            }
            mean += psd[i];
        }
        size_t peak_width = 150 / freq_bin;
        if (stop - start < peak_width) {
            peak_width = (stop - start) / 2;
        }
        mean -= peak_width * max;
        mean /= (stop - start - peak_width);
        // Adjust pos for USB
        if (!lower_band) {
            max_pos--;
        }
        int16_t peak_freq = (max_pos - center) * freq_bin;

        // Set threshold based on freq (6 db for 3000 Hz, ~12 db on 600 Hz)
        float threshold = 5000.0f / (peak_freq + 2000.0f) * 6.0f;
        if (max - mean > threshold){
            freq_hist[hist_pos] = peak_freq;
        } else {
            freq_hist[hist_pos] = 0;
        }
        hist_pos = (hist_pos + 1) % hist_size;

        // Inspect history
        int16_t mean_freq = 0;
        for (size_t i = 0; i < hist_size; i++) {
            mean_freq += freq_hist[i];
        }
        mean_freq /= (int16_t)hist_size;

        int16_t deviations = 0;
        for (size_t i = 0; i < hist_size; i++) {
            deviations += abs(mean_freq - freq_hist[i]);
        }
        deviations /= (int16_t)hist_size - 1;
        if (deviations < freq_bin) {
            int16_t new_freq = roundf((float)mean_freq / 50) * 50;
            if (lower_band) {
                new_freq = - new_freq;
            }
            notch_freq_subj->set(new_freq);
        }
        last_ts = now;
        spgramcf_reset(sg);
    }
}

/* * */

static void on_anf_update(Subject *subj, void *user_data) {
    if (!anf_enabled) {
        return;
    }
    int32_t new_val = subject_get_int(subj);
    if ((new_val <= 0) || (new_val > 3000)) {
        new_val = 3000;
    }
    subject_set_int(cfg.dnf_center.val, new_val);
}

void dsp_init() {
    dc_block = iirfilt_cccf_create_dc_blocker(0.005f);

    setup_spectrum_spgram();

    waterfall_sg_rx = new ChunkedSpgram(RADIO_SAMPLES, WATERFALL_NFFT);
    waterfall_sg_rx->set_alpha(0.8f);
    waterfall_sg_tx = new ChunkedSpgram(RADIO_SAMPLES, WATERFALL_NFFT);
    waterfall_sg_tx->set_alpha(0.8f);

    spectrum_time  = get_time();
    waterfall_time = get_time();

    anf = new Anf(ANF_DECIM_FACTOR, RADIO_SAMPLES, ANF_NFFT, ANF_INTERVAL_MS, ANF_STEP);
    anf->notch_freq_subj->subscribe(on_anf_update);

    psd_delay = 4;

    audio      = (cfloat *)malloc(AUDIO_CAPTURE_RATE * sizeof(cfloat));
    audio_hilb = firhilbf_create(7, 60.0f);

    subject_add_observer_and_call(cfg_cur.zoom, on_zoom_change, NULL);
    subject_add_observer_and_call(cfg_cur.filter.real.from, on_real_filter_from_change, NULL);
    subject_add_observer_and_call(cfg_cur.filter.real.to, on_real_filter_to_change, NULL);
    cfg.dnf_auto.val->subscribe(update_dnf_enabled);
    cfg_cur.mode->subscribe(update_dnf_enabled)->notify();

    cfg_cur.fg_freq->subscribe(on_cur_freq_change);
    ready = true;
}

void dsp_reset() {
    psd_delay = 4;

    iirfilt_cccf_reset(dc_block);
    spectrum_sg_rx->reset();
    spectrum_sg_tx->reset();
    waterfall_sg_rx->reset();
    waterfall_sg_tx->reset();
}

static void process_samples(cfloat *buf_samples, uint16_t size, firdecim_crcf sp_decim, ChunkedSpgram *sp_sg,
                            ChunkedSpgram *wf_sg, bool tx) {
    iirfilt_cccf_execute_block(dc_block, buf_samples, size, buf_filtered);
    // Swap I and Q
    for (size_t i = 0; i < size; i++) {
        buf_filtered[i] = {buf_filtered[i].imag(), buf_filtered[i].real()};
    }

    if (spectrum_factor > 1) {
        firdecim_crcf_execute_block(sp_decim, buf_filtered, size / spectrum_factor, spectrum_dec_buf);
        sp_sg->execute_block(spectrum_dec_buf);
    } else {
        sp_sg->execute_block(buf_filtered);
    }

    wf_sg->execute_block(buf_filtered);

    if (!tx && anf_enabled) {
        anf->execute_block(buf_filtered, size);
    }
}

static bool update_spectrum(ChunkedSpgram *sp_sg, uint64_t now, bool tx) {
    if ((now - spectrum_time > spectrum_fps_ms)) {
        sp_sg->get_psd(spectrum_psd);
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

static bool update_waterfall(ChunkedSpgram *wf_sg, uint64_t now, bool tx) {
    if ((now - waterfall_time > waterfall_fps_ms) && (!psd_delay)) {
        wf_sg->get_psd(waterfall_psd);
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

void dsp_samples(cfloat *buf_samples, uint16_t size, bool tx) {
    firdecim_crcf sp_decim;
    ChunkedSpgram *sp_sg, *wf_sg;
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
    if (!tx  && !psd_delay) {
        anf->update(now, cur_mode==x6100_mode_lsb);
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

    if (spectrum_factor > 1) {
        spectrum_decim_rx = firdecim_crcf_create_kaiser(spectrum_factor, 8, 60.0f);
        firdecim_crcf_set_scale(spectrum_decim_rx, sqrt(1.0f / (float)spectrum_factor));
        spectrum_decim_tx = firdecim_crcf_create_kaiser(spectrum_factor, 8, 60.0f);
        firdecim_crcf_set_scale(spectrum_decim_tx, sqrt(1.0f / (float)spectrum_factor));
    }

    for (uint16_t i = 0; i < SPECTRUM_NFFT; i++)
        spectrum_psd_filtered[i] = S_MIN;

    pthread_mutex_unlock(&spectrum_mux);
}

static void on_real_filter_from_change(Subject *subj, void *user_data) {
    filter_from = subject_get_int(subj);
    anf->set_freq_from(filter_from);
}

static void on_real_filter_to_change(Subject *subj, void *user_data) {
    filter_to = subject_get_int(subj);
    anf->set_freq_to(filter_to);
}

static void update_dnf_enabled(Subject *subj, void *user_data) {
    cur_mode = (x6100_mode_t)subject_get_int(cfg_cur.mode);
    bool enabled = subject_get_int(cfg.dnf_auto.val);
    switch (cur_mode) {
        case x6100_mode_lsb:
        case x6100_mode_usb:
            anf_enabled = enabled;
            break;
        default:
            anf_enabled = false;
            break;
    }
    anf->reset();
}

static void on_cur_freq_change(Subject *subj, void *user_data) {
    int32_t new_freq = static_cast<SubjectT<int32_t> *>(subj)->get();
    int32_t diff = new_freq - cur_freq;
    cur_freq = new_freq;
    anf->shift(diff, cur_mode == x6100_mode_lsb);
    waterfall_sg_rx->reset();
    psd_delay = 1;
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
    uint16_t min_nth = size * 15 / 100;
    uint16_t max_nth = size * 10 / 100;

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
        delete spectrum_sg_rx;
    }
    if (spectrum_sg_tx) {
        delete spectrum_sg_tx;
    }
    size_t chunk_size = RADIO_SAMPLES / spectrum_factor;
    spectrum_sg_rx = new ChunkedSpgram(chunk_size, SPECTRUM_NFFT, chunk_size);
    spectrum_sg_rx->set_alpha(0.4f);
    spectrum_sg_tx = new ChunkedSpgram(chunk_size, SPECTRUM_NFFT, chunk_size);
    spectrum_sg_tx->set_alpha(0.4f);
}
