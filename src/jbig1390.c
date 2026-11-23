/*
 * jbig1390.c - PagePro 1390MF wire-format JBIG encoder.
 *
 * Parameters verified against Windows driver FILE: captures on 2026-11-23:
 *   D=0, P=1
 *   L0=image height (one stripe per band)
 *   MX=8, MY=0
 *   order=0x03 (JBG_ILEAVE | JBG_SMID)
 *   options=0x1c (JBG_TPDON | JBG_TPBON | JBG_DPON)
 *
 * JBG_FIX_AT fixes the AT pixel at MX from line 0.  The output omits BIH and
 * contains ATMOVE followed by one PSCD and one SDNORM.  A full-page blank is
 * therefore exactly 10 bytes and matches ref-blank.prn byte-for-byte.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "jbig.h"
#include "jbig1390.h"

#define JBIG1390_ORDER   (JBG_ILEAVE | JBG_SMID)            /* 0x03 */
#define JBIG1390_OPTIONS (JBG_TPDON | JBG_TPBON | JBG_DPON) /* 0x1c */
#define JBIG1390_MX      8
#define JBIG1390_MY      0

struct outbuf {
    unsigned char *data;
    size_t len, cap;
    int failed;
};

static void out_cb(unsigned char *start, size_t len, void *file)
{
    struct outbuf *b = (struct outbuf *) file;
    if (b->failed) return;
    if (b->len + len > b->cap) {
        size_t ncap = b->cap ? b->cap * 2 : 65536;
        while (ncap < b->len + len) ncap *= 2;
        unsigned char *nd = (unsigned char *) realloc(b->data, ncap);
        if (!nd) { b->failed = 1; return; }
        b->data = nd;
        b->cap = ncap;
    }
    memcpy(b->data + b->len, start, len);
    b->len += len;
}

int jbig_encode(const unsigned char *pbm_1bpp, int w, int h,
                unsigned char **out_jbig, int *out_len)
{
    static const unsigned char atmove[8] =
        { 0xff, 0x06, 0x00, 0x00, 0x00, 0x00, JBIG1390_MX, 0x00 };
    struct jbg_enc_state s;
    struct outbuf b = { NULL, 0, 0, 0 };
    unsigned char *planes[1];
    unsigned char *out;

    if (!pbm_1bpp || w <= 0 || h <= 0 || !out_jbig || !out_len)
        return -1;

    planes[0] = (unsigned char *) pbm_1bpp;
    jbg_enc_init(&s, (unsigned long) w, (unsigned long) h, 1, planes,
                 out_cb, &b);
    jbg_enc_layers(&s, 0);
    jbg_enc_options(&s, JBIG1390_ORDER, JBIG1390_OPTIONS | JBG_FIX_AT,
                    h, JBIG1390_MX, JBIG1390_MY);
    jbg_enc_out(&s);
    jbg_enc_free(&s);

    if (b.failed || !b.data) {
        free(b.data);
        return -1;
    }

    /* JBigcomp2 does NOT emit the 20-byte BIH (proof: unicorn emulation of
     * FUN_0042e2a0 produces ATMOVE + PSCDs only, byte-identical to jbigkit
     * output minus the BIH).  The printer firmware takes image parameters
     * from the band header (cmd 0x0c) and decodes with fixed L0/MX/options,
     * so the BIH must be stripped: its 20 bytes would be misread as stripe-0
     * PSCD data and the page is rejected (blank print). */
    if (b.len < 20) {
        free(b.data);
        return -1;
    }

    out = (unsigned char *) malloc(sizeof(atmove) + b.len - 20);
    if (!out) {
        free(b.data);
        return -1;
    }
    memcpy(out, atmove, sizeof(atmove));
    memcpy(out + sizeof(atmove), b.data + 20, b.len - 20);
    free(b.data);

    *out_jbig = out;
    *out_len = (int) (sizeof(atmove) + b.len - 20);
    return 0;
}
