#include "hardware.h"

using namespace daisy;
using namespace chompi;

Hardware hw;

static const uint32_t kPrintPeriod = 500;

int main(void)
{
    hw.Init();
    System::Delay(1000);

    hw.seed.StartLog(true); // wait for PC

    hw.usb_sw.Write(true);     // take over USB control
    uint32_t last_print = System::GetNow();
    bool prev_state = true;
    while (1)
    {
        bool state = hw.mpc_int.Read();
        if(!state && prev_state) // falling edge
        {
            uint8_t buff[6];
            hw.MpReadAll(buff);

            // hack a byte into an int to print in binary
            for(int i = 0; i < 6; i++)
            {
                char binary[9];
                itoa(buff[i], binary, 2);

                hw.seed.PrintLine("REG %#04x: 0B%s", 0x11 + i, binary);
            }
        }
        prev_state = state;

        System::DelayUs(1);
    }
}