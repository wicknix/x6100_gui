/*
 *  SPDX-License-Identifier: LGPL-2.1-or-later
 *
 *  Xiegu X6100 LVGL GUI
 *
 *  Copyright (c) 2024 Georgy Dyuldin aka R2RFE
 */

#include "tools.h"

static void swap(int* xp, int* yp) {
    int temp = *xp;
    *xp = *yp;
    *yp = temp;
}

// Function to perform Selection Sort
static void selectionSort(int arr[], int n) {
    int i, j, min_idx;

    for (i = 0; i < n-1; i++) {
        min_idx = i;

        for (j = i+1; j < n; j++)
          if (arr[j] < arr[min_idx])
            min_idx = j;

        swap(&arr[min_idx], &arr[i]);
    }
}

int ftx_get_snr(const ftx_waterfall_t* wf, const ftx_candidate_t *candidate) {
    //array with wf.num_blocks (row of watterfall) x 8*wf.freq_osr (signals witdh)
    //Get this watterfall zoom on the candidate symbols
    //Sort max to min and calculate max/min = ft8snr and return with substract of -26db for get snr on 2500hz

    int     m = wf->freq_osr * wf->time_osr;
    int     l = 8 * m;
    int     n = 2 * m;
    float   freq_hz = (candidate->freq_offset + (float)candidate->freq_sub / 2) / 0.160f;
    float   minC = 0, maxC = 0;
    int     i = 0;

    while (i < wf->num_blocks) {
        int candidate_zoom[l];

        for (int j = 0; j< 8; j++) {
            for(int k = 0; k<m; k++) {
                candidate_zoom[(j*m)+k] = wf->mag[( (i * wf->block_stride) + candidate->freq_offset + candidate->freq_sub + (j*m) + k )];
            }
        }

        selectionSort(candidate_zoom,l);

        for (int j = 0; j < n; j++) {
            minC += candidate_zoom[j+(n)];
        }

        for (int j = 1; j <= m; j++) {
            maxC += candidate_zoom[(l)-j];
        }

        i++;
    }

    minC = minC / (wf->num_blocks*wf->freq_osr*wf->time_osr*2);
    maxC = maxC / (wf->num_blocks*wf->freq_osr*wf->time_osr);

    int min = (int)(minC/2 - 240);
    int max = (int)(maxC/2 - 240);
    int snr= max - min - 26;

    return snr;
}
