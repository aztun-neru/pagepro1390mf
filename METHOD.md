# How the PagePro 1390MF protocol was reverse-engineered

## The problem

The PagePro 1390MF is a GDI ("host-based") printer: it has no PCL/PostScript
interpreter, so the host must render each page into the printer's own raster
format. Konica Minolta ships a Windows-only driver (`sp701fdr.dll`) speaking a
proprietary protocol. The goal was to reproduce that protocol on Linux/macOS.

## Approach

Black-box USB capture of the Windows driver, plus targeted disassembly of the
driver DLL to resolve ambiguities. No code was copied — the wire format was
derived from observation and re-implemented from scratch.

## Steps

1. **Capture ground truth.** The Windows driver was installed in an offline
   Windows 10 VM with the printer queue pointed at the `FILE:` port. Printing
   a known input (blank page, black square, text + geometry) yields the exact
   byte stream the driver would send over USB. These are the `ref-*.prn`
   fixtures in this repo.

2. **Find the framing.** The stream is a sequence of records
   `[0x0B][len][cmd][params... LE][checksum]`, `checksum = -(sum)&0xff`,
   zero-padded to 0x20 boundaries and wrapped in PJL. Records `0x11/0x19/0x15`
   open the job, `0x12` opens a page, `0x0c` carries the band header + image
   data, `0x1a` a running checksum, `0x1f` the black-pixel popcount,
   `0x33/0x17` close the job.

3. **Identify the image compression.** The image payload is JBIG (ITU-T T.85),
   a bi-level arithmetic codec. The wire format is `ATMOVE + PSCD + SDNORM`
   with the 20-byte BIH header stripped — the decoder already knows the page
   geometry, so the header is redundant.

4. **The critical fix: L0.** The first encoder used the JBIG default L0=128
   (128-line stripes). The reference streams only decode to the correct raster
   with **L0 = full image height** (6752 for A4 at 600 dpi). That one constant
   was the difference between a black/blank page and a correct print. Final
   encoder parameters: L0=height, MX=8, MY=0, order ILEAVE|SMID,
   options 0x1c (TPDON|TPBON|DPON), JBG_FIX_AT (AT fixed at line 8).

5. **The USB transport trap.** The printer only accepts the job when it is
   written as four separate USB transfers — 0xa0 bytes, 0x100 bytes, the band,
   0x62 bytes. CUPS's stock `usb://` backend concatenates writes and the
   printer silently ignores the job, so a custom backend reproduces the exact
   transfer boundaries.

6. **Verify.** The generator reproduces all three reference streams
   byte-for-byte. The CUPS filter's output decodes bit-exact to its input
   raster. Finally, the stream prints correctly on live hardware.

## Why this works

A GDI printer is essentially a dumb raster sink: the host does all the
rendering, and the printer runs one fixed decompression + framing parser. Once
the framing and JBIG parameters match what the firmware expects, any host can
drive it — no firmware emulation needed.
