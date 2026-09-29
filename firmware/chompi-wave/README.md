# CHOMPI — WAVE v1.0 Firmware

The bonus wavetable synthesizer firmware for **CHOMPI**.

Of the three CHOMPI firmwares, WAVE is the smallest and the simplest, and it has the most
room left in the code space — which also makes it a good candidate to use if you'd like to
build your own custom firmware.

---

## Firmware description

WAVE is an 8-voice wavetable synthesizer. Parts of it will feel
familiar if you've spent time with TAPE or TEMPO, but it's also its own thing —
intentionally stripped back, while keeping some CHOMPI flavor throughout.

Eight voices, each a wavetable oscillator through a resonant filter and an amp envelope,
into a shared chain of delay, reverb, compression and saturation. Two LFOs (pitch and
filter), a 32-step sequencer with gate length, 14 preset slots plus a defaults slot, and
MIDI in and out over TRS and USB.

WAVE, TAPE and TEMPO are siblings, but contain many differences: each uses its own
card layout and preset format, and they're not interchangeable.


### Where things are in `code/src`

```
chompi_main.cpp        entry point: audio callback, main loop, SD timer callback, boot sequence
subtractiveEngine.h    the engine: voices, LFOs, the FX chain, voice allocation
WavetableManager.h     the wavetable oscillator (frame scan + crossfade) and the boot-time table loader
FileStreamingManager.* the SD request queue for the loader
ui.h                   page plumbing: events, page switching, preset file writing
NormalPage.h           play mode: knobs, keys, LEDs
MenuPage.h             the shift layer (everything reached with SHIFT held)
Sequencer.h            the 32-step sequencer
clockManager.h         tempo and clock division
MidiManager.h          MIDI in/out over TRS and USB
OptionsManager.h       options.json
PresetManager.h        presets.json
hardware.h             the CHOMPI hardware: encoders, keys, switches, LEDs, battery
encoder.h / .cpp       encoder driver
temp_led_stuff.h       LED driver (ui_utils.h: LED flush/clear helpers)
DJFilter.h, BasicMMF.h, reverb.h, fx_engine.h, limiter.h, InterpolatedDelayLine.h, EnvFollower.h
                       the DSP blocks the engine is built from
BootPage.h, RainbowWavePage.h, NoSDPage.h, TestPage.h
                       boot animation, rainbow-wave animation, no-card screen, hardware test mode
chompi_sram.lds        linker script (the firmware runs from SRAM, placed there by the bootloader)
```

## Building

Toolchain: GNU Arm Embedded 10.3-2021.10. Newer compilers can technically build it,
but the results can sometimes intermittently cause the SD card communication to break. We've
found this compiler to work best.

## Repository layout

```
code/src/                 the firmware
code/libs/                vendored libDaisy, DaisySP, coreJSON (MIT)
code/Chompi_Bootloader/   the bootloader this firmware is loaded by
code/bms_test/            standalone battery-management bring-up example
bin/                      bootloader binary and install script
wavetables/               the seven factory wavetables
```

## SD card layout

The card holds the firmware binary, the wavetables, and two JSON files: `options.json`
(MIDI and config settings) and `presets.json` (knob settings for each preset slot).

```
CHOMPI.bin                        the firmware (one per card)
wavetable01.wav … wavetable07.wav the wavetables — up to seven, loaded in alphabetical order
options.json                      MIDI in/out channel, clock out, CC in, CC out
presets.json                      saved preset settings
```

The firmware loads the first seven `.wav` files it finds on the card, in alphabetical
order, so naming them `wavetable01.wav` through `wavetable07.wav` lands them where you'd
expect. The format is the common Serum wavetable layout: 32-bit float, mono, 33 waves of
2048 samples each, sample data starting at byte 136 (the firmware reads raw from that
offset rather than parsing the WAV header).

One thing to keep in mind: presets remember their wavetable by its slot number, not by
its name. If you rearrange or rename the files on the card, saved presets will point at
different tables.

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
