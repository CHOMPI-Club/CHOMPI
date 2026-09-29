#pragma once
#include "daisy_seed.h"

namespace chompi
{

    /** @brief Hardware support class for the CHOMPI hardware */
    class BootHardware
    {
    public:

        /** Initialize the hardware */
        BootHardware() {}
        void Init()
        {
        	seed.Configure();

            /** Daisy Seed Initialization */
            seed.Init(false);

            /** secondary audio */
            /** Configure the SAI2 peripheral for our secondary codec. */
            daisy::SaiHandle::Config external_sai_cfg;
            external_sai_cfg.periph = daisy::SaiHandle::Config::Peripheral::SAI_2;
            external_sai_cfg.sr = daisy::SaiHandle::Config::SampleRate::SAI_48KHZ;
            external_sai_cfg.bit_depth = daisy::SaiHandle::Config::BitDepth::SAI_24BIT;
            external_sai_cfg.a_sync = daisy::SaiHandle::Config::Sync::SLAVE;
            external_sai_cfg.b_sync = daisy::SaiHandle::Config::Sync::MASTER;
            external_sai_cfg.a_dir = daisy::SaiHandle::Config::Direction::TRANSMIT;
            external_sai_cfg.b_dir = daisy::SaiHandle::Config::Direction::RECEIVE;
            external_sai_cfg.pin_config.fs = daisy::seed::D27;
            external_sai_cfg.pin_config.mclk = daisy::seed::D24;
            external_sai_cfg.pin_config.sck = daisy::seed::D28;
            external_sai_cfg.pin_config.sb = daisy::seed::D25;
            external_sai_cfg.pin_config.sa = daisy::seed::D26;

            /** Initialize the SAI new handle */
            external_sai_handle.Init(external_sai_cfg);

            daisy::AudioHandle::Config audio_cfg;
            audio_cfg.blocksize = 12;
            audio_cfg.samplerate = daisy::SaiHandle::Config::SampleRate::SAI_48KHZ;
            audio_cfg.postgain = 1.0f; /*< TODO: we may want to fine tune this */

            seed.audio_handle.Init(audio_cfg, seed.AudioSaiHandle(), external_sai_handle);

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

        uint8_t batt_low_bounce = 0;
        uint8_t vin_gd_bounce = 0xff;

        bool legacy_cable;
        bool iindpm_stat;

        void BMCPerformCheck()
        {
            uint8_t buff[6];
            MpReadAll(buff);

            // const uint8_t dpdm_stat = (buff[0] & 0B11110000) >> 4;
            iindpm_stat = buff[0] & 1;

            const bool vin_gd = buff[1] >> 6 & 1;
            // const bool vin_rdy = buff[1] >> 5 & 1;
            legacy_cable = buff[1] >> 4 & 1;
            // const bool vsys_stat = buff[1] >> 2 & 1;

            const bool batt_low_stat = buff[5] >> 4 & 1;

            batt_low_bounce = (batt_low_bounce << 1) | batt_low_stat;
            vin_gd_bounce = (vin_gd_bounce << 1) | vin_gd;
        }


        void LowBatteryLockoutCheck()
        {
            BMCPerformCheck();

            if(batt_low_bounce == 0xff && vin_gd_bounce == 0x00) // unplugged and low battery
            {
                MpWrite(0x08, 0B10111111); // SHIPPING MODE
            }
            
            // plugged into low current source with low batt
            while(batt_low_bounce == 0xff && (legacy_cable || iindpm_stat))
            {
                // it's OK to jump to the app on a legacy cable
                // HAL_PWR_EnterSTOPMode(PWR_LOWPOWERREGULATOR_ON , PWR_STOPENTRY_WFI);
            }
        }

        void MpWrite(uint8_t reg, uint8_t data)
        {
            uint16_t address = 0x3F;

            uint8_t tx_buff[] = {reg, data};
            i2c.TransmitBlocking(address, tx_buff, 2, 200);
        }


        void MpReadAll(uint8_t* buff)
        {
            uint16_t address = 0x3F;

            uint8_t tx_buff[] = {0x11};
            i2c.TransmitBlocking(address, tx_buff, 1, 200);

            size_t buff_size = 0x06;
            i2c.ReceiveBlocking(address | 0B10000000, buff, buff_size, 200);
        }


        /** This starts up a callback that is on the lowest priority interrupt level
            *  This provides an area for non-background tasks that should interrupt low
            *  level activity like diskio.
            *  Adapted from Electrosmith reference source for the SD card interrupt 
            *
            *  @param cb callback to take place at target frequency
            *  @param target_freq freq in hz that the callback should take place.
            *  @param data any data to send through callback; this defaults to nullptr
            */
        void StartLowPriorityCallback(daisy::TimerHandle::PeriodElapsedCallback cb,
                                    uint32_t target_freq,
                                    void    *data = nullptr)
        {
            daisy::TimerHandle::Config timcfg;
            timcfg.periph        = daisy::TimerHandle::Config::Peripheral::TIM_4; // originally 5, we use that for leds though
            timcfg.dir           = daisy::TimerHandle::Config::CounterDir::UP;
            auto tim_base_freq   = daisy::System::GetPClk2Freq();
            auto tim_target_freq = target_freq;
            auto tim_period      = tim_base_freq / tim_target_freq;
            timcfg.period        = tim_period;
            timcfg.enable_irq    = true;
            tim4_handle.Init(timcfg);
            tim4_handle.SetCallback(cb, data);
            /** Start Audio */
            tim4_handle.Start();
        }

        void StartAudio(daisy::AudioHandle::AudioCallback cb)
        {
            seed.StartAudio(cb);
        }

        daisy::DaisySeed seed;

        daisy::SaiHandle external_sai_handle;
        daisy::I2CHandle i2c; // comms w/ MP2722
        daisy::GPIO usb_sw, mpc_int;
        daisy::TimerHandle tim4_handle;
    private:
    };

} // namespace chompi