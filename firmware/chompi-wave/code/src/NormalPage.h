/** @file NormalPage.h
 *  @brief The main play-mode UiPage (see ui.h) which is active until the CHOMPI key
 *  is pressed. Uses the libDaisy Draw(), OnEncoderTurned() and OnButton() functions
 *  to run UI.
 */
#pragma once

#include "hardware.h"
#include "subtractiveEngine.h"
#include "Sequencer.h"
#include "temp_led_stuff.h"
#include "clockManager.h"
#include "PresetManager.h"

namespace chompi
{
        static const uint8_t cc_map[3][6] = {
            {20, 21, 22, 23, 24, 25},
            {26, 27, 28, 29, 0, 30},
            {0, 0, 0, 0, 0, 0}}; // no encoder has a third page, so CC 31 is unused

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
    static const float kEncoderTempoStep = .003125f;
    static const float kEncoderCoarseStep = .01f;
    static const float kEncoderEnvCoarseStep = .03f;
    static const float kEncoderWtStep = 1.f / 6.f;
    static const float kEncoderCycleStep = 1.f / 33.f;
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

    // Number of pages per knob. Cycled through by clicking. Other firmwares have different number of pages
    static const uint8_t knob_num_pages[6] = {2, 2, 2, 2, 1, 2};

    class NormalPage : public daisy::UiPage
    {
    public:
        uint32_t init_time;
        bool init_ignore = true;

        void Init(clockManager *clock_manager, myEngine *engine, Sequencer *seq, Hardware *hw, float** enc_arr, const float** def_arr, 
                    uint8_t* page, PresetManager* pre, uint8_t midi_out_channel)
        {
            hw_ = hw;
            engine_ = engine;
            seq_ = seq;
            clock_manager_ = clock_manager;
            presets_ = pre;
            enc_values = enc_arr;
            enc_defaults = def_arr;
            knob_page = page;

            midi_channel = midi_out_channel;

            // normal turns are always FINE pitch, half-steps live in the shift menu
            quantized_pitch_ = false;

            for (int knob = 0; knob < 6; knob++)
            {
                for (int page = 0; page < 3; page++)
                {
                    enc_values[page][knob] = enc_defaults[page][knob];
                }
            }


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

        uint32_t last_clear_blink;

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

        // Things that need to run repeatedly like checking sequencer stuff or button holds run here
        void Draw(const daisy::UiCanvasDescriptor &canvasDescriptor) override
        {
            uint32_t now = System::GetNow();

            if (seq_->checkReset()) {
                last_clear_blink = now;
            }
            if (seq_->showClearedAll()) {
                erase_rainbow_startt = now;
            }
            if (seq_->showSequenceFull()) {
                full_blink_startt = now;
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

                if (engine_->isKeyPlaying(i)) {
                    SetSmtLedFloat(led_map[i], 1.f, 1.f, 1.f);
                }
                else {
                    SetSmtLed(led_map[i], 0, 0, 0);
                }
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
                    if(page == 0) // pitch
                    {

                        r = color_quad_xfade(med_blue[0], green[0], yellow[0], red[0], value);
                        g = color_quad_xfade(med_blue[1], green[1], yellow[1], red[1], value);
                        b = color_quad_xfade(med_blue[2], green[2], yellow[2], red[2], value);

                    }
                    else if(page == 1) // wavetable cycle
                    {
                        r = color_triple_xfade(blue[0], pink[0], red[0], value);
                        g = color_triple_xfade(blue[1], pink[1], red[1], value);
                        b = color_triple_xfade(blue[2], pink[2], red[2], value);

                    }

                    SetPthLedFloat(1, r, g, b);
                }
                break;
                case 1:
                {
                    if (page == 0) // attack
                    {
                        r = color_xfade(yellow[0], orange[0], value);
                        g = color_xfade(yellow[1], orange[1], value);
                        b = color_xfade(yellow[2], orange[2], value);

                        engine_->setAttack(value);
                    }
                    else // pitch LFO amount
                    {
                        r = color_xfade(purple[0] * .2f, purple[0], value);
                        g = color_xfade(purple[1] * .2f, purple[1], value);
                        b = color_xfade(purple[2] * .2f, purple[2], value);

                        engine_->setPitchLfoDepth(value);
                    } 

                    SetPthLedFloat(2, r, g, b);
                    break;
                }
                case 2:
                {
                    if (page == 0) // release
                    {
                        r = color_xfade(orange[0], red[0], value);
                        g = color_xfade(orange[1], red[1], value);
                        b = color_xfade(orange[2], red[2], value);

                        engine_->setRelease(value);
                    }
                    else // filter LFO amount
                    {
                        r = color_xfade(purple[0] * .2f, purple[0], value);
                        g = color_xfade(purple[1] * .2f, purple[1], value);
                        b = color_xfade(purple[2] * .2f, purple[2], value);

                        engine_->setLfoDepth(value);
                    }

                    SetPthLedFloat(3, r, g, b);
                    break;
                }
                case 3: // magic
                {
                    if (page == 0) // reverb/delay
                    {
                        engine_->setDelayFeedback(value);
                        r = color_triple_xfade(green[0], (green[0] + blue[0]) * .5f, blue[0], value);
                        g = color_triple_xfade(green[1], (green[1] + blue[1]) * .5f, blue[1], value);
                        b = color_triple_xfade(green[2], (green[2] + blue[2]) * .5f, blue[2], value);
                    }
                    else // filter
                    {
                        engine_->setMasterCutoff(value);
                        r = color_triple_xfade(purple[0], pink[0], 1.f, value);
                        g = color_triple_xfade(purple[1], pink[1], 1.f, value);
                        b = color_triple_xfade(purple[2], pink[2], 1.f, value);
                    }

                    SetPthLedFloat(4, r, g, b);

                    break;
                }
                case 4: // transport
                {
                    r = color_quad_xfade(med_blue[0], green[0], yellow[0], red[0], value);
                    g = color_quad_xfade(med_blue[1], green[1], yellow[1], red[1], value);
                    b = color_quad_xfade(med_blue[2], green[2], yellow[2], red[2], value);

                    SetPthLedFloat(5, r, g, b);
                    SetPthLedFloat(6, r, g, b);

                    if (seq_->getPlaying()) {
                        if (seq_->getLeftLights()) {
                            SetPthLedFloat(5, 0.f, 0.f, 0.f);
                        }
                        else {
                            SetPthLedFloat(6, 0.f, 0.f, 0.f);
                        }
                    }

                    break;
                }
                case 5: // gain
                {
                    if(batt_display && System::GetNow() - batt_hold > 1250)
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
                        float vu_sample = engine_->getVUSample();

                        r = value * color_quad_xfade(.1f, green[0], yellow[0], pink[0], vu_sample);
                        g = value * color_quad_xfade(.1f, green[1], yellow[1], pink[1], vu_sample);
                        b = value * color_quad_xfade(.1f, green[2], yellow[2], pink[2], vu_sample);

                        engine_->setGain(value);
                    }
                    else
                    {
                        r = color_xfade(blue[0], red[0], value);
                        g = color_xfade(blue[1], red[1], value);
                        b = color_xfade(blue[2], red[2], value);

                        engine_->setPan(value);
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

            r = g = b = 0.f;
            if (seq_->getPlaying()) {
                r = teal[0];
                g = teal[1];
                b = teal[2];
            }
            else if (seq_->getSequence()) {
                r = teal[0] * .1f;
                g = teal[1] * .1f;
                b = teal[2] * .1f;
            }

            SetPthLedFloat(led_map[33], r, g, b);

            // loop key

            if (now - full_blink_startt < 1000) {
                if (((now - full_blink_startt) / 125) % 2 == 0) {
                    r = g = b = 0.f;
                }
                else {
                    r = 1.f;
                    g = b = 0.f;
                }
            }

            else if (now - last_clear_blink < 200) {
                if (seq_->getRecording()) {
                    r = g = b = 0.f;
                }
                else {
                    r = 1.f;
                    g = b = 0.f;
                }
            }
            else if (seq_->getRecording()) {
                r = 1.f;
                g = b = 0.f;
            }
            else {
                r = g = b = 0.f;
            }
            SetPthLedFloat(led_map[34], r, g, b);

            if (now - erase_rainbow_startt < 1000) {
                static const float rainbow_r[8] = {1.f, 1.f, 1.f, 0.f, 0.f, .29f, .93f, 1.f};
                static const float rainbow_g[8] = {0.f, .57f, 1.f, 1.f, 0.f, 0.f, .51f, 0.f};
                static const float rainbow_b[8] = {0.f, 0.f, 0.f, 0.f, 1.f, .51f, .93f, 0.f};
                float idx = (now - erase_rainbow_startt) * .007f; // 0..7 over the second
                int i0 = static_cast<int>(idx);
                float frac = idx - i0;
                if (i0 > 6) { i0 = 6; frac = 1.f; }
                float rr = rainbow_r[i0] + (rainbow_r[i0 + 1] - rainbow_r[i0]) * frac;
                float rg = rainbow_g[i0] + (rainbow_g[i0 + 1] - rainbow_g[i0]) * frac;
                float rb = rainbow_b[i0] + (rainbow_b[i0 + 1] - rainbow_b[i0]) * frac;
                SetPthLedFloat(led_map[33], rr, rg, rb);
                SetPthLedFloat(led_map[34], rr, rg, rb);
            }


            // chompi key

            if (chompi_key_pressed) {
                if (!switch_state) {
                    r = g = b = 1.f;
                }
                else {
                    r = .67f;
                    g = 0.f;
                    b = 1.f;
                }
            }
            else {
                r = g = b = 0.f;
            }

            SetPthLedFloat(led_map[5], r, g, b);

            // ========   send the data   =========
            fill_led_data();
        }

        bool OnButton(uint16_t buttonID,
                      uint8_t numberOfPresses,
                      bool isRetriggering) override
        {
            if (init_ignore)
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

            // Hold to check battery level.
            case static_cast<uint16_t>(Hardware::SwId::ENC_6_SW):
            {
                if(!rising && System::GetNow() - batt_hold < 1250)
                {
                    uint8_t knob = key_map[buttonID];
                    knob_page[knob]++;
                    knob_page[knob] %= knob_num_pages[knob];
                }

                batt_hold = System::GetNow();
                batt_display = rising;

                break;
            }

            // transport encoder press
            case ENC_5_SW:
            {
                if (!rising)
                {
                    hw_->SendCC(midi_channel, cc_map[0][4], enc_values[0][4] * 127.f);
                }
                else {
                    enc_values[0][4] = clock_manager_->processTapClock(enc_values[0][4]);
                }

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

            //         DumpValuePresets();
            //         SetPthLedFloat(1, green[0], green[1], green[2]);
            //     }

            //     hw_->SendCC(midi_channel, key_map[buttonID], rising ? 127 : 0);
            //     break;
            // }

            // CC buttons
            case static_cast<uint16_t>(Hardware::SwId::KEY_27): // play
            {
                seq_->playButton(rising);
                last_arm_blink = System::GetNow();
                break;
            }
            case static_cast<uint16_t>(Hardware::SwId::KEY_28): // loop
            {
                last_arm_blink = System::GetNow();
                seq_->loopButton(rising);
                hw_->SendCC(midi_channel, 15, rising ? 127 : 0);
                break;
            }

            case static_cast<uint16_t>(Hardware::SwId::KEY_26):
            {
                chompi_key_pressed = rising;
                if (!switch_state) {
                    hw_->SendCC(midi_channel, 14, rising ? 127 : 0);
                    if (rising) {
                        if (seq_->getRecording())
                            seq_->insertRest();
                        else
                            seq_->setMuted(true);
                    }
                    else {
                        seq_->setMuted(false);
                    }
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
                        key_note_on_[buttonID] = true;
                        engine_->request_fifo.PushBack(KeyRequest(KeyRequest::Type::START, 
                            key_map[buttonID] - 60, buttonID, 127.f));

                        int n = key_map[buttonID] + 12 * engine_->getOctave();
                        if(n < 0) n = 0; else if(n > 127) n = 127;
                        midi_note_sent_[buttonID] = static_cast<uint8_t>(n);
                        hw_->queueMidiNote(midi_channel, midi_note_sent_[buttonID], 127, NoteOn);
                        
                    }
                }
                else
                {
                    if(!isRetriggering && key_note_on_[buttonID])
                    {
                        // Gate on key_note_on_: releases of keys whose PRESS was
                        // consumed by the shift menu must not stop voices, record sequencer
                        // steps, or emit MIDI note-offs (otherwise menu keys record)
                        key_note_on_[buttonID] = false;
                        //regular key mode. Stop transpose_nn_ is 0 because you don't need it?
                        engine_->request_fifo.PushBack(KeyRequest(KeyRequest::Type::STOP,
                            0, buttonID, 127.f));
                        if (seq_->getRecording()) {
                            seq_->insertNextKey(KeyRequest(KeyRequest::Type::STOP,
                            key_map[buttonID] - 60, buttonID, 127.f));
                        }
                        hw_->queueMidiNote(midi_channel, midi_note_sent_[buttonID], 127, NoteOff);
                        
                    }
                }
                break;
            }

            return true;
        }

        /** stepsPerRevolution is used as a flag to determine whether its a MIDI or real knob turn */
        bool OnEncoderTurned(uint16_t encoderID,
                             int16_t turns,
                             uint16_t stepsPerRevolution) override
        {
            if (init_ignore)
                return false;

            uint8_t page = knob_page[encoderID];

            // overrode this to mean increment vs force knob position (used for CCs)
            if(stepsPerRevolution > 0)
            {
                enc_values[page][encoderID] = turns / 127.f;
            }
            else{
                float inc = turns * kEncoderCoarseStep;

                // fine steps for pitch, sample start, and sample end
                if ((encoderID == 0 && page == 0 && !quantized_pitch_)
                    || (encoderID == 1 && page == 0)
                    || (encoderID == 2 && page == 0))
                {
                    inc = turns * kEncoderFineStep;
                }
                else if (encoderID == 4 && page == 0) {
                    inc = turns * kEncoderTempoStep;
                }
                else if (encoderID == 0 && page == 1) {
                    inc = turns * kEncoderCycleStep;
                }

                enc_values[page][encoderID] += inc;
            }

            // clip
            enc_values[page][encoderID] = fclamp(enc_values[page][encoderID], 0.f, 1.f);

            if (encoderID == 0 && page == 0)
            {
                if(quantized_pitch_) {
                    engine_->setGlobalPitch(enc_values[0][0]);
                }
                else
                    engine_->setGlobalPitch(enc_values[0][0]);
            }
            else if (encoderID == 0 && page == 1) {
                if (stepsPerRevolution > 0) {
                    uint8_t cyc = static_cast<uint8_t>(floor(enc_values[1][0] * 32.f));
                    engine_->setCycle(cyc, true);
                }
                else {
                    engine_->setCycle(turns, false);
                }
            }
            else if (encoderID == 4)
            {
                uint16_t t = clock_manager_->getTempo();
                t += turns;
                if (t > 480) {
                    t = 480;
                }
                else if (t < 160) {
                    t = 160;
                }
                clock_manager_->changeTempo(t);
            }

            // don't allow end point too close to start point. let page 1 have any range, but scale it
            if(page == 0 && (encoderID == 1 || encoderID == 2))
            {
                //enc_values[0][1] = powf(enc_values[0][1], 3.f); //This doesnt work

            }

            if (stepsPerRevolution == 0) {
                // only PHYSICAL turns
                // re-emit CC - MIDI-sourced turns (stepsPerRev==1) must not bounce back out
                // or a mirroring DAW creates a feedback loop
                hw_->SendCC(midi_channel, cc_map[page][encoderID], enc_values[page][encoderID] * 127);
            }


            if(encoderID < 3)
            {
                DumpValuePresets();
            }

            return true;
        }

        void DumpValuePresets()
        {
            size_t slot = 0;

            presets_->SetValue(enc_values[0][0], slot, 0);
            presets_->SetValue(enc_values[0][1], slot, 1);
            presets_->SetValue(enc_values[0][2], slot, 2);
            presets_->SetValue(enc_values[1][1], slot, 3);
            presets_->SetValue(enc_values[1][2], slot, 4);
            presets_->SetValue(0.f, slot, 5);
            presets_->SetValue(0.f, slot, 6);

            presets_->SetValue(enc_values[1][0], slot, 7);
            presets_->SetValue(.5, slot, 8);

        }

        inline bool getSwitchState() { return switch_state; }

        void SetSwitchState(bool state)
        {

            if (state && !switch_state)
                seq_->setMuted(false); // flipping the switch DOWN mid-mute releases it
            switch_state = state; 
        }

        inline void SetInitIgnore(bool ignore) { init_ignore = ignore; }

    private:
        Hardware *hw_;
        myEngine *engine_;
        Sequencer *seq_;
        clockManager *clock_manager_;
        PresetManager* presets_;

        float** enc_values;
        const float** enc_defaults;

        /** todo: these really shouldn't be stored in here
         *  Gonna move them out to a midi engine later
        */ 
        uint8_t midi_channel = 0;
        bool switch_state = false;
        bool chompi_key_pressed = false;
        bool key_note_on_[40] = {};
        uint8_t midi_note_sent_[40] = {};
        uint32_t full_blink_startt = 0;
        uint32_t erase_rainbow_startt = 0;
        uint8_t* knob_page;
        bool quantized_pitch_;

        bool batt_display;
        uint32_t batt_hold;
        uint32_t del_last_time;
        uint32_t del_seq_time;
    };

} // namespace chompi