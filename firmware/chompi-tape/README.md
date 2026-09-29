# CHOMPI — TAPE v2.0 Firmware

The flagship sampler firmware for **CHOMPI**: the main firmware every CHOMPI ships with.

---

## Firmware description

TAPE is a 7-voice sampler and varispeed tape looper. Samples stream from the microSD card; the
looper and sample buffer record into SDRAM. Seven streaming voices, varispeed playback,
tape-style looper, delay and reverb, and MIDI in and out over TRS and USB.

## Building

Toolchain: GNU Arm Embedded 10.3-2021.10. Newer compilers overflow the firmware's SRAM region
and fail at the link step.

Warning: This firmware is pretty much at capacity, with only 376 bytes of SRAM space remaining.
This means that any additional tweaks or features will very likely require sacrificing something
to free up the necessary code space.

## Repository layout

```
code/src/                 the firmware
code/libs/                vendored libDaisy, DaisySP, coreJSON (MIT)
code/Chompi_Bootloader/   the bootloader this firmware is loaded by
code/bms_test/            standalone battery-management bring-up example
bin/                      bootloader binary and install script
```

## SD card layout

The card holds the firmware binary, the sample banks, and two JSON files: `options.json`
(global settings) and `presets.json` (per-slot knob positions). Samples are named
`<instrument>_<bank><slot>.wav` — 48 kHz, 16-bit stereo — with a matching `_double` variant
used for high-pitched playback.

## Support Guidelines

This is a discontinuation open-source release. As such, this repo is intended to be a permanent
source for files and documentation, and will likely not be receiving updates in the future. If you wish
to customize your own project, we recommend cloning this repo into your own GitHub.

## Community

Even though this version of CHOMPI is now discontinued, the CLUB is expanding. If you want to
discuss this project, share your creations, see what other users have made on their CHOMPI, feel
free to check out the CHOMPI Open Source channel on the Chase Bliss Discord.

## License

MIT — see [`LICENSE`](../../LICENSE) at the root of this repo. [`THIRD_PARTY.md`](../../THIRD_PARTY.md)
lists the work this builds on. The CHOMPI name and marks are not covered by the license — see
[`TRADEMARKS.md`](../../TRADEMARKS.md).
