#pragma once
#include "daisy_seed.h"
#include "encoder.h"
#include "temp_led_stuff.h"

#define ENC_5_SW 4 // this one weird switch isn't on the SR

#define NO_BATT false

using namespace daisy;

namespace chompi
{

    /** @brief Hardware support class for the CHOMPI hardware */
    class Hardware
    {
    public:
        /* Indices for encoder access based on hardware reference name */
        enum class EncoderId : int
        {
            SW1,
            SW2,
            SW3,
            SW4,
            SW5,
            SW6,
            ENC_LAST,
        };

        /** A B reference for encoder 4021 */
        enum class EncoderSrId : int
        {
            ENC_1_A,
            ENC_1_B,
            ENC_2_A,
            ENC_2_B,
            ENC_3_A,
            ENC_3_B,
            ENC_4_A,
            ENC_4_B,
        };

        /** Switch reference for the 4021 chain
         */
        enum class SwId
        {
            /** SR 1 (start of chain) (U5) */
            ENC_1_SW, /**< U5 1 */
            ENC_2_SW, /**< U5 2 */
            ENC_3_SW, /**< U5 3 */
            ENC_4_SW, /**< U5 4 */
            NC_6,     /**< U5 5 */
            KEY_26,   /**< U5 6 */
            SW_TOG,   /**< U5 7 */
            KEY_16,   /**< U5 8 */
            /** SR 2 (start of chain) (U7) */
            KEY_2,  /**< U7 1 */
            KEY_3,  /**< U7 2 */
            KEY_4,  /**< U7 3 */
            KEY_5,  /**< U7 4 */
            KEY_17, /**< U7 5 */
            KEY_18, /**< U7 6 */
            KEY_19, /**< U7 7 */
            KEY_1,  /**< U7 8 */
            /** SR 3 (start of chain) (U8) */
            KEY_6,  /**< U8 1 */
            KEY_7,  /**< U8 2 */
            KEY_8,  /**< U8 3 */
            KEY_9,  /**< U8 4 */
            KEY_10, /**< U8 5 */
            KEY_20, /**< U8 6 */
            KEY_21, /**< U8 7 */
            KEY_22, /**< U8 8 */
            /** SR 4 (start of chain) (U9) */
            KEY_11, /**< U9 1 */
            KEY_12, /**< U9 2 */
            KEY_13, /**< U9 3 */
            KEY_14, /**< U9 4 */
            KEY_15, /**< U9 5 */
            KEY_23, /**< U9 6 */
            KEY_24, /**< U9 7 */
            KEY_25, /**< U9 8 */
            /** SR 5 (start of chain) (U10) */
            ENC_6_SW, /**< U10 1 */
            KEY_27,   /**< U10 2 */
            KEY_28,   /**< U10 3 */
            NC_1,     /**< U10 4 */
            NC_2,     /**< U10 5 */
            NC_3,     /**< U10 6 */
            NC_4,     /**< U10 7 */
            NC_5,     /**< U10 8 */
            SR_LAST,
        };

        /** Initialize the hardware */
        Hardware() {}
        void Init()
        {
            /** Daisy Seed Initialization */
            seed.Init(true);

            /** secondary audio */
            /** Configure the SAI2 peripheral for our secondary codec. */
            SaiHandle::Config external_sai_cfg;
            external_sai_cfg.periph = SaiHandle::Config::Peripheral::SAI_2;
            external_sai_cfg.sr = SaiHandle::Config::SampleRate::SAI_48KHZ;
            external_sai_cfg.bit_depth = SaiHandle::Config::BitDepth::SAI_24BIT;
            external_sai_cfg.a_sync = SaiHandle::Config::Sync::SLAVE;
            external_sai_cfg.b_sync = SaiHandle::Config::Sync::MASTER;
            external_sai_cfg.a_dir = SaiHandle::Config::Direction::TRANSMIT;
            external_sai_cfg.b_dir = SaiHandle::Config::Direction::RECEIVE;
            external_sai_cfg.pin_config.fs = seed::D27;
            external_sai_cfg.pin_config.mclk = seed::D24;
            external_sai_cfg.pin_config.sck = seed::D28;
            external_sai_cfg.pin_config.sb = seed::D25;
            external_sai_cfg.pin_config.sa = seed::D26;

            /** Initialize the SAI new handle */
            external_sai_handle.Init(external_sai_cfg);

            AudioHandle::Config audio_cfg;
            audio_cfg.blocksize = 24;
            audio_cfg.samplerate = SaiHandle::Config::SampleRate::SAI_48KHZ;
            audio_cfg.postgain = 1.0f; /*< TODO: we may want to fine tune this */

            seed.audio_handle.Init(audio_cfg, seed.AudioSaiHandle(), external_sai_handle);

            /** MP2722 Power Comms */
            I2CHandle::Config i2c_conf;
            i2c_conf.mode = I2CHandle::Config::Mode::I2C_MASTER;
            i2c_conf.periph = I2CHandle::Config::Peripheral::I2C_1;
            i2c_conf.speed = I2CHandle::Config::Speed::I2C_100KHZ;
            i2c_conf.address = 0x3F;
            i2c_conf.pin_config.scl = seed::D11;
            i2c_conf.pin_config.sda = seed::D12;

            i2c.Init(i2c_conf);

            // 2722 Interrupt, USB Switch Control, input jack detection
            mpc_int.Init(seed::D31, GPIO::Mode::INPUT, GPIO::Pull::NOPULL);     // move this to be an actual interrupt?
            usb_sw.Init(seed::D32, GPIO::Mode::OUTPUT, GPIO::Pull::NOPULL);     // pulldown in hw
            jack_detect.Init(seed::D21, GPIO::Mode::INPUT, GPIO::Pull::NOPULL); // pullup in hw

            /** Encoders */
            enc[(int)EncoderId::SW1].Init(Pin(), Pin(), Pin());
            enc[(int)EncoderId::SW2].Init(Pin(), Pin(), Pin());
            enc[(int)EncoderId::SW3].Init(Pin(), Pin(), Pin());
            enc[(int)EncoderId::SW4].Init(Pin(), Pin(), Pin());
            enc[(int)EncoderId::SW5].Init(seed::D0, seed::D20, seed::D10);
            enc[(int)EncoderId::SW6].Init(seed::D15, seed::D17, Pin());

            /** Switches 5x CD4021 in series
             *  See SwId for mapping
             */
            ShiftRegister4021<5, 1>::Config button_sr_cfg;
            button_sr_cfg.clk = seed::D8;
            button_sr_cfg.latch = seed::D7;
            button_sr_cfg.data[0] = seed::D9;
            button_sr_cfg.dbc_size = 7;
            button_sr.Init(button_sr_cfg);

            ShiftRegister4021<1, 1>::Config encoder_sr_cfg;
            encoder_sr_cfg.clk = seed::D22;
            encoder_sr_cfg.latch = seed::D23;
            encoder_sr_cfg.data[0] = seed::D19;
            encoder_sr_cfg.dbc_size = 50; // we're not actually using this
            encoder_sr.Init(encoder_sr_cfg);

            /** Digital LEDs */
            /** TODO: cleanup stuff in temp_led_stuff.h and add here */

            /** MIDI */
            MidiUartHandler::Config uart_midi_cfg;
            uart_midi.Init(uart_midi_cfg);
            uart_midi.StartReceive(); // shoukld we do this repeatedly to catch crashes?

            MidiUsbHandler::Config usb_midi_cfg;
            usb_midi_cfg.transport_config.periph = MidiUsbTransport::Config::EXTERNAL;
            usb_midi.Init(usb_midi_cfg);
            usb_midi.Listen();

            for (size_t i = 0; i < 6; i++)
                enc_trackers[i] = 0;
        }

        enum BatteryLevel {
            FULL, // on the charger, and hit the full state
            HIGH, // > 3V3, not fully charged
            MEDIUM, // < 3V3
            LOW, // unused for now
            LAST,
        };

        BatteryLevel batt_level = MEDIUM;
        uint8_t batt_check_state = 0;

        inline BatteryLevel GetBatteryLevel() { return batt_level; }

        uint8_t batt_low_bounce = 0;
        uint8_t vin_gd_bounce = 0xff;

        uint8_t legacy_cable_bounce = 0;
        uint8_t iindpm_stat_bounce = 0;

        void BMCPerformCheck()
        {
            MpReadAll();
        }

        void BMCMediumBattCheck()
        {
            if(batt_level == BatteryLevel::FULL)
                return;

            uint32_t now = System::GetNow();
            if (batt_check_state == 0 && now - batt_level_checkt > 30000) { // Check every 30s
                MpWrite(0x0c, 0B01011101); // set BATT_LOW to 3V3
                batt_level_checkt = now;
                batt_check_state = 1;
            }
            else if (batt_check_state == 1 && now - batt_level_checkt > 30) {
                MpReadAll();
                MpWrite(0x0c, 0B01010001); // set BATT_LOW to 3V
                batt_level_checkt = now;
                batt_check_state = 2;
            }
            else if (batt_check_state == 2 && now - batt_level_checkt > 30) {
                const bool batt_low_stat = mp_buff_[5] >> 4 & 1;
                batt_level = batt_low_stat ? BatteryLevel::MEDIUM : BatteryLevel::HIGH;
                batt_level_checkt = now;
                batt_check_state = 0;
            }
        }

        /** in case ofbatt_level_checkt legacy cable sleep, shut off leds first */
        void LedsOff()
        {
            for(size_t i = 0; i < kNumPthLeds; i++)
            {
                SetPthLed(i, 0, 0, 0);
            }

            for(size_t i = 0; i < kNumSmtLeds; i++)
            {
                SetSmtLed(i, 0, 0, 0);
            }

            // ========   send the data   =========
            fill_led_data();
        }

        uint32_t batt_level_checkt;
        uint32_t batt_fullt;
        void LowBatteryLockoutCheck()
        {
            #if NO_BATT
                return;
            #endif 

            BMCPerformCheck();

            if(batt_low_bounce == 0xff && vin_gd_bounce == 0x00) // unplugged and low battery
            {
                uint32_t shutdown_time_ = System::GetNow();
                uint32_t last_update = shutdown_time_;

                while(System::GetNow() - shutdown_time_ < 15000)
                {
                    uint32_t now = System::GetNow();
                    if(now - last_update >= 250)
                    {
                        last_update = now;

                        BMCPerformCheck(); // re-check battery status

                        if(!(batt_low_bounce == 0xff && vin_gd_bounce == 0x00))
                            return; // break out early if battery status changed

                        bool led_on = (now / 250) % 2 == 0;
                        for(size_t i = 0; i < kNumPthLeds; ++i)
                        {
                            if(led_on)
                                SetPthLedFloat(i, 1.f, .95f, .05f);
                            else
                                SetPthLedFloat(i, 0.f, 0.f, 0.f);
                        }

                        fill_led_data();
                    }
                }

                MpWrite(0x08, 0B10111111); // SHIPPING MODE
            }
            
            // plugged into low current source with low batt
            while(batt_low_bounce == 0xff && (legacy_cable_bounce == 0xff || iindpm_stat_bounce == 0xff))
            {
                LedsOff();
                System::Delay(100);
                HAL_PWR_EnterSTOPMode(PWR_LOWPOWERREGULATOR_ON , PWR_STOPENTRY_WFI);
            }

            // check for medium state every 30 seconds
            if(System::GetNow() - batt_level_checkt > 30000)
            {
                batt_level_checkt = System::GetNow();
                BMCMediumBattCheck();
            }
        }

        void MpWrite(uint8_t reg, uint8_t data)
        {
            #if NO_BATT
                return;
            #endif

            uint16_t address = 0x3F;

            uint8_t tx_buff[] = {reg, data};
            i2c.TransmitBlocking(address, tx_buff, 2, 200);
        }


        void MpReadAll()
        {
            #if NO_BATT
                return;
            #endif 

            uint16_t address = 0x3F;

            uint8_t tx_buff[] = {0x11};
            i2c.TransmitBlocking(address, tx_buff, 1, 200);

            read_ready = false;
            size_t buff_size = 0x06;
            i2c.ReceiveDma(address | 0B10000000, mp_buff_, buff_size, batteryCallback, this);
        }

        static void batteryCallback(void *context, daisy::I2CHandle::Result result)
        {
            Hardware *self = static_cast<Hardware*>(context);
            if(result != daisy::I2CHandle::Result::OK)
                return; // handle error or retry later

            uint8_t *buff = self->mp_buff_;

            const bool iindpm_stat = buff[0] & 1;
            const bool vin_gd = (buff[1] >> 6) & 1;
            const bool legacy_cable = (buff[1] >> 4) & 1;
            const bool batt_low_stat = (buff[5] >> 4) & 1;

            self->batt_low_bounce = (self->batt_low_bounce << 1) | batt_low_stat;
            self->vin_gd_bounce   = (self->vin_gd_bounce << 1) | vin_gd;
            self->legacy_cable_bounce = (self->legacy_cable_bounce << 1) | legacy_cable;
            self->iindpm_stat_bounce  = (self->iindpm_stat_bounce << 1) | iindpm_stat;

            const uint8_t chg_stat = (buff[2] >> 5) & 0B111;
            if(chg_stat == 0B101)
            {
                self->batt_fullt = System::GetNow();
                self->batt_level = Hardware::BatteryLevel::FULL;
            }
            else if((System::GetNow() - self->batt_fullt < 0x124f80)
                    && self->batt_level == Hardware::BatteryLevel::FULL)
            {
                // do nothing
            }
            else if(self->batt_level == Hardware::BatteryLevel::FULL)
            {
                self->batt_level = Hardware::BatteryLevel::HIGH;
            }
            self->read_ready = true;
        }

        bool GetToggleState()
        {
            return tog_state < 100;
        }

        uint32_t tog_state = 0;
        void ProcessAllControls() __attribute__((optimize("-O0")))
        {
            button_sr.Update();
            encoder_sr.Update();

            if(!button_sr.State(static_cast<int>(Hardware::SwId::SW_TOG)))
                tog_state++;
            else if(tog_state != 0)
                tog_state--;

            tog_state = tog_state > 200 ? 200 : tog_state;

            for (size_t i = 0; i < 4; i++)
            {
                const bool a_state = encoder_sr.RawState(i * 2);
                const bool b_state = encoder_sr.RawState(i * 2 + 1);
                enc[i].Debounce(a_state, b_state);
            }

            enc[4].Debounce();
            enc[5].Debounce();

            for (int i = 0; i < 6; i++)
            {
                enc_trackers[i] += enc[i].Increment();
                if (enc[i].RisingEdge())
                    enc_trackers[i] = 0;
            }
        }

        void StartAudio(AudioHandle::AudioCallback cb)
        {
            seed.StartAudio(cb);
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
        void StartLowPriorityCallback(TimerHandle::PeriodElapsedCallback cb,
                                    uint32_t target_freq,
                                    void    *data = nullptr)
        {
            TimerHandle::Config timcfg;
            timcfg.periph        = TimerHandle::Config::Peripheral::TIM_4; // originally 5, we use that for leds though
            timcfg.dir           = TimerHandle::Config::CounterDir::UP;
            auto tim_base_freq   = System::GetPClk2Freq();
            auto tim_target_freq = target_freq;
            auto tim_period      = tim_base_freq / tim_target_freq;
            timcfg.period        = tim_period;
            timcfg.enable_irq    = true;
            tim4_handle.Init(timcfg);
            tim4_handle.SetCallback(cb, data);
            /** Start Audio */
            tim4_handle.Start();
        }

        bool GetMidi(MidiEvent &event)
        {
            uart_midi.Listen();

            if(uart_midi.HasEvents())
            {
                event = uart_midi.PopEvent();
                return true;
            }
            else if(usb_midi.HasEvents())
            {
                event = usb_midi.PopEvent();
                return true;
            }

            return false;
        }

        // MIDI helpers to send via Uart and USB in parallel
        void SendMessage(uint8_t *bytes, size_t size)
        {
        }

        bool usb_midi_active = true;
        uint32_t last_reset;

        void ResetUSBMidi()
        {
            last_reset = System::GetNow();

            usb_midi.ResetTransport();
            /// are we still listening?
        }

        void USBMidiActive(bool a)
        {
            // if(a)
                // ResetUSBMidi();

            usb_midi_active = a;
        }

        void SendNoteOn(uint8_t channel, uint8_t note, uint8_t velocity)
        {
            uart_midi.SendNoteOn(channel, note, velocity);

            if(usb_midi_active)
            {
                const bool succ = usb_midi.SendNoteOn(channel, note, velocity);

                if(!succ && System::GetNow() - last_reset > 1000)
                {
                    ResetUSBMidi();
                }
            }
        }

        void SendNoteOff(uint8_t channel, uint8_t note, uint8_t velocity)
        {
            uart_midi.SendNoteOff(channel, note, velocity);
        
            if(usb_midi_active)
            {
                const bool succ = usb_midi.SendNoteOff(channel, note, velocity);

                if(!succ && System::GetNow() - last_reset > 1000)
                {
                    ResetUSBMidi();
                }
            }
        }

        // turn off CC sending for initial FW
        void SendCC(uint8_t channel, uint8_t control_number, uint8_t value)
        {
            uart_midi.SendCC(channel, control_number, value);
            usb_midi.SendCC(channel, control_number, value);
        }

        /** Prints all controls via the Daisy's micro USB */
        void PrintAllControls()
        {
            seed.PrintLine("-----------------------------");
            seed.PrintLine("Encoder States:");
            for (int i = 0; i < 6; i++)
            {
                seed.PrintLine("enc:\t%d\tval:\t%d", i, enc_trackers[i]);
            }
            seed.PrintLine("Jack Detection: %s", jack_detect.Read() ? "Plugged In" : "Unplugged");
        }

        DaisySeed seed;

        int enc_trackers[6];

        uint8_t mp_buff_[6];
        bool read_ready = false;

        ChompiEncoder enc[6];
        ShiftRegister4021<5, 1> button_sr;
        ShiftRegister4021<1, 1> encoder_sr;
        SaiHandle external_sai_handle;
        MidiUsbHandler usb_midi;
        MidiUartHandler uart_midi;
        I2CHandle i2c; // comms w/ MP2722
        GPIO usb_sw, mpc_int, jack_detect;
        TimerHandle tim4_handle;
    private:
    };

} // namespace chompi