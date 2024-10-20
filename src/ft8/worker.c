/*
 *  SPDX-License-Identifier: LGPL-2.1-or-later
 *
 *  Xiegu X6100 LVGL GUI
 *
 *  Copyright (c) 2024 Georgy Dyuldin aka R2RFE
 */

#include "worker.h"

#include "../gfsk.h"

#include "lvgl/lvgl.h"
#include <ft8lib/constants.h>
#include <ft8lib/encode.h>
#include <ft8lib/hashtable.h>
#include <ft8lib/message.h>


bool ftx_worker_generate_samples(const char *text, const uint16_t signal_freq, ftx_protocol_t proto, int16_t *samples,
                                 uint32_t *n_samples) {
    ftx_message_t    msg;
    ftx_message_rc_t rc = ftx_message_encode(&msg, &hash_if, text);

    if (rc != FTX_MESSAGE_RC_OK) {
        LV_LOG_ERROR("Cannot parse message %i", rc);
        return false;
    }

    uint8_t tones[FT4_NN];
    uint8_t n_tones;
    float   symbol_period;
    float   symbol_bt;

    if (proto == FTX_PROTOCOL_FT8) {
        n_tones = FT8_NN;
        ft8_encode(msg.payload, tones);
        symbol_period = FT8_SYMBOL_PERIOD;
        symbol_bt = FT8_SYMBOL_BT;
    } else if (proto == FTX_PROTOCOL_FT4) {
        n_tones = FT4_NN;
        ft4_encode(msg.payload, tones);
        symbol_period = FT4_SYMBOL_PERIOD;
        symbol_bt = FT4_SYMBOL_BT;
    }

    samples = gfsk_synth(tones, n_tones, signal_freq, symbol_bt, symbol_period, n_samples);
    return true;
}
