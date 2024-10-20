/*
 *  SPDX-License-Identifier: LGPL-2.1-or-later
 *
 *  Xiegu X6100 LVGL GUI
 *
 *  Copyright (c) 2024 Georgy Dyuldin aka R2RFE
 */

#pragma once

#include <ft8lib/constants.h>

#include <stdbool.h>
#include <stdint.h>
#include <complex.h>

typedef enum {
    // "CQ CALL ..." or "CQ DX CALL ..." or "CQ EU CALL ..."
    MSG_TYPE_CQ,
    // "CALL1 CALL2 GRID"
    MSG_TYPE_GRID,
    // "CALL1 CALL2 +1"
    MSG_TYPE_REPORT,
    // "CALL1 CALL2 R+1"
    MSG_TYPE_R_REPORT,
    // "CALL1 CALL2 RR73"
    MSG_TYPE_RR73,
    // "CALL1 CALL2 73"
    MSG_TYPE_73,

    MSG_TYPE_OTHER,
} ftx_msg_type_t;


/**
 * Init worker
 */
void ftx_worker_init();

/**
 * Cleanup worker
 */
void ftx_worker_free();


/**
 * Reset worker
 */
void ftx_worker_reset();




/**
 * Create CQ message
 */
void ftx_worker_make_cq_msg(char *msg, char *from_call, char *grid, char *cq_modifier);

/**
 * Create answer
 */
bool ftx_worker_make_answer_msg(char *call_from, char *call_to, ftx_msg_type_t incoming_type, char *text);




/**
 * Generate samples
 */
bool ftx_worker_generate_samples(const char *text, const uint16_t signal_freq, ftx_protocol_t proto, int16_t *samples,
                                 uint32_t *n_samples);

/**
 * Process samples
 */
bool ftx_worker_process_samples(float complex *samples, uint32_t n_samples);

/**
 * Decode samples
 */
void ftx_worker_decode();

