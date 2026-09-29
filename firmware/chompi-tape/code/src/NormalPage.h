#pragma once

#include "hardware.h"
#include "DSPEngine.h"
#include "temp_led_stuff.h"
#include "FileCopier.h"

namespace chompi
{
        static const uint8_t cc_map[3][6] = {
            {20, 21, 22, 23, 24, 25},
            {28, 29, 30, 31, 0, 32},
            {0, 0, 0, 33, 0, 0}};

        static const uint8_t key_map[40] = {
            0x01, /**< ENC_1_SW  page */
            0x02, /**< ENC_2_SW page */
            0x03, /**< ENC_3_SW page */
            0x00, /**< ENC_4_SW page */ /** TODO: was CC 16 out */
            0x04, /**< ENC_5_SW page */
            0x15, /**< KEY_26 chompi cc */
            0x00, /**< SW_TOG skip */
            0x31, /**< KEY_16 */
            0x32, /**< KEY_2 */
            0x34, /**< KEY_3 */
            0x35, /**< KEY_4 */
            0x37, /**< KEY_5 */
            0x33, /**< KEY_17 */
            0x36, /**< KEY_18 */
            0x38, /**< KEY_19 */
            0x30, /**< KEY_1 */
            0x39, /**< KEY_6 */
            0x3b, /**< KEY_7 */
            0x3c, /**< KEY_8 */
            0x3e, /**< KEY_9 */
            0x40, /**< KEY_10 */
            0x3a, /**< KEY_20 */
            0x3d, /**< KEY_21 */
            0x3f, /**< KEY_22 */
            0x41, /**< KEY_11 */
            0x43, /**< KEY_12 */
            0x45, /**< KEY_13 */
            0x47, /**< KEY_14 */
            0x48, /**< KEY_15 */
            0x42, /**< KEY_23 */
            0x44, /**< KEY_24 */
            0x46, /**< KEY_25 */
            0x05, /**< ENC_6_SW page */
            0x17, /**< KEY_27 play cc */
            0x18, /**< KEY_28 loop cc */
            0x00, /**< NC_1 skip */
            0x00, /**< NC_2 skip */
            0x00, /**< NC_3 skip */
            0x00, /**< NC_4 skip */
            0x00, /**< NC_5 skip */
        };

        static const uint8_t led_map[40]{
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

    // .00787 ~= what midi was. 1 / 127
    static const float kEncoderFineStep = .003f;
    static const float kEncoderCoarseStep = .01f;
    static const float kRecDim = .7f;

    uint8_t led_pth_cache[kNumPthLeds][3]; /**< RGB data */
    uint8_t led_smt_cache[kNumSmtLeds][3]; /**< RGB data */

    static const float white[3] = {1.f, 1.f, 1.f};
    static const float red[3] = {1.f, 0.f, 0.f};
    static const float orange[3] = {1.f, .6f, .24f};
    static const float yellow[3] = {1.f, .95f, 0.05f};
    static const float green[3] = {0.f, 1.f, 0.f};
    static const float teal[3] = {.14f, 1.f, .92f};
    static const float med_blue[3] = {0.f, .84f, 1.f};
    static const float blue[3] = {0.f, 0.f, 1.f};
    static const float purple[3] = {.58f, .05f, 1.f};
    static const float pink[3] = {1.f, .36f, .62f};
    static const float dark_orange[3] = {.77f, .38f, .06f};
    static const float yellow_green[3] = {.706f, 1.f, 0.f};

    static const uint8_t knob_num_pages[6] = {2, 2, 2, 3, 1, 2};

    class NormalPage : public daisy::UiPage
    {
    public:
        uint32_t init_time;
        bool init_ignore = true;

        void Init(Hardware *hw, Engine *fx, FileCopier *copier, float** enc_arr, const float** def_arr, 
                    uint8_t* page, PresetManager* pre, uint8_t midi_out_channel, bool ps_quant, bool split_delay)
        {
            hw_ = hw;
            fx_ = fx;
            copier_ = copier;
            presets_ = pre;
            enc_values = enc_arr;
            enc_defaults = def_arr;
            knob_page = page;

            midi_channel = midi_out_channel;

            quantized_pitch_ = !ps_quant;
            split_delay_ = split_delay;

            for (int knob = 0; knob < 6; knob++)
            {
                for (int page = 0; page < 3; page++)
                {
                    enc_values[page][knob] = enc_defaults[page][knob];
                }
            }

            if (split_delay_) {
                enc_values[0][3] = .5f;
            }

            fx_->SetGlobalPitch(1.f);
            fx_->SetReverse(false);
            
            fx_->SetInputGain(.75f);


            for (int i = 0; i < kNumSmtLeds; i++)
                SetSmtLed(i, 0, 0, 0);
            for (int i = 0; i < kNumPthLeds; i++)
                SetPthLed(i, 0, 0, 0);

            // 8mm leds
            SetPthLedFloat(0, 0.f, 0.f, 0.f);
            SetPthLedFloat(1, green[0], green[1], green[2]);
            SetPthLedFloat(2, 0.f, 0.f, 0.f);
            SetPthLedFloat(3, 0.f, 0.f, 0.f);
            SetPthLedFloat(4, 0.f, 0.f, 0.f);
            SetPthLedFloat(9, 0.f, 0.f, 0.f);

            // 5mm leds
            SetPthLedFloat(5, 1.f, 1.f, 1.f);
            SetPthLedFloat(6, 1.f, 1.f, 1.f);
            SetPthLedFloat(7, 1.f, 1.f, 1.f);
            SetPthLedFloat(8, 1.f, 1.f, 1.f);

            for (int i = 0; i < kNumSmtLeds; i++)
                SetSmtLed(i, 0, 0, 0);
            // SetSmtLedFloat(i, .1f, .1f, .0f);
            // SetSmtLedFloat(0, 0.f, 0.f, 0.f);
            // SetSmtLedFloat(9, 0.f, 0.f, 0.f);
            // SetSmtLedFloat(10, 0.f, 0.f, 0.f);

            init_time = System::GetNow();
        }

        uint32_t last_arm_blink;
        bool arm_blink = true;

        uint32_t last_record_blink;
        bool record_blink = false;

        void ResetSmtLeds()
        {
            for(size_t i = 0; i < 25; i++)
            {
                SetSmtLed(i, 0.f, 0.f, 0.f);
            }
        }

        void CacheLeds()
        {
            std::copy(&led_pth_data[0][0], &led_pth_data[0][0] + kNumPthLeds * 3, &led_pth_cache[0][0]);
            std::copy(&led_smt_data[0][0], &led_smt_data[0][0] + kNumSmtLeds * 3, &led_smt_cache[0][0]);
        }

        void RefreshLeds()
        {
            std::copy(&led_pth_cache[0][0], &led_pth_cache[0][0] + kNumPthLeds * 3, &led_pth_data[0][0]);
            std::copy(&led_smt_cache[0][0], &led_smt_cache[0][0] + kNumSmtLeds * 3, &led_smt_data[0][0]);
        }

        void Draw(const daisy::UiCanvasDescriptor &canvasDescriptor) override
        {
            uint32_t now = System::GetNow();

            if(fx_->CheckReset())
            {
                enc_values[0][4] = enc_defaults[0][4];
                fx_->ResetLooperPitchQuant();
            }

            // ignore the first 1500 ms of inputs. Hack to stop random button presses on boot for now.
            if (init_ignore)
            {
                if (now - init_time > 1500)
                {
                    init_ignore = false;
                }
            }

            for(size_t i = 7; i < (25 + 7); i++)
            {
                size_t slot = KeyToSlot(i);
                const float* color = &pink[0];
                if(fx_->GetVoiceSlot() != 15 || fx_->GetVoiceMode() == VoiceMode::CUBBI)
                {
                    if(fx_->GetVoiceBank() == 0) // this shouldn't change if we're not actually on that bank
                        color = &purple[0];
                    else if(fx_->GetVoiceBank() == 1)
                        color = &orange[0];
                    else if(fx_->GetVoiceBank() == 2)
                        color = &teal[0];
                    else if(fx_->GetVoiceBank() == 3)
                        color = &dark_orange[0];
                    else if(fx_->GetVoiceBank() == 4)
                        color = &yellow_green[0];
                }

                if (fx_->IsKeyPlaying(i))
                    SetSmtLedFloat(led_map[i], 1.f, 1.f, 1.f);
                else if(fx_->GetVoiceMode() == VoiceMode::CUBBI 
                        && (switch_state || fx_->GetInputSource() != InputSource::MIC)) // no perm KB leds if we're monitoring the mic
                {
                    if (fx_->GetFileExists(slot - 1) && i == 28)
                        SetSmtLedFloat(led_map[i], pink[0] * .25f, pink[1] * .25f, pink[2] * .25f);
                    else if(fx_->GetFileExists(slot - 1) && slot != kSlotNone)
                        SetSmtLedFloat(led_map[i], color[0] * .25f, color[1] * .25f, color[2] * .25f);
                }
                else if(fx_->GetVoiceMode() == VoiceMode::JAMMI && (i == 15 || i == 18 || i == 28)
                        && (switch_state || fx_->GetInputSource() != InputSource::MIC)) // no perm KB leds if we're monitoring the mic
                    SetSmtLedFloat(led_map[i], color[0] * .25f, color[1] * .25f, color[2] * .25f);
                else
                    SetSmtLed(led_map[i], 0, 0, 0);
            }

            // =========   encoders   =========
            for (int i = 0; i < 6; i++)
            {
                uint8_t page = knob_page[i];
                float value = enc_values[page][i];

                float r = 0.f; 
                float g = 0.f;
                float b = 0.f;
                switch (i)
                {
                case 0: // speed, gain, pan
                {
                    if(!switch_state)
                    {
                        r = g = b = 0.f;
                    }
                    else if(page == 0) // speed
                    {
                        float idx = enc_values[0][0] < .5f ? enc_values[0][0] * 2.f : (1.f - enc_values[0][0]) * 2.f; // 0 - 1 - 0
                        r = color_quad_xfade(med_blue[0], green[0], yellow[0], red[0], idx);
                        g = color_quad_xfade(med_blue[1], green[1], yellow[1], red[1], idx);
                        b = color_quad_xfade(med_blue[2], green[2], yellow[2], red[2], idx);
                    }
                    else if(page == 1) // gain
                    {
                        r = color_triple_xfade(blue[0], pink[0], red[0], value);
                        g = color_triple_xfade(blue[1], pink[1], red[1], value);
                        b = color_triple_xfade(blue[2], pink[2], red[2], value);

                        fx_->SetGain(value);
                    }

                    SetPthLedFloat(1, r, g, b);
                }
                break;
                case 1: // start point
                {
                    if (page == 0) // start point
                    {
                        r = color_xfade(yellow[0], orange[0], value);
                        g = color_xfade(yellow[1], orange[1], value);
                        b = color_xfade(yellow[2], orange[2], value);

                        // fx_->SetStartPoint(value);
                    }
                    else // env. attack
                    {
                        r = color_xfade(purple[0] * .2f, purple[0], value);
                        g = color_xfade(purple[1] * .2f, purple[1], value);
                        b = color_xfade(purple[2] * .2f, purple[2], value);

                        fx_->SetAttack(value);
                    }

                    if(!switch_state)
                    {
                        r = g = b = 0.f;
                    }   

                    SetPthLedFloat(2, r, g, b);
                    break;
                }
                case 2: // end point
                {
                    if (page == 0) // end point
                    {
                        r = color_xfade(orange[0], red[0], value);
                        g = color_xfade(orange[1], red[1], value);
                        b = color_xfade(orange[2], red[2], value);

                        // fx_->SetEndPoint(value);
                    }
                    else // env. decay
                    {
                        r = color_xfade(purple[0] * .2f, purple[0], value);
                        g = color_xfade(purple[1] * .2f, purple[1], value);
                        b = color_xfade(purple[2] * .2f, purple[2], value);

                        fx_->SetDecay(value);
                    }

                    if(!switch_state)
                    {
                        r = g = b = 0.f;
                    }

                    SetPthLedFloat(3, r, g, b);
                    break;
                }
                case 3: // magic
                {
                    if (page == 0) // reverb / delay
                    {   
                        if (split_delay_) {
                            if (value < .5) {
                                fx_->SetReverb(0.f);
                                fx_->SetDelayFeedback((.5f - value) * 2.f);
                            }
                            else {
                                fx_->SetReverb((value - .5f) * 2.f);
                                fx_->SetDelayFeedback(0.f);
                            }

                            r = color_triple_xfade(green[0], (green[0] + blue[0]) * .5f, blue[0], value);
                            g = color_triple_xfade(green[1], (green[1] + blue[1]) * .5f, blue[1], value);
                            b = color_triple_xfade(green[2], (green[2] + blue[2]) * .5f, blue[2], value);
                        }
                        else {
                            fx_->SetReverb(value);
                            fx_->SetDelayFeedback(value);

                            r = color_triple_xfade(teal[0], med_blue[0], blue[0], value);
                            g = color_triple_xfade(teal[1], med_blue[1], blue[1], value);
                            b = color_triple_xfade(teal[2], med_blue[2], blue[2], value);
                        }
                    }
                    else if (page == 1) // lofi
                    {
                        fx_->SetSaturate(value);
                        r = color_triple_xfade(yellow[0], orange[0], red[0], value);
                        g = color_triple_xfade(yellow[1], orange[1], red[1], value);
                        b = color_triple_xfade(yellow[2], orange[2], red[2], value);
                    }
                    else // filter
                    {
                        fx_->SetFilter(value);
                        r = color_triple_xfade(purple[0], pink[0], 1.f, value);
                        g = color_triple_xfade(purple[1], pink[1], 1.f, value);
                        b = color_triple_xfade(purple[2], pink[2], 1.f, value);
                    }

                    if(!switch_state)
                    {
                        r = g = b = 0.f;
                    }

                    SetPthLedFloat(4, r, g, b);

                    break;
                }
                case 4: // transport
                {                    
                    if(fx_->GetLooperIsEmpty())
                    {
                        SetPthLedFloat(5, 0.f, 0.f, 0.f);
                        SetPthLedFloat(6, 0.f, 0.f, 0.f);
                    }
                    else if(fx_->IsLooperPlaying())
                    {
                        float idx = value < .5f ? value * 2.f : (1.f - value) * 2.f; // 0 - 1 - 0
                        int led_on = value > .5f ? 6 : 5;
                        int led_off = value > .5f ? 5 : 6;

                        r = color_quad_xfade(med_blue[0], green[0], yellow[0], red[0], idx);
                        g = color_quad_xfade(med_blue[1], green[1], yellow[1], red[1], idx);
                        b = color_quad_xfade(med_blue[2], green[2], yellow[2], red[2], idx);

                        if(!switch_state)
                        {
                            r *= kRecDim;
                            g *= kRecDim;
                            b *= kRecDim;
                        }

                        SetPthLedFloat(led_on, r, g, b);

                        if (idx > .8f)
                        {
                            float dim = (idx - .8f) * 5.f;

                            r = color_xfade(0.f, red[0], dim);
                            g = color_xfade(0.f, red[1], dim);
                            b = color_xfade(0.f, red[2], dim);

                            if(!switch_state)
                            {
                                r *= kRecDim;
                                g *= kRecDim;
                                b *= kRecDim;
                            }

                            SetPthLedFloat(led_off, r, g, b);
                        }
                        else
                        {
                            SetPthLedFloat(led_off, 0.f, 0.f, 0.f);
                        }

                        value = value * 4.f - 2.f; // -2 - 2
                    }
                    else
                    {
                        float scrub = fx_->GetLooperScrub() * .5f;
                        int led = 5 + (scrub > 0.f);

                        scrub = fabsf(scrub);
                        SetPthLedFloat(led, scrub, scrub, scrub);
                    }

                    break;
                }
                case 5: // gain
                {
                    if(batt_display && System::GetNow() - batt_hold > 2000)
                    {
                        const float* color = &green[0];

                        switch(hw_->GetBatteryLevel())
                        {
                            case Hardware::BatteryLevel::FULL:
                                color = &white[0];
                            break;
                            case Hardware::BatteryLevel::HIGH:
                                color = &green[0];
                            break;
                            case Hardware::BatteryLevel::MEDIUM:
                                color = &yellow[0];
                            break;
                            case Hardware::BatteryLevel::LOW:
                                color = &red[0];
                            break;
                            default:
                            break;
                        }

                        r = color[0];
                        g = color[1];
                        b = color[2];
                    }
                    else if (page == 0)
                    {
                        float vu_sample = fx_->GetVUSample(VUTarget::VU_OUTPUT);

                        r = value * color_quad_xfade(.1f, green[0], yellow[0], pink[0], vu_sample);
                        g = value * color_quad_xfade(.1f, green[1], yellow[1], pink[1], vu_sample);
                        b = value * color_quad_xfade(.1f, green[2], yellow[2], pink[2], vu_sample);

                        fx_->SetMainGain(value);
                    }
                    else
                    {
                        r = color_xfade(blue[0], red[0], value);
                        g = color_xfade(blue[1], red[1], value);
                        b = color_xfade(blue[2], red[2], value);

                        fx_->SetInputGain(value);
                    }
                    SetPthLedFloat(9, r, g, b);
                    break;
                }
                default:
                    break;
                }
            }

            /** PTH leds */
            float r, g, b;
            // play key
            if(fx_->GetLooperIsEmpty() && !fx_->IsLooperRecordArmed())
            {
                r = g = b = 0.f;
            }
            else if(fx_->IsLooperRecordArmed())
            {
                r = g = b = 1.f;
            }
            else if(fx_->IsLooperFirstRecording() && fx_->IsLooperRecording())
            {
                r = teal[0];
                g = teal[1];
                b = teal[2];
            }
            else if(fx_->IsLooperPlaying())
            {
                float position = 1.f - fx_->GetLooperPosition();
                r = teal[0] * position;
                g = teal[1] * position;
                b = teal[2] * position;
            }
            else // we're paused
            {
                float position = 1.f - fx_->GetLooperPosition();
                r = position;
                g = position;
                b = position;
            }

            if(!switch_state)
            {
                r *= kRecDim;
                g *= kRecDim;
                b *= kRecDim;
            }
            SetPthLedFloat(led_map[33], r, g, b);

            // loop key
            if(fx_->GetLooperIsEmpty() && !fx_->IsLooperRecordArmed())
            {
                r = g = b = 0.f;
            }
            else if(fx_->IsLooperRecordArmed())
            {
                if(now - last_arm_blink > 300)
                {
                    arm_blink = !arm_blink;
                    last_arm_blink = now;
                }

                r = arm_blink ? 1.f : 0.f;
                g = 0.f;
                b = 0.f;
            }
            else if(fx_->IsLooperFirstRecording() && fx_->IsLooperRecording())
            {
                r = red[0];
                g = red[1];
                b = red[2];
            }
            else if(fx_->IsLooperRecording()) // overdub
            {
                float position = fx_->GetLooperPosition();
                r = yellow[0] * position;
                g = yellow[1] * position;
                b = yellow[2] * position;
            }
            else
            {
                float position = fx_->GetLooperPosition();
                r = position;
                g = position;
                b = position;
            }

            if(!switch_state)
            {
                r *= kRecDim;
                g *= kRecDim;
                b *= kRecDim;
            }
            SetPthLedFloat(led_map[34], r, g, b);

            // chompi key
            fx_->SetInputMonitor(!switch_state);
            if (!switch_state)
            {
                if(copier_->IsCopying())
                {
                    if(now - last_record_blink > 300)
                    {
                        record_blink = !record_blink;
                        last_record_blink = now;
                    }

                    if(record_blink)
                    {
                        r = pink[0];
                        g = pink[1];
                        b = pink[2];
                    }
                    else
                    {
                        r = g = b = 0.f;
                    }
                }
                else if (fx_->Recording())
                {
                    r = red[0];
                    g = red[1];
                    b = red[2];
                }
                else
                {
                    float vu_sample = fx_->GetVUSample(VUTarget::VU_INPUT);
                
                    r = color_quad_xfade(.1f, green[0], yellow[0], pink[0], vu_sample);
                    g = color_quad_xfade(.1f, green[1], yellow[1], pink[1], vu_sample);
                    b = color_quad_xfade(.1f, green[2], yellow[2], pink[2], vu_sample);
                }
            }
            else
            {
                if (chompi_key_pressed)
                {
                    r = .67f;
                    g = 0.f;
                    b = 1.f;
                }
                else
                {
                    r = g = b = 0.f;
                }
            } 

            SetPthLedFloat(led_map[5], r, g, b);

            // ========   send the data   =========
            fill_led_data();
        }

        bool OnButton(uint16_t buttonID,
                      uint8_t numberOfPresses,
                      bool isRetriggering) override
        {
            if (init_ignore || copier_->IsCopying())
                return false;

            bool rising = numberOfPresses == 1;
            switch (buttonID)
            {
            // NO CONNECT, SKIP THESE
            case static_cast<uint16_t>(Hardware::SwId::NC_1): // fall through
            case static_cast<uint16_t>(Hardware::SwId::NC_2): // fall through
            case static_cast<uint16_t>(Hardware::SwId::NC_3): // fall through
            case static_cast<uint16_t>(Hardware::SwId::NC_4): // fall through
            case static_cast<uint16_t>(Hardware::SwId::NC_5): // fall through
                                                              // case static_cast<uint16_t>(Hardware::SwId::NC_6): // caught in ui.h
                break;

            // encoder clicks, toggle pages
            case static_cast<uint16_t>(Hardware::SwId::ENC_1_SW): // fall through
            case static_cast<uint16_t>(Hardware::SwId::ENC_2_SW): // fall through
            case static_cast<uint16_t>(Hardware::SwId::ENC_3_SW): // fall through
            case static_cast<uint16_t>(Hardware::SwId::ENC_4_SW): // fall through
            {
                if(!rising)
                {
                    uint8_t knob = key_map[buttonID];
                    knob_page[knob]++;
                    knob_page[knob] %= knob_num_pages[knob];
                }
                break;
            }

            case static_cast<uint16_t>(Hardware::SwId::ENC_6_SW):
            {
                if(!rising && System::GetNow() - batt_hold < 2000)
                {
                    uint8_t knob = key_map[buttonID];
                    knob_page[knob]++;
                    knob_page[knob] %= knob_num_pages[knob];
                }

                batt_hold = System::GetNow();
                batt_display = rising;

                break;
            }

            // reset the looper pitch
            case ENC_5_SW:
            {
                if (!rising)
                {
                    enc_values[0][4] = enc_defaults[0][4];
                    hw_->SendCC(midi_channel, cc_map[0][4], enc_values[0][4] * 127.f);
                }

                fx_->SetLooperPitch(1.f);
                fx_->ResetLooperPitchQuant();
                break;
            }

            // toggle. We're not using this anymore, just here in case something breaks
            case static_cast<uint16_t>(Hardware::SwId::SW_TOG): // fall through
                // switch_state = rising;
                // if (!rising)
                //     midi_channel = 0;

                // some weirdness results in handling this on edges rather than as pressed
                // for example if you hold the chompi key with the switch up then toggle the sw
                // down, you'll be on ch 1 until you release and repress the chompi key
                // then it will go to ch 2 like it should
                break;

            // CC buttons
            // case static_cast<uint16_t>(Hardware::SwId::ENC_4_SW):
            // {
            //     if (!rising)
            //     {
            //         enc_values[0][0] = enc_defaults[0][0];
            //         hw_->SendCC(midi_channel, cc_map[0][0], enc_values[0][0] * 127);
            //         fx_->SetGlobalPitch(1.f);
            //         fx_->SetReverse(false);

            //         DumpValuePresets();
            //         SetPthLedFloat(1, green[0], green[1], green[2]);
            //     }

            //     hw_->SendCC(midi_channel, key_map[buttonID], rising ? 127 : 0);
            //     break;
            // }

            // CC buttons
            case static_cast<uint16_t>(Hardware::SwId::KEY_27): // play
            {
                last_arm_blink = System::GetNow();
                fx_->LooperPlayButton(rising);    
                hw_->SendCC(midi_channel, 26, rising ? 127 : 0);
                break;
            }
            case static_cast<uint16_t>(Hardware::SwId::KEY_28): // loop
            {
                last_arm_blink = System::GetNow();
                if(!fx_->Recording())
                    fx_->LooperRecordButton(rising);
                hw_->SendCC(midi_channel, 27, rising ? 127 : 0);
                break;
            }

            case static_cast<uint16_t>(Hardware::SwId::KEY_26):
            {
                chompi_key_pressed = rising;
                if (!switch_state)
                {
                    hw_->SendCC(midi_channel, key_map[buttonID], rising ? 127 : 0);
                }
                else
                {
                    midi_channel = rising;
                }

                // chompi mode
                if(!switch_state)
                {
                    bool latch = fx_->GetRecordLatch();
                    bool rec = fx_->Recording();

                    if (rising && !rec)
                    {
                        fx_->StartNewRecording(0);
                        last_record_blink = System::GetNow();
                        record_blink = false;
                    }
                    else if(!rising && !latch && rec)
                        StopVoiceRecording();
                    else if(rising && rec && latch)
                        StopVoiceRecording();
                }
 
                break;
            }

            // keys
            default:
                if (rising)
                {
                    // real keypress
                    if(!isRetriggering)
                    {
                        if(fx_->GetVoiceMode() == VoiceMode::CUBBI)
                        {
                            size_t slot = KeyToSlot(buttonID);
                            if(slot == kSlotNone)
                                return true;

                            OpenCubbiSlot(slot);
                        }

                        fx_->request_fifo.PushBack(KeyRequest(KeyRequest::Type::START, 
                            key_map[buttonID] - 60, buttonID, 127.f));
                    }

                    if(fx_->GetLooperRecordArm())
                        fx_->ToggleLooperRecord();

                    // this can take some time, so it must happen last
                    if(!isRetriggering)
                        hw_->SendNoteOn(midi_channel, key_map[buttonID], 127);
                }
                else
                {
                    if(!isRetriggering)
                    {
                        fx_->request_fifo.PushBack(KeyRequest(KeyRequest::Type::STOP, 
                            0, buttonID, 127.f));
                        hw_->SendNoteOff(midi_channel, key_map[buttonID], 127);
                    }
                }
                break;
            }

            return true;
        }

        void OpenCubbiSlot(size_t slot)
        {
            if(!fx_->GetFileExists(slot - 1))
                return; // no file in that slot

            size_t mode = static_cast<size_t>(fx_->GetVoiceMode());
            size_t bank = fx_->GetBank();

            bool loop = true;
            bool sustain = true;
            float pan = .5f;

            if(!presets_->IsValid(mode, bank, slot))
            {
                enc_values[0][0] = enc_defaults[0][0];
                enc_values[0][1] = enc_defaults[0][1];
                enc_values[0][2] = enc_defaults[0][2];
                enc_values[1][0] = enc_defaults[1][0];
                enc_values[1][1] = enc_defaults[1][1];
                enc_values[1][2] = enc_defaults[1][2];
            }
            else
            {
                /** TODO: this scheme is bad now. Either we mess up the order, or we don't match the old scheme's slots */
                enc_values[0][0] = presets_->GetValue(mode, bank, slot, 0);
                enc_values[0][1] = presets_->GetValue(mode, bank, slot, 1);
                enc_values[0][2] = presets_->GetValue(mode, bank, slot, 2);
                enc_values[1][1] = presets_->GetValue(mode, bank, slot, 3);
                enc_values[1][2] = presets_->GetValue(mode, bank, slot, 4);
                loop = presets_->GetValue(mode, bank, slot, 5);
                sustain = presets_->GetValue(mode, bank, slot, 6);

                enc_values[1][0] = presets_->GetValue(mode, bank, slot, 7);
                pan = presets_->GetValue(mode, bank, slot, 8);
            }

            // TODO: clean up pitch code repetition
            float val = enc_values[0][0];
            val = val < .5f ? (.5f - val) * -2.f : (val - .5f) * 2.f; // 1 - 0 - 1

            float inv = val < 0.f ? -1.f : 1.f;
            float pitch;
            if(fabsf(val) < .33f) // .01x - .5x
                pitch = val * 1.484848f + .01f * inv;
            else if (fabsf(val) < .66f ) // .5x - 1x
                pitch = (val - .33f * inv) * 1.515151 + .5f * inv;
            else // 1x - 2x
                pitch = (val - .66 * inv) * 2.941176 + 1.f * inv;

            fx_->OpenCubbiSlot(pitch, enc_values[0][1], enc_values[0][2], enc_values[1][1], 
            enc_values[1][2], loop, sustain, enc_values[1][0],
            pan);
        }

        bool OnEncoderTurned(uint16_t encoderID,
                             int16_t turns,
                             uint16_t stepsPerRevolution) override
        {
            if (init_ignore || copier_->IsCopying())
                return false;

            uint8_t page = knob_page[encoderID];
            float old_val = enc_values[page][encoderID];

            bool update_presets = true;

            // overrode this to mean increment vs force knob position (used for CCs)
            if(stepsPerRevolution > 0)
            {
                enc_values[page][encoderID] = turns / 127.f;
            }
            else{
                float inc = turns * kEncoderCoarseStep;

                // fine steps for pitch, sample start, and sample end
                if((encoderID == 0 && page == 0 && quantized_pitch_)
                    || (encoderID == 4 && quantized_pitch_))
                {
                    inc = 0.f;
                }
                else if ((encoderID == 0 && page == 0 && !quantized_pitch_)
                    || (encoderID == 1 && page == 0)
                    || (encoderID == 2 && page == 0)
                    || (encoderID == 4 && !quantized_pitch_))
                {
                    inc = turns * kEncoderFineStep;
                }

                enc_values[page][encoderID] += inc;
            }

            // clip
            enc_values[page][encoderID] = fclamp(enc_values[page][encoderID], 0.f, 1.f);

            if (encoderID == 0 && page == 0)
            {
                if(quantized_pitch_)
                    enc_values[0][0] = fx_->SetGlobalPitchQuantized(turns, enc_values[0][0]);
                else
                    fx_->SetGlobalPitchFree(enc_values[0][0]);
            }
            else if (encoderID == 4)
            {
                if (fx_->IsLooperPlaying())
                {
                    if(quantized_pitch_)
                        enc_values[0][4] = fx_->SetLooperPitchQuantized(turns, enc_values[0][4]);
                    else
                        fx_->SetLooperPitchFree(enc_values[0][4]);
                }
                else
                {
                    enc_values[0][4] = old_val;
                    fx_->SetLooperScrub(turns);
                }  
            }

            // don't allow end point too close to start point
            // TODO: set here, if they don't update, don't update
            if(page == 0 && (encoderID == 1 || encoderID == 2))
            {
                if ((enc_values[0][1] + .01f) >= enc_values[0][2])
                {
                    enc_values[page][encoderID] = old_val;
                    // enc_values[0][2] = enc_values[0][1] + .01f;
                }
                else if(encoderID == 1)
                {
                    if(!fx_->SetStartPoint(enc_values[0][1]) && turns > 0)
                    {
                        update_presets = false;
                        enc_values[page][encoderID] = old_val;
                    }
                }
                else if(encoderID == 2)
                {
                    if(!fx_->SetEndPoint(enc_values[0][2]) && turns < 0)
                    {
                        update_presets = false;
                        enc_values[page][encoderID] = old_val;                        
                    }
                }

            }

            if (stepsPerRevolution == 0) {
                hw_->SendCC(midi_channel, cc_map[page][encoderID], enc_values[page][encoderID] * 127);
            }


            if(encoderID < 3 && update_presets)
            {
                DumpValuePresets();
            }

            return true;
        }

        void DumpValuePresets()
        {
            size_t mode = static_cast<size_t>(fx_->GetVoiceMode());
            size_t bank = fx_->GetBank();
            size_t slot = fx_->GetVoiceSlot();

            presets_->SetValue(enc_values[0][0], mode, bank, slot, 0);
            presets_->SetValue(enc_values[0][1], mode, bank, slot, 1);
            presets_->SetValue(enc_values[0][2], mode, bank, slot, 2);
            presets_->SetValue(enc_values[1][1], mode, bank, slot, 3);
            presets_->SetValue(enc_values[1][2], mode, bank, slot, 4);
            presets_->SetValue(fx_->GetAutoLoop(), mode, bank, slot, 5);
            presets_->SetValue(fx_->GetSustainActive(), mode, bank, slot, 6);

            presets_->SetValue(enc_values[1][0], mode, bank, slot, 7);
            presets_->SetValue(fx_->GetPan(), mode, bank, slot, 8);

        }

        void SetSwitchState(bool state)
        {
            if(state && !switch_state && fx_->Recording() && !copier_->IsCopying())
            {
                StopVoiceRecording();
            }

            switch_state = state; 
        }

        void StopVoiceRecording()
        {
            enc_values[0][0] = enc_defaults[0][0];
            enc_values[0][1] = enc_defaults[0][1];
            enc_values[0][2] = enc_defaults[0][2];
            enc_values[1][0] = enc_defaults[1][0];
            enc_values[1][1] = enc_defaults[1][1];
            enc_values[1][2] = enc_defaults[1][2];

            fx_->StopRecording();
        }

        inline void SetInitIgnore(bool ignore) { init_ignore = ignore; }

    private:
        Hardware *hw_;
        Engine *fx_;
        FileCopier *copier_;
        PresetManager* presets_;

        float** enc_values;
        const float** enc_defaults;

        /** todo: these really shouldn't be stored in here
         *  Gonna move them out to a midi engine later
        */ 
        uint8_t midi_channel = 0;
        bool switch_state = false;
        bool chompi_key_pressed = false;
        uint8_t* knob_page;
        bool quantized_pitch_;
        bool split_delay_;

        bool batt_display;
        uint32_t batt_hold;
    };

} // namespace chompi