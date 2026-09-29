# CHOMPI — Hardware, Revision 4

The production files for **CHOMPI**'s two internal circuit boards: schematic, BOM, EAGLE
PCB files, and the full fabrication package that was used by Electro-Smith for manufacturing.

---

## What this is

Two boards, built around the Daisy Seed, laid out as one v-scored panel:

- **Bottom board** — the Daisy Seed2 DFM, a PCM3060 second codec, the MP2722 charger for the
  LiPo battery, USB-C, microSD, MEMS microphone, five 3.5 mm jacks, etc.
- **Top board** — 28 Kailh hot-swap key sockets read through CD4021 shift registers, six
  encoders, 35 addressable RGB LEDs, etc.

1 oz copper, ENIG, black soldermask, SMT assembly on both sides.

## What's in the folder

```
CHOMPI_Rev4_Schematic.pdf     the schematic
CHOMPI_Rev4_BOM.csv           assembly parts list: quantity, manufacturer part number, value,
                              package, designators, etc.
board files/                  the EAGLE project files
  CC_Chompi_Rev4.sch            schematic
  CC_Chompi_Rev4.brd            both boards in one .brd layout
  CC_Chompi_Rev4_1X1.brd        the same boards on the fabrication panel, with rails
fabrication/                  mfg package ordered September 2023
  CC_Chompi_Rev4_1X1.zip        gerbers, drill file, v-score file
  CC_Chompi_Rev4-smd_cleaned_centroid.txt   pick-and-place positions
  CC_Chompi_Rev4_1X1_ordering.txt           the fabrication parameters
```

## What's not here

- **The enclosure.** The six enclosure panels have their own folder.
- **Parts list.** The BOM names a manufacturer part number for every placed part.
- **The rest of the instrument.** The Daisy Seed2 DFM is a separate module; the battery,
  keycaps, knobs and cables aren't included in these files, most of which were custom parts orders.

If you build your own hardware from these files, we ask that you name it something else to
avoid trademark infringement.

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
