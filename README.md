# pagepro1390mf — CUPS driver for Konica Minolta PagePro 1390MF

A Linux/macOS CUPS driver for the Konica Minolta **PagePro 1390MF**, a GDI
(host-based) laser printer with no PCL/PostScript support. The printer's only
official driver is Windows-only, so this is a clean-room reimplementation of
its proprietary "MFPXDMA" wire protocol.

## Status

Working. Prints text, raster and vector content at 600×600 dpi on A4, verified
against the original Windows driver byte-for-byte and on live hardware.

## How it works

```
CUPS raster (600 dpi, 1 bpp)
  → rastertopagepro1390 filter (JBIG encode + MFPXDMA framing)
  → pagepro1390 backend (USB, 160/96/band/98-byte writes)
  → printer
```

- **JBIG** (ITU-T T.85) encoder compatible with the printer's `JBigcomp2`
  decoder: L0 = image height, MX=8, options 0x1c (TPDON|TPBON|DPON),
  JBG_FIX_AT, BIH stripped from the output.
- **MFPXDMA** record framing: `[0x0B][len][cmd][params LE][checksum]` with
  0x20-byte zero padding. Records 0x11/0x19/0x15 open the job, 0x12 opens a
  page, 0x0c carries the band header + image, 0x1a/0x1f carry checksum and
  popcount, 0x33/0x17 close the job.

## Build & install

See [INSTALL.md](INSTALL.md). Short version:

```sh
make                              # build/rastertopagepro1390 (universal on macOS)
make test                         # encoder + backend unit tests
sudo ./install/install-linux.sh   # Linux
./install/install-macos.sh        # macOS (see SIP caveat in INSTALL.md)
```

## Reverse engineering

See [METHOD.md](METHOD.md) for the full story.

## Layout

- `src/` — CUPS raster filter + JBIG encoder + vendored libjbig (GPL jbigkit)
- `backend/` — CUPS USB backend (Python/PyUSB) + its unit tests
- `ppd/` — PPD
- `tests/` — encoder test
- `ref-*.prn` — byte-captured reference streams from the Windows driver

## License

GPL-2.0 — libjbig is GPL-2.0 and the driver links against it.

## Disclaimer

Independent reverse-engineering project. No affiliation with Konica Minolta.
"PagePro" and "Konica Minolta" are trademarks of their respective owners.
