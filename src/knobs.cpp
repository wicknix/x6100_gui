/*
 *  SPDX-License-Identifier: LGPL-2.1-or-later
 *
 *  Xiegu X6100 LVGL GUI
 *
 *  Copyright (c) 2025 Adrian Grzeca SQ5FOX
 *  Copyright (c) 2025 Georgy Dyuldin R2RFE
 */


#include "knobs.h"

#include "buttons.h"

#include <string>
#include <vector>
#include <map>

extern "C" {
    #include "styles.h"

    #include <stdio.h>
    #include <stdlib.h>
}

#define KNOBS_HEIGHT 26
#define KNOBS_STATIC_WIDTH 24
#define KNOBS_PADDING 2
#define KNOBS_DYNAMIC_WIDTH 400

#define COLOR_ACTIVE "70ff70"
#define COLOR_INACTIVE "b0b0b0"
#define MFK_FMT

enum modes_t {
    MODE_EDIT,
    MODE_SELECT,
};

/* Knob items classes - for each of possible knob action */

struct Control {
    const char *name;

    Control(const char *name) : name(name) {};

    virtual std::string to_str()=0;

    virtual ObserverDelayed *subscribe(void (*cb)(Subject *, void *), void *user_data) {
        return nullptr;
    }

  protected:
    static std::string float_to_str(float val, std::string fmt) {
        size_t len = snprintf(nullptr, 0, fmt.c_str(), val);
        char  *buf = (char *)malloc(len + 1);
        sprintf(buf, fmt.c_str(), val);
        return std::string(buf);
    }
};

struct ControlSubj: public Control {
    Subject **subj;

    ControlSubj(const char *name, Subject **subj): Control(name), subj(subj) {};

    ObserverDelayed *subscribe(void (*cb)(Subject *, void *), void *user_data) {
        return (*subj)->subscribe_delayed(cb, user_data);
    }
};

struct ControlSubjInt: public ControlSubj {
    using ControlSubj::ControlSubj;

    std::string to_str() {
        return std::to_string(subject_get_int(*subj));
    }

};

struct ControlSubjFloat: public ControlSubj {
    std::string fmt;

    ControlSubjFloat(const char *name, Subject **subj, std::string fmt="%0.1f"): ControlSubj(name, subj), fmt(fmt) {};

    std::string to_str() {
        float val = subject_get_float(*subj);
        return float_to_str(val, fmt);
    }
};

struct ControlSubjChoices: public ControlSubj {
    std::vector<std::string> choices;

    ControlSubjChoices(const char *name, Subject **subj, std::vector<std::string> choices): ControlSubj(name, subj), choices(choices) {};

    std::string to_str() {
        int32_t val = subject_get_int(*subj);
        if ((choices.size() > val) && (val >= 0)) {
            return choices[val];
        } else {
            return std::string("Unknown");
        }
    }
};

struct ControlSubjOnOff: public ControlSubjChoices {
    ControlSubjOnOff(const char *name, Subject **subj): ControlSubjChoices(name, subj, {"Off", "On"}) {};
};


template <typename T> struct ControlInt: public Control {
    T *val;

    ControlInt(const char *name, T *val): Control(name), val(val) {};

    std::string to_str() {
        return std::to_string(*val);
    }
};

template <typename T> struct ControlChoices: public Control {
    T *val;
    std::vector<std::string> choices;

    ControlChoices(const char *name, T *val, std::vector<std::string> choices): Control(name), val(val), choices(choices) {};

    std::string to_str() {
        if ((choices.size() > *val) && (*val >= 0)) {
            return choices[*val];
        } else {
            return std::string("Unknown");
        }
    }
};

struct ControlComp: public ControlSubj {
    using ControlSubj::ControlSubj;

    std::string to_str() {
        return std::string(params_comp_str_get(subject_get_int(*subj)));
    }
};


/* Knob info - class for displaying information about knobs */

class KnobInfo {
    lv_obj_t         **label=nullptr;
    Control         *item=nullptr;
    const std::string arrow_symbol;
    modes_t           mode = MODE_EDIT;

    ObserverDelayed *observer=nullptr;

    void update() {
        if (!item) {
            return;
        }
        if (!*label) {
            return;
        }
        std::string val = item->to_str();
        char buf[64];
        snprintf(buf, 64, "%s #%s %s:# #%s %s#", arrow_symbol.c_str(),
                 mode == MODE_EDIT ? COLOR_INACTIVE : COLOR_ACTIVE, item->name,
                 mode == MODE_SELECT ? COLOR_INACTIVE : COLOR_ACTIVE, val.c_str());
        lv_label_set_text(*label, buf);
    }

    static void on_subj_change(Subject *subj, void *user_data) {
        KnobInfo *obj = (KnobInfo *)user_data;
        obj->update();
    }

  public:
    KnobInfo(lv_obj_t **label, const std::string arrow_symbol) : label(label), arrow_symbol(arrow_symbol) {};

    void set_mode(bool edit) {
        if (edit) {
            mode = MODE_EDIT;
        } else {
            mode = MODE_SELECT;
        }
        update();
    }

    void set_ctrl(Control *item) {
        if (item == this->item) {
            update();
        } else {
            if (observer) {
                delete observer;
                observer = nullptr;
            }
            this->item = item;
            observer = item->subscribe(on_subj_change, (void *)this);
            if (observer) {
                observer->notify();
            } else {
                update();
            }
        }
    }
};

static void on_knob_info_enabled_change(Subject *subj, void *user_data);


static std::map<int, Control*> vol_controls = {
    {VOL_VOL, new ControlSubjInt("Volume", &cfg.vol.val)},
    {VOL_VOL, new ControlSubjInt("Volume", &cfg.vol.val)},
    {VOL_SQL, new ControlSubjInt("Voice SQL", &cfg.sql.val)},
    {VOL_RFG, new ControlSubjInt("RF gain", &cfg_cur.band->rfg.val)},
    {VOL_FILTER_LOW, new ControlSubjInt("Filter low", &cfg_cur.filter.low)},
    {VOL_FILTER_HIGH, new ControlSubjInt("Filter high", &cfg_cur.filter.high)},
    {VOL_FILTER_BW, new ControlSubjInt("Filter bw", &cfg_cur.filter.bw)},
    {VOL_PWR, new ControlSubjFloat("Power", &cfg.pwr.val, "%0.1f")},
    {VOL_MIC, new ControlChoices("MIC", &params.mic, {"Built-In", "Handle", "Auto"})},
    {VOL_HMIC, new ControlInt("H-MIC gain", &params.hmic)},
    {VOL_IMIC, new ControlInt("I-MIC gain", &params.imic)},
    {VOL_MONI, new ControlInt("Moni level", &params.moni)}
};

static std::map<int, Control *> mfk_controls = {
    {MFK_SPECTRUM_FACTOR, new ControlSubjInt("Zoom", &cfg_cur.zoom)},
    {MFK_COMP, new ControlComp("Compressor", &cfg.comp.val)},
    {MFK_ANT, new ControlSubjInt("Ant", &cfg.ant_id.val)},
    {MFK_RIT, new ControlSubjInt("RIT", &cfg.rit.val)},
    {MFK_XIT, new ControlSubjInt("XIT", &cfg.xit.val)},

    {MFK_DNF, new ControlSubjOnOff("Notch filter", &cfg.dnf.val)},
    {MFK_DNF_CENTER, new ControlSubjInt("DNF center", &cfg.dnf_center.val)},
    {MFK_DNF_WIDTH, new ControlSubjInt("DNF width", &cfg.dnf_width.val)},
    {MFK_DNF_AUTO, new ControlSubjOnOff("DNF auto", &cfg.dnf_auto.val)},
    {MFK_NB, new ControlSubjOnOff("Noise blanker", &cfg.nb.val)},
    {MFK_NB_LEVEL, new ControlSubjInt("NB level", &cfg.nb_level.val)},
    {MFK_NB_WIDTH, new ControlSubjInt("NB width", &cfg.nb_width.val)},
    {MFK_NR, new ControlSubjOnOff("Noise reduction", &cfg.nr.val)},
    {MFK_NR_LEVEL, new ControlSubjInt("NR level", &cfg.nr_level.val)},

    {MFK_AGC_HANG, new ControlSubjOnOff("AGC hang", &cfg.agc_hang.val)},
    {MFK_AGC_KNEE, new ControlSubjInt("AGC knee", &cfg.agc_knee.val)},
    {MFK_AGC_SLOPE, new ControlSubjInt("AGC slope", &cfg.agc_slope.val)},

    {MFK_KEY_SPEED, new ControlSubjInt("Key speed", &cfg.key_speed.val)},
    {MFK_KEY_TRAIN, new ControlSubjOnOff("Key train", &cfg.key_train.val)},
    {MFK_KEY_MODE, new ControlSubjChoices("Key mode", &cfg.key_mode.val, {"Manual", "Auto-L", "Auto-R"})},
    {MFK_IAMBIC_MODE, new ControlSubjChoices("Iambic mode", &cfg.iambic_mode.val, {"A", "B"})},
    {MFK_KEY_TONE, new ControlSubjInt("Key tone", &cfg.key_tone.val)},
    {MFK_KEY_VOL, new ControlSubjInt("Key vol", &cfg.key_vol.val)},
    {MFK_QSK_TIME, new ControlSubjInt("QSK time", &cfg.qsk_time.val)},
    {MFK_KEY_RATIO, new ControlSubjFloat("Key ratio", &cfg.key_ratio.val)},
    {MFK_CW_DECODER, new ControlSubjOnOff("CW decoder", &cfg.cw_decoder.val)},
    {MFK_CW_TUNE, new ControlSubjOnOff("CW tuner", &cfg.cw_tune.val)},
    {MFK_CW_DECODER_SNR, new ControlSubjFloat("CW decoded snr", &cfg.cw_decoder_snr.val)},
    {MFK_CW_DECODER_PEAK_BETA, new ControlSubjFloat("CW decoder peak beta", &cfg.cw_decoder_peak_beta.val, "%0.2f")},
    {MFK_CW_DECODER_NOISE_BETA,
     new ControlSubjFloat("CW decoder noise beta", &cfg.cw_decoder_noise_beta.val, "%0.2f")},
    // {MFK_RTTY_RATE, Control("RTTY rate", []() { return to_str((float)params.rtty_rate / 100.0f, "%0.2f"); })},
    // {MFK_RTTY_SHIFT, Control("RTTY shift", []() { return std::to_string(params.rtty_shift); })},
    // {MFK_RTTY_CENTER, Control("RTTY center", []() { return std::to_string(params.rtty_center); })},
    // {MFK_RTTY_REVERSE, Control("RTTY reverse", []() { return std::string(params.rtty_reverse ? "On" : "Off"); })},
};

static lv_obj_t *vol_info;

static lv_obj_t *mfk_info;

static KnobInfo *vol_knob_info = new KnobInfo(&vol_info, LV_SYMBOL_UP);
static KnobInfo *mfk_knob_info = new KnobInfo(&mfk_info, LV_SYMBOL_DOWN);

static bool enabled;


void knobs_init(lv_obj_t * parent) {
    // Basic positon calculation
    uint16_t y = 480 - BTN_HEIGHT - 5;
    uint16_t x_static = KNOBS_PADDING;
    uint16_t x_dynamic = x_static  + KNOBS_STATIC_WIDTH + KNOBS_PADDING;

    // Init
    vol_info = lv_label_create(parent);
    lv_obj_add_style(vol_info, &knobs_style, 0);
    lv_obj_set_pos(vol_info, x_static, y - KNOBS_HEIGHT * 2);
    lv_label_set_recolor(vol_info, true);
    lv_label_set_text(vol_info, "");
    // vol_knob_info = new KnobInfo(vol_info, LV_SYMBOL_UP);
    // Init values
    vol_update(0, 0);

    mfk_info = lv_label_create(parent);
    lv_obj_add_style(mfk_info, &knobs_style, 0);
    lv_obj_set_pos(mfk_info, x_static, y - KNOBS_HEIGHT * 1);
    lv_label_set_recolor(mfk_info, true);
    lv_label_set_text(mfk_info, "");
    // mfk_knob_info = new KnobInfo(mfk_info, LV_SYMBOL_DOWN);
    mfk_update(0, 0);

    subject_add_delayed_observer_and_call(cfg.knob_info.val, on_knob_info_enabled_change, nullptr);
}

void knobs_display(bool on) {
    if (on && enabled) {
        lv_obj_clear_flag(vol_info, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(mfk_info, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(vol_info, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(mfk_info, LV_OBJ_FLAG_HIDDEN);
    }
}

bool knobs_visible() {
    return vol_info && !lv_obj_has_flag(vol_info, LV_OBJ_FLAG_HIDDEN);
}

/* VOL */

void knobs_set_vol_mode(bool edit) {
    vol_knob_info->set_mode(edit);
}

void knobs_set_vol_param(cfg_vol_mode_t control) {
    Control *item;
    try {
        item = vol_controls.at(control);
    } catch (const std::out_of_range &ex) {
        LV_LOG_WARN("VOL Control %d is unknown, skip, %s", control, ex.what());
        return;
    }
    vol_knob_info->set_ctrl(item);
}

/* MFK */

void knobs_set_mfk_mode(bool edit) {
    mfk_knob_info->set_mode(edit);
}

void knobs_set_mfk_param(cfg_mfk_mode_t control) {
    Control *item;
    try {
        item = mfk_controls.at(control);
    } catch (const std::out_of_range &ex) {
        LV_LOG_WARN("MFK Control %d is unknown, skip, %s", control, ex.what());
        return;
    }
    mfk_knob_info->set_ctrl(item);
}


static void on_knob_info_enabled_change(Subject *subj, void *user_data) {
    enabled = subject_get_int(subj);
}
