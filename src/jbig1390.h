/*
 * jbig1390.h - JBIG (T.82) encoder compatible with the Konica Minolta
 * PagePro 1390MF firmware decoder ("JBigcomp2" from sp701fdr.dll).
 *
 * The firmware stream is a standard single-layer (D=0, P=1) T.82 BIE
 * with L0=128, MX=8, MY=0, order=ILEAVE|SMID (0x03),
 * options=TPDON|TPBON|DPON (0x1c), stripes terminated by SDNORM,
 * prefixed by an 8-byte ATMOVE marker segment that fixes the adaptive
 * template pixel at horizontal offset 8 from line 0:
 *
 *   ff 06 00 00 00 00 08 00   (ESC ATMOVE, YAT=0, ATX=8, ATY=0)
 *
 * Input bitmap: 1 bit per pixel, 1 = black, rows MSB-first and padded
 * to whole bytes (PBM P4 layout).
 */
#ifndef JBIG1390_H
#define JBIG1390_H

int jbig_encode(const unsigned char *pbm_1bpp, int w, int h,
                unsigned char **out_jbig, int *out_len);

#endif
