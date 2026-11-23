# PagePro 1390MF CUPS Driver — build & install

## Build

```sh
make
```

This produces `build/rastertopagepro1390`. On macOS a universal (arm64 + x86_64) binary is built automatically.

## Linux install

```sh
./install/install-linux.sh
```

Requires `python3-usb` (installed automatically if missing). Creates queue `pagepro1390`.

## macOS install

```sh
./install/install-macos.sh
```

Requires Homebrew. Installs `libusb` and `pyusb` if missing.

### SIP caveat

On modern macOS, writing to `/usr/libexec/cups/` is blocked by System Integrity Protection. The installer detects this and prints instructions. Options:

1. **Disable SIP** (not recommended): reboot to recovery mode, run `csrutil disable`, reboot, re-run the installer.
2. **Network print alternative** (recommended): share the queue from a Linux host via IPP and add it on macOS as a network printer:

```sh
lpadmin -p PagePro1390 -E -v ipp://HOST:631/printers/pagepro1390 -m everywhere
```

Replace `HOST` with the Linux host's address.
