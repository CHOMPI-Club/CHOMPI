# libDaisy modifications

`libs/libDaisy` is vendored **already carrying these changes** — there is no patch to
apply. This file lists what differs from the libDaisy the CHOMPI application firmware
uses.

## 1. `INF_TIMEOUT` in `BootInfo::Type` — `src/sys/system.h`

```cpp
enum class Type : uint32_t
{
    INVALID      = 0x00000000,
    JUMP         = 0xDEADBEEF,
    SKIP_TIMEOUT = 0x5AFEB007,
    INF_TIMEOUT  = 0x1FFFFFFF,   // added
} status;
```

`startup_process()` checks for this status to hold the unit in the bootloader indefinitely
instead of timing out. The value `0x1FFFFFFF` is the one upstream Electrosmith uses —
matching it exactly keeps a future upstream merge conflict-free.

## 2. `vC6_3` and `vC6_4` in `BootInfo::Version` — `src/sys/system.h`

```cpp
enum class Version : uint32_t
{
    LT_v6_0 = 0,
    NONE,
    v6_0,
    v6_1,
    vC6_2,       // chompi v6.2
    vC6_3,       // chompi v6.3 — added (earlier beta, not published)
    vC6_4,       // chompi v6.4 — added
    LAST
} version;
```

The bootloader writes `vC6_4` into the boot-info block so the application can identify which
bootloader is installed. `vC6_3` is reserved for an earlier beta that was not published.
Both are appended after `vC6_2`, so `vC6_2` keeps its numeric value of `4` and any
application comparing against it numerically still works. An application built against a
libDaisy that does not know these values is unaffected: `System::GetBootloaderVersion()`
reports a newer bootloader as the latest version that library knows.

These enum additions are purely additive: no existing value is renamed, reordered or
removed, and adding enum values does not change struct size or layout.

## 3. The QSPI driver — `src/per/qspi.cpp`, `src/per/qspi.h`

These two files are upstream libDaisy's, unmodified, at commit `cc3bfb1` — the revision
Electrosmith's Daisy Bootloader v6.4.0 is built against — in place of the older copies that
come with the rest of this libDaisy.

The newer driver adds `PreInit()`, which runs on the first QSPI initialization after a
reset. It drives the flash's IO2/WP# and IO3/HOLD# pins as plain GPIO — high, a 20 ms low
pulse, then high — brings the peripheral up on a single data line, resets the flash, and
calls `DefaultStatusRegister()`, which writes the status register to `0x40` (quad-enable
set, protection bits clear) and polls until it reads back exactly that. The pins then go
back to the QSPI peripheral and the normal quad-mode initialization runs.

`qspi.h` differs from the older copy only in whitespace.

## Everything else

Apart from the changes above, `libs/libDaisy` is the same libDaisy the CHOMPI application
firmware uses, including its own local modifications for this hardware. See
[`THIRD_PARTY.md`](../../THIRD_PARTY.md) at the root of this repo for those.
