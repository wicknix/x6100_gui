/*
 *  SPDX-License-Identifier: LGPL-2.1-or-later
 *
 *  Xiegu X6100 LVGL GUI
 *
 *  Copyright (c) 2022-2023 Belousov Oleg aka R1CBU
 */

#include "vol.h"
#include "helpers.h"
#include "util.h"

extern "C" {
    #include "msg.h"
    #include "radio.h"
    #include "main.h"
    #include "params/params.h"
    #include "voice.h"
    #include "cfg/mode.h"
}

static cfg_vol_mode_t   vol_mode = VOL_VOL;

void vol_update(int16_t diff, bool voice) {
    int32_t     x;
    float       f;
    char        *s;
    bool        b;
    int32_t     freq;

    uint32_t    color = vol->mode == VOL_EDIT ? 0xFFFFFF : 0xBBBBBB;

    switch (vol_mode) {
        case VOL_VOL:
            x = radio_change_vol(diff);
            msg_update_text_fmt("#%3X Volume: %i", color, x);

            if (diff) {
                voice_say_int("Audio level", x);
            } else if (voice) {
                voice_say_text_fmt("Audio level");
            }
            break;

        case VOL_RFG:
            x = subject_get_int(cfg_cur.band->rfg.val);
            x = limit(x + diff, 0, 100);
            subject_set_int(cfg_cur.band->rfg.val, x);
            msg_update_text_fmt("#%3X RF gain: %i", color, x);

            if (diff) {
                voice_say_int("RF gain", x);
            } else if (voice) {
                voice_say_text_fmt("RF gain");
            }
            break;

        case VOL_SQL:
            x = subject_get_int(cfg.sql.val);
            x = limit(x + diff, 0, 100);
            subject_set_int(cfg.sql.val, x);
            msg_update_text_fmt("#%3X Voice SQL: %i", color, x);

            if (diff) {
                voice_say_int("Squelch level %i", x);
            } else if (voice) {
                voice_say_text_fmt("Squelch level");
            }
            break;

        case VOL_FILTER_LOW:
            x = subject_get_int(cfg_cur.filter.low);
            if (diff) {
                // TODO: make step depending on freq
                x = align_int(x + diff * 10, 10);
                x = cfg_mode_set_low_filter(x);
            }

            msg_update_text_fmt("#%3X Filter low: %i Hz", color, x);

            if (diff) {
                voice_delay_say_text_fmt("%i", x);
            } else if (voice) {
                voice_say_text_fmt("Low filter limit");
            }
            break;

        case VOL_FILTER_HIGH:
            x = subject_get_int(cfg_cur.filter.high);
            if (diff) {
                uint8_t freq_step;
                switch (subject_get_int(cfg_cur.mode)) {
                case x6100_mode_cw:
                case x6100_mode_cwr:
                    freq_step = 10;
                    break;
                default:
                    freq_step = 50;
                    break;
                }
                x = align_int(x + diff * freq_step, freq_step);
                x = cfg_mode_set_high_filter(x);
            }

            msg_update_text_fmt("#%3X Filter high: %i Hz", color, x);

            if (diff) {
                voice_say_int("High filter limit", x);
            } else if (voice) {
                voice_say_text_fmt("High filter limit");
            }
            break;

        case VOL_FILTER_BW:
            {
                uint32_t bw = subject_get_int(cfg_cur.filter.bw);
                if (diff) {
                    bw = align_int(bw + diff * 20, 20);
                    subject_set_int(cfg_cur.filter.bw, bw);
                }
                msg_update_text_fmt("#%3X Filter bw: %i Hz", color, bw);

                if (diff) {
                    voice_delay_say_text_fmt("%i", bw);
                } else if (voice) {
                    voice_say_text_fmt("Bandwidth filter limit");
                }
            }
            break;

        case VOL_PWR:
            f = subject_get_float(cfg.pwr.val);
            f += diff * 0.1f;
            f = LV_MIN(10.0f, f);
            f = LV_MAX(0.1f, f);
            subject_set_float(cfg.pwr.val, f);
            msg_update_text_fmt("#%3X Power: %0.1f W", color, f);

            if (diff) {
                voice_say_float("Transmit power", f);
            } else if (voice) {
                voice_say_text_fmt("Transmit power");
            }
            break;

        case VOL_MIC:
            x = radio_change_mic(diff);
            s = params_mic_str_get((x6100_mic_sel_t)x);
            msg_update_text_fmt("#%3X MIC: %s", color, s);

            if (diff) {
                voice_say_text("Mic selector", s);
            } else if (voice) {
                voice_say_text_fmt("Mic selector");
            }
            break;

        case VOL_HMIC:
            x = radio_change_hmic(diff);
            msg_update_text_fmt("#%3X H-MIC gain: %i", color, x);

            if (diff) {
                voice_say_int("Hand microphone gain", x);
            } else if (voice) {
                voice_say_text_fmt("Hand microphone gain");
            }
            break;

        case VOL_IMIC:
            x = radio_change_imic(diff);
            msg_update_text_fmt("#%3X I-MIC gain: %i", color, x);

            if (diff) {
                voice_say_int("Internal microphone gain", x);
            } else if (voice) {
                voice_say_text_fmt("Internal microphone gain");
            }
            break;

        case VOL_MONI:
            x = radio_change_moni(diff);
            msg_update_text_fmt("#%3X Moni level: %i", color, x);

            if (diff) {
                voice_say_int("Monitor level", x);
            } else if (voice) {
                voice_say_text_fmt("Monitor level");
            }
            break;

        default:
            break;
    }
}

void vol_change_mode(int16_t dir) {
    uint64_t mask = subject_get_uint64(cfg.vol_modes.val);
    int size = sizeof(cfg_encoder_vol_modes) / sizeof(cfg_encoder_vol_modes[0]);
    std::vector<cfg_vol_mode_t> all_modes(cfg_encoder_vol_modes, cfg_encoder_vol_modes + size);
    vol_mode = loop_modes(dir, vol_mode, mask, all_modes);
    vol_update(0, true);
}

void vol_set_mode(cfg_vol_mode_t mode) {
    vol_mode = mode;
    vol->mode = VOL_EDIT;
}
