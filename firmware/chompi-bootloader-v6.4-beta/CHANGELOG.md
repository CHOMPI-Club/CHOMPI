# Changelog

## v6.4 (beta)

This bootloader never shipped on units — they run the original **v6.2** version. 
It was created to improve the stability of the Daisy Seed's integration with CHOMPI's hardware: 
it adds CHOMPI's customizations from a fork of Electrosmith's Daisy Bootloader (around their v6.0) 
onto their **v6.4** source, and takes its version number from that release. 

### From upstream

- **QSPI start-up state.** The first QSPI initialization after a reset holds the flash's
  WP# and HOLD# pins high, resets the chip, and restores and verifies the status register
  before quad mode is enabled. (libDaisy's QSPI driver, at the revision Electrosmith's
  bootloader v6.4.0 builds against.)
- **QSPI initialization is checked.** `startup_process()` initializes the HAL first, lets
  the flash settle, and resets instead of jumping if `QSPIHandle::Init()` failed.
  (Electrosmith v6.3.0.)
- **DFU flash I/O runs in the main loop** rather than inside the USB interrupt.
  (Electrosmith v6.4.0.)
- **`INF_TIMEOUT` boot mode** — holds the unit in the bootloader with no timeout. Opt-in;
  normal boots are unaffected.
- **USB descriptor strings from build flags** — set to `Chompi Club` / `CHOMPI Bootloader`.

### Unchanged from v6.2

- `BootHardware` — second codec, MP2722 power management, USB switch. Byte-identical.
- The battery lockout, at the same three points.
- The 1 kHz TIM4 callback that stands in for the stock audio callback.
- The LED boot animation, the 100 ms boot window, the boot-button mapping and the linker
  script.

### Structure

- `shared/` holds `bootloader.cpp` and the DFU code as Electrosmith's source rather than a
  CHOMPI fork of it. CHOMPI's additions there are marked `CHOMPI:`.
- `CallbackProcess()` — `AudioProcess()` without the buffer parameters, for boards that run
  the bootloader from a timer. `AudioProcess()` remains as a wrapper.
- `IsLoading()` — true once the bootloader starts shutting hardware down before the jump.
  Replaces v6.2's internal `load_prog` flag.
- The LED animation moved out of the bootloader class into `bootloader/main.cpp`.
- libDaisy changes are listed in `LIBDAISY_PATCH.md`.
