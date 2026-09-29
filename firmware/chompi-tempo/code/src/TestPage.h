#include "hardware.h"
#include "temp_led_stuff.h"

namespace chompi
{
    class TestPage : public daisy::UiPage
    {
    public:

        struct Dir
        {
            bool left = false;
            bool right = false;
            int ctr = 0;
        };

        void Init(Hardware* hw)
        {
            hw_ = hw;

            for(size_t i = 0; i < size_t(Hardware::SwId::SR_LAST); i++)
            {
                clicked[i] = false;
            }

            init_ignore = true;
            init_time = System::GetNow();
        }

        void Draw(const daisy::UiCanvasDescriptor &canvasDescriptor) override
        {
            /** PTH leds */
            float r, g, b;
            uint32_t now = System::GetNow();

            // ignore the first 1500 ms of inputs. Hack to stop random button presses on boot for now.
            if (init_ignore)
            {
                if (now - init_time > 1500)
                {
                    init_ignore = false;
                }
            }

            // jack detection test
            jack_detect |= false;

            // send midi notes
            if(now - last_midi_out > 50 && now - midi_timeout > 500)
            {
                if(!note_sent)
                {
                    if(note_out == 60)
                        note_out = 64;
                    else if(note_out == 64)
                        note_out = 67;
                    else if(note_out == 67)
                        note_out = 60;
    
                    //hw_->SendNoteOn(0, note_out, 127);
                    note_received = false;
                }
                else
                {
                    if(note_received == false && num_notes_received != 20)
                        num_notes_received = 0;
                    //hw_->SendNoteOff(0, note_out, 127);
                }

                note_sent = !note_sent;
                last_midi_out = now;
            }
            
            for(size_t i = 0; i < size_t(Hardware::SwId::SR_LAST); i++)
            {
                r = g = b = clicked[i];
                if(i == 6 || i >= 35)
                { 
                    // do nothing 
                }
                else if(i == 4)
                {
                    r = turned[i].left;
                    g = turned[i].right;
                    SetPthLedFloat(5, r, g, b);
                    SetPthLedFloat(6, r, g, b);
                }
                else if(i == 5)
                {
                    b = tog_sw.left;
                    r = g = tog_sw.right;
                    SetPthLedFloat(led_map[i], r, g, b);
                }
                else if(i < 5)
                {
                    r = turned[i].left;
                    g = turned[i].right;
                    SetPthLedFloat(led_map[i], r, g, b);
                }
                else if(i == 32)
                {
                    r = turned[5].left;
                    g = turned[5].right;
                    SetPthLedFloat(led_map[i], r, g, b);
                }
                else if(i > 32 && i < 35)
                {
                    b = sd_card;
                    SetPthLedFloat(led_map[i], r, g, b);
                }
                else if(i == 7)
                {
                    r = 1.f;
                    b = power_cable;
                    SetSmtLedFloat(led_map[i], r, g, b);
                }
                else if(i == 12)
                {
                    r = 1.f;
                    b = bmc_good;
                    SetSmtLedFloat(led_map[i], r, g, b);
                }
                else if(i == 15)
                {
                    r = 1.f;
                    b = num_notes_received == 20;
                    SetSmtLedFloat(led_map[i], r, g, b);
                }
                else if(i == 28)
                {
                    r = 1.f;
                    b = jack_detect;
                    SetSmtLedFloat(led_map[i], r, g, b);
                }
                else
                {
                    r = 1.f;
                    SetSmtLedFloat(led_map[i], r, g, b);
                }
            }

            // ========   send the data   =========
            fill_led_data();
        }

        bool OnEncoderTurned(uint16_t encoderID,
                             int16_t turns,
                             uint16_t stepsPerRevolution) override
        {
            if (init_ignore)
                return true;

            if(encoderID == 0)
                encoderID = 3;
            else if(encoderID < 4)
                encoderID -= 1;

            
            turned[encoderID].ctr += turns;

            if(turned[encoderID].ctr > 5)
            {
                turned[encoderID].right = true;
                turned[encoderID].ctr = 0;
            }
            else if(turned[encoderID].ctr < -5)
            {
                turned[encoderID].ctr = 0;
                turned[encoderID].left = true;
            }

            // we're receiving a knob position via CC
            if(stepsPerRevolution > 0)
                return true;

            return true;
        }

        bool OnButton(uint16_t buttonID,
                uint8_t numberOfPresses,
                bool isRetriggering) override
        {
            if (init_ignore)
                return true;

            bool rising = numberOfPresses == 1;

            if(isRetriggering) // midi key
            {
                if( (note_out == 60 && buttonID == 18)
                    || (note_out == 64 && buttonID == 20)
                    || (note_out == 67 && buttonID == 25))
                {
                    note_received = true;
                    num_notes_received = num_notes_received >= 20 ? 20 : num_notes_received + 1;
                }

                return true; // fall through to normal page, plays back samples from SD card
            }
            else if(rising && buttonID != 5) // normal keypress, ignore chompi key
            {
                clicked[buttonID] = true;
            }
            else if(rising && buttonID == 5)
            {
                CheckAndClose();
            }

            if(buttonID <= 6 || buttonID >= 32)
                return true;

            return true;
        }

        void SetSwitchState(bool state)
        {
            if(init_ignore)
            {
                tog_sw.ctr = state;
                return;
            }

            if(tog_sw.ctr != state)
            {
                tog_sw.ctr = state;
                if(state)
                    tog_sw.left = true;
                else
                    tog_sw.right = true;
            }
        }

        void SetPowerCable(bool cable)
        {
            if(cable && power_low) // detect rising edge
            {
                midi_timeout = System::GetNow();
                power_cable = true;
            }
        
            power_low = !cable;
        }

        /** Check BMC Registers: NTC_MISSING, BATT_MISSING, NTC1_FAULT, and NTC2_FAULT */
        inline void SetBMCGood(bool good) { bmc_good = good; }

        FIL fptr;
        bool SDCardTest()
        {
            const char filename[32] = "test_file.txt";
            const char write_test[32] = "This is a test!";
            const size_t length = strlen(write_test);

            char read_test[32];

            FRESULT res;
            UINT num;

            res = f_open(&fptr, filename, (FA_CREATE_ALWAYS | FA_WRITE | FA_READ));
            if(res != FR_OK)
                return false;

            res = f_write(&fptr, &write_test, length, &num);
            if(res != FR_OK || num != length)
                return false;

            res = f_sync(&fptr);
            if(res != FR_OK)
                return false;

            res = f_rewind(&fptr);
            if(res != FR_OK)
                return false;
            
            res = f_read(&fptr, read_test, length, &num);
            if(res != FR_OK || num != length)
                return false;
            
            for(size_t i = 0; i < length; i++)
            {
                if(read_test[i] != write_test[i])
                    return false;
            }

            res = f_close(&fptr);
            fptr.obj.objsize = 0;
            if(res != FR_OK)
                return false;

            res = f_unlink(filename);
            if(res != FR_OK)
                return false;

            return true;
        }

        void OnFocusGained() override
        {
            sd_card = SDCardTest();
            
            for(size_t i = 0; i < size_t(Hardware::SwId::SR_LAST); i++)
            {
                clicked[i] = false;
            }

            start_time = System::GetNow();
        }

        
        // when you press the chompi key, close if the test is complete
        bool close = false;
        void CheckAndClose()
        {
            bool ret = true;

            ret &= tog_sw.left && tog_sw.right;
            ret &= num_notes_received == 20;
            ret &= jack_detect;
            ret &= sd_card;

            for(size_t i = 0; i < size_t(Hardware::SwId::SR_LAST); i++)
            {
                if(i == 6 || i >= 35 || i == 5)
                    { /* do nothing */ }
                else
                    ret &= clicked[i];
            }

            for(size_t i = 0; i < size_t(Hardware::EncoderId::ENC_LAST); i++)
                ret &= turned[i].left && turned[i].right;

            ret &= num_notes_received == 20;
            ret &= jack_detect;
            ret &= power_cable;
            ret &= bmc_good;
            
            close =  ret;
        }

        bool IsClosable()
        {
            return close;
        }


    private:
        bool init_ignore;
        uint32_t init_time;
        Hardware* hw_;

        // interface elements
        bool clicked[size_t(Hardware::SwId::SR_LAST)];
        Dir turned[size_t(Hardware::EncoderId::ENC_LAST)];
        Dir tog_sw;
        bool jack_detect, sd_card;
        bool power_low, power_cable;
        bool bmc_good;

        // midi out
        bool note_sent, note_received;
        int note_out = 60;
        uint8_t num_notes_received;
        uint32_t last_midi_out;
        uint32_t midi_timeout;

        uint32_t start_time;

        /** TODO: I have another copy of this in the NormalPage*/
        uint8_t led_map[40]{
            2,    /**< ENC_1_SW  page */
            3,    /**< ENC_2_SW page */
            4,    /**< ENC_3_SW page */
            1,    /**< ENC_4_SW cc */
            0,    /**< ENC_5_SW page */
            0,    /**< KEY_26 chompi cc */
            0x00, /**< SW_TOG skip */
            0,    /**< KEY_16 */
            23,   /**< KEY_2 */
            22,   /**< KEY_3 */
            21,   /**< KEY_4 */
            20,   /**< KEY_5 */
            1,    /**< KEY_17 */
            2,    /**< KEY_18 */
            3,    /**< KEY_19 */
            24,   /**< KEY_1 */
            19,   /**< KEY_6 */
            18,   /**< KEY_7 */
            17,   /**< KEY_8 */
            16,   /**< KEY_9 */
            15,   /**< KEY_10 */
            4,    /**< KEY_20 */
            5,    /**< KEY_21 */
            6,    /**< KEY_22 */
            14,   /**< KEY_11 */
            13,   /**< KEY_12 */
            12,   /**< KEY_13 */
            11,   /**< KEY_14 */
            10,   /**< KEY_15 */
            7,    /**< KEY_23 */
            8,    /**< KEY_24 */
            9,    /**< KEY_25 */
            9,    /**< ENC_6_SW page */
            7,    /**< KEY_27 play cc */
            8,    /**< KEY_28 loop cc */
            0x00, /**< NC_1 skip */
            0x00, /**< NC_2 skip */
            0x00, /**< NC_3 skip */
            0x00, /**< NC_4 skip */
            0x00, /**< NC_5 skip */
        };
    };
} // namespace chompi
