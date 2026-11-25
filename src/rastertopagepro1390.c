/*
 * rastertopagepro1390.c - CUPS raster filter for the Konica Minolta
 * PagePro 1390MF laser printer (USB, "MFPXDMA" protocol from
 * sp701fdr.dll).
 *
 * Pipeline: CUPS raster (600 dpi, 1 bpp) -> crop or center in the
 * 4672x6752 printable area -> JBIG encoder compatible with the
 * printer's "JBigcomp2" decoder (see jbig1390.c) -> MFPXDMA record
 * stream on stdout.
 *
 * The stream layout, record checksums and constants replicate the
 * Windows driver byte-for-byte (verified against a stream that prints
 * on live hardware):
 *
 *   chunk 1 (0xa0): PJL enter + cmd 0x11 + cmd 0x19 + cmd 0x15
 *   chunk 2 (0x60): cmd 0x12 (StartPage) + cmd 0x13 + cmd 0x32
 *   chunk 3:        cmd 0x0c (band header) + JBIG + ESC FF + padding
 *                   + cmd 0x1a (JBIG byte sum + 0x29e) + cmd 0x1f
 *                   (black pixel popcount) + cmd 0x18
 *   chunk 4 (0x62): cmd 0x33 + cmd 0x17 + final PJL UEL/EOJ
 *
 * All records are padded with zeros to 0x20-byte boundaries; each
 * record ends with a checksum byte ck = -(sum of previous bytes)&0xff.
 *
 * Self-contained: parses the CUPS Raster v2 format ("RaS2"/"2SaR")
 * directly, no libcups dependency.
 *
 * Build:  gcc -O2 -o rastertopagepro1390 rastertopagepro1390.c \
 *           jbig1390.c -I/path/to/jbigkit/libjbig /path/to/libjbig.a
 * Install: cp rastertopagepro1390 /usr/lib/cups/filter/
 *          chmod 755 /usr/lib/cups/filter/rastertopagepro1390
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "jbig1390.h"

/* printable area on A4 at 600 dpi (from sp701fdr.dll Lres_CalPaperPixels) */
#define PRINT_W    4672
#define PRINT_H    6752
#define MARGIN_X   144
#define MARGIN_Y   131
#define PRINT_BPL  ((PRINT_W + 7) / 8)          /* 584 */
#define PRINT_RAW  (PRINT_BPL * PRINT_H)        /* 3943168 = 0x3C2B00 */

#define CUPS_CSPACE_W 0
#define CUPS_CSPACE_K 3

static const unsigned char PJL_ENTER[] = "\033%-12345X@PJL\r\n@PJL ENTER LANGUAGE=PL\r\n";
static const unsigned char PJL_EXIT[]  = "\033%-12345X@PJL\r\n@PJL EOJ\r\n\033%-12345X";
static const unsigned char ESC_FF[]    = "\033\xff-12345X";

/* ------------------------------------------------------------------ */
/* MFPXDMA record building                                             */
/* ------------------------------------------------------------------ */

struct buf {
    unsigned char *d;
    size_t len, cap;
};

static void die(const char *msg)
{
    fprintf(stderr, "ERROR: rastertopagepro1390: %s\n", msg);
    exit(1);
}

static void bput(struct buf *b, const void *p, size_t n)
{
    if (b->len + n > b->cap) {
        size_t nc = b->cap ? b->cap * 2 : 4096;
        while (nc < b->len + n) nc *= 2;
        b->d = realloc(b->d, nc);
        if (!b->d) die("out of memory");
        b->cap = nc;
    }
    memcpy(b->d + b->len, p, n);
    b->len += n;
}

static void bzeros(struct buf *b, size_t n)
{
    static const unsigned char z[256];
    while (n) {
        size_t c = n > sizeof z ? sizeof z : n;
        bput(b, z, c);
        n -= c;
    }
}

/* pad with zeros until the chunk length reaches 'align' (absolute) */
static void bpad_to(struct buf *b, size_t align)
{
    if (b->len < align) bzeros(b, align - b->len);
}

/* append one record: 0x0B len cmd params... ck, ck=-(sum)&0xff */
static void brec(struct buf *b, unsigned char cmd, const void *params, size_t plen)
{
    unsigned char hdr[3] = { 0x0B, (unsigned char) (plen + 4), cmd };
    unsigned sum = 0;
    size_t i;

    bput(b, hdr, 3);
    if (plen) bput(b, params, plen);
    for (i = 0; i < 3 + plen; i++) sum += b->d[b->len - 3 - plen + i];
    hdr[0] = (unsigned char) (-(int) sum & 0xff);
    bput(b, hdr, 1);
}

/* ------------------------------------------------------------------ */
/* CUPS Raster v2 reader                                               */
/* ------------------------------------------------------------------ */

struct raster {
    FILE *f;
    int be;                 /* big-endian header fields */
    int compressed;         /* v2: PackBits RLE, v3: raw rows */
    unsigned width, height, bpl, bpp, bpc, cspace, resx, resy;
    unsigned char *row;     /* decoded row buffer (bpl bytes) */
};

static unsigned rd32(const unsigned char *p, int be)
{
    if (be)
        return ((unsigned) p[0] << 24) | ((unsigned) p[1] << 16) |
               ((unsigned) p[2] << 8) | p[3];
    return ((unsigned) p[3] << 24) | ((unsigned) p[2] << 16) |
           ((unsigned) p[1] << 8) | p[0];
}

/* read next page header; return 1 on page, 0 on clean EOF, -1 on error */
static int raster_next_page(struct raster *r)
{
    unsigned char hdr[1796];
    size_t got;

    if (r->be < 0) {
        unsigned char magic[4];
        got = fread(magic, 1, 4, r->f);
        if (got == 0) return 0;
        if (got < 4) return -1;
        if (!memcmp(magic, "RaS2", 4)) { r->be = 1; r->compressed = 1; }
        else if (!memcmp(magic, "2SaR", 4)) { r->be = 0; r->compressed = 1; }
        else if (!memcmp(magic, "RaS3", 4)) { r->be = 1; r->compressed = 0; }
        else if (!memcmp(magic, "3SaR", 4)) { r->be = 0; r->compressed = 0; }
        else { fprintf(stderr, "ERROR: bad raster magic\n"); return -1; }
    }
    got = fread(hdr, 1, sizeof hdr, r->f);
    if (got == 0) return 0;
    if (got < sizeof hdr) return -1;

    r->resx   = rd32(hdr + 276, r->be);
    r->resy   = rd32(hdr + 280, r->be);
    r->width  = rd32(hdr + 372, r->be);
    r->height = rd32(hdr + 376, r->be);
    r->bpc    = rd32(hdr + 384, r->be);
    r->bpp    = rd32(hdr + 388, r->be);
    r->bpl    = rd32(hdr + 392, r->be);
    r->cspace = rd32(hdr + 400, r->be);

    if (r->width == 0 || r->height == 0 || r->bpl == 0) return -1;
    if (r->bpp != 1 && !(r->bpp == 8 && r->bpc == 8)) return -1;
    if (r->bpp == 1 && r->bpl < r->width / 8 + (r->width % 8 != 0)) return -1;
    if (r->bpp == 8 && r->bpl < r->width) return -1;
    if (r->cspace != CUPS_CSPACE_K && r->cspace != CUPS_CSPACE_W) return -1;

    r->row = realloc(r->row, r->bpl);
    if (!r->row) die("out of memory");
    return 1;
}

/* decode one raster row (PackBits + row repetition handled by caller) */
static int raster_read_row(struct raster *r)
{
    size_t done = 0;
    while (done < r->bpl) {
        int n = fgetc(r->f);
        if (n == EOF) return -1;
        if (n < 128) {
            n++;
            if (done + (size_t) n > r->bpl) return -1;
            if (fread(r->row + done, 1, (size_t) n, r->f) != (size_t) n)
                return -1;
            done += (size_t) n;
        } else {
            int c;
            n = 257 - n;
            c = fgetc(r->f);
            if (c == EOF || done + (size_t) n > r->bpl) return -1;
            memset(r->row + done, c, (size_t) n);
            done += (size_t) n;
        }
    }
    return 0;
}

/* ------------------------------------------------------------------ */
/* page composition: CUPS raster -> 4672x6752 1bpp (1=black)           */
/* ------------------------------------------------------------------ */

static unsigned char *read_page_bitmap(struct raster *r, unsigned *popcount)
{
    unsigned char *bmp = calloc(PRINT_RAW, 1);
    unsigned y = 0, pop = 0, offx = 0, offy = 0;
    unsigned dstx = 0, dsty = 0, copy_w, copy_h;
    static const unsigned char bits[256] = {
#define B2(n) n, n+1, n+1, n+2
#define B4(n) B2(n), B2(n+1), B2(n+1), B2(n+2)
#define B6(n) B4(n), B4(n+1), B4(n+1), B4(n+2)
        B6(0), B6(1), B6(1), B6(2)
    };
    int invert = (r->cspace == CUPS_CSPACE_W);

    if (!bmp) die("out of memory");

    if (r->width >= (unsigned) PRINT_W + MARGIN_X &&
        r->height >= (unsigned) PRINT_H + MARGIN_Y) {
        offx = MARGIN_X;
        offy = MARGIN_Y;
        copy_w = PRINT_W;
        copy_h = PRINT_H;
    } else {
        copy_w = r->width < PRINT_W ? r->width : PRINT_W;
        copy_h = r->height < PRINT_H ? r->height : PRINT_H;
        dstx = (PRINT_W - copy_w) / 2;
        dsty = (PRINT_H - copy_h) / 2;
    }

    while (y < r->height) {
        unsigned rcount;
        if (r->compressed) {
            int rep = fgetc(r->f);      /* row repeat count - 1 */
            if (rep == EOF) { free(bmp); return NULL; }
            rcount = (unsigned) rep + 1;
            if (raster_read_row(r)) { free(bmp); return NULL; }
        } else {
            rcount = 1;
            if (fread(r->row, 1, r->bpl, r->f) != r->bpl) {
                free(bmp);
                return NULL;
            }
        }

        for (; rcount && y < r->height; rcount--, y++) {
            if (y >= offy && y < offy + copy_h) {
                unsigned char *dst = bmp + (size_t) (y - offy + dsty) * PRINT_BPL;
                unsigned x;
                for (x = 0; x < copy_w; x++) {
                    unsigned sx = offx + x;
                    unsigned dx = dstx + x;
                    int black;
                    if (r->bpp == 1) {
                        black = (r->row[sx >> 3] & (0x80 >> (sx & 7))) != 0;
                        if (invert) black = !black;
                    } else {
                        if (sx >= r->bpl) break;
                        black = invert ? r->row[sx] < 128 : r->row[sx] >= 128;
                    }
                    if (black) dst[dx >> 3] |= 0x80 >> (dx & 7);
                }
            }
        }
    }

    {
        size_t i;
        for (i = 0; i < PRINT_RAW; i++) pop += bits[bmp[i]];
    }
    *popcount = pop;
    return bmp;
}

/* ------------------------------------------------------------------ */
/* MFPXDMA stream chunks                                               */
/* ------------------------------------------------------------------ */

static void emit_page_header(struct buf *b)
{
    bput(b, PJL_ENTER, sizeof(PJL_ENTER) - 1);
    bpad_to(b, 0x40);
    {
        unsigned char p[2] = { 0x00, 0x10 };        /* simplex */
        brec(b, 0x11, p, 2);
    }
    bpad_to(b, 0x60);
    brec(b, 0x19, "\0", 1);
    bpad_to(b, 0x80);
    brec(b, 0x15, "\3", 1);
    bpad_to(b, 0xa0);
}

static void emit_start_page(struct buf *b)
{
    unsigned char setup[14] = {
        0x01,                   /* paper: A4 */
        0x00,                   /* media: plain */
        0x3c, 0x3c,             /* 600 x 600 dpi / 10 */
        0x01,                   /* tray: default */
        0x00,                   /* simplex */
        0x01, 0x00,             /* copies = 1 (printer ignores this field;
                                   real copies via pdftopdf page duplication) */
        0x00, 0x00,             /* margin x = 144 (LE) */
        0x00, 0x00,             /* margin y = 131 (LE) */
        0x01, 0x00
    };
    setup[8] = (unsigned char) MARGIN_X;
    setup[9] = (unsigned char) (MARGIN_X >> 8);
    setup[10] = (unsigned char) MARGIN_Y;
    setup[11] = (unsigned char) (MARGIN_Y >> 8);

    brec(b, 0x12, setup, sizeof setup);
    bpad_to(b, 0x20);
    brec(b, 0x13, "\0\0\0\0\0\0\0\0", 8);
    bpad_to(b, 0x40);
    brec(b, 0x32, NULL, 0);
    bpad_to(b, 0x60);
}

static void emit_band(struct buf *b, const unsigned char *jbig, int jlen,
                      unsigned popcount)
{
    unsigned char ih[14] = { 0 };
    unsigned raw = PRINT_RAW;       /* raw bitmap bytes, NOT jbig length */
    unsigned sum = 0;
    int i;

    ih[0] = 0x00;
    ih[1] = 0x20;                   /* 0x10 << 1: JBIG compression */
    ih[2] = PRINT_W & 0xff;  ih[3] = PRINT_W >> 8;
    ih[4] = PRINT_H & 0xff;  ih[5] = PRINT_H >> 8;
    ih[6] = PRINT_W & 0xff;  ih[7] = PRINT_W >> 8;
    ih[8] = PRINT_H & 0xff;  ih[9] = PRINT_H >> 8;
    ih[10] = (unsigned char) raw;
    ih[11] = (unsigned char) (raw >> 8);
    ih[12] = (unsigned char) (raw >> 16);
    ih[13] = (unsigned char) (raw >> 24);
    if ((size_t) raw != (size_t) PRINT_BPL * PRINT_H) die("raw size assert");

    brec(b, 0x0c, ih, sizeof ih);
    bput(b, jbig, (size_t) jlen);
    {
        size_t tail = 0x40 - (b->len & 0x1f);
        bput(b, ESC_FF, sizeof(ESC_FF) - 1);
        bzeros(b, tail - (sizeof(ESC_FF) - 1));
    }

    for (i = 0; i < jlen; i++) sum += jbig[i];
    sum = (sum + 0x29e) & 0xffff;   /* 0x29e = sum of ESC FF bytes */
    {
        unsigned char p[2] = { (unsigned char) sum,
                               (unsigned char) (sum >> 8) };
        brec(b, 0x1a, p, 2);
    }
    while (b->len % 0x20) bzeros(b, 1);
    {
        unsigned char p[4] = { (unsigned char) popcount,
                               (unsigned char) (popcount >> 8),
                               (unsigned char) (popcount >> 16),
                               (unsigned char) (popcount >> 24) };
        brec(b, 0x1f, p, 4);
    }
    while (b->len % 0x20) bzeros(b, 1);
    brec(b, 0x18, NULL, 0);
    bzeros(b, 0x1c);
}

static void emit_close_job(struct buf *b)
{
    brec(b, 0x33, NULL, 0);
    bzeros(b, 28);
    brec(b, 0x17, NULL, 0);
    bzeros(b, 28);
    bput(b, PJL_EXIT, sizeof(PJL_EXIT) - 1);
}

static void write_chunk(const unsigned char *d, size_t n)
{
    if (fwrite(d, 1, n, stdout) != n || fflush(stdout))
        die("write to stdout failed");
}

/* ------------------------------------------------------------------ */

int main(int argc, char *argv[])
{
    struct raster r = { NULL, -1, 0, 0, 0, 0, 0, 0, 0, 0, 0, NULL };
    int page = 0;

    if (argc < 6 || argc > 7) {
        fprintf(stderr,
                "usage: %s job-id user title copies options [file]\n",
                argv[0]);
        return 1;
    }
    r.f = stdin;
    if (argc == 7) {
        r.f = fopen(argv[6], "rb");
        if (!r.f) die("cannot open input file");
    }

    for (;;) {
        int st = raster_next_page(&r);
        if (st < 0) die("invalid or truncated CUPS raster");
        if (st == 0) break;
        page++;
        fprintf(stderr, "INFO: page %d: %ux%u, %u bpp, cspace %u, %ux%u dpi\n",
                page, r.width, r.height, r.bpp, r.cspace, r.resx, r.resy);
        if (r.resx && r.resx != 600)
            fprintf(stderr, "WARNING: expected 600 dpi\n");

        unsigned popcount;
        unsigned char *bmp = read_page_bitmap(&r, &popcount);
        if (!bmp) die("truncated raster data");

        unsigned char *jbig = NULL;
        int jlen = 0;
        if (jbig_encode(bmp, PRINT_W, PRINT_H, &jbig, &jlen))
            die("JBIG encoding failed");
        fprintf(stderr, "INFO: page %d: JBIG %d bytes, %u black pixels\n",
                page, jlen, popcount);

        struct buf ch1 = {0}, ch2 = {0}, ch3 = {0};
        if (page == 1) {
            emit_page_header(&ch1);
            write_chunk(ch1.d, ch1.len);
        }
        emit_start_page(&ch2);
        emit_band(&ch3, jbig, jlen, popcount);
        write_chunk(ch2.d, ch2.len);
        write_chunk(ch3.d, ch3.len);

        free(ch1.d); free(ch2.d); free(ch3.d);
        free(jbig);
        free(bmp);
    }

    if (page > 0) {
        struct buf ch4 = {0};
        emit_close_job(&ch4);
        write_chunk(ch4.d, ch4.len);
        free(ch4.d);
    } else {
        die("no pages in job");
    }

    free(r.row);
    if (argc == 7) fclose(r.f);
    return 0;
}
