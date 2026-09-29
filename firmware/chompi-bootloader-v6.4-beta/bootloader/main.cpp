// CHOMPI bootloader v6.4
//
// CHOMPI's hardware layer on the Electrosmith Daisy Bootloader v6.4 (shared/):
// board bring-up, the battery lockout, and the boot LED animation.

#include "bootloader.h"
#include "boot_hardware.h"
#include "temp_led_stuff.h"

using namespace daisy;
using namespace chompi;

BootHardware hw;
Bootloader   boot;

// Boot LED animation: a slow fade through random colours on both LED chains.
static float kAnim_r          = 0.f;
static float kAnim_g          = 0.f;
static float kAnim_b          = 0.f;
static float kAnim_bright     = 0.f;
static float kAnim_bright_inc = 0.0002f;

static void LedsOff()
{
    for (size_t i = 0; i < kNumPthLeds; i++)
    {
        SetPthLed(i, 0, 0, 0);
    }
    for (size_t i = 0; i < kNumSmtLeds; i++)
    {
        SetSmtLed(i, 0, 0, 0);
    }
    fill_led_data();
}

static void RandomColors()
{
    kAnim_r = daisy::System::GetNow() % 66;
    kAnim_g = daisy::System::GetNow() % 53;
    kAnim_b = daisy::System::GetNow() % 36;

    kAnim_r /= 66.f;
    kAnim_g /= 53.f;
    kAnim_b /= 36.f;
}

static void BootAnimation()
{
    kAnim_bright += kAnim_bright_inc;
    if (kAnim_bright > 1.f)
    {
        kAnim_bright_inc *= -1.f;
    }
    else if (kAnim_bright < 0.f)
    {
        RandomColors();
        kAnim_bright_inc *= -1.f;
    }

    for (size_t i = 0; i < kNumPthLeds; i++)
    {
        SetPthLedFloat(i,
                       kAnim_r * kAnim_bright,
                       kAnim_g * kAnim_bright,
                       kAnim_b * kAnim_bright);
    }
    for (size_t i = 0; i < kNumSmtLeds; i++)
    {
        SetSmtLedFloat(i,
                       kAnim_r * kAnim_bright,
                       kAnim_g * kAnim_bright,
                       kAnim_b * kAnim_bright);
    }
    fill_led_data();
}

// Runs inside Bootloader::DeInit(), just before the reset into the application.
// The timer stops first so the LED callback cannot run while the peripherals
// under it are shut down.
void ChompiDeInitCallback(void* context)
{
    BootHardware* hardware = reinterpret_cast<BootHardware*>(context);
    if (!hardware) return;

    hardware->tim4_handle.SetCallback(nullptr, nullptr);
    hardware->tim4_handle.Stop();
    hardware->seed.StopAudio(); // both codecs
    hardware->seed.DeInit();
}

// 1 kHz low-priority timer, in place of the stock bootloader's audio callback.
// The LEDs go dark once the bootloader starts shutting hardware down.
void TimerCallback(void* data)
{
    boot.CallbackProcess();

    if (!boot.IsLoading())
    {
        BootAnimation();
    }
    else
    {
        LedsOff();
    }
}

int main(void)
{
    // Jumps to the application if one has just been staged; otherwise returns
    // the boot window in milliseconds.
    uint32_t timeout_ms = startup_process();

    hw.Init();

    // Battery lockout, before USB and the SD card come up.
    for (size_t i = 0; i < 10; i++)
    {
        hw.LowBatteryLockoutCheck();
        System::Delay(10);
    }

    // D-cache stays off while the bootloader works on QSPI.
    SCB_DisableDCache();

    // LED pin: the Daisy Seed's onboard LED. No button pin: the bootloader
    // falls back to its default boot-button mapping (PG3).
    boot.Init(hw.seed.qspi,
              Pin(daisy::PORTC, 7),
              Pin(),
              timeout_ms,
              ChompiDeInitCallback,
              static_cast<void*>(&hw));

    // The LED timers and DMA must be running before the callback that feeds them.
    chompi::LedSetup();
    hw.StartLowPriorityCallback(TimerCallback, 1000); // 1 kHz

    // Battery lockout again, with the timer running.
    for (size_t i = 0; i < 10; i++)
    {
        hw.LowBatteryLockoutCheck();
        System::Delay(10);
    }

    // SD card, USB, FatFS.
    boot.IoInit();

    // SD / USB / DFU state machine, with the battery lockout kept live.
    while (1)
    {
        boot.LoopProcess();
        hw.LowBatteryLockoutCheck();
    }
}
