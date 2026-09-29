#pragma once
#include "daisy_seed.h"

namespace chompi
{

    /** @brief Hardware support class for the CHOMPI hardware */
    class Hardware
    {
    public:
        /** Initialize the hardware */
        Hardware() {}
        void Init()
        {
            /** Daisy Seed Initialization */
            seed.Init(true);

            /** MP2722 Power Comms */
            daisy::I2CHandle::Config i2c_conf;
            i2c_conf.mode = daisy::I2CHandle::Config::Mode::I2C_MASTER;
            i2c_conf.periph = daisy::I2CHandle::Config::Peripheral::I2C_1;
            i2c_conf.speed = daisy::I2CHandle::Config::Speed::I2C_100KHZ;
            i2c_conf.address = 0x3F;
            i2c_conf.pin_config.scl = daisy::seed::D11;
            i2c_conf.pin_config.sda = daisy::seed::D12;

            i2c.Init(i2c_conf);

            // 2722 Interrupt, USB Switch Control, input jack detection
            mpc_int.Init(daisy::seed::D31, daisy::GPIO::Mode::INPUT, daisy::GPIO::Pull::NOPULL);     // move this to be an actual interrupt?
            usb_sw.Init(daisy::seed::D32, daisy::GPIO::Mode::OUTPUT, daisy::GPIO::Pull::NOPULL);     // pulldown in hw
        }

        void MpReadAll(uint8_t* buff)
        {
            uint16_t address = 0x3F;

            uint8_t tx_buff[] = {0x11};
            i2c.TransmitBlocking(address, tx_buff, 1, 200);

            size_t buff_size = 0x06;
            i2c.ReceiveBlocking(address | 0B10000000, buff, buff_size, 200);
        }

        daisy::DaisySeed seed;

        daisy::GPIO usb_sw, mpc_int;
        daisy::I2CHandle i2c;

    private:
    };

} // namespace chompi