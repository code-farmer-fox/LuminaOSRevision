#!/usr/bin/env bash
set -euo pipefail

# 参数解析：--no-debug / --serial / --headless / -h|--help
NO_DEBUG=0
SERIAL=0
HEADLESS=0

usage() {
    cat <<'EOF'
Usage: run.sh [options]

Options:
  --no-debug     Disable GDB stub (no -gdb tcp::1234)
  --serial       Write serial output to build/serial.log
  --headless     Run with -nographic; debug output to build/debug.log
  -h, --help     Show this help message
EOF
}

for arg in "$@"; do
    case "$arg" in
        --no-debug) NO_DEBUG=1 ;;
        --serial)   SERIAL=1 ;;
        --headless) HEADLESS=1 ;;
        -h|--help)  usage; exit 0 ;;
        *)
            echo "Unknown option: $arg" >&2
            usage >&2
            exit 2
            ;;
    esac
done

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
IMG="$ROOT/build/floppy.img"
HDD="$ROOT/build/hdd.img"

if [ ! -f "$IMG" ]; then
    echo "Error: $IMG not found. Run build.ps1 first." >&2
    exit 1
fi
if [ ! -f "$HDD" ]; then
    echo "Error: $HDD not found. Run build.ps1 first." >&2
    exit 1
fi

QEMU_ARGS=(
    -drive "file=$IMG,format=raw,if=floppy"
    -drive "file=$HDD,format=raw,if=ide,media=disk"
    -boot a
    -m 32M
    -rtc base=localtime
)

if [ "$HEADLESS" -eq 1 ]; then
    QEMU_ARGS+=(-nographic)
    QEMU_ARGS+=(-debugcon "file:$ROOT/build/debug.log")
else
    QEMU_ARGS+=(-display sdl)
fi

if [ "$SERIAL" -eq 1 ]; then
    QEMU_ARGS+=(-serial "file:$ROOT/build/serial.log")
fi

if [ "$NO_DEBUG" -eq 0 ]; then
    QEMU_ARGS+=(-gdb tcp::1234)
fi

echo "Starting LuminaOS in QEMU..."
exec qemu-system-i386 "${QEMU_ARGS[@]}"