#include "cfg.h"

#include "band.private.h"

#include <aether_radio/x6100_control/control.h>

#include <stdlib.h>
#include <stdio.h>
#include <assert.h>

#define kHz 1000
#define MHz 1000 * kHz

#define CHECK(fn) ({printf("Start " #fn ":\n"); fn; printf(" OK\n");})
#define ARRAY_SIZE(arr) (sizeof(arr) / sizeof(*arr))


void test_load_band_by_freq() {
    struct data_t {
        uint32_t freq;
        int32_t expected_bands;
    } data[] = {
        {400 * kHz, -1},
        {14 * MHz - 1, -1},
        {14 * MHz, -1},
        {14 * MHz + 1, 6},
        {14070 * kHz - 1, 6},
        {14070 * kHz, 6},
        {14070 * kHz + 1, 7},
        {14350 * kHz, 7},
        {14350 * kHz  + 1, -1},
        {99 * MHz, -1},
        {14350 * kHz, 7},
        {14070 * kHz + 1, 7},
        {14070 * kHz, 6},
        {14 * MHz + 1, 6},
        {14 * MHz, -1},
    };
    size_t freq_len = sizeof(data) / sizeof(*data);
    bool success = true;
    for (size_t i = 0; i < freq_len; i++) {
        int32_t id = get_band_info_by_freq(data[i].freq)->id;
        if (id != data[i].expected_bands) {
            printf("Wrong band id for freq %lu: expected: %i, actual: %i\n", data[i].freq, data[i].expected_bands, id);
            success = false;
        }
    }
    if (!success)
        exit(1);
}

void test_load_band_by_pk() {
    band_info_t *info = get_band_info_by_pk(7);
    assert(info->start_freq == 14070 * kHz && "Wrong start freq");
    assert(info->stop_freq == 14350 * kHz && "Wrong stop freq");
    assert(strcmp("20m SSB", info->name) == 0 && "Wrong name");
}

void test_set_another_band() {
    int32_t new_band = 7;
    assert(cfg_band.vfo_a.freq.pk != new_band);
    assert(cfg_band.vfo_b.freq.pk != new_band);
    assert(cfg_band.vfo.pk != new_band);
    uint32_t freq_a = subject_get_int(cfg_band.vfo_a.freq.val);
    uint32_t freq_b = subject_get_int(cfg_band.vfo_b.freq.val);
    printf("freq_a: %lu, freq_b: %lu\n", freq_a, freq_b);
    subject_set_int(cfg.band_id.val, new_band);
    printf("freq_a: %lu, freq_b: %lu\n", subject_get_int(cfg_band.vfo_a.freq.val), subject_get_int(cfg_band.vfo_b.freq.val));
    assert(subject_get_int(cfg_band.vfo_a.freq.val) != freq_a && "Freq is not changed");
    assert(subject_get_int(cfg_band.vfo_b.freq.val) != freq_b && "Freq is not changed");
    assert(cfg_band.vfo_a.freq.pk == new_band);
    assert(cfg_band.vfo_b.freq.pk == new_band);
    assert(cfg_band.vfo.pk == new_band);
}

void test_set_another_band_using_freq() {
    subject_set_int(cfg.band_id.val, 6);
    subject_set_int(cfg_band.vfo.val, X6100_VFO_A);
    uint32_t prev_a_freq = subject_get_int(cfg_band.vfo_a.freq.val);
    subject_set_int(cfg_band.vfo_a.freq.val , 14200 * kHz);
    assert(subject_get_int(cfg.band_id.val) == 7);
    assert(subject_get_int(cfg_band.vfo_a.freq.val) == 14200 * kHz);
    assert(cfg_band.vfo.pk == 7);
    assert(cfg_band.vfo_a.freq.pk == 7);
    assert(cfg_band.vfo_b.freq.pk == 7);

    subject_set_int(cfg.band_id.val, 6);
    assert(subject_get_int(cfg_band.vfo_a.freq.val) == prev_a_freq);
}

void test_switch_band_up() {
    struct {
        uint32_t freq;
        int32_t cur_id;
        int32_t expected_id;
    } data[] = {
        {14000 * kHz - 1, BAND_UNDEFINED, 6},
        {14000 * kHz, 6, 7},
        {14000 * kHz + 1, 6, 7},
        {14070 * kHz - 1, 6, 7},
        {14070 * kHz, 6, 7},
        {14070 * kHz, 7, 8},
        {600 * MHz, -1, -1},
    };
    for (size_t i = 0; i < ARRAY_SIZE(data); i++) {
        band_info_t * band = get_band_info_next(data[i].freq, true, data[i].cur_id);
        if (data[i].expected_id < 0) {
            assert(band == NULL);
        } else {
            assert(band != NULL);
            printf("%lu next -> %s, %lu, %lu, %i\n", data[i].freq, band->name, band->start_freq, band->stop_freq, band->id);
            assert(band->id == data[i].expected_id);
        }
    }
}

void test_switch_band_down() {
    struct {
        uint32_t freq;
        int32_t cur_id;
        int32_t expected_id;
    } data[] = {
        {14350 * kHz + 1, BAND_UNDEFINED, 7},
        {14350 * kHz, 7, 6},
        {14070 * kHz + 1, 7, 6},
        {14070 * kHz, 7, 6},
        {14070 * kHz, 6, 5},
        {14000 * kHz, 6, 5},
        {1000 * kHz, -1, -1},
    };
    for (size_t i = 0; i < ARRAY_SIZE(data); i++) {
        band_info_t * band = get_band_info_next(data[i].freq, false, data[i].cur_id);
        if (data[i].expected_id < 0) {
            assert(band == NULL);
        } else {
            assert(band != NULL);
            printf("%lu prev -> %s, %lu, %lu, %i\n", data[i].freq, band->name, band->start_freq, band->stop_freq, band->id);
            assert(band->id == data[i].expected_id);
        }
    }
}

void test() {
    subject_set_int(cfg.band_id.val, 6);
    printf("Start testing\n");
    CHECK(test_load_band_by_freq());
    CHECK(test_load_band_by_pk());
    CHECK(test_set_another_band());
    CHECK(test_set_another_band_using_freq());
    CHECK(test_switch_band_up());
    CHECK(test_switch_band_down());
    printf("Testing is done\n");
    exit(0);
}
