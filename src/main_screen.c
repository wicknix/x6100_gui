/*
 *  SPDX-License-Identifier: LGPL-2.1-or-later
 *
 *  Xiegu X6100 LVGL GUI
 *
 *  Copyright (c) 2022-2023 Belousov Oleg aka R1CBU
 */

#include "main_screen.h"

#include "styles.h"
#include "spectrum.h"
#include "waterfall.h"
#include "util.h"
#include "radio.h"
#include "events.h"
#include "msg.h"
#include "msg_tiny.h"
#include "dsp.h"
#include "clock.h"
#include "cw_tune_ui.h"
#include "info.h"
#include "meter.h"
#include "band_info.h"
#include "tx_info.h"
#include "mfk.h"
#include "vol.h"
#include "main.h"
#include "pannel.h"
#include "rtty.h"
#include "screenshot.h"
#include "keyboard.h"
#include "dialog.h"
#include "dialog_settings.h"
#include "dialog_freq.h"
#include "dialog_msg_cw.h"
#include "dialog_msg_voice.h"
#include "dialog_swrscan.h"
#include "dialog_ft8.h"
#include "dialog_gps.h"
#include "dialog_qth.h"
#include "dialog_recorder.h"
#include "dialog_callsign.h"
#include "dialog_wifi.h"
#include "backlight.h"
#include "buttons.h"
#include "recorder.h"
#include "voice.h"
#include "pubsub_ids.h"
#include "cfg/mode.h"
#include "cfg/memory.h"

#include <unistd.h>
#include <stdint.h>
#include <stdlib.h>


static uint16_t     spectrum_height = (480 / 3);
static uint16_t     freq_height = 36;
static lv_obj_t     *obj;
static Subject      *freq_lock;
static bool         mode_lock = false;
static bool         ab_lock = false;
static bool         band_lock = false;

static lv_obj_t     *spectrum;
static lv_obj_t     *freq[3];
static lv_obj_t     *waterfall;
static lv_obj_t     *msg;
static lv_obj_t     *msg_tiny;
static lv_obj_t     *meter;
static lv_obj_t     *tx_info;

static void freq_shift(int16_t diff);
static void next_freq_step(bool up);
static void toggle_atu_enabled();

// Observers functions

static void on_fg_freq_change(Subject *subj, void *user_data);
static void update_freq_boundaries(Subject *subj, void *user_data);


void mem_load(uint16_t id) {
    if (!cfg_memory_load(id)) {
        msg_update_text_fmt("Nothing to load for memory %i", id);
    }
    if (id != MEM_BACKUP_ID) {
        msg_update_text_fmt("Loaded from memory %i", id);
    }
}

void mem_save(uint16_t id) {
    cfg_memory_save(id);

    if (id <= MEM_HKEY_MAX_ID) {
        msg_update_text_fmt("Saved in memory %i", id);
    }
}

static void toggle_atu_enabled() {
    bool new_atu_enabled = !subject_get_int(cfg.atu_enabled.val);
    subject_set_int(cfg.atu_enabled.val, new_atu_enabled);
    voice_say_text_fmt("Auto tuner %s", new_atu_enabled ? "On" : "Off");
}

static void next_freq_step(bool up) {
    uint16_t new_step = cfg_mode_change_freq_step(up);
    msg_update_text_fmt("Freq step: %i Hz", new_step);
    voice_say_text_fmt("Frequency step %i herz", new_step);
}

static void apps_disable() {
    dialog_destruct();

    rtty_set_state(RTTY_OFF);
    pannel_visible();
}

void main_screen_start_app(press_action_t app_action) {
    apps_disable();

    switch (app_action) {
        case ACTION_APP_RTTY:
            buttons_load_page(&buttons_page_rtty);
            rtty_set_state(RTTY_RX);
            pannel_visible();
            voice_say_text_fmt("Teletype window");
            break;

        case ACTION_APP_SETTINGS:
            dialog_construct(dialog_settings, obj);
            voice_say_text_fmt("Settings window");
            break;

        case ACTION_APP_SWRSCAN:
            dialog_construct(dialog_swrscan, obj);
            voice_say_text_fmt("SWR scan window");
            break;

        case ACTION_APP_FT8:
            dialog_construct(dialog_ft8, obj);
            voice_say_text_fmt("FT8 window");
            break;

        case ACTION_APP_GPS:
            dialog_construct(dialog_gps, obj);
            voice_say_text_fmt("GPS window");
            break;

        case ACTION_APP_RECORDER:
            dialog_construct(dialog_recorder, obj);
            voice_say_text_fmt("Audio recorder window");
            break;

        case ACTION_APP_WIFI:
            dialog_construct(dialog_wifi, obj);
            voice_say_text_fmt("Wi-Fi window");
            break;

        default:
            break;
    }
}

void main_screen_action(press_action_t action) {
    bool b;
    switch (action) {
        case ACTION_NONE:
            break;

        case ACTION_SCREENSHOT:
            screenshot_take();
            break;

        case ACTION_RECORDER:
            if (recorder_is_on()) {
                recorder_set_on(false);
                voice_say_text_fmt("Audio recorder off");
            } else {
                voice_say_text_fmt("Audio recorder on");
                recorder_set_on(true);
            }
            break;

        case ACTION_MUTE:
            radio_change_mute();
            break;

        case ACTION_VOICE_MODE:
            voice_change_mode();
            break;

        case ACTION_BAT_INFO:
            clock_say_bat_info();
            break;

        case ACTION_STEP_UP:
            next_freq_step(true);
            break;

        case ACTION_STEP_DOWN:
            next_freq_step(false);
            break;

        case ACTION_NR_TOGGLE:
            b = subject_get_int(cfg.nr.val);
            b = !b;
            subject_set_int(cfg.nr.val, b);
            msg_update_text_fmt("#FFFFFF NR: %s", b ? "On" : "Off");
            break;

        case ACTION_NB_TOGGLE:
            b = subject_get_int(cfg.nb.val);
            b = !b;
            subject_set_int(cfg.nb.val, b);
            msg_update_text_fmt("#FFFFFF NB: %s", b ? "On" : "Off");
            break;

        case ACTION_APP_RTTY:
        case ACTION_APP_FT8:
        case ACTION_APP_SWRSCAN:
        case ACTION_APP_GPS:
        case ACTION_APP_SETTINGS:
        case ACTION_APP_RECORDER:
        case ACTION_APP_WIFI:
            main_screen_start_app(action);
            break;

        case ACTION_APP_QTH:
            dialog_construct(dialog_qth, obj);
            voice_say_text_fmt("QTH window");
            break;

        case ACTION_APP_CALLSIGN:
            dialog_construct(dialog_callsign, obj);
            voice_say_text_fmt("Callsign window");
            break;
    }
}

static x6100_mode_t get_next_mode_am_fm(bool long_press) {
    x6100_mode_t    mode = subject_get_int(cfg_cur.mode);
    switch (mode) {
        case x6100_mode_am:
            mode = x6100_mode_nfm;
            break;
        case x6100_mode_nfm:
        default:
            mode = x6100_mode_am;
            break;
    }
    return mode;
}

static x6100_mode_t get_next_mode_cw(bool long_press) {
    x6100_mode_t    mode = subject_get_int(cfg_cur.mode);
    switch (mode) {
        case x6100_mode_cw:
            mode = x6100_mode_cwr;
            break;
        case x6100_mode_cwr:
        default:
            mode = x6100_mode_cw;
            break;
    }
    return mode;
}

static x6100_mode_t get_next_mode_ssb(bool long_press) {
    x6100_mode_t    mode = subject_get_int(cfg_cur.mode);
    switch (mode) {
        case x6100_mode_lsb_dig:
            if (long_press) {
                mode = x6100_mode_lsb;
            } else {
                mode = x6100_mode_usb_dig;
            }
            break;
        case x6100_mode_usb_dig:
            if (long_press) {
                mode = x6100_mode_usb;
            } else {
                mode = x6100_mode_lsb_dig;
            }
            break;
        case x6100_mode_lsb:
            if (long_press) {
                mode = x6100_mode_lsb_dig;
            } else {
                mode = x6100_mode_usb;
            }
            break;
        case x6100_mode_usb:
            if (long_press) {
                mode = x6100_mode_usb_dig;
            } else {
                mode = x6100_mode_lsb;
            }
            break;
        default:
            mode = x6100_mode_lsb;
            break;
    }
    return mode;
}

static void change_mode(keypad_key_t key, keypad_state_t state) {
    switch (state) {
        case KEYPAD_LONG:
        case KEYPAD_RELEASE:
            break;
        default:
            return;
    }

    // Define mode->text struct
    typedef struct {
        x6100_mode_t    mode;
        const char*     msg;
    } mode_text_t;

    mode_text_t modes_text[] = {
        {.mode=x6100_mode_nfm, .msg="N F M modulation"},
        {.mode=x6100_mode_am, .msg="A M modulation"},
        {.mode=x6100_mode_cwr, .msg="CWR modulation"},
        {.mode=x6100_mode_cw, .msg="CW modulation"},
        {.mode=x6100_mode_lsb_dig, .msg="LSB digital modulation"},
        {.mode=x6100_mode_lsb, .msg="LSB modulation"},
        {.mode=x6100_mode_usb_dig, .msg="USB digital modulation"},
        {.mode=x6100_mode_usb, .msg="USB modulation"},
    };

    // find next mode
    x6100_mode_t    next_mode;
    switch (key) {
        case KEYPAD_MODE_AM:
            next_mode = get_next_mode_am_fm(state == KEYPAD_LONG);
            break;
        case KEYPAD_MODE_CW:
            next_mode = get_next_mode_cw(state == KEYPAD_LONG);
            break;
        case KEYPAD_MODE_SSB:
            next_mode = get_next_mode_ssb(state == KEYPAD_LONG);
            break;
        default:
            break;
    }

    for (size_t i = 0; i < sizeof(modes_text)/sizeof(modes_text[0]); i++) {
        if (modes_text[i].mode == next_mode) {
            voice_say_text_fmt(modes_text[i].msg);
            break;
        }
    }
    subject_set_int(cfg_cur.mode, next_mode);
}

static void main_screen_keypad_cb(lv_event_t * e) {
    event_keypad_t *keypad = lv_event_get_param(e);

    switch (keypad->key) {
        case KEYPAD_PRE: ;
            int32_t pre = subject_get_int(cfg_cur.pre);
            int32_t att = subject_get_int(cfg_cur.att);
            if (keypad->state == KEYPAD_RELEASE) {
                pre = !pre;
                if (pre && att) {
                    subject_set_int(cfg_cur.att, x6100_att_off);
                }
                subject_set_int(cfg_cur.pre, pre);
                voice_say_text_fmt("Preamplifier %s", pre ? "On" : "Off");
                // radio_change_pre();
                //

                if (params.mag_info.x) {
                    msg_tiny_set_text_fmt("Pre: %s", pre ? "On" : "Off");
                }
            } else if (keypad->state == KEYPAD_LONG) {
                att = !att;
                if (pre && att) {
                    subject_set_int(cfg_cur.pre, x6100_pre_off);
                }
                subject_set_int(cfg_cur.att, att);
                voice_say_text_fmt("Attenuator %s", att ? "On" : "Off");
                // radio_change_att();
                //

                if (params.mag_info.x) {
                    msg_tiny_set_text_fmt("Att: %s", att ? "On" : "Off");
                }
            }
            break;

        case KEYPAD_BAND_UP:
            if (keypad->state == KEYPAD_RELEASE) {
                if (!band_lock) {
                    cfg_band_load_next(true);
                }
                dialog_send(EVENT_BAND_UP, NULL);
            }
            break;

        case KEYPAD_BAND_DOWN:
            if (keypad->state == KEYPAD_RELEASE) {
                if (!band_lock) {
                    cfg_band_load_next(false);
                }
                dialog_send(EVENT_BAND_DOWN, NULL);
            }
            break;

        case KEYPAD_MODE_AM:
        case KEYPAD_MODE_CW:
        case KEYPAD_MODE_SSB:
            if (!mode_lock) {
                change_mode(keypad->key, keypad->state);
            }
            break;

        case KEYPAD_AGC:
            if (keypad->state == KEYPAD_RELEASE) {
                x6100_agc_t agc = subject_get_int(cfg_cur.agc);
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
                subject_set_int(cfg_cur.agc, agc);
                // radio_change_agc();
                //

                if (params.mag_info.x) {
                    msg_tiny_set_text_fmt("AGC: %s", info_params_agc());
                }
            } else if (keypad->state == KEYPAD_LONG) {
                bool new_split = !subject_get_int(cfg_cur.band->split.val);
                subject_set_int(cfg_cur.band->split.val, new_split);
                voice_say_text_fmt("Split %s", new_split ? "On" : "Off");

                spectrum_clear();

                if (params.mag_info.x) {
                    msg_tiny_set_text_fmt("%s", info_params_vfo_label_get());
                }
            }
            break;

        case KEYPAD_FST:
            if (keypad->state == KEYPAD_RELEASE) {
                next_freq_step(true);
            } else if (keypad->state == KEYPAD_LONG) {
                next_freq_step(false);
            }
            break;

        case KEYPAD_ATU:
            if (keypad->state == KEYPAD_RELEASE) {
                toggle_atu_enabled();

                if (params.mag_info.x) {
                    msg_tiny_set_text_fmt("ATU: %s", subject_get_int(cfg.atu_enabled.val) ? "On" : "Off");
                }
            } else if (keypad->state == KEYPAD_LONG) {
                radio_start_atu();
            }
            break;

        case KEYPAD_F1:
            if (keypad->state == KEYPAD_RELEASE) {
                buttons_press(0, false);
            } else if (keypad->state == KEYPAD_LONG) {
                buttons_press(0, true);
            }
            break;

        case KEYPAD_F2:
            if (keypad->state == KEYPAD_RELEASE) {
                buttons_press(1, false);
            } else if (keypad->state == KEYPAD_LONG) {
                buttons_press(1, true);
            }
            break;

        case KEYPAD_F3:
            if (keypad->state == KEYPAD_RELEASE) {
                buttons_press(2, false);
            } else if (keypad->state == KEYPAD_LONG) {
                buttons_press(2, true);
            }
            break;

        case KEYPAD_F4:
            if (keypad->state == KEYPAD_RELEASE) {
                buttons_press(3, false);
            } else if (keypad->state == KEYPAD_LONG) {
                buttons_press(3, true);
            }
            break;

        case KEYPAD_F5:
            if (keypad->state == KEYPAD_RELEASE) {
                buttons_press(4, false);
            } else if (keypad->state == KEYPAD_LONG) {
                buttons_press(4, true);
            }
            break;

        case KEYPAD_GEN:
            if (keypad->state == KEYPAD_RELEASE) {
                apps_disable();
                buttons_load_page_group(buttons_group_gen);
            } else if (keypad->state == KEYPAD_LONG) {
                main_screen_action(params.long_gen);
            }
            break;

        case KEYPAD_APP:
            if (keypad->state == KEYPAD_RELEASE) {
                apps_disable();
                buttons_load_page_group(buttons_group_app);
            } else if (keypad->state == KEYPAD_LONG) {
                main_screen_action(params.long_app);
            }
            break;

        case KEYPAD_KEY:
            if (keypad->state == KEYPAD_RELEASE) {
                apps_disable();
                buttons_load_page_group(buttons_group_key);
            } else if (keypad->state == KEYPAD_LONG) {
                main_screen_action(params.long_key);
            }
            break;

        case KEYPAD_MSG:
            if (keypad->state == KEYPAD_RELEASE) {
                switch (subject_get_int(cfg_cur.mode)) {
                    case x6100_mode_cw:
                    case x6100_mode_cwr:
                        if (!dialog_type_is_run(dialog_msg_cw)) {
                            apps_disable();
                        }

                        pannel_hide();
                        dialog_construct(dialog_msg_cw, obj);
                        voice_say_text_fmt("CW messages window");
                        break;

                    case x6100_mode_lsb:
                    case x6100_mode_usb:
                    case x6100_mode_am:
                    case x6100_mode_nfm:
                        if (!dialog_type_is_run(dialog_msg_voice)) {
                            apps_disable();
                        }

                        pannel_hide();
                        dialog_construct(dialog_msg_voice, obj);
                        voice_say_text_fmt("Voice messages window");
                        break;

                    default:
                        msg_tiny_set_text_fmt("Not used in this mode");
                        break;
                }
            } else if (keypad->state == KEYPAD_LONG) {
                main_screen_action(params.long_msg);
            }
            break;

        case KEYPAD_DFN:
            if (keypad->state == KEYPAD_RELEASE) {
                apps_disable();
                buttons_load_page_group(buttons_group_dfn);
            } else if (keypad->state == KEYPAD_LONG) {
                main_screen_action(params.long_dfn);
            }
            break;

        case KEYPAD_DFL:
            if (keypad->state == KEYPAD_RELEASE) {
                apps_disable();
                buttons_load_page_group(buttons_group_dfl);
                voice_say_text_fmt("DFL parameters");
            } else if (keypad->state == KEYPAD_LONG) {
                main_screen_action(params.long_dfl);
            }
            break;

        case KEYPAD_AB:
            if (!ab_lock) {
                if (keypad->state == KEYPAD_RELEASE) {
                    radio_toggle_vfo();

                    spectrum_clear();

                    if (params.mag_info.x) {
                        msg_tiny_set_text_fmt("%s", info_params_vfo_label_get());
                    }
                } else if (keypad->state == KEYPAD_LONG) {
                    x6100_vfo_t cur_vfo = subject_get_int(cfg_cur.band->vfo.val);
                    cfg_band_vfo_copy();
                    // radio_vfo_set();
                    msg_update_text_fmt("Clone VFO %s", cur_vfo == X6100_VFO_A ? "A->B" : "B->A");
                    voice_say_text_fmt("V F O cloned %s", cur_vfo == X6100_VFO_A ? "from A to B" : "from B to A");
                }
            }
            break;

        case KEYPAD_POWER:
            if (keypad->state == KEYPAD_RELEASE) {
                backlight_switch();
            } else if (keypad->state == KEYPAD_LONG) {
                voice_say_text_fmt("Power off");
                msg_update_text_fmt("Power off");
                radio_poweroff();
            }
            break;

        case KEYPAD_LOCK:
            if (keypad->state == KEYPAD_RELEASE) {
                subject_set_int(freq_lock, !subject_get_int(freq_lock));
                voice_say_text_fmt("Frequency %s", subject_get_int(freq_lock) ? "locked" : "unlocked");
            } else if (keypad->state == KEYPAD_LONG) {
                radio_bb_reset();
                exit(1);
            }
            break;

        case KEYPAD_PTT:
            switch (keypad->state) {
                case KEYPAD_PRESS:
                    radio_set_ptt(true);

                    switch (subject_get_int(cfg_cur.mode)) {
                        case x6100_mode_cw:
                        case x6100_mode_cwr:
                            radio_set_morse_key(true);
                            break;
                    }
                    break;

                case KEYPAD_RELEASE:
                case KEYPAD_LONG_RELEASE:
                    switch (subject_get_int(cfg_cur.mode)) {
                        case x6100_mode_cw:
                        case x6100_mode_cwr:
                            radio_set_morse_key(false);
                            break;
                    }

                    radio_set_ptt(false);
                    break;
                default:
                    break;
            }
            break;

        case KEYPAD_VM:
            if ((keypad->state == KEYPAD_RELEASE) && !dialog_is_run()) {
                buttons_load_page_group(buttons_group_vm);
                voice_say_text_fmt("VM parameters");
            }
            break;

        default:
            LV_LOG_WARN("Unsuported key: %u", keypad->key);
            break;
    }
}

static void main_screen_hkey_cb(lv_event_t * e) {
    event_hkey_t *hkey = lv_event_get_param(e);
    switch (hkey->key) {
        case HKEY_1:
        case HKEY_2:
        case HKEY_3:
        case HKEY_4:
        case HKEY_5:
        case HKEY_6:
        case HKEY_7:
        case HKEY_8:
        case HKEY_9:
            if (hkey->state == HKEY_RELEASE) {
                mem_load(hkey->key - HKEY_1 + 1);
                voice_say_text_fmt("Memory %i loaded", hkey->key - HKEY_1 + 1);
            } else if (hkey->state == HKEY_LONG) {
                mem_save(hkey->key - HKEY_1 + 1);
                voice_say_text_fmt("Memory %i stored", hkey->key - HKEY_1 + 1);
            }
            break;

        case HKEY_SPCH:
            if (hkey->state == HKEY_RELEASE) {
                subject_set_int(freq_lock, !subject_get_int(freq_lock));
                voice_say_text_fmt("Frequency %s", subject_get_int(freq_lock) ? "locked" : "unlocked");
            }
            break;

        case HKEY_TUNER:
            if (hkey->state == HKEY_RELEASE) {
                toggle_atu_enabled();

            } else if (hkey->state == HKEY_LONG) {
                radio_start_atu();
            }
            break;

        case HKEY_XFC:
            if (hkey->state == HKEY_RELEASE) {
                radio_toggle_vfo();

                spectrum_clear();
            }
            break;

        case HKEY_UP:
            if (hkey->state == HKEY_RELEASE) {
                if (!subject_get_int(freq_lock)) {
                    freq_shift(+1);
                }
            } else if (hkey->state == HKEY_LONG) {
                if (!band_lock) {
                    cfg_band_load_next(true);
                }
                dialog_send(EVENT_BAND_UP, NULL);
            }
            break;

        case HKEY_DOWN:
            if (hkey->state == HKEY_RELEASE) {
                if (!subject_get_int(freq_lock)) {
                    freq_shift(-1);
                }
            } else if (hkey->state == HKEY_LONG) {
                if (!band_lock) {
                    cfg_band_load_next(false);
                }
                dialog_send(EVENT_BAND_DOWN, NULL);
            }
            break;

        case HKEY_F1:
            if (hkey->state == HKEY_RELEASE) {
                main_screen_action(params.press_f1);
            } else if (hkey->state == HKEY_LONG) {
                main_screen_action(params.long_f1);
            }
            break;

        case HKEY_F2:
            if (hkey->state == HKEY_RELEASE) {
                main_screen_action(params.press_f2);
            } else if (hkey->state == HKEY_LONG) {
                main_screen_action(params.long_f2);
            }
            break;

        default:
            LV_LOG_WARN("Unsuported key: %u", hkey->key);
            break;
    }
}

static void main_screen_radio_cb(lv_event_t * e) {
    lv_event_code_t code = lv_event_get_code(e);

    lv_event_send(meter, code, NULL);
    lv_event_send(tx_info, code, NULL);
    lv_event_send(spectrum, code, NULL);

    dialog_send(code, NULL);
}

static void main_screen_update_cb(lv_event_t * e) {

    spectrum_clear();
}

static uint16_t freq_accel(uint16_t diff) {
    if (diff < 3) {
        return 1;
    }

    switch (params.freq_accel.x) {
        case FREQ_ACCEL_NONE:
            return 1;

        case FREQ_ACCEL_LITE:
            return (diff < 6) ? 5 : 10;

        case FREQ_ACCEL_STRONG:
            return (diff < 6) ? 10 : 30;
    }
    return 1;
}

static void freq_shift(int16_t diff) {
    if (subject_get_int(freq_lock)) {
        return;
    }

    int32_t freq = subject_get_int(cfg_cur.fg_freq);
    int32_t df = diff * subject_get_int(cfg_cur.freq_step) * freq_accel(abs(diff));
    freq = align_int(freq + df, abs(df));
    subject_set_int(cfg_cur.fg_freq, freq);

    voice_say_freq(freq);
}

static void main_screen_rotary_cb(lv_event_t * e) {
    int32_t *diff = (int32_t *) lv_event_get_param(e);

    freq_shift(*diff);
    dialog_rotary(*diff);
    free(diff);
}

static void spectrum_key_cb(lv_event_t * e) {
    uint32_t key = *((uint32_t *)lv_event_get_param(e));

    switch (key) {
        case '-':
            if (!subject_get_int(freq_lock)) {
                freq_shift(-1);
            }
            break;

        case '=':
            if (!subject_get_int(freq_lock)) {
                freq_shift(+1);
            }
            break;

        case '_':
            next_freq_step(false);
            break;

        case '+':
            next_freq_step(true);
            break;

        case KEY_VOL_LEFT_EDIT:
        case '[':
            vol_update(-1, false);
            break;

        case KEY_VOL_RIGHT_EDIT:
        case ']':
            vol_update(+1, false);
            break;

        case KEY_VOL_LEFT_SELECT:
        case '{':
            vol_change_mode(-1);
            break;

        case KEY_VOL_RIGHT_SELECT:
        case '}':
            vol_change_mode(+1);
            break;

        case KEYBOARD_F9:
            dialog_construct(dialog_settings, obj);
            break;

        case LV_KEY_LEFT:
            switch (mfk_state) {
                case MFK_STATE_EDIT:
                    mfk_update(-1, false);
                    break;

                case MFK_STATE_SELECT:
                    mfk_change_mode(-1);
                    break;
            }
            break;

        case LV_KEY_RIGHT:
            switch (mfk_state) {
                case MFK_STATE_EDIT:
                    mfk_update(+1, false);
                    break;

                case MFK_STATE_SELECT:
                    mfk_change_mode(+1);
                    break;
            }
            break;

        case LV_KEY_ESC:
            if (!dialog_is_run()) {
                switch (vol->mode) {
                    case VOL_EDIT:
                        vol->mode = VOL_SELECT;
                        voice_say_text_fmt("Selection mode");
                        break;

                    case VOL_SELECT:
                        vol->mode = VOL_EDIT;
                        voice_say_text_fmt("Edit mode");
                        break;
                }
                vol_update(0, false);
            }
            break;

        case KEYBOARD_PRINT:
        case KEYBOARD_PRINT_SCR:
            screenshot_take();
            break;

        case KEYBOARD_SCRL_LOCK:
            subject_set_int(freq_lock, !subject_get_int(freq_lock));
            break;

        case KEYBOARD_PGUP:
            if (!band_lock) {
                cfg_band_load_next(true);
            }
            dialog_send(EVENT_BAND_UP, NULL);
            break;

        case KEYBOARD_PGDN:
            if (!band_lock) {
                cfg_band_load_next(false);
            }
            dialog_send(EVENT_BAND_DOWN, NULL);
            break;

        case HKEY_FINP:
            if (!subject_get_int(freq_lock)) {
                voice_say_text_fmt("Enter frequency");
                dialog_construct(dialog_freq, obj);
            }
            break;

        default:
            break;
    }
}

static void spectrum_pressed_cb(lv_event_t * e) {
    switch (mfk_state) {
        case MFK_STATE_EDIT:
            mfk_state = MFK_STATE_SELECT;
            voice_say_text_fmt("Selection mode");
            break;

        case MFK_STATE_SELECT:
            mfk_state = MFK_STATE_EDIT;
            voice_say_text_fmt("Edit mode");
            break;
    }
    mfk_update(0, false);
}

static void keys_enable_cb(lv_timer_t *t) {
    lv_group_add_obj(keyboard_group, spectrum);
    lv_group_set_editing(keyboard_group, true);
}

void main_screen_keys_enable(bool value) {
    if (value) {
        lv_timer_t *timer = lv_timer_create(keys_enable_cb, 100, NULL);
        lv_timer_set_repeat_count(timer, 1);
    } else {
        lv_group_remove_obj(spectrum);
        lv_group_set_editing(keyboard_group, false);
    }
}

void main_screen_lock_freq(bool lock) {
    subject_set_int(freq_lock, lock);
}

void main_screen_lock_band(bool lock) {
    band_lock = lock;
}

void main_screen_lock_mode(bool lock) {
    mode_lock = lock;
    info_lock_mode(lock);
}

void main_screen_lock_ab(bool lock) {
    ab_lock = lock;
}

void main_screen_set_freq(uint64_t freq) {
    subject_set_int(cfg_cur.fg_freq, freq);
    event_send(lv_scr_act(), EVENT_SCREEN_UPDATE, NULL);
}

lv_obj_t * main_screen() {
    uint16_t y = 0;

    freq_lock = subject_create_int(false);

    obj = lv_obj_create(NULL);

    lv_obj_add_event_cb(obj, main_screen_rotary_cb, EVENT_ROTARY, NULL);
    lv_obj_add_event_cb(obj, main_screen_keypad_cb, EVENT_KEYPAD, NULL);
    lv_obj_add_event_cb(obj, main_screen_hkey_cb, EVENT_HKEY, NULL);
    lv_obj_add_event_cb(obj, main_screen_radio_cb, EVENT_RADIO_TX, NULL);
    lv_obj_add_event_cb(obj, main_screen_radio_cb, EVENT_RADIO_RX, NULL);
    lv_obj_add_event_cb(obj, main_screen_update_cb, EVENT_SCREEN_UPDATE, NULL);

    lv_obj_add_style(obj, &background_style, LV_PART_MAIN);
    lv_obj_clear_flag(obj, LV_OBJ_FLAG_SCROLLABLE);

    spectrum = spectrum_init(obj);
    main_screen_keys_enable(true);

    lv_obj_add_event_cb(spectrum, spectrum_key_cb, LV_EVENT_KEY, NULL);
    lv_obj_add_event_cb(spectrum, spectrum_pressed_cb, LV_EVENT_PRESSED, NULL);

    spectrum_min_max_reset();

    lv_obj_set_y(spectrum, y);
    lv_obj_set_height(spectrum, spectrum_height);

    y += spectrum_height;

    lv_obj_t *f;

    f = lv_label_create(obj);
    lv_obj_add_style(f, &freq_style, 0);
    lv_obj_set_pos(f, 0, y);
    lv_label_set_recolor(f, true);
    freq[0] = f;

    f = lv_label_create(obj);
    lv_obj_add_style(f, &freq_main_style, 0);
    lv_obj_set_pos(f, 800/2 - 500/2, y - 1);
    lv_label_set_recolor(f, true);
    freq[1] = f;

    f = lv_label_create(obj);
    lv_obj_add_style(f, &freq_style, 0);
    lv_obj_set_pos(f, 800 - 150, y);
    lv_label_set_recolor(f, true);
    freq[2] = f;

    y += freq_height;

    waterfall = waterfall_init(obj);

    waterfall_min_max_reset();

    lv_obj_set_y(waterfall, y);
    waterfall_set_height(480 - y);

    buttons_init(obj);
    buttons_load_page(&buttons_page_vol_1);

    pannel_init(obj);
    msg = msg_init(obj);
    msg_tiny = msg_tiny_init(obj);

    clock_init(obj);
    info_init(obj);

    meter = meter_init(obj);
    tx_info = tx_info_init(obj);

    cw_tune_init(obj);

    msg_schedule_text_fmt("X6100 de R1CBU " VERSION);

    subject_add_delayed_observer(freq_lock, on_fg_freq_change, NULL);
    subject_add_delayed_observer(cfg_cur.band->split.val, on_fg_freq_change, NULL);
    subject_add_delayed_observer(cfg_cur.fg_freq, on_fg_freq_change, NULL);

    subject_add_delayed_observer(freq_lock, update_freq_boundaries, NULL);
    // update boundaries on TX with split
    subject_add_delayed_observer(cfg_cur.fg_freq, update_freq_boundaries, NULL);
    subject_add_delayed_observer(cfg_cur.zoom, update_freq_boundaries, NULL);
    update_freq_boundaries(cfg_cur.zoom, NULL);

    subject_add_delayed_observer(cfg_cur.bg_freq, on_fg_freq_change, NULL);
    on_fg_freq_change(cfg_cur.bg_freq, NULL);

    return obj;
}

void main_screen_notify_tx()
{
    event_send(obj, EVENT_RADIO_TX, NULL);
}

void main_screen_notify_rx()
{
    event_send(obj, EVENT_RADIO_RX, NULL);
}

static void on_fg_freq_change(Subject *subj, void *user_data) {
    int32_t    f;
    uint32_t    color = subject_get_int(freq_lock) ? 0xBBBBBB : 0xFFFFFF;

    bool split = subject_get_int(cfg_cur.band->split.val);
    Subject *freq_fg_subj = cfg_cur.fg_freq;
    Subject *freq_bg_subj = cfg_cur.bg_freq;

    if (split && radio_get_state() == RADIO_TX) {
        freq_fg_subj = cfg_cur.bg_freq;
        freq_bg_subj = cfg_cur.fg_freq;
    }

    f = subject_get_int(freq_fg_subj);

    uint16_t    mhz, khz, hz;

    split_freq(f, &mhz, &khz, &hz);

    if (params.mag_freq.x) {
        if (mhz < 100) {
            msg_tiny_set_text_fmt("%i.%03i.%03i", mhz, khz, hz);
        } else {
            msg_tiny_set_text_fmt("%i.%03i", mhz, khz);
        }
    }

    if (split) {
        uint16_t    mhz2, khz2, hz2;
        int32_t    f2 = subject_get_int(freq_bg_subj);

        split_freq(f2, &mhz2, &khz2, &hz2);

        lv_label_set_text_fmt(freq[1], "#%03X %i.%03i.%03i / %i.%03i.%03i", color, mhz, khz, hz, mhz2, khz2, hz2);
    } else {
        lv_label_set_text_fmt(freq[1], "#%03X %i.%03i.%03i", color, mhz, khz, hz);
    }
}

static void update_freq_boundaries(Subject *subj, void *user_data) {
    bool split = subject_get_int(cfg_cur.band->split.val);
    Subject *freq_fg_subj = cfg_cur.fg_freq;

    if (split && radio_get_state() == RADIO_TX) {
        freq_fg_subj = cfg_cur.bg_freq;
    }
    int32_t  f = subject_get_int(freq_fg_subj);

    uint16_t    mhz, khz, hz;
    uint32_t    half_width = 50000;
    uint32_t    color = subject_get_int(freq_lock) ? 0xBBBBBB : 0xFFFFFF;

    int32_t zoom = subject_get_int(cfg_cur.zoom);

    if (params.waterfall_zoom.x) {
        half_width /= zoom;
    }

    split_freq(f - half_width, &mhz, &khz, &hz);
    lv_label_set_text_fmt(freq[0], "#%03X %i.%03i", color, mhz, khz);

    split_freq(f + half_width, &mhz, &khz, &hz);
    lv_label_set_text_fmt(freq[2], "#%03X %i.%03i", color, mhz, khz);
}
