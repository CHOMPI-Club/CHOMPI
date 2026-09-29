# CHOMPI — Bootloader v6.4 (beta)

The internal bootloader for **CHOMPI**. It runs as soon as the Daisy receives power, installs new 
application firmware from the SD card or over DFU, and starts the application.

This v6.4 bootloader never actually shipped on units (which used the original **v6.2** bootloader),
but was created to improve the stability of the Daisy Seed's integration with CHOMPI's hardware... 
so it's technically still considered a BETA.

---

## What this is

CHOMPI's original v6.2 bootloader was forked from the [Electrosmith Daisy Bootloader](https://github.com/electro-smith/DaisyBootloader)
around their v6.0 timeline. This new v6.4 version
adds CHOMPI's hardware-specific code onto Electrosmith's **v6.4** source and the libDaisy QSPI driver
it builds against, which restores the flash chip's status register at start-up.
`CHANGELOG.md` lists what changed from v6.2.

## Building

Toolchain: Arm GNU Toolchain 13.3.rel1, the one the released binary was built with.

```bash
CHOMPI_TOOLCHAIN_BIN=/path/to/arm-gnu-toolchain-13.3.rel1/bin ./build-bootloader.sh
```

A correct build is byte-identical to the release: chompi_bootloader_v6_4.bin, 119,612 bytes,
md5 580b187fec405849fb401eb699281e4c.

## Installing

The SD-card update never touches the bootloader; it lives in the processor's internal flash.
Install it over DFU (BOOT held while the Daisy powers up) or with an ST-Link, at `0x08000000`:

```bash
dfu-util -a 0 -s 0x08000000:leave -D bootloader/build/chompi_bootloader_v6_4.bin -d ,0483:df11
```

## Repository layout

```
bootloader/     main.cpp, board bring-up (boot_hardware.h), LED driver, linker script, Makefile
shared/         the bootloader and DFU implementation — Electrosmith's v6.4 source
cube_dfu/       ST USB device / DFU middleware and HAL drivers
libs/libDaisy/  vendored libDaisy (MIT) — see LIBDAISY_PATCH.md
```

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
