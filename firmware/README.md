

# CHOMPI Firmware: Build Quickstart (macOS)

This takes you from source code to a firmware build flashed onto your CHOMPI. The firmware runs on a Daisy Seed (STM32H750) and uses the libDaisy / DaisySP libraries, which are already included in `code/libs/`.

## 1. Get what you need

### Software

You need three tools:

| Tool | What it does |
|------|--------------|
| `arm-none-eabi-gcc` **10.3-2021.10** (**13.3.rel1** for TEMPO) | Compiles code for the Daisy's ARM chip |
| `make` | Runs the build (comes with the Xcode command line tools) |
| `dfu-util` (optional) | Flashes the bootloader to CHOMPI (not your code) |

**Important:** libDaisy only supports the older ARM GCC **10.3-2021.10**, the Daisy toolchain default. Newer versions are not officially supported by Electrosmith, and testing has shown that compiling with newer versions can create issues with the SD card communication. Use 10.3.1 for **TAPE** and **WAVE** or your own custom firmwares.

**The exception is TEMPO**, which must be built with the Arm GNU Toolchain **13.3.rel1** (GCC 13.3.1). You can install it alongside 10.3 and switch between them using the PATH setting below.

The easiest install is the official Daisy toolchain script (macOS):
https://github.com/electro-smith/DaisyToolchain (see `macOS/install.command`).
It installs the compiler via Homebrew, and also `openocd` and `dfu-util`.

If you already have other GCC versions installed, that's fine: just put version 10.3's `bin` folder first on your PATH. For example, if it is installed at `/Applications/ArmGNUToolchain/10.3-2021.10`:

```bash
export PATH="/Applications/ArmGNUToolchain/10.3-2021.10/bin:$PATH"
```

Add that line to `~/.zshrc` to make it permanent. Check it worked:

```bash
arm-none-eabi-gcc --version    # should say 10.3.1
```

To build TEMPO, point your PATH at the 13.3.rel1 `bin` folder instead (in that terminal window only, or edit your `~/.zshrc` line). For example, if it is installed at `/Applications/ArmGNUToolchain/13.3.rel1`:

```bash
export PATH="/Applications/ArmGNUToolchain/13.3.rel1/arm-none-eabi/bin:$PATH"
arm-none-eabi-gcc --version    # should say 13.3.1
```

### Hardware (recommended for writing firmware)

If you plan on writing or editing firmware, it is highly recommended to get an additional Daisy Seed with a [debug header](https://www.digikey.com/en/products/detail/amphenol-cs-fci/20021111-00010T4LF/2209072) and an [STLINK-V3MINIE debugger](https://www.digikey.com/en/products/detail/stmicroelectronics/STLINK-V3MINIE/16284301).

The debug header must be soldered to the Daisy Seed, and the back panel of the enclosure will not fit with the header installed. That is why two Daisy Seeds are recommended: one with the header for development, and one without for the enclosure.

If you only plan to load existing or community-made firmware onto your CHOMPI and do not require debugging features, you may not need these. Any firmware can be swapped out manually via the SD card, so even though it'll probably be a bit more clunky, it should technically work just fine.

## 2. Navigate to the correct directory

All source code lives in `code/src/`, and this is the folder you run `make` from. Using Terminal, navigate to the `code/src` folder of the firmware that you want to build. 

## 3. Build

Run the command:
```bash
make
```

When this finishes you will see a memory usage table, and the output files appear in `code/src/build/`:

- `CHOMPI.bin`: the firmware you flash to the board
- `CHOMPI.elf`: same firmware plus debug info (used with gdb)

Handy variants:

```bash
make clean      # delete build/ and start fresh
make -j4        # build faster using 4 CPU cores
```

Some yellow or purple "warning" lines (like unused variables) are normal. Only lines that say `error` mean the build failed.

If you ever need to rebuild the libraries themselves (rare) you will need to navigate to the `code/libs/libDaisy` or `code/libs/DaisySP` folder and run `make` there.

## 4. Flash it to the board

CHOMPI runs as a "BOOT_SRAM" app, meaning it is loaded by the Daisy bootloader from the board's QSPI flash.

All you need to do for this kind of app is to take the CHOMPI.bin from the `/build` folder and put it on the SD card, deleting any other .bin file there first. When you power on your CHOMPI, it will reprogram QSPI flash with your new app which is indicated by the slow rainbow LED pattern.

**First time only:** if the board has never had the bootloader installed, navigate to the `bin/` folder in Terminal and run `./install_bootloader.sh`, then flash the app as above. Make sure you put the Daisy Seed in DFU mode first by holding the BOOT button and tapping RESET.

## 5. Use the SD card

The firmware loads sound files (`.wav` files) from the SD card, and also stores `options.json` and `presets.json` there. Put your wavetable or sample files in the top-level folder of a FAT-formatted card.

## 6. Debugging

**This requires an STLINK-V3MINIE debugger.**

To find out what is happening in your program, you can use the debugging tools. Open a new terminal window and run `openocd -f interface/stlink.cfg -f target/stm32h7x.cfg`
In another window, navigate into the `/build` folder and run `arm-none-eabi-gdb CHOMPI.elf`. In this same window, run `target remote localhost:3333` and you will be connected.

## Troubleshooting

| Problem | Fix |
|---------|-----|
| `arm-none-eabi-gcc: command not found` | The toolchain isn't on your PATH (see step 1) |
| `make` says "Nothing to be done" but you changed a file | Run `make clean && make` |
|Debugger indicates SD card is stuck|Try switching compiler versions|
|CHOMPI is not responding to input | Use the debugger to find where in the code it got stuck |

If you ever get stuck to the point where CHOMPI is not responding and all else fails, you can erase and reflash the bootloader. https://flash.daisy.audio contains a web tool to do this.
