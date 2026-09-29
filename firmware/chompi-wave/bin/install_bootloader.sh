#!/bin/bash
# Installs the CHOMPI bootloader (V6.2) into the Daisy's internal flash over USB DFU.
# Recovery and factory use only: the normal firmware update from the SD card never touches
# the bootloader, and you do not need this script to install WAVE.
# Requires dfu-util, and the Daisy Seed in STM32 system DFU mode (BOOT held while it powers up).
set -e
cd "$(dirname "$0")"
command -v dfu-util >/dev/null 2>&1 || { echo "dfu-util not found — install it first (brew install dfu-util / apt install dfu-util)." >&2; exit 1; }
BINNAME=CHOMPI_Bootloader_V6_2_0.bin
[ -f "$BINNAME" ] || { echo "$BINNAME not found next to this script." >&2; exit 1; }
dfu-util -a 0 -s 0x08000000:leave -D "./$BINNAME" -d ,0483:df11
