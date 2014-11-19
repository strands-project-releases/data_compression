/*
 * RTMPE network protocol
 * Copyright (c) 2008-2009 Andrej Stepanchuk
 * Copyright (c) 2009-2010 Howard Chu
 * Copyright (c) 2012 Samuel Pitoiset
 *
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

/**
 * @file
 * RTMPE protocol
 */

#include "libavutil/blowfish.h"
#include "libavutil/intreadwrite.h"
#include "libavutil/opt.h"
#include "libavutil/rc4.h"
#include "libavutil/xtea.h"

#include "internal.h"
#include "rtmp.h"
#include "rtmpdh.h"
#include "rtmpcrypt.h"
#include "url.h"

/* protocol handler context */
typedef struct RTMPEContext {
    const AVClass *class;
    URLContext   *stream;            ///< TCP stream
    FF_DH        *dh;                ///< Diffie-Hellman context
    struct AVRC4 key_in;             ///< RC4 key used for decrypt data
    struct AVRC4 key_out;            ///< RC4 key used for encrypt data
    int          handshaked;         ///< flag indicating when the handshake is performed
    int          tunneling;          ///< use a HTTP connection (RTMPTE)
} RTMPEContext;

static const uint8_t rtmpe8_keys[16][16] = {
    { 0xbf, 0xf0, 0x34, 0xb2, 0x11, 0xd9, 0x08, 0x1f,
      0xcc, 0xdf, 0xb7, 0x95, 0x74, 0x8d, 0xe7, 0x32 },
    { 0x08, 0x6a, 0x5e, 0xb6, 0x17, 0x43, 0x09, 0x0e,
      0x6e, 0xf0, 0x5a, 0xb8, 0xfe, 0x5a, 0x39, 0xe2 },
    { 0x7b, 0x10, 0x95, 0x6f, 0x76, 0xce, 0x05, 0x21,
      0x23, 0x88, 0xa7, 0x3a, 0x44, 0x01, 0x49, 0xa1 },
    { 0xa9, 0x43, 0xf3, 0x17, 0xeb, 0xf1, 0x1b, 0xb2,
      0xa6, 0x91, 0xa5, 0xee, 0x17, 0xf3, 0x63, 0x39 },
    { 0x7a, 0x30, 0xe0, 0x0a, 0xb5, 0x29, 0xe2, 0x2c,
      0xa0, 0x87, 0xae, 0xa5, 0xc0, 0xcb, 0x79, 0xac },
    { 0xbd, 0xce, 0x0c, 0x23, 0x2f, 0xeb, 0xde, 0xff,
      0x1c, 0xfa, 0xae, 0x16, 0x11, 0x23, 0x23, 0x9d },
    { 0x55, 0xdd, 0x3f, 0x7b, 0x77, 0xe7, 0xe6, 0x2e,
      0x9b, 0xb8, 0xc4, 0x99, 0xc9, 0x48, 0x1e, 0xe4 },
    { 0x40, 0x7b, 0xb6, 0xb4, 0x71, 0xe8, 0x91, 0x36,
      0xa7, 0xae, 0xbf, 0x55, 0xca, 0x33, 0xb8, 0x39 },
    { 0xfc, 0xf6, 0xbd, 0xc3, 0xb6, 0x3c, 0x36, 0x97,
      0x7c, 0xe4, 0xf8, 0x25, 0x04, 0xd9, 0x59, 0xb2 },
    { 0x28, 0xe0, 0x91, 0xfd, 0x41, 0x95, 0x4c, 0x4c,
      0x7f, 0xb7, 0xdb, 0x00, 0xe3, 0xa0, 0x66, 0xf8 },
    { 0x57, 0x84, 0x5b, 0x76, 0x4f, 0x25, 0x1b, 0x03,
      0x46, 0xd4, 0x5b, 0xcd, 0xa2, 0xc3, 0x0d, 0x29 },
    { 0x0a, 0xcc, 0xee, 0xf8, 0xda, 0x55, 0xb5, 0x46,
      0x03, 0x47, 0x34, 0x52, 0x58, 0x63, 0x71, 0x3b },
    { 0xb8, 0x20, 0x75, 0xdc, 0xa7, 0x5f, 0x1f, 0xee,
      0xd8, 0x42, 0x68, 0xe8, 0xa7, 0x2a, 0x44, 0xcc },
    { 0x07, 0xcf, 0x6e, 0x9e, 0xa1, 0x6d, 0x7b, 0x25,
      0x9f, 0xa7, 0xae, 0x6c, 0xd9, 0x2f, 0x56, 0x29 },
    { 0xfe, 0xb1, 0xea, 0xe4, 0x8c, 0x8c, 0x3c, 0xe1,
      0x4e, 0x00, 0x64, 0xa7, 0x6a, 0x38, 0x7c, 0x2a },
    { 0x89, 0x3a, 0x94, 0x27, 0xcc, 0x30, 0x13, 0xa2,
      0xf1, 0x06, 0x38, 0x5b, 0xa8, 0x29, 0xf9, 0x27 }
};

static const uint8_t rtmpe9_keys[16][24] = {
    { 0x79, 0x34, 0x77, 0x4c, 0x67, 0xd1, 0x38, 0x3a, 0xdf, 0xb3, 0x56, 0xbe,
      0x8b, 0x7b, 0xd0, 0x24, 0x38, 0xe0, 0x73, 0x58, 0x41, 0x5d, 0x69, 0x67, },
    { 0x46, 0xf6, 0xb4, 0xcc, 0x01, 0x93, 0xe3, 0xa1, 0x9e, 0x7d, 0x3c, 0x65,
      0x55, 0x86, 0xfd, 0x09, 0x8f, 0xf7, 0xb3, 0xc4, 0x6f, 0x41, 0xca, 0x5c, },
    { 0x1a, 0xe7, 0xe2, 0xf3, 0xf9, 0x14, 0x79, 0x94, 0xc0, 0xd3, 0x97, 0x43,
      0x08, 0x7b, 0xb3, 0x84, 0x43, 0x2f, 0x9d, 0x84, 0x3f, 0x21, 0x01, 0x9b, },
    { 0xd3, 0xe3, 0x54, 0xb0, 0xf7, 0x1d, 0xf6, 0x2b, 0x5a, 0x43, 0x4d, 0x04,
      0x83, 0x64, 0x3e, 0x0d, 0x59, 0x2f, 0x61, 0xcb, 0xb1, 0x6a, 0x59, 0x0d, },
    { 0xc8, 0xc1, 0xe9, 0xb8, 0x16, 0x56, 0x99, 0x21, 0x7b, 0x5b, 0x36, 0xb7,
      0xb5, 0x9b, 0xdf, 0x06, 0x49, 0x2c, 0x97, 0xf5, 0x95, 0x48, 0x85, 0x7e, },
    { 0xeb, 0xe5, 0xe6, 0x2e, 0xa4, 0xba, 0xd4, 0x2c, 0xf2, 0x16, 0xe0, 0x8f,
      0x66, 0x23, 0xa9, 0x43, 0x41, 0xce, 0x38, 0x14, 0x84, 0x95, 0x00, 0x53, },
    { 0x66, 0xdb, 0x90, 0xf0, 0x3b, 0x4f, 0xf5, 0x6f, 0xe4, 0x9c, 0x20, 0x89,
      0x35, 0x5e, 0xd2, 0xb2, 0xc3, 0x9e, 0x9f, 0x7f, 0x63, 0xb2, 0x28, 0x81, },
    { 0xbb, 0x20, 0xac, 0xed, 0x2a, 0x04, 0x6a, 0x19, 0x94, 0x98, 0x9b, 0xc8,
      0xff, 0xcd, 0x93, 0xef, 0xc6, 0x0d, 0x56, 0xa7, 0xeb, 0x13, 0xd9, 0x30, },
    { 0xbc, 0xf2, 0x43, 0x82, 0x09, 0x40, 0x8a, 0x87, 0x25, 0x43, 0x6d, 0xe6,
      0xbb, 0xa4, 0xb9, 0x44, 0x58, 0x3f, 0x21, 0x7c, 0x99, 0xbb, 0x3f, 0x24, },
    { 0xec, 0x1a, 0xaa, 0xcd, 0xce, 0xbd, 0x53, 0x11, 0xd2, 0xfb, 0x83, 0xb6,
      0xc3, 0xba, 0xab, 0x4f, 0x62, 0x79, 0xe8, 0x65, 0xa9, 0x92, 0x28, 0x76, },
    { 0xc6, 0x0c, 0x30, 0x03, 0x91, 0x18, 0x2d, 0x7b, 0x79, 0xda, 0xe1, 0xd5,
      0x64, 0x77, 0x9a, 0x12, 0xc5, 0xb1, 0xd7, 0x91, 0x4f, 0x96, 0x4c, 0xa3, },
    { 0xd7, 0x7c, 0x2a, 0xbf, 0xa6, 0xe7, 0x85, 0x7c, 0x45, 0xad, 0xff, 0x12,
      0x94, 0xd8, 0xde, 0xa4, 0x5c, 0x3d, 0x79, 0xa4, 0x44, 0x02, 0x5d, 0x22, },
    { 0x16, 0x19, 0x0d, 0x81, 0x6a, 0x4c, 0xc7, 0xf8, 0xb8, 0xf9, 0x4e, 0xcd,
      0x2c, 0x9e, 0x90, 0x84, 0xb2, 0x08, 0x25, 0x60, 0xe1, 0x1e, 0xae, 0x18, },
    { 0xe9, 0x7c, 0x58, 0x26, 0x1b, 0x51, 0x9e, 0x49, 0x82, 0x60, 0x61, 0xfc,
      0xa0, 0xa0, 0x1b, 0xcd, 0xf5, 0x05, 0xd6, 0xa6, 0x6d, 0x07, 0x88, 0xa3, },
    { 0x2b, 0x97, 0x11, 0x8b, 0xd9, 0x4e, 0xd9, 0xdf, 0x20, 0xe3, 0x9c, 0x10,
      0xe6, 0xa1, 0x35, 0x21, 0x11, 0xf9, 0x13, 0x0d, 0x0b, 0x24, 0x65, 0xb2, },
    { 0x53, 0x6a, 0x4c, 0x54, 0xac, 0x8b, 0x9b, 0xb8, 0x97, 0x29, 0xfc, 0x60,
      0x2c, 0x5b, 0x3a, 0x85, 0x68, 0xb5, 0xaa, 0x6a, 0x44, 0xcd, 0x3f, 0xa7, },
};

int ff_rtmpe_gen_pub_key(URLContext *h, uint8_t *buf)
{
    RTMPEContext *rt = h->priv_data;
    int offset, ret;

    if (!(rt->dh = ff_dh_init(1024)))
        return AVERROR(ENOMEM);

    offset = ff_rtmp_calc_digest_pos(buf, 768, 632, 8);
    if (offset < 0)
        return offset;

    /* generate a Diffie-Hellmann public key */
    if ((ret = ff_dh_generate_public_key(rt->dh)) < 0)
        return ret;

    /* write the public key into the handshake buffer */
    if ((ret = ff_dh_write_public_key(rt->dh, buf + offset, 128)) < 0)
        return ret;

    return 0;
}

int ff_rtmpe_compute_secret_key(URLContext *h, const uint8_t *serverdata,
                                const uint8_t *clientdata, int type)
{
    RTMPEContext *rt = h->priv_data;
    uint8_t secret_key[128], digest[32];
    int server_pos, client_pos;
    int ret;

    if (type) {
        if ((server_pos = ff_rtmp_calc_digest_pos(serverdata, 1532, 632, 772)) < 0)
            return server_pos;
    } else {
        if ((server_pos = ff_rtmp_calc_digest_pos(serverdata, 768, 632, 8)) < 0)
            return server_pos;
    }

    if ((client_pos = ff_rtmp_calc_digest_pos(clientdata, 768, 632, 8)) < 0)
        return client_pos;

    /* compute the shared secret secret in order to compute RC4 keys */
    if ((ret = ff_dh_compute_shared_secret_key(rt->dh, serverdata + server_pos,
                                               128, secret_key)) < 0)
        return ret;

    /* set output key */
    if ((ret = ff_rtmp_calc_digest(serverdata + server_pos, 128, 0, secret_key,
                                   128, digest)) < 0)
        return ret;
    av_rc4_init(&rt->key_out, digest, 16 * 8, 1);

    /* set input key */
    if ((ret = ff_rtmp_calc_digest(clientdata + client_pos, 128, 0, secret_key,
                                   128, digest)) < 0)
        return ret;
    av_rc4_init(&rt->key_in, digest, 16 * 8, 1);

    return 0;
}

static void rtmpe8_sig(const uint8_t *in, uint8_t *out, int key_id)
{
    struct AVXTEA ctx;

    av_xtea_init(&ctx, rtmpe8_keys[key_id]);
    av_xtea_crypt(&ctx, out, in, 1, NULL, 0);
}

static void rtmpe9_sig(const uint8_t *in, uint8_t *out, int key_id)
{
    struct AVBlowfish ctx;
    uint32_t xl, xr;

    xl = AV_RL32(in);
    xr = AV_RL32(in + 4);

    av_blowfish_init(&ctx, rtmpe9_keys[key_id], 24);
    av_blowfish_crypt_ecb(&ctx, &xl, &xr, 0);

    AV_WL32(out, xl);
    AV_WL32(out + 4, xr);
}

void ff_rtmpe_encrypt_sig(URLContext *h, uint8_t *sig, const uint8_t *digest,
                          int type)
{
    int i;

    for (i = 0; i < 32; i += 8) {
        if (type == 8) {
            /* RTMPE type 8 uses XTEA on the signature */
            rtmpe8_sig(sig + i, sig + i, digest[i] % 15);
        } else if (type == 9) {
            /* RTMPE type 9 uses Blowfish on the signature */
            rtmpe9_sig(sig + i, sig + i, digest[i] % 15);
        }
    }
}

int ff_rtmpe_update_keystream(URLContext *h)
{
    RTMPEContext *rt = h->priv_data;
    char buf[RTMP_HANDSHAKE_PACKET_SIZE];

    /* skip past 1536 bytes of the RC4 bytestream */
    av_rc4_crypt(&rt->key_in, buf, NULL, sizeof(buf), NULL, 1);
    av_rc4_crypt(&rt->key_out, buf, NULL, sizeof(buf), NULL, 1);

    /* the next requests will be encrypted using RC4 keys */
    rt->handshaked = 1;

    return 0;
}

static int rtmpe_close(URLContext *h)
{
    RTMPEContext *rt = h->priv_data;

    ff_dh_free(rt->dh);
    ffurl_close(rt->stream);

    return 0;
}

static int rtmpe_open(URLContext *h, const char *uri, int flags)
{
    RTMPEContext *rt = h->priv_data;
    char host[256], url[1024];
    int ret, port;

    av_url_split(NULL, 0, NULL, 0, host, sizeof(host), &port, NULL, 0, uri);

    if (rt->tunneling) {
        if (port < 0)
            port = 80;
        ff_url_join(url, sizeof(url), "ffrtmphttp", NULL, host, port, NULL);
    } else {
        if (port < 0)
            port = 1935;
        ff_url_join(url, sizeof(url), "tcp", NULL, host, port, NULL);
    }

    /* open the tcp or ffrtmphttp connection */
    if ((ret = ffurl_open(&rt->stream, url, AVIO_FLAG_READ_WRITE,
                          &h->interrupt_callback, NULL)) < 0) {
        rtmpe_close(h);
        return ret;
    }

    return 0;
}

static int rtmpe_read(URLContext *h, uint8_t *buf, int size)
{
    RTMPEContext *rt = h->priv_data;
    int ret;

    rt->stream->flags |= h->flags & AVIO_FLAG_NONBLOCK;
    ret = ffurl_read(rt->stream, buf, size);
    rt->stream->flags &= ~AVIO_FLAG_NONBLOCK;

    if (ret < 0 && ret != AVERROR_EOF)
        return ret;

    if (rt->handshaked && ret > 0) {
        /* decrypt data received by the server */
        av_rc4_crypt(&rt->key_in, buf, buf, ret, NULL, 1);
    }

    return ret;
}

static int rtmpe_write(URLContext *h, const uint8_t *buf, int size)
{
    RTMPEContext *rt = h->priv_data;
    int ret;

    if (rt->handshaked) {
        /* encrypt data to send to the server */
        av_rc4_crypt(&rt->key_out, buf, buf, size, NULL, 1);
    }

    if ((ret = ffurl_write(rt->stream, buf, size)) < 0)
        return ret;

    return size;
}

#define OFFSET(x) offsetof(RTMPEContext, x)
#define DEC AV_OPT_FLAG_DECODING_PARAM

static const AVOption ffrtmpcrypt_options[] = {
    {"ffrtmpcrypt_tunneling", "Use a HTTP tunneling connection (RTMPTE).", OFFSET(tunneling), AV_OPT_TYPE_INT, {.i64 = 0}, 0, 1, DEC},
    { NULL },
};

static const AVClass ffrtmpcrypt_class = {
    .class_name = "ffrtmpcrypt",
    .item_name  = av_default_item_name,
    .option     = ffrtmpcrypt_options,
    .version    = LIBAVUTIL_VERSION_INT,
};

URLProtocol ff_ffrtmpcrypt_protocol = {
    .name            = "ffrtmpcrypt",
    .url_open        = rtmpe_open,
    .url_read        = rtmpe_read,
    .url_write       = rtmpe_write,
    .url_close       = rtmpe_close,
    .priv_data_size  = sizeof(RTMPEContext),
    .flags           = URL_PROTOCOL_FLAG_NETWORK,
    .priv_data_class = &ffrtmpcrypt_class,
};
