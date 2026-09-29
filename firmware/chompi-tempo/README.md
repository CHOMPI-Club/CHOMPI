# CHOMPI — TEMPO v1.0 Firmware

The pattern generator firmware for **CHOMPI**: the counterpart to TAPE.

---

## Firmware description

TEMPO is a dual-engine pattern generator. The chromatic engine plays a single sample chromatically
across the keybed, and the slice engine chops a single sample into sixteen individual slices. Each engine
has 8 voices, its own pattern generator and its own MIDI channel, all linked to the master clock.

## Building

Toolchain: Arm GNU Toolchain 13.3.rel1 (GCC 13.3.1). Unlike TAPE and WAVE, which use GNU Arm
Embedded 10.3-2021.10, TEMPO must be built with this newer compiler, the one the released
firmware was built with.

Put the 13.3.rel1 `bin` folder first on your PATH, check the version, then run `make` from
`code/src`:

```bash
export PATH="/Applications/ArmGNUToolchain/13.3.rel1/arm-none-eabi/bin:$PATH"
arm-none-eabi-gcc --version    # should say 13.3.1
cd code/src
make
```

The output is `code/src/build/CHOMPI.bin`. See the [firmware build guide](../README.md) for the
rest of the setup.

## Repository layout

```
code/src/                 the firmware
code/libs/                vendored libDaisy, DaisySP, coreJSON (MIT)
code/Chompi_Bootloader/   the bootloader this firmware is loaded by
code/bms_test/            standalone battery-management bring-up example
bin/                      bootloader binary and install script
```

## SD card layout

The card holds the firmware binary, the sample folders, and two JSON files: `options.json`
(global settings) and `presets.json` (per-slot knob settings state). All samples are 48 kHz,
16-bit stereo WAV, and are limited to 10 seconds per sample slot.

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
