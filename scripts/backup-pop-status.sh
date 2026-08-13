#!/usr/bin/env bash
# SPDX-License-Identifier: MIT
#
# Read a PopStatus/Panda Status-family ESP32-C3 without writing to it, then make
# a separate share-safe copy with the complete NVS partition redacted.
#
# Usage:
#   ./scripts/backup-pop-status.sh /dev/ttyUSB0
#   ./scripts/backup-pop-status.sh /dev/ttyACM0 ~/popstatus-backups
#
# The raw image is a private recovery backup and can contain Wi-Fi and Bambu
# credentials. Do not upload or send it. The "-nvs-redacted" image is intended
# for firmware analysis/sharing; its NVS data is deliberately unusable.

set -euo pipefail
umask 077

FLASH_BYTES=$((0x400000))
NVS_OFFSET=$((0x9000))
NVS_BYTES=$((0x3000))
BAUD=460800

usage() {
    cat <<'EOF'
Usage: backup-pop-status.sh SERIAL_PORT [OUTPUT_DIRECTORY]

Examples:
  ./scripts/backup-pop-status.sh /dev/ttyUSB0
  ./scripts/backup-pop-status.sh /dev/ttyACM0 ~/popstatus-backups

This script only reads the device. It creates:
  * a private full-flash backup (contains credentials; mode 600)
  * an NVS-redacted copy for sharing (mode 644)
EOF
}

if [[ ${1:-} == "-h" || ${1:-} == "--help" ]]; then
    usage
    exit 0
fi

if [[ $# -lt 1 || $# -gt 2 ]]; then
    usage >&2
    printf '\nLinux serial ports are commonly /dev/ttyUSB0 or /dev/ttyACM0.\n' >&2
    exit 2
fi

PORT=$1
OUT_DIR=${2:-"$PWD"}

if [[ ! -e $PORT ]]; then
    printf 'Serial port not found: %s\n' "$PORT" >&2
    printf 'Try: ls -1 /dev/ttyUSB* /dev/ttyACM* /dev/serial/by-id/* 2>/dev/null\n' >&2
    exit 2
fi

if command -v esptool >/dev/null 2>&1; then
    ESPTOOL=(esptool)
elif command -v esptool.py >/dev/null 2>&1; then
    ESPTOOL=(esptool.py)
elif python3 -c 'import esptool' >/dev/null 2>&1; then
    ESPTOOL=(python3 -m esptool)
else
    cat >&2 <<'EOF'
esptool is required. On Debian/Raspberry Pi OS, install it with:
  sudo apt install python3-esptool

Alternatively, use a non-system Python environment:
  python3 -m pip install --user esptool
EOF
    exit 2
fi

mkdir -p "$OUT_DIR"
OUT_DIR=$(cd "$OUT_DIR" && pwd)
STAMP=$(date -u +%Y%m%dT%H%M%SZ)
PREFIX="$OUT_DIR/popstatus-stock-$STAMP"
RAW="$PREFIX-private-full.bin"
REDACTED="$PREFIX-nvs-redacted.bin"
TMP_RAW="$RAW.partial"

cleanup() { rm -f "$TMP_RAW"; }
trap cleanup EXIT

printf '\nReading 4 MiB from %s. This does not change the device.\n' "$PORT"
"${ESPTOOL[@]}" --chip esp32c3 --port "$PORT" --baud "$BAUD" \
    read_flash 0x0 "$FLASH_BYTES" "$TMP_RAW"

if [[ $(wc -c < "$TMP_RAW" | tr -d ' ') != "$FLASH_BYTES" ]]; then
    printf 'Backup size is wrong; keeping no partial backup.\n' >&2
    exit 1
fi

printf 'Verifying the private backup against the device...\n'
"${ESPTOOL[@]}" --chip esp32c3 --port "$PORT" --baud "$BAUD" \
    verify_flash 0x0 "$TMP_RAW"

mv "$TMP_RAW" "$RAW"
chmod 600 "$RAW"

# Redact every byte of NVS in the *copy*. This covers currently known and any
# unknown credential keys without needing to understand the OEM NVS schema.
cp "$RAW" "$REDACTED"
dd if=/dev/zero of="$REDACTED" bs=1 seek="$NVS_OFFSET" count="$NVS_BYTES" \
    conv=notrunc status=none
if ! cmp -n "$NVS_BYTES" \
    <(dd if="$REDACTED" bs=1 skip="$NVS_OFFSET" count="$NVS_BYTES" status=none) \
    /dev/zero; then
    printf 'Could not verify NVS redaction. Keeping the private backup only.\n' >&2
    rm -f "$REDACTED"
    exit 1
fi
chmod 644 "$REDACTED"

if command -v sha256sum >/dev/null 2>&1; then
    sha256sum "$RAW" "$REDACTED" > "$PREFIX-SHA256SUMS.txt"
else
    shasum -a 256 "$RAW" "$REDACTED" > "$PREFIX-SHA256SUMS.txt"
fi
chmod 600 "$PREFIX-SHA256SUMS.txt"

cat <<EOF

Backup complete.

PRIVATE — do not send/upload (contains Wi-Fi/Bambu credentials):
  $RAW

SAFE TO SHARE for firmware analysis (entire NVS partition zeroed):
  $REDACTED

Checksums:
  $PREFIX-SHA256SUMS.txt

The NVS-redacted image is intentionally not a credential-preserving restore
backup. Keep the private full image somewhere safe if the owner wants a path
back to their exact stock configuration.
EOF
