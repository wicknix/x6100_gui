/*
 *  SPDX-License-Identifier: LGPL-2.1-or-later
 *
 *  Xiegu X6100 LVGL GUI
 *
 *  Copyright (c) 2022-2023 Belousov Oleg aka R1CBU
 */

/*
 * X6100 protocol implementation (Mfg 3087)
 */

#include "cat.h"

#include "cfg/cfg.h"
#include "events.h"
#include "main_screen.h"
#include "meter.h"
#include "params/params.h"
#include "radio.h"
#include "scheduler.h"
#include "spectrum.h"
#include "util.h"
#include "waterfall.h"

#include "lvgl/lvgl.h"
#include <aether_radio/x6100_control/low/gpio.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/poll.h>
#include <termios.h>
#include <unistd.h>

#define FRAME_PRE 0xFE
#define FRAME_END 0xFD

#define CODE_OK 0xFB
#define CODE_NG 0xFA

#define LOCAL_ADDRESS 0xA4

#define C_SND_FREQ 0x00      /* Send frequency data  transceive mode does not ack*/
#define C_SND_MODE 0x01      /* Send mode data, Sc  for transceive mode does not ack */
#define C_RD_BAND 0x02       /* Read band edge frequencies */
#define C_RD_FREQ 0x03       /* Read display frequency */
#define C_RD_MODE 0x04       /* Read display mode */
#define C_SET_FREQ 0x05      /* Set frequency data(1) */
#define C_SET_MODE 0x06      /* Set mode data, Sc */
#define C_SET_VFO 0x07       /* Set VFO */
#define C_SET_MEM 0x08       /* Set channel, Sc(2) */
#define C_WR_MEM 0x09        /* Write memory */
#define C_MEM2VFO 0x0a       /* Memory to VFO */
#define C_CLR_MEM 0x0b       /* Memory clear */
#define C_RD_OFFS 0x0c       /* Read duplex offset frequency; default changes with HF/6M/2M */
#define C_SET_OFFS 0x0d      /* Set duplex offset frequency */
#define C_CTL_SCAN 0x0e      /* Control scan, Sc */
#define C_CTL_SPLT 0x0f      /* Control split, and duplex mode Sc */
#define C_SET_TS 0x10        /* Set tuning step, Sc */
#define C_CTL_ATT 0x11       /* Set/get attenuator, Sc */
#define C_CTL_ANT 0x12       /* Set/get antenna, Sc */
#define C_CTL_ANN 0x13       /* Control announce (speech synth.), Sc */
#define C_CTL_LVL 0x14       /* Set AF/RF/squelch, Sc */
#define C_RD_SQSM 0x15       /* Read squelch condition/S-meter level, Sc */
#define C_CTL_FUNC 0x16      /* Function settings (AGC,NB,etc.), Sc */
#define C_SND_CW 0x17        /* Send CW message */
#define C_SET_PWR 0x18       /* Set Power ON/OFF, Sc */
#define C_RD_TRXID 0x19      /* Read transceiver ID code */
#define C_CTL_MEM 0x1a       /* Misc memory/bank/rig control functions, Sc */
#define C_SET_TONE 0x1b      /* Set tone frequency */
#define C_CTL_PTT 0x1c       /* Control Transmit On/Off, Sc */
#define C_CTL_EDGE 0x1e      /* Band edges */
#define C_CTL_DVT 0x1f       /* Digital modes calsigns & messages */
#define C_CTL_DIG 0x20       /* Digital modes settings & status */
#define C_CTL_RIT 0x21       /* RIT/XIT control */
#define C_CTL_DSD 0x22       /* D-STAR Data */
#define C_SEND_SEL_FREQ 0x25 /* Send/Recv sel/unsel VFO frequency */
#define C_SEND_SEL_MODE 0x26
#define C_CTL_SCP 0x27   /* Scope control & data */
#define C_SND_VOICE 0x28 /* Transmit Voice Memory Contents */
#define C_CTL_MTEXT 0x70 /* Microtelecom Extension */
#define C_CTL_MISC 0x7f  /* Miscellaneous control, Sc */

#define S_VFOA 0x00      /* Set to VFO A */
#define S_VFOB 0x01      /* Set to VFO B */
#define S_BTOA 0xa0      /* VFO A=B */
#define S_XCHNG 0xb0     /* Switch VFO A and B */
#define S_SUBTOMAIN 0xb1 /* MAIN = SUB */
#define S_DUAL_OFF 0xc0  /* Dual watch off */
#define S_DUAL_ON 0xc1   /* Dual watch on */
#define S_DUAL 0xc2      /* Dual watch (0 = off, 1 = on) */
#define S_MAIN 0xd0      /* Select MAIN band */
#define S_SUB 0xd1       /* Select SUB band */
#define S_SUB_SEL 0xd2   /* Read/Set Main/Sub selection */
#define S_FRONTWIN 0xe0  /* Select front window */

// modes
#define M_LSB 0x00
#define M_USB 0x01
#define M_AM 0x02
#define M_CW 0x03
#define M_NFM 0x05
#define M_CWR 0x07

// memory/bank/rig control
#define MEM_BS_REG 0x01 /* Get band stacking register */
#define MEM_IF_FW 0x03  /* Get IF filter width */
#define MEM_LOCK 0x05   /* LOCK status */
#define MEM_DM_FG 0x06  /* Get data mode switch and filter group */

#define FRAME_ADD_LEN 5 /* Header and end len */

typedef struct {
    Subject *subj;
    int32_t   val;
} subj_set_cmd_t;

static int fd;

typedef struct {
    uint8_t start[2];
    uint8_t dst_addr;
    uint8_t src_addr;
    uint8_t command;
    uint8_t subcommand;
    uint8_t args[1500];
} frame_t;

static pthread_mutex_t uart_mutex = PTHREAD_MUTEX_INITIALIZER;

static int32_t fg_freq;

static void schedule_change_fg_freq(int32_t freq);
static void send_waterfall_data();

static void on_fg_freq_change(Subject *s, void *user_data);

static void frame_repr(frame_t *frame, uint16_t len, char *buf_ptr) {
    buf_ptr += sprintf(buf_ptr, "[%02X:", frame->start[0]);
    buf_ptr += sprintf(buf_ptr, "%02X:", frame->start[1]);
    buf_ptr += sprintf(buf_ptr, "%02X:", frame->dst_addr);
    buf_ptr += sprintf(buf_ptr, "%02X]-", frame->src_addr);
    buf_ptr += sprintf(buf_ptr, "[%02X:", frame->command);
    uint16_t remain_len = len - 1;
    if (remain_len) {
        buf_ptr += sprintf(buf_ptr, "%02X:", frame->subcommand);
        remain_len--;
    }
    size_t i = 0;
    while (remain_len) {
        buf_ptr += sprintf(buf_ptr, "%02X:", frame->args[i++]);
        remain_len--;
    }
    buf_ptr += sprintf(buf_ptr - 1, "]-[%02X]", FRAME_END);
    *(buf_ptr - 1) = '\0';
}

static void log_msg(frame_t *frame, uint16_t len) {
    char buf[512];
    frame_repr(frame, len, buf);
    LV_LOG_USER("Cmd %s (Len %i)", buf, len);
}

static bool frame_get(frame_t *frame_ptr, uint16_t *len) {
    char *data_ptr = (uint8_t *)frame_ptr;
    while (true) {
        int res = read(fd, data_ptr + *len, sizeof(*frame_ptr) - *len);
        if ((res <= 0) && (*len == 0))
            return false;

        if (res > 0)
            *len += res;
        char *frame_start = strchr(data_ptr, FRAME_PRE);
        if (frame_start == NULL) {
            *len = 0;
            return false;
        }
        if (frame_start != data_ptr) {
            *len = *len + data_ptr - frame_start;
            memmove(data_ptr, frame_start, *len);
        }

        if (*len >= 2) {
            if (frame_ptr->start[1] != FRAME_PRE) {
                *len -= 2;
                continue;
            }
            if (*len <= FRAME_ADD_LEN) {
                continue;
            }
            for (size_t i = FRAME_ADD_LEN; i < *len; i++) {
                if (data_ptr[i] == FRAME_END) {
                    *len = i;
                    return true;
                }
            }
        }
    }
}

static void swap_addresses(frame_t *frame) {
    // set dst address from sender, src address is fixed - 0xA4
    frame->dst_addr = frame->src_addr;
    frame->src_addr = LOCAL_ADDRESS;
}

static void send_frame(frame_t *frame, uint16_t len) {
    char   *frame_ptr;
    ssize_t l;
    frame_ptr = (char *)frame;
    len += FRAME_ADD_LEN;
    frame_ptr[len - 1] = FRAME_END;
    while (len) {
        l = write(fd, (const char *)frame_ptr, len);
        if (l < 0) {
            perror("Error during writing message");
            return;
        }
        len -= l;
    }
}

static void send_code(frame_t *frame, uint8_t code) {
    frame->command = code;
    send_frame(frame, 1);
}

static void set_subject_value(void *arg) {
    subj_set_cmd_t *set_cmd = (subj_set_cmd_t *)arg;
    subject_set_int(set_cmd->subj, set_cmd->val);
}

static void schedule_change_fg_freq(int32_t new_freq) {
    if (new_freq != subject_get_int(cfg_cur.fg_freq)) {
        fg_freq                = new_freq;
        subj_set_cmd_t set_cmd = {.subj = cfg_cur.fg_freq, .val = new_freq};
        scheduler_put(set_subject_value, &set_cmd, sizeof(set_cmd));
        // scheduler_put(set_freq, &new_freq, sizeof(new_freq));
    }
}

static void schedule_change_fg_mode(x6100_mode_t mode) {
    if (mode != subject_get_int(cfg_cur.mode)) {
        subj_set_cmd_t set_cmd = {.subj = cfg_cur.mode, .val = mode};
        scheduler_put(set_subject_value, &set_cmd, sizeof(set_cmd));
    }
}

static void schedule_change_fg_att(x6100_att_t att) {
    if (att != subject_get_int(cfg_cur.att)) {
        subj_set_cmd_t set_cmd = {.subj = cfg_cur.att, .val = att};
        scheduler_put(set_subject_value, &set_cmd, sizeof(set_cmd));
    }
}

static void schedule_change_subj(Subject *subj, int32_t val) {
    if (val != subject_get_int(subj)) {
        subj_set_cmd_t set_cmd = {.subj = subj, .val = val};
        scheduler_put(set_subject_value, &set_cmd, sizeof(set_cmd));
    }
}

static void set_vfo(void *arg) {
    if (!arg) {
        LV_LOG_ERROR("arg is NULL");
    }
    x6100_vfo_t vfo = *(x6100_vfo_t *)arg;
    subject_set_int(cfg_cur.band->vfo.val, vfo);
    // lv_event_send(lv_scr_act(), EVENT_SCREEN_UPDATE, NULL);
}

static x6100_mode_t ci_mode_2_x_mode(uint8_t mode, uint8_t *dig_mode) {
    x6100_mode_t r_mode;
    bool         data_mode = (dig_mode != NULL) && *dig_mode;
    switch (mode) {
        case M_LSB:
            r_mode = data_mode ? x6100_mode_lsb_dig : x6100_mode_lsb;
            break;
        case M_USB:
            r_mode = data_mode ? x6100_mode_usb_dig : x6100_mode_usb;
            break;
        case M_AM:
            r_mode = x6100_mode_am;
            break;
        case M_CW:
            r_mode = x6100_mode_cw;
            break;
        case M_NFM:
            r_mode = x6100_mode_nfm;
            break;
        case M_CWR:
            r_mode = x6100_mode_cwr;
            break;
        default:
            break;
    }
    return r_mode;
}

static uint8_t x_mode_2_ci_mode(x6100_mode_t mode) {
    switch (mode) {
        case x6100_mode_lsb:
        case x6100_mode_lsb_dig:
            return M_LSB;
            break;
        case x6100_mode_usb:
        case x6100_mode_usb_dig:
            return M_USB;
            break;
        case x6100_mode_cw:
            return M_CW;
            break;
        case x6100_mode_cwr:
            return M_CWR;
            break;
        case x6100_mode_am:
            return M_AM;
            break;
        case x6100_mode_nfm:
            return M_NFM;
            break;
        default:
            return 0;
            break;
    }
}

static uint8_t get_if_bandwidth() {
    uint32_t bw = subject_get_int(cfg_cur.filter.bw);
    switch (subject_get_int(cfg_cur.mode)) {
        case x6100_mode_cw:
        case x6100_mode_cwr:
        case x6100_mode_lsb:
        case x6100_mode_lsb_dig:
        case x6100_mode_usb:
        case x6100_mode_usb_dig:
            if (bw <= 500) {
                return (bw - 25) / 50;
            } else {
                return (bw - 50) / 100 + 5;
            }
            break;
        case x6100_mode_am:
        case x6100_mode_nfm:
            return (bw - 100) / 200;
        default:
            return 31;
            break;
    }
}

static void send_unsupported(frame_t *frame, uint16_t len) {
    char buf[512];
    frame_repr(frame, len, buf);
    LV_LOG_WARN("Unsupported %s (Len %i)", buf, len);
    send_code(frame, CODE_NG);
}

static void frame_parse(frame_t *frame, uint16_t len) {
    int32_t        new_freq;
    x6100_vfo_t    cur_vfo    = subject_get_int(cfg_cur.band->vfo.val);
    int32_t        cur_freq   = subject_get_int(cfg_cur.fg_freq);
    x6100_mode_t   cur_mode   = subject_get_int(cfg_cur.mode);
    x6100_vfo_t    target_vfo = cur_vfo;
    subj_set_cmd_t set_cmd;
    uint8_t        vfo_id;

    struct vfo_params *vfo_params[2];
    if (cur_vfo == X6100_VFO_A) {
        vfo_params[0] = &cfg_cur.band->vfo_a;
        vfo_params[1] = &cfg_cur.band->vfo_b;
    } else {
        vfo_params[0] = &cfg_cur.band->vfo_b;
        vfo_params[1] = &cfg_cur.band->vfo_a;
    }

#if 1
    log_msg(frame, len);
#endif

    // echo input frame
    send_frame(frame, len);
    swap_addresses(frame);

    switch (frame->command) {
        case C_SND_FREQ:
            schedule_change_fg_freq(from_bcd(&frame->subcommand, 10));
            send_code(frame, CODE_OK);
            break;
        case C_RD_FREQ:
            to_bcd(&frame->subcommand, cur_freq, 10);
            // bcd len - 5 bytes
            send_frame(frame, 6);
            break;

        case C_RD_MODE:;
            uint8_t v         = x_mode_2_ci_mode(cur_mode);
            frame->subcommand = v;
            frame->args[0]    = v;
            send_frame(frame, 3);
            break;

        case C_SET_FREQ:
            schedule_change_fg_freq(from_bcd(&frame->subcommand, 10));
            send_code(frame, CODE_OK);
            break;

        case C_SET_MODE:;
            schedule_change_fg_mode(ci_mode_2_x_mode(frame->subcommand, NULL));
            send_code(frame, CODE_OK);
            break;

        case C_CTL_SPLT:
            if (frame->subcommand == FRAME_END) {
                frame->subcommand = subject_get_int(cfg_cur.band->split.val);
                send_frame(frame, 2);
            } else {
                send_unsupported(frame, len);
            }
            break;

        case C_CTL_ATT:
            if (frame->subcommand == FRAME_END) {
                frame->subcommand = subject_get_int(cfg_cur.att) * 0x20;
                send_frame(frame, 2);
            } else if (frame->args[0] == FRAME_END) {
                x6100_att_t new_att = (bool)frame->subcommand;
                schedule_change_fg_att(new_att);
                send_code(frame, CODE_OK);
            } else {
                send_unsupported(frame, len);
            }
            break;

        case C_CTL_LVL:
            if (frame->subcommand == 1) {
                to_bcd_be(frame->args, subject_get_int(cfg.vol.val) * 255 / 55, 3);
                send_frame(frame, 4);
            } else if (frame->subcommand == 2) {
                to_bcd_be(frame->args, subject_get_int(cfg_cur.band->rfg.val) * 255 / 100, 3);
                send_frame(frame, 4);
            } else if (frame->subcommand == 3) {
                to_bcd_be(frame->args, params.sql * 255 / 100, 3);
                send_frame(frame, 4);
            } else if (frame->subcommand == 0x0a) {
                to_bcd_be(frame->args, params.pwr * 255 / 10, 3);
                send_frame(frame, 4);
            } else {
                send_unsupported(frame, len);
            }
            break;

        case C_CTL_FUNC:
            if (frame->subcommand == 2) {
                frame->args[0] = subject_get_int(cfg_cur.pre);
                send_frame(frame, 3);
            } else {
                send_unsupported(frame, len);
            }
            break;

        case C_CTL_PTT:
            if (frame->subcommand == 0x00) {
                if (frame->args[0] == FRAME_END) {
                    frame->args[0] = (radio_get_state() == RADIO_RX) ? 0 : 1;
                    send_frame(frame, 3);
                } else {
                    switch (frame->args[0]) {
                        case 0:
                            radio_set_ptt(false);
                            break;

                        case 1:
                            radio_set_ptt(true);
                            break;
                    }
                    frame->args[0] = CODE_OK;
                    send_frame(frame, 3);
                }
            }
            break;

        case C_SET_VFO:;
            x6100_vfo_t new_vfo;
            switch (frame->subcommand) {
                case S_VFOA:
                    if (cur_vfo != X6100_VFO_A) {
                        new_vfo = X6100_VFO_A;
                        scheduler_put(set_vfo, &new_vfo, sizeof(new_vfo));
                    }

                    send_code(frame, CODE_OK);
                    break;

                case S_VFOB:
                    if (cur_vfo != X6100_VFO_B) {
                        new_vfo = X6100_VFO_A;
                        scheduler_put(set_vfo, &new_vfo, sizeof(new_vfo));
                    }
                    send_code(frame, CODE_OK);
                    break;

                default:
                    send_unsupported(frame, len);
                    break;
            }
            break;

        case C_RD_SQSM:
            if (frame->subcommand == 2) {
                uint8_t val;
                int16_t db = meter_get_raw_db();
                if (db < S9) {
                    val = (db - S9) * 2.1f + 120;
                } else {
                    val = (db - S9) * 2 + 120;
                }
                to_bcd_be(frame->args, val, 3);
                send_frame(frame, 4);
            } else {
                send_code(frame, CODE_NG);
            }
            break;

        case C_SEND_SEL_FREQ:;
            vfo_id = frame->subcommand > 0;
            if (frame->args[0] == FRAME_END) {
                int32_t freq = subject_get_int(vfo_params[vfo_id]->freq.val);
                to_bcd(frame->args, freq, 10);
                send_frame(frame, 7);
            } else {
                int32_t freq = from_bcd(frame->args, 10);
                schedule_change_subj(vfo_params[vfo_id]->freq.val, freq);
                send_code(frame, CODE_OK);
            }
            break;

        case C_SEND_SEL_MODE:;
            vfo_id = frame->subcommand > 0;
            if (frame->args[0] == FRAME_END) {
                uint8_t v      = x_mode_2_ci_mode(subject_get_int(vfo_params[vfo_id]->mode.val));
                frame->args[0] = v;
                frame->args[1] = 0;
                frame->args[2] = 1;
                send_frame(frame, 5);
            } else {
                x6100_mode_t new_mode = ci_mode_2_x_mode(frame->args[0], &frame->args[1]);
                schedule_change_subj(vfo_params[vfo_id]->mode.val, new_mode);
                send_code(frame, CODE_OK);
            }
            break;

        case C_CTL_MEM:
            // TODO: Implement another options
            if (frame->args[0] == FRAME_END) {
                switch (frame->subcommand) {
                    case MEM_IF_FW:
                        frame->args[0] = get_if_bandwidth();
                        send_frame(frame, 3);
                        break;

                    case MEM_DM_FG:
                        frame->args[0] = x_mode_2_ci_mode(cur_mode);
                        // data mode
                        frame->args[1] = (cur_mode == x6100_mode_lsb_dig) || (cur_mode == x6100_mode_usb_dig);
                        // filter group
                        frame->args[2] = 0;
                        send_frame(frame, 5);
                        break;
                    default:
                        send_unsupported(frame, len);
                        break;
                }
            } else {
                switch (frame->subcommand) {
                    case MEM_DM_FG:;
                        x6100_mode_t new_mode = ci_mode_2_x_mode(frame->args[0], &frame->args[1]);
                        schedule_change_subj(cfg_cur.mode, new_mode);
                        send_code(frame, CODE_OK);
                        break;
                    default:
                        send_unsupported(frame, len);
                        break;
                }
            }
            break;

        case C_RD_TRXID:
            if (frame->subcommand == 0) {
                frame->args[0] = 0xA4;
                send_frame(frame, 3);
            }
            break;

        case C_CTL_SCP:
            if (frame->subcommand == 0x10) {
                // Send/read the Scope ON/OFF status
                if (frame->args[0] == FRAME_END) {
                    frame->args[0] = 1;
                    send_frame(frame, 3);
                } else {
                    send_frame(frame, 2);
                }
            } else if (frame->subcommand == 0x11) {
                // Send/read the Scope wave data output*4
                if (frame->args[0] == FRAME_END) {
                    frame->args[0] = 1;
                    send_frame(frame, 3);
                } else {
                    send_frame(frame, 2);
                }
            } else if (frame->subcommand == 0x14) {
                // Send/read the Scope Center mode or Fixed mode setting
                if (frame->args[0] == FRAME_END) {
                    // Report center mode
                    frame->args[0] = 0;
                    frame->args[1] = 0;
                    send_frame(frame, 4);
                } else {
                    send_frame(frame, 2);
                }
            } else if (frame->subcommand == 0x15) {
                // Scope span settings
                if (frame->args[1] == FRAME_END) {
                    // Span +- 50kHz
                    to_bcd(frame->args + 1, 50000, 10);
                    send_frame(frame, 8);
                } else {
                    send_frame(frame, 2);
                }
            } else if (frame->subcommand == 0x19) {
                frame->args[0] = 0;
                frame->args[1] = 0;
                frame->args[2] = 0;
                frame->args[3] = 0;
                send_frame(frame, 6);
            } else {
                send_unsupported(frame, len);
            }
            break;

        default:
            send_unsupported(frame, len);
            break;
    }
    // send_waterfall_data();
}

static uint8_t counter = 0;

static void send_waterfall_data() {
    frame_t frame;
    counter++;
    if (counter < 10) {
        return;
    }
    counter          = 0;
    uint8_t *data    = malloc(1024);
    frame.dst_addr   = 0x00;
    frame.src_addr   = LOCAL_ADDRESS;
    frame.command    = C_CTL_SCP;
    frame.subcommand = 0;
    uint8_t i        = 0;
    // main
    frame.args[i++] = 0;
    // SequenceNumber[01-11]
    frame.args[i++] = 1;
    // TotalSeqneuceSize[01=LAN, 11=USB]
    frame.args[i++] = 1;
    // Center/Fixed (for lan radio)  [00=cent, 01=fixed]
    frame.args[i++] = 0;
    // Center freq
    to_bcd(frame.args + i, subject_get_int(cfg_cur.fg_freq), 10);
    i += 5;
    // Span
    to_bcd(frame.args + i, 50000, 10);
    i += 5;
    // Range information 00 = In range, 01 = Out of range
    frame.args[i++] = 0;
    // FFT data
    // printf("%u\n", i);
    size_t c;
    for (c = 0; c < 300; c++) {
        frame.args[i + c] = c % 160;
    }
    // printf("%u, %u\n", i, c);
    send_frame(&frame, i + c + 3);
    // printf("len: %u\n", i+c);
    free(data);
}

static void *cat_thread(void *arg) {
    frame_t frame;
    while (true) {
        uint16_t len = 0;
        pthread_mutex_lock(&uart_mutex);
        if (frame_get(&frame, &len)) {
            frame_parse(&frame, len - FRAME_ADD_LEN + 1);
            pthread_mutex_unlock(&uart_mutex);
        } else {
            pthread_mutex_unlock(&uart_mutex);
            sleep_usec(100000);
        }
    }
}

void cat_init() {
    /* UART */

    x6100_gpio_set(x6100_pin_usb, 1); /* USB -> CAT */

    fd = open("/dev/ttyS2", O_RDWR | O_NONBLOCK | O_NOCTTY);

    if (fd > 0) {
        struct termios attr;

        tcgetattr(fd, &attr);

        cfsetispeed(&attr, B19200);
        cfsetospeed(&attr, B19200);
        // cfsetispeed(&attr, B115200);
        // cfsetospeed(&attr, B115200);
        cfmakeraw(&attr);

        if (tcsetattr(fd, 0, &attr) < 0) {
            close(fd);
            LV_LOG_ERROR("UART set speed");
        }
    } else {
        LV_LOG_ERROR("UART open");
    }
    printf("cat initialized, %i\n", fd);

    fg_freq = subject_get_int(cfg_cur.fg_freq);
    subject_add_observer(cfg_cur.fg_freq, on_fg_freq_change, NULL);

    /* * */

    pthread_t thread;

    pthread_create(&thread, NULL, cat_thread, NULL);
    pthread_detach(thread);
}

static void init_egress_frame(frame_t *frame) {
    frame->start[0] = FRAME_PRE;
    frame->start[1] = FRAME_PRE;
    frame->dst_addr = '\0';
    frame->src_addr = LOCAL_ADDRESS;
}

static void on_fg_freq_change(Subject *s, void *user_data) {
    int32_t new_freq = subject_get_int(s);
    if (new_freq != fg_freq) {
        fg_freq = new_freq;
        pthread_mutex_lock(&uart_mutex);
        frame_t frame;
        init_egress_frame(&frame);
        frame.command = C_SND_FREQ;
        to_bcd(&frame.subcommand, subject_get_int(s), 10);
        // bcd len - 5 bytes
        send_frame(&frame, 6);
        pthread_mutex_unlock(&uart_mutex);
    }
}
