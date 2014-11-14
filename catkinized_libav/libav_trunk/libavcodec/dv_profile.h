/*
 * This file is part of Libav.
 *
 * Libav is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * Libav is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with Libav; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

#ifndef AVCODEC_DV_PROFILE_H
#define AVCODEC_DV_PROFILE_H

#include <stdint.h>

#include "libavutil/pixfmt.h"
#include "libavutil/rational.h"
#include "avcodec.h"

typedef struct DVwork_chunk {
    uint16_t  buf_offset;
    uint16_t  mb_coordinates[5];
} DVwork_chunk;

/*
 * DVprofile is used to express the differences between various
 * DV flavors. For now it's primarily used for differentiating
 * 525/60 and 625/50, but the plans are to use it for various
 * DV specs as well (e.g. SMPTE314M vs. IEC 61834).
 */
typedef struct DVprofile {
    int              dsf;                   /* value of the dsf in the DV header */
    int              video_stype;           /* stype for VAUX source pack */
    int              frame_size;            /* total size of one frame in bytes */
    int              difseg_size;           /* number of DIF segments per DIF channel */
    int              n_difchan;             /* number of DIF channels per frame */
    AVRational       time_base;             /* 1/framerate */
    int              ltc_divisor;           /* FPS from the LTS standpoint */
    int              height;                /* picture height in pixels */
    int              width;                 /* picture width in pixels */
    AVRational       sar[2];                /* sample aspect ratios for 4:3 and 16:9 */
    DVwork_chunk    *work_chunks;           /* each thread gets its own chunk of frame to work on */
    uint32_t        *idct_factor;           /* set of iDCT factor tables */
    enum AVPixelFormat pix_fmt;             /* picture pixel format */
    int              bpm;                   /* blocks per macroblock */
    const uint8_t   *block_sizes;           /* AC block sizes, in bits */
    int              audio_stride;          /* size of audio_shuffle table */
    int              audio_min_samples[3];  /* min amount of audio samples */
                                            /* for 48kHz, 44.1kHz and 32kHz */
    int              audio_samples_dist[5]; /* how many samples are supposed to be */
                                            /* in each frame in a 5 frames window */
    const uint8_t  (*audio_shuffle)[9];     /* PCM shuffling table */
} DVprofile;

const DVprofile* avpriv_dv_frame_profile(const DVprofile *sys,
                                         const uint8_t* frame, unsigned buf_size);
const DVprofile* avpriv_dv_codec_profile(AVCodecContext* codec);

/**
 *  Print all allowed DV profiles into logctx at specified logging level.
 */
void ff_dv_print_profiles(void *logctx, int loglevel);

#endif /* AVCODEC_DV_PROFILE_H */
