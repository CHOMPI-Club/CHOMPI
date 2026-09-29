#!/usr/bin/env bash
# build-bootloader.sh — one-command build for the CHOMPI bootloader v6.4 (beta).
#
#   ./build-bootloader.sh          build
#   ./build-bootloader.sh clean    purge all build output, then build
#
# TOOLCHAIN IS PINNED TO Arm GNU Toolchain 13.3.rel1 (arm-none-eabi).
# The released v6.4 beta was built with it, and a correct build of this tree is
# byte-identical to that release:
#
#   chompi_bootloader_v6_4.bin   119,612 bytes   md5 580b187fec405849fb401eb699281e4c
#
# Do NOT use Homebrew's arm-none-eabi-gcc — it is compiler-only, with no newlib.
#
# Point CHOMPI_TOOLCHAIN_BIN at your arm-none-eabi .../bin directory:
#   export CHOMPI_TOOLCHAIN_BIN=/path/to/arm-gnu-toolchain-13.3.rel1-*/bin

set -euo pipefail

TOOLCHAIN_BIN="${CHOMPI_TOOLCHAIN_BIN:-}"
if [ -z "$TOOLCHAIN_BIN" ]; then
  if command -v arm-none-eabi-gcc >/dev/null 2>&1; then
    TOOLCHAIN_BIN="$(dirname "$(command -v arm-none-eabi-gcc)")"
    echo "note: using arm-none-eabi-gcc from PATH ($TOOLCHAIN_BIN)"
    echo "      set CHOMPI_TOOLCHAIN_BIN to pin 13.3.rel1 explicitly."
  else
    echo "ERROR: no arm-none-eabi toolchain found." >&2
    echo "Set CHOMPI_TOOLCHAIN_BIN to the bin/ directory of Arm GNU Toolchain 13.3.rel1." >&2
    exit 1
  fi
fi
[ -x "$TOOLCHAIN_BIN/arm-none-eabi-gcc" ] || { echo "ERROR: no arm-none-eabi-gcc in $TOOLCHAIN_BIN" >&2; exit 1; }
export PATH="$TOOLCHAIN_BIN:$PATH"

VER="$(arm-none-eabi-gcc -dumpversion)"
echo "== Toolchain: GCC $VER =="
[ "$VER" = "13.3.1" ] || echo "   WARNING: expected 13.3.1. Other versions produce a different binary."

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
LIBDAISY_DIR="$SCRIPT_DIR/libs/libDaisy"
BOOT_DIR="$SCRIPT_DIR/bootloader"

if [ "${1:-}" = "clean" ]; then
  echo "== Cleaning all build output =="
  rm -rf "$BOOT_DIR/build" "$LIBDAISY_DIR/build"
fi

echo "== Building libDaisy ==";       make -C "$LIBDAISY_DIR" -j8 -s
echo "== Building the bootloader =="; make -C "$BOOT_DIR" -j8

BIN="$BOOT_DIR/build/chompi_bootloader_v6_4.bin"
RELEASED="580b187fec405849fb401eb699281e4c"
if command -v md5sum >/dev/null 2>&1; then HASH="$(md5sum "$BIN" | cut -d' ' -f1)"; else HASH="$(md5 -q "$BIN")"; fi

echo ""; echo "== Result =="
arm-none-eabi-size "$BOOT_DIR/build/chompi_bootloader_v6_4.elf"
echo "chompi_bootloader_v6_4.bin: $(wc -c < "$BIN" | tr -d ' ') bytes  (released v6.4 beta: 119,612)"
if [ "$HASH" = "$RELEASED" ]; then
  echo "md5: $HASH — MATCHES the released v6.4 beta"
else
  echo "md5: $HASH — DIFFERS from the released v6.4 beta ($RELEASED)"
  echo "     Expected if you changed the source. If you did not, the toolchain is not Arm GNU 13.3.rel1 (this build used GCC $VER)."
fi
echo ""
echo "To install: write $BIN to 0x08000000 over DFU or with an ST-Link — see README.md."
