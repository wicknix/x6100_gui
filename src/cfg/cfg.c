#include "cfg.private.h"

#include "band.private.h"
#include "mode.private.h"
#include "params.private.h"
#include "subjects.private.h"

#include "../lvgl/lvgl.h"
#include "../util.h"
#include <aether_radio/x6100_control/control.h>

#include <stdio.h>
#include <stdlib.h>

cfg_t      cfg;
cfg_band_t cfg_band;

cfg_cur_t cfg_cur;

cfg_mode_t cfg_mode;

static band_info_t cur_band_info;

static int init_params_cfg(sqlite3 *db);
static int init_band_cfg(sqlite3 *db);
static int init_mode_cfg(sqlite3 *db);

static void  init_items(cfg_item_t *cfg_arr, uint32_t count);
static int   load_items_from_db(cfg_item_t *cfg_arr, uint32_t count);
static void  save_item_to_db(cfg_item_t *item, bool force);
static void  save_items_to_db(cfg_item_t *cfg_arr, uint32_t cfg_size);
static void *params_save_thread(void *arg);

static void on_key_tone_change(subject_t subj, void *user_data);
static void on_item_change(subject_t subj, void *user_data);
static void on_vfo_change(subject_t subj, void *user_data);
static void on_band_id_change(subject_t subj, void *user_data);
static void on_ab_freq_change(subject_t subj, void *user_data);
static void on_ab_mode_change(subject_t subj, void *user_data);
static void on_filter_low_change(subject_t subj, void *user_data);
static void on_filter_high_change(subject_t subj, void *user_data);
static void on_freq_step_change(subject_t subj, void *user_data);
static void on_zoom_change(subject_t subj, void *user_data);

static void on_cur_freq_change(subject_t subj, void *user_data);
static void on_cur_mode_change(subject_t subj, void *user_data);
static void on_cur_filter_low_change(subject_t subj, void *user_data);
static void on_cur_filter_high_change(subject_t subj, void *user_data);
static void on_cur_filter_bw_change(subject_t subj, void *user_data);
static void on_cur_freq_step_change(subject_t subj, void *user_data);
static void on_cur_zoom_change(subject_t subj, void *user_data);

/*

select band (on cfg->band_id change)
* save cur band params using pk (check that freq is within range)
* update pk from band_id
* load band params (if band_id != -1)

 */

// #define TEST_CFG
#ifdef TEST_CFG
#include "test_cfg.c"
#endif

int cfg_init(sqlite3 *db) {
    int rc;

    rc = init_params_cfg(db);
    if (rc != 0) {
        LV_LOG_ERROR("Error during loading params");
        return rc;
    }
    rc = init_band_cfg(db);
    if (rc != 0) {
        LV_LOG_ERROR("Error during loading band params");
        return rc;
    }

    rc = init_mode_cfg(db);
    if (rc != 0) {
        LV_LOG_ERROR("Error during loading mode params");
        return rc;
    }

    pthread_t thread;
    pthread_create(&thread, NULL, params_save_thread, NULL);
    pthread_detach(thread);

#ifdef TEST_CFG
    run_tests();
#endif
    return rc;
}

/**
 * Delayed save of item
 */
static void on_item_change(subject_t subj, void *user_data) {
    cfg_item_t *item = (cfg_item_t *)user_data;
    pthread_mutex_lock(&item->dirty->mux);
    item->dirty->val = true;
    pthread_mutex_unlock(&item->dirty->mux);
}

/**
 * Changing of key tone
 */
static void on_key_tone_change(subject_t subj, void *user_data) {
    int32_t key_tone = subject_get_int(subj);
    x6100_mode_t db_mode = xmode_2_db(subject_get_int(cfg_cur.mode));
    if (db_mode == x6100_mode_cw) {
        int32_t high, low, bw;
        bw = subject_get_int(cfg_cur.filter_bw);
        low = key_tone - bw / 2;
        high = low + bw;
        subject_set_int(cfg_cur.filter_high, high);
        subject_set_int(cfg_cur.filter_low, low);
    }
}

/**
 * Change freq/mode on changing band
 */
static void on_band_id_change(subject_t subj, void *user_data) {
    if (cfg_band.vfo_a.freq.dirty == NULL) {
        LV_LOG_USER("Skip updating cfg_band, not initialized");
        return;
    }
    cfg_item_t *cfg_band_arr;
    cfg_band_arr = (cfg_item_t *)&cfg_band;
    uint32_t cfg_band_size = sizeof(cfg_band) / sizeof(cfg_item_t);

    LV_LOG_INFO("Save band params for %u", cfg_band_arr[0].pk);
    save_items_to_db(cfg_band_arr, cfg_band_size);
    int32_t new_band_id = subject_get_int(subj);
    if (new_band_id == BAND_UNDEFINED)
        return;
    for (size_t i = 0; i < cfg_band_size; i++) {
        cfg_band_arr[i].pk = new_band_id;
    }
    LV_LOG_INFO("Load band params for %u", cfg_band_arr[0].pk);
    load_items_from_db(cfg_band_arr, cfg_band_size);
}

static void on_vfo_change(subject_t subj, void *user_data) {
    subject_t freq_src, mode_src;
    if (subject_get_int(subj) == X6100_VFO_A) {
        freq_src = cfg_band.vfo_a.freq.val;
        mode_src = cfg_band.vfo_a.mode.val;
    } else {
        freq_src = cfg_band.vfo_b.freq.val;
        mode_src = cfg_band.vfo_b.mode.val;
    }
    subject_set_int(cfg_cur.freq, subject_get_int(freq_src));
    subject_set_int(cfg_cur.mode, subject_get_int(mode_src));
}

/**
 * On changing freq
 */
static void on_ab_freq_change(subject_t subj, void *user_data) {
    /**
     * change freq (on cfg_band->freq change):
     * check for cross band
     * find band
     * save new band vfo (a/b) and freq from current values
     * select band (update cfg->band_id)
     */
    cfg_item_t *cfg_band_arr;
    cfg_band_arr = (cfg_item_t *)&cfg_band;
    uint32_t cfg_band_size = sizeof(cfg_band) / sizeof(cfg_item_t);

    cfg_item_t *item = (cfg_item_t *)user_data;
    uint32_t    freq = subject_get_int(subj);
    x6100_vfo_t vfo = subject_get_int(cfg_band.vfo.val);
    bool        is_active = ((vfo == X6100_VFO_A) && (strcmp(item->db_name, "vfoa_freq") == 0)) ||
                     ((vfo == X6100_VFO_B) && (strcmp(item->db_name, "vfob_freq")) == 0);

    if (is_active) {
        // Set freq to current
        subject_set_int(cfg_cur.freq, freq);
    }

    band_info_t *band_info = get_band_info_by_freq(freq);
    LV_LOG_INFO("Loaded band info: %i for freq: %lu (pk = %i, name: %s)", band_info->id, freq, item->pk, item->db_name);

    if ((band_info->id != item->pk) && (band_info->id != BAND_UNDEFINED)) {
        if (is_active) {
            LV_LOG_INFO("Save new freq and vfo with target band_id");
            item->pk = band_info->id;
            save_item_to_db(item, true);
            cfg_band.vfo.pk = band_info->id;
            save_item_to_db(&cfg_band.vfo, true);
            subject_set_int(cfg.band_id.val, band_info->id);
        }
    }
}

/**
 * On changing mode
 */
static void on_ab_mode_change(subject_t subj, void *user_data) {
    cfg_item_t  *item = (cfg_item_t *)user_data;
    x6100_mode_t mode = subject_get_int(subj);
    x6100_vfo_t  vfo = subject_get_int(cfg_band.vfo.val);
    bool         is_active = ((vfo == X6100_VFO_A) && (strcmp(item->db_name, "vfoa_mode") == 0)) ||
                     ((vfo == X6100_VFO_B) && (strcmp(item->db_name, "vfob_mode")) == 0);

    if (is_active) {
        // Set mode to current
        subject_set_int(cfg_cur.mode, mode);
    }
}

/**
 * On changing mode params
 */
static void on_filter_low_change(subject_t subj, void *user_data) {
    cfg_item_t  *item = (cfg_item_t *)user_data;
    int32_t cur_low = subject_get_int(subj);
    switch (item->pk) {
        case x6100_mode_cw:
        case x6100_mode_cwr:
        case x6100_mode_am:
        case x6100_mode_nfm:
            // cur_low = subject_get_int(cfg.key_tone.val) - subject_get_int(cfg_mode.filter_high.val) / 2;
            return;
            break;

        default:
            break;
    }
    subject_set_int(cfg_cur.filter_low, cur_low);
}

static void on_filter_high_change(subject_t subj, void *user_data) {
    cfg_item_t  *item = (cfg_item_t *)user_data;
    int32_t cur_high = subject_get_int(subj);
    int32_t bw;
    switch (item->pk) {
        case x6100_mode_cw:
        case x6100_mode_cwr:
            bw = cur_high;
            cur_high = subject_get_int(cfg.key_tone.val) + bw / 2;
            subject_set_int(cfg_cur.filter_low, cur_high - bw);
            break;

        case x6100_mode_am:
        case x6100_mode_nfm:
            subject_set_int(cfg_cur.filter_low, 0);
            break;

        default:
            break;
    }
    subject_set_int(cfg_cur.filter_high, cur_high);
}

static void on_freq_step_change(subject_t subj, void *user_data) {
    subject_set_int(cfg_cur.freq_step, subject_get_int(subj));
}

static void on_zoom_change(subject_t subj, void *user_data) {
    subject_set_int(cfg_cur.zoom, subject_get_int(subj));
}

/**
 * On changing current params
 */

static void on_cur_freq_change(subject_t subj, void *user_data) {
    // Copy freq to active vfo
    subject_t target_subj;
    if (subject_get_int(cfg_band.vfo.val) == X6100_VFO_A) {
        target_subj = cfg_band.vfo_a.freq.val;
    } else {
        target_subj = cfg_band.vfo_b.freq.val;
    }
    subject_set_int(target_subj, subject_get_int(subj));
}

static void on_cur_mode_change(subject_t subj, void *user_data) {
    // Copy mode to active vfo
    x6100_mode_t new_mode = subject_get_int(subj);
    subject_t    target_subj;
    if (subject_get_int(cfg_band.vfo.val) == X6100_VFO_A) {
        target_subj = cfg_band.vfo_a.mode.val;
    } else {
        target_subj = cfg_band.vfo_b.mode.val;
    }
    subject_set_int(target_subj, new_mode);

    if (cfg_mode.filter_high.dirty == NULL) {
        LV_LOG_USER("Skip updating mode values, not initialized");
        return;
    }

    // Update mode params
    db_mode_t   db_mode = xmode_2_db(new_mode);
    cfg_item_t *cfg_mode_arr;
    cfg_mode_arr = (cfg_item_t *)&cfg_mode;
    uint32_t cfg_mode_size = sizeof(cfg_mode) / sizeof(cfg_item_t);
    for (size_t i = 0; i < cfg_mode_size; i++) {
        if (cfg_mode_arr[i].pk != db_mode) {
            save_item_to_db(&cfg_mode_arr[i], true);
            cfg_mode_arr[i].pk = db_mode;
            cfg_mode_arr[i].load(&cfg_mode_arr[i]);
        }
    }
}

static void on_cur_filter_low_change(subject_t subj, void *user_data) {
    int32_t new_low = subject_get_int(subj);
    LV_LOG_INFO("New current low=%i", new_low);
    subject_set_int(cfg_cur.filter_bw, subject_get_int(cfg_cur.filter_high) - new_low);
    int32_t new_high;
    switch (cfg_mode.filter_low.pk) {
        case x6100_mode_cw:
        case x6100_mode_cwr:
            new_high = (subject_get_int(cfg.key_tone.val) - new_low) * 2;
            subject_set_int(cfg_mode.filter_high.val, new_high);
            break;

        default:
            subject_set_int(cfg_mode.filter_low.val, new_low);
            break;
    }
}

static void on_cur_filter_high_change(subject_t subj, void *user_data) {
    int32_t new_high = subject_get_int(subj);
    LV_LOG_INFO("New current high=%i", new_high);
    subject_set_int(cfg_cur.filter_bw, new_high - subject_get_int(cfg_cur.filter_low));
    switch (cfg_mode.filter_high.pk) {
        case x6100_mode_cw:
        case x6100_mode_cwr:
            new_high = (new_high - subject_get_int(cfg.key_tone.val)) * 2;
            subject_set_int(cfg_mode.filter_high.val, new_high);
            break;

        default:
            subject_set_int(cfg_mode.filter_high.val, new_high);
            break;
    }
}

static void on_cur_filter_bw_change(subject_t subj, void *user_data) {
    if (cfg_mode.filter_low.pk != cfg_mode.filter_high.pk) {
        LV_LOG_INFO("Skip update bw, different modes");
        return;
    }
    int32_t new_bw = subject_get_int(subj);
    int32_t new_low, new_high;
    switch (cfg_mode.filter_high.pk) {
        case x6100_mode_am:
        case x6100_mode_nfm:
            new_low = subject_get_int(cfg_cur.filter_low);
            new_high = new_low + new_bw;
            break;
        default:
            new_low = (subject_get_int(cfg_cur.filter_high) + subject_get_int(cfg_cur.filter_low) - new_bw) / 2;
            new_high = new_low + new_bw;
            break;
    }
    LV_LOG_INFO("New bw=%i, set cur filters: low=%i high=%i", new_bw, new_low, new_high);

    subject_set_int(cfg_cur.filter_low, new_low);
    subject_set_int(cfg_cur.filter_high, new_high);
}

static void on_cur_freq_step_change(subject_t subj, void *user_data) {
    if (cfg_mode.freq_step.dirty == NULL) {
        LV_LOG_USER("Freq step is not initialized, skip updating");
    }
    subject_set_int(cfg_mode.freq_step.val, subject_get_int(subj));;
}

static void on_cur_zoom_change(subject_t subj, void *user_data) {
    subject_set_int(cfg_mode.zoom.val, subject_get_int(subj));;
}

/**
 * Init cfg items
 */
static void init_items(cfg_item_t *cfg_arr, uint32_t count) {
    for (size_t i = 0; i < count; i++) {
        cfg_arr[i].dirty = malloc(sizeof(*cfg_arr[i].dirty));
        cfg_arr[i].dirty->val = false;
        pthread_mutex_init(&cfg_arr[i].dirty->mux, NULL);
        observer_t o = subject_add_observer(cfg_arr[i].val, on_item_change, &cfg_arr[i]);
    }
}
/**
 * Load items from db
 */
static int load_items_from_db(cfg_item_t *cfg_arr, uint32_t count) {
    int rc;
    for (size_t i = 0; i < count; i++) {
        LV_LOG_INFO("Loading %s from db", cfg_arr[i].db_name);
        rc = cfg_arr[i].load(&cfg_arr[i]);
        if (rc != 0) {
            LV_LOG_USER("Can't load %s", cfg_arr[i].db_name);
        }
        cfg_arr[i].dirty->val = false;
    }
    return rc;
}

/**
 * Save items to db
 */

static void save_item_to_db(cfg_item_t *item, bool force) {
    int rc;
    pthread_mutex_lock(&item->dirty->mux);
    if (item->dirty->val || force) {
        rc = item->save(item);
        if (rc != 0) {
            LV_LOG_USER("Can't save %s", item->db_name);
        }
        item->dirty->val = false;
    }
    pthread_mutex_unlock(&item->dirty->mux);
}

static void save_items_to_db(cfg_item_t *cfg_arr, uint32_t cfg_size) {
    int rc;
    for (size_t i = 0; i < cfg_size; i++) {
        save_item_to_db(&cfg_arr[i], false);
    }
}

/**
 * Save thread
 */
static void *params_save_thread(void *arg) {
    cfg_item_t *cfg_arr;
    cfg_arr = (cfg_item_t *)&cfg;
    uint32_t cfg_size = sizeof(cfg) / sizeof(cfg_item_t);

    cfg_item_t *cfg_band_arr;
    cfg_band_arr = (cfg_item_t *)&cfg_band;
    uint32_t cfg_band_size = sizeof(cfg_band) / sizeof(cfg_item_t);

    cfg_item_t *cfg_mode_arr;
    cfg_mode_arr = (cfg_item_t *)&cfg_mode;
    uint32_t cfg_mode_size = sizeof(cfg_mode) / sizeof(cfg_item_t);

    while (true) {
        save_items_to_db(cfg_arr, cfg_size);
        save_items_to_db(cfg_band_arr, cfg_band_size);
        save_items_to_db(cfg_mode_arr, cfg_mode_size);
        sleep_usec(500000);
    }
}

/**
 * Initialization functions
 */
static int init_params_cfg(sqlite3 *db) {
    /* Init db modules */
    cfg_params_init(db);

    /* Fill configuration */
    cfg.key_tone = (cfg_item_t){
        .val = subject_create_int(700),
        .db_name = "key_tone",
        .load = cfg_params_load_item,
        .save = cfg_params_save_item,
    };
    cfg.vol = (cfg_item_t){
        .val = subject_create_int(20),
        .db_name = "vol",
        .load = cfg_params_load_item,
        .save = cfg_params_save_item,
    };
    cfg.band_id = (cfg_item_t){
        .val = subject_create_int(5),
        .db_name = "band",
        .load = cfg_params_load_item,
        .save = cfg_params_save_item,
    };

    /* Bind callbacks */
    subject_add_observer(cfg.band_id.val, on_band_id_change, NULL);
    subject_add_observer(cfg.key_tone.val, on_key_tone_change, NULL);

    /* Load values from table */
    cfg_item_t *cfg_arr = (cfg_item_t *)&cfg;
    uint32_t    cfg_size = sizeof(cfg) / sizeof(*cfg_arr);
    init_items(cfg_arr, cfg_size);
    return load_items_from_db(cfg_arr, cfg_size);
}

static int init_band_cfg(sqlite3 *db) {
    int rc;

    cfg_band_params_init(db);

    x6100_mode_t default_mode;
    band_info_t *band_info = get_band_info_by_pk(subject_get_int(cfg.band_id.val));
    uint32_t     default_freq = 14000000;
    if (band_info) {
        default_freq = (band_info->start_freq + band_info->stop_freq) / 2;
    }

    if (default_freq > 10000000) {
        default_mode = x6100_mode_usb;
    } else {
        default_mode = x6100_mode_lsb;
    }

    cfg_cur.freq = subject_create_int(default_freq);
    subject_add_observer(cfg_cur.freq, on_cur_freq_change, NULL);

    cfg_cur.mode = subject_create_int(default_mode);
    subject_add_observer(cfg_cur.mode, on_cur_mode_change, NULL);

    /* Fill band configuration */
    int32_t band_id = subject_get_int(cfg.band_id.val);

    cfg_band.vfo_a.freq = (cfg_item_t){
        .val = subject_create_int(default_freq),
        .db_name = "vfoa_freq",
        .load = cfg_band_params_load_item,
        .save = cfg_band_params_save_item,
        .pk = band_id,
    };
    cfg_band.vfo_a.mode = (cfg_item_t){
        .val = subject_create_int(default_mode),
        .db_name = "vfoa_mode",
        .load = cfg_band_params_load_item,
        .save = cfg_band_params_save_item,
        .pk = band_id,
    };
    cfg_band.vfo_b.freq = (cfg_item_t){
        .val = subject_create_int(default_freq),
        .db_name = "vfob_freq",
        .load = cfg_band_params_load_item,
        .save = cfg_band_params_save_item,
        .pk = band_id,
    };
    cfg_band.vfo_b.mode = (cfg_item_t){
        .val = subject_create_int(default_mode),
        .db_name = "vfob_mode",
        .load = cfg_band_params_load_item,
        .save = cfg_band_params_save_item,
        .pk = band_id,
    };
    cfg_band.vfo = (cfg_item_t){
        .val = subject_create_int(X6100_VFO_A),
        .db_name = "vfo",
        .load = cfg_band_params_load_item,
        .save = cfg_band_params_save_item,
        .pk = band_id,
    };

    subject_add_observer(cfg_band.vfo_a.freq.val, on_ab_freq_change, &cfg_band.vfo_a.freq);
    subject_add_observer(cfg_band.vfo_b.freq.val, on_ab_freq_change, &cfg_band.vfo_b.freq);

    subject_add_observer(cfg_band.vfo_a.mode.val, on_ab_mode_change, &cfg_band.vfo_a.mode);
    subject_add_observer(cfg_band.vfo_b.mode.val, on_ab_mode_change, &cfg_band.vfo_b.mode);

    subject_add_observer(cfg_band.vfo.val, on_vfo_change, NULL);

    /* Load values from table */
    cfg_item_t *cfg_arr = (cfg_item_t *)&cfg_band;
    uint32_t    cfg_size = sizeof(cfg_band) / sizeof(*cfg_arr);
    init_items(cfg_arr, cfg_size);
    return load_items_from_db(cfg_arr, cfg_size);
}

static int init_mode_cfg(sqlite3 *db) {
    int rc;

    cfg_mode_params_init(db);

    /* Fill mode configuration */
    db_mode_t db_mode = xmode_2_db(subject_get_int(cfg_cur.mode));
    uint32_t  low, high, step, zoom, cur_low, cur_high;
    mode_default_values(db_mode, &low, &high, &step, &zoom);
    if (db_mode == x6100_mode_cw) {
        cur_low = subject_get_int(cfg.key_tone.val) - high / 2;
        cur_high = subject_get_int(cfg.key_tone.val) + high / 2;
    } else {
        cur_low = low;
        cur_high = high;
    }

    cfg_cur.filter_low = subject_create_int(cur_low);
    cfg_cur.filter_high = subject_create_int(cur_high);
    cfg_cur.filter_bw = subject_create_int(high - low);
    cfg_cur.freq_step = subject_create_int(step);
    cfg_cur.zoom = subject_create_int(zoom);

    subject_add_observer(cfg_cur.filter_low, on_cur_filter_low_change, NULL);
    subject_add_observer(cfg_cur.filter_high, on_cur_filter_high_change, NULL);
    subject_add_observer(cfg_cur.filter_bw, on_cur_filter_bw_change, NULL);
    subject_add_observer(cfg_cur.freq_step, on_cur_freq_step_change, NULL);
    subject_add_observer(cfg_cur.zoom, on_cur_zoom_change, NULL);

    cfg_mode.filter_low = (cfg_item_t){
        .val = subject_create_int(low),
        .db_name = "filter_low",
        .load = cfg_mode_params_load_item,
        .save = cfg_mode_params_save_item,
        .pk = db_mode,
    };
    cfg_mode.filter_high = (cfg_item_t){
        .val = subject_create_int(high),
        .db_name = "filter_high",
        .load = cfg_mode_params_load_item,
        .save = cfg_mode_params_save_item,
        .pk = db_mode,
    };
    cfg_mode.freq_step = (cfg_item_t){
        .val = subject_create_int(step),
        .db_name = "freq_step",
        .load = cfg_mode_params_load_item,
        .save = cfg_mode_params_save_item,
        .pk = db_mode,
    };
    cfg_mode.zoom = (cfg_item_t){
        .val = subject_create_int(zoom),
        .db_name = "spectrum_factor",
        .load = cfg_mode_params_load_item,
        .save = cfg_mode_params_save_item,
        .pk = db_mode,
    };

    subject_add_observer(cfg_mode.filter_low.val, on_filter_low_change, &cfg_mode.filter_low);
    subject_add_observer(cfg_mode.filter_high.val, on_filter_high_change, &cfg_mode.filter_high);
    subject_add_observer(cfg_mode.freq_step.val, on_freq_step_change, NULL);
    subject_add_observer(cfg_mode.zoom.val, on_zoom_change, NULL);

    /* Load values from table */
    cfg_item_t *cfg_arr = (cfg_item_t *)&cfg_mode;
    uint32_t    cfg_size = sizeof(cfg_mode) / sizeof(*cfg_arr);
    init_items(cfg_arr, cfg_size);
    return load_items_from_db(cfg_arr, cfg_size);
}
