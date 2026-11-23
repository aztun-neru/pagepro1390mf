/*
 * test_jbig1390.c - round-trip test for the JBigcomp2-compatible encoder.
 *
 * usage: test_jbig1390 in.pbm out_printer.jbg out_decodable.jbg
 *
 * out_printer.jbg   = firmware stream layout: ATMOVE(8B) + PSCD + SDNORM
 * out_decodable.jbg = synthesized BIH + the firmware stream (stock jbgtopbm
 *                     cannot parse a leading ATMOVE without a BIH)
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "jbig1390.h"

static unsigned char *read_pbm(const char *path, int *wp, int *hp)
{
    FILE *f = fopen(path, "rb");
    char magic[3] = {0};
    int w, h, c;
    unsigned char *buf;
    size_t need, got;

    if (!f) { perror(path); return NULL; }
    if (fscanf(f, "%2s", magic) != 1 || strcmp(magic, "P4")) {
        fprintf(stderr, "%s: not a binary PBM (P4)\n", path);
        fclose(f);
        return NULL;
    }
    /* skip whitespace/comments, read width and height */
    for (int field = 0; field < 2; field++) {
        int v = 0;
        do {
            c = fgetc(f);
            if (c == '#') while (c != '\n' && c != EOF) c = fgetc(f);
        } while (c == ' ' || c == '\t' || c == '\r' || c == '\n');
        if (c == EOF || ungetc(c, f) == EOF || !fscanf(f, "%d", &v)) {
            fclose(f);
            return NULL;
        }
        if (field == 0) w = v; else h = v;
        /* fscanf consumed digits; next char is single whitespace */
    }
    fgetc(f);   /* single whitespace separating header from raster data */
    need = (size_t) ((w + 7) / 8) * (size_t) h;
    buf = (unsigned char *) malloc(need);
    if (!buf) { fclose(f); return NULL; }
    got = fread(buf, 1, need, f);
    fclose(f);
    if (got != need) {
        fprintf(stderr, "%s: short raster (%zu of %zu bytes)\n", path, got, need);
        free(buf);
        return NULL;
    }
    *wp = w; *hp = h;
    return buf;
}

int main(int argc, char **argv)
{
    unsigned char *pbm, *jbig;
    int w, h, jlen;
    FILE *f;

    if (argc != 4) {
        fprintf(stderr, "usage: %s in.pbm out_printer.jbg out_decodable.jbg\n",
                argv[0]);
        return 2;
    }
    pbm = read_pbm(argv[1], &w, &h);
    if (!pbm) return 1;
    if (jbig_encode(pbm, w, h, &jbig, &jlen)) {
        fprintf(stderr, "jbig_encode failed\n");
        return 1;
    }
    printf("encoded %dx%d -> %d bytes (incl. 8-byte ATMOVE prefix)\n",
           w, h, jlen);

    f = fopen(argv[2], "wb");
    fwrite(jbig, 1, (size_t) jlen, f);
    fclose(f);

    /* decodable variant: synthesized BIH(20) + the printer stream as-is
     * (ATMOVE + PSCDs).  The printer stream itself carries no BIH. */
    {
        unsigned char bih[20] = {
            0, 0, 1, 0,
            (unsigned char) (w >> 24), (unsigned char) (w >> 16),
            (unsigned char) (w >> 8), (unsigned char) w,
            (unsigned char) (h >> 24), (unsigned char) (h >> 16),
            (unsigned char) (h >> 8), (unsigned char) h,
            (unsigned char) (h >> 24), (unsigned char) (h >> 16),
            (unsigned char) (h >> 8), (unsigned char) h, 8, 0, 3, 0x1c
        };
        f = fopen(argv[3], "wb");
        fwrite(bih, 1, 20, f);
        fwrite(jbig, 1, (size_t) jlen, f);
        fclose(f);
    }

    free(jbig);
    free(pbm);
    return 0;
}
