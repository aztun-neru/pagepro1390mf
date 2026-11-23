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

echo "Installing PagePro 1390MF driver for Linux..."

# Install filter
sudo install -m 0755 "$FILTER_BIN" /usr/lib/cups/filter/rastertopagepro1390

# Install backend
sudo install -m 0700 "$BACKEND_BIN" /usr/lib/cups/backend/pagepro1390
sudo chown root:root /usr/lib/cups/backend/pagepro1390

# Install PPD
sudo install -m 0644 "$PPD_FILE" /usr/share/cups/model/pagepro1390mf.ppd

# Check PyUSB dependency for the backend
if ! sudo /usr/bin/python3 -c 'import usb.core' 2>/dev/null; then
    echo "Installing python3-usb dependency..."
    sudo apt-get install -y python3-usb
fi

# Create and enable print queue
echo "Configuring print queue 'pagepro1390'..."
sudo lpadmin -p pagepro1390 -E -v 'pagepro1390://usb' -P /usr/share/cups/model/pagepro1390mf.ppd
sudo cupsaccept pagepro1390
sudo cupsenable pagepro1390

echo ""
echo "Installation complete. Queue status:"
lpstat -p pagepro1390 -l
