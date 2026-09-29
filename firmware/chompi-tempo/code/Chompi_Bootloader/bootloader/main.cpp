#include "bootloader.h"
#include "boot_hardware.h"

using namespace daisy;
using namespace chompi;

BootHardware hw;
Bootloader boot;

void Callback(void* data)
{
	boot.CallbackProcess();
}


int main(void) {
	uint32_t timeout_ms = 100;
	startup_process();

	hw.Init();

    // hw.MpWrite(0x0c, 0B01011101); // set BATT_LOW to 3.3V
	for(size_t i = 0; i < 10; i++)
	{
		hw.LowBatteryLockoutCheck();
		System::Delay(10);
	}

	SCB_DisableDCache();

	boot.Init(hw.seed, timeout_ms);

    hw.StartLowPriorityCallback(Callback, 1000);

	for(size_t i = 0; i < 10; i++)
	{
		hw.LowBatteryLockoutCheck();
		System::Delay(10);
	}

	boot.IoInit();

	while(1)
	{
		boot.LoopProcess();
		hw.LowBatteryLockoutCheck();
	}
}
