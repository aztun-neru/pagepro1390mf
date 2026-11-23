#!/usr/bin/env bash
set -euo pipefail

# Resolve script directory
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
ROOT_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"

FILTER_BIN="$ROOT_DIR/build/rastertopagepro1390"
BACKEND_BIN="$ROOT_DIR/backend/pagepro1390"
PPD_FILE="$ROOT_DIR/ppd/pagepro1390mf.ppd"

# Verify required files exist
for f in "$FILTER_BIN" "$BACKEND_BIN" "$PPD_FILE"; do
    if [ ! -f "$f" ]; then
        echo "ERROR: Required file missing: $f" >&2
        echo "Run 'make' first to build the filter." >&2
        exit 1
    fi
done

echo "Installing PagePro 1390MF driver for macOS..."

# Check Homebrew
if ! command -v brew >/dev/null 2>&1; then
    echo "ERROR: Homebrew not found. Install it from https://brew.sh" >&2
    exit 1
fi

# Install libusb
if ! brew list libusb >/dev/null 2>&1; then
    echo "Installing libusb via Homebrew..."
    brew install libusb
fi

# Ensure PyUSB is available for python3
if ! python3 -c 'import usb' 2>/dev/null; then
    echo "Installing pyusb via pip3..."
    pip3 install --user pyusb
    echo "WARNING: If CUPS runs the backend as root, root's python3 must also see pyusb."
    echo "         You may need: sudo pip3 install pyusb"
fi

# SIP detection for /usr/libexec/cups
if [ -w /usr/libexec/cups ] 2>/dev/null; then
    echo "Writing filter and backend to /usr/libexec/cups/..."
    sudo install -m 0755 "$FILTER_BIN" /usr/libexec/cups/filter/rastertopagepro1390
    sudo install -m 0700 "$BACKEND_BIN" /usr/libexec/cups/backend/pagepro1390
else
    echo ""
    echo "WARNING: Cannot write to /usr/libexec/cups/ (SIP protection)."
    echo ""
    echo "To install locally, either:"
    echo "  1. Disable SIP: reboot to recovery mode, run 'csrutil disable', reboot"
    echo "  2. Use the network-print alternative (see INSTALL.md):"
    echo "     share the queue from a Linux host via IPP and print from macOS"
    echo "     as a network printer."
    echo ""
    echo "Continuing with PPD installation and queue setup..."
fi

# Install PPD
mkdir -p /Library/Printers/PPDs/Contents/Resources
sudo install -m 0644 "$PPD_FILE" /Library/Printers/PPDs/Contents/Resources/pagepro1390mf.ppd

# Create print queue
echo "Configuring print queue 'pagepro1390'..."
lpadmin -p pagepro1390 -E -v 'pagepro1390://usb' -P /Library/Printers/PPDs/Contents/Resources/pagepro1390mf.ppd

echo ""
echo "Installation complete. Queue status:"
lpstat -p pagepro1390 -l
