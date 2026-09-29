/** @file MenuPage.h
 *  @brief The shift menu UiPage (see ui.h) Opened by holding the CHOMPI key.
 *  Closed automatically a moment after releasing it (IsClosable()) 
 *  unless a preset operation is still in progress. Two things happen here:
 *
 *   1. While the chompi key is held, all 6 knobs' "page 1" alternate functions
 *      become live (wavetable select/cycle, LFO rates, filter resonance, delay
 *      time, output compression, clock division) An encoder click resets that knob's 
 *      current page to its default value and flashes its LED white for a moment.
 *   2. The 14 white keys double as a preset-slot picker, and 3 dedicated keys
 *      (save/copy/erase) handle the presets
 */
#include "hardware.h"
#include "temp_led_stuff.h"
#include "Sequencer.h"

namespace chompi
{
    class MenuPage : public daisy::UiPage
    {
    public:

        enum class PresetMode
        {
            NONE = 0,
            ERASE_SEL,
            ERASING,
            COPY_SRC,
            COPY_DEST,
            COPYING,
            SAVE_SEL,
            SAVING,
            LAST,
        };


        void Init(clockManager *clock_manager, myEngine *engine, Sequencer *seq, Hardware *hw, float** enc_arr, const float** def_arr,
            uint8_t* page, PresetManager* pre)
        {
            hw_ = hw;
            engine_ = engine;
            seq_ = seq;
            clock_manager_ = clock_manager;
            presets_ = pre;
            enc_values = enc_arr;
            enc_defaults = def_arr;
            knob_page = page;
            
            quantized_pitch_ = true;

            key_color = &purple[0];

            last_blink = System::GetNow();

            chompi_key_pressed = false;

            preset_mode = PresetMode::NONE;

            pre_quantized_amount = .5;

            final_comp = 0.f;
            res = .63f;
            del_time = .4f;
            pitch_lfo_rate = .58f;
            filter_lfo_rate = .58f;
            engine_->setDelayTime(del_time);

        }

        void Draw(const daisy::UiCanvasDescriptor &canvasDescriptor) override
        {
            /** PTH leds */
            float r, g, b;
            uint32_t now = System::GetNow();

            if(now - last_blink > 250)
            {
                blink_state = !blink_state;
                last_blink = now;
            }

            // chompi key
            if (chompi_key_pressed && preset_mode == PresetMode::NONE)
            {
                r = .67f;
                g = 0.f;
                b = 1.f;
            }
            else if (
                    (preset_mode == PresetMode::SAVE_SEL
                    || preset_mode == PresetMode::ERASE_SEL
                    || preset_mode == PresetMode::COPY_DEST)
                    && selected_slot != kSlotNone)
            {
                r = blink_state;
                g = b = 0.f;
            }
            else if (preset_mode == PresetMode::SAVING 
                        || preset_mode == PresetMode::COPYING 
                        || preset_mode == PresetMode::ERASING)
            {
                r = g = b = blink_state;
            }
            else
            {
                r = g = b = 0.f;
            }
            SetPthLedFloat(0, r, g, b);
        
            // play / overdub keys
            if(preset_mode == PresetMode::NONE)
            {
                if(tempo_reset)
                {
                    SetPthLedFloat(5, 1.f, 1.f, 1.f);
                    SetPthLedFloat(6, 1.f, 1.f, 1.f);
                }
                else
                {
                    float dled[6];
                    clock_manager_->fillLEDdata(dled);
                    SetPthLedFloat(5, dled[0], dled[1], dled[2]);
                    SetPthLedFloat(6, dled[3], dled[4], dled[5]);
                }
            }

            else if (preset_mode == PresetMode::COPY_SRC
                || preset_mode == PresetMode::COPY_DEST)
            {
                //SetPthLedFloat(5, 0.f, 0.f, 0.f);
                //SetPthLedFloat(6, 0.f, 0.f, 0.f);                

                if(selected_slot == 16)
                {
                    SetPthLedFloat(7, blue[0], blue[1], blue[2]);
                    SetPthLedFloat(8, blue[0], blue[1], blue[2]);
                }
                else if(copy_src == 16)
                {
                    SetPthLedFloat(7, green[0], green[1], green[2]);
                    SetPthLedFloat(8, green[0], green[1], green[2]);
                }
                else if(!blink_state)
                {
                    SetPthLedFloat(7, 0.f, 0.f, 0.f);
                    SetPthLedFloat(8, 0.f, 0.f, 0.f);
                }
            }
            else
            {
                //SetPthLedFloat(5, 0.f, 0.f, 0.f);
                //SetPthLedFloat(6, 0.f, 0.f, 0.f);                

                SetPthLedFloat(7, 0.f, 0.f, 0.f);
                SetPthLedFloat(8, 0.f, 0.f, 0.f);                
            }

            // shift encoder display
            if(chompi_key_pressed)
            {
                // FX
                if(fx_reset)
                {
                    SetPthLedFloat(4, 1.f, 1.f, 1.f);
                }
                else if(knob_page[3] == 0)
                {
                    SetPthLedFloat(4, del_time, del_time, del_time);
                }
                else // filter
                {
                    SetPthLedFloat(4, res, res, res);
                }

                if(att_reset)
                {
                    SetPthLedFloat(2, 1.f, 1.f, 1.f);
                }
                else if(knob_page[1] == 0)
                {
                    r = color_xfade(yellow[0], orange[0], enc_values[0][1]);
                    g = color_xfade(yellow[1], orange[1], enc_values[0][1]);
                    b = color_xfade(yellow[2], orange[2], enc_values[0][1]);
                    SetPthLedFloat(2, r, g, b);
                }
                else
                {
                    SetPthLedFloat(2, pitch_lfo_rate, pitch_lfo_rate, pitch_lfo_rate);
                }

                if(dec_reset)
                {
                    SetPthLedFloat(3, 1.f, 1.f, 1.f);
                }
                else if(knob_page[2] == 0)
                {
                    r = color_xfade(orange[0], red[0], enc_values[0][2]);
                    g = color_xfade(orange[1], red[1], enc_values[0][2]);
                    b = color_xfade(orange[2], red[2], enc_values[0][2]);
                    SetPthLedFloat(3, r, g, b);
                }
                else
                {
                    SetPthLedFloat(3, filter_lfo_rate, filter_lfo_rate, filter_lfo_rate);
                }

                // headphone os gain
                // float idx = final_comp;

                if(vol_reset)
                {
                    r = g = b = 1.f;
                }
                else
                {
                    r = med_blue[0] * (final_comp * .9f + .1f);
                    g = med_blue[1] * (final_comp * .9f + .1f);
                    b = med_blue[2] * (final_comp * .9f + .1f);
                }

                SetPthLedFloat(9, r, g, b);

                // pitch knob
                if(pitch_reset)
                {
                    r = g = b = 1.f;
                }
                else if(knob_page[0] == 0)
                {
                    r = color_quad_xfade(med_blue[0], green[0], yellow[0], red[0], enc_values[0][0]);
                    g = color_quad_xfade(med_blue[1], green[1], yellow[1], red[1], enc_values[0][0]);
                    b = color_quad_xfade(med_blue[2], green[2], yellow[2], red[2], enc_values[0][0]);
                }
                else if(knob_page[0] == 1)
                {// wavetable select
                    r = color_triple_xfade(teal[0], blue[0], green[0], wavetable_idx);
                    g = color_triple_xfade(teal[1], blue[1], green[1], wavetable_idx);
                    b = color_triple_xfade(teal[2], blue[2], green[2], wavetable_idx);
                }

                SetPthLedFloat(1, r, g, b);
            }

            // save key
            if((preset_mode == PresetMode::SAVE_SEL 
                || preset_mode == PresetMode::SAVING
                || preset_mode == PresetMode::NONE)
                && !no_sd_card_)
            {
                SetSmtLedFloat(9, blue[0], blue[1], blue[2]);
            }
            else
            {
                SetSmtLedFloat(9, 0.f, 0.f, 0.f);
            }

            // copy key
            if((preset_mode == PresetMode::COPY_SRC 
                || preset_mode == PresetMode::COPY_DEST
                || preset_mode == PresetMode::COPYING
                || preset_mode == PresetMode::NONE)
                && !no_sd_card_)
            {
                SetSmtLedFloat(8, green[0], green[1], green[2]); // copy
            }
            else
            {
                SetSmtLedFloat(8, 0.f, 0.f, 0.f);
            }

            // erase key
            if((preset_mode == PresetMode::ERASE_SEL 
                || preset_mode == PresetMode::ERASING
                || preset_mode == PresetMode::NONE)
                && !no_sd_card_)
            {
                SetSmtLedFloat(7, red[0], red[1], red[2]); // erase
            }
            else
            {
                SetSmtLedFloat(7, 0.f, 0.f, 0.f);
            }

            if (engine_->getPitchLfoOn())
                SetSmtLedFloat(5, yellow[0], yellow[1], yellow[2]);
            else
                SetSmtLedFloat(5, 0.f, 0.f, 0.f);
            if (engine_->getFilterLfoOn())
                SetSmtLedFloat(6, yellow[0], yellow[1], yellow[2]);
            else
                SetSmtLedFloat(6, 0.f, 0.f, 0.f);
        
            SetSmtLedFloat(2, 0.f, 0.f, 0.f);
            SetSmtLedFloat(3, 0.f, 0.f, 0.f);
            SetSmtLedFloat(4, 0.f, 0.f, 0.f);
            if(seq_->getGate() <= .25f)
                SetSmtLedFloat(2, yellow[0], yellow[1], yellow[2]);
            else if(seq_->getGate() <= .75f)
                SetSmtLedFloat(3, yellow[0], yellow[1], yellow[2]);
            else
                SetSmtLedFloat(4, yellow[0], yellow[1], yellow[2]);

            // white keys
            for (uint8_t i = 1; i < 16; i++)
            {
                if(no_sd_card_ && i != 15)
                    SetSmtLedFloat(25 - i, red[0], red[1], red[2]);
                else if(preset_mode == PresetMode::SAVE_SEL && i == 15)
                    SetSmtLedFloat(25 - i, pink[0], pink[1], pink[2]);
                else if(selected_slot == i && preset_mode == PresetMode::SAVE_SEL)
                    SetSmtLedFloat(25 - i, blue[0], blue[1], blue[2]);
                else if(selected_slot == i && preset_mode == PresetMode::ERASE_SEL)
                    SetSmtLedFloat(25 - i, red[0], red[1], red[2]);
                else if(selected_slot == i && preset_mode == PresetMode::COPY_DEST)
                    SetSmtLedFloat(25 - i, blue[0], blue[1], blue[2]);
                else if((selected_slot == i && preset_mode == PresetMode::COPY_SRC) || (copy_src == i && preset_mode == PresetMode::COPY_DEST))
                    SetSmtLedFloat(25 - i, green[0], green[1], green[2]);
                else if(engine_->getVoiceSlot() == i && preset_mode == PresetMode::NONE)
                    SetSmtLedFloat(25 - i, 1.f, 1.f, 1.f);
                else if (
                    preset_mode == PresetMode::SAVE_SEL 
                    || preset_mode == PresetMode::ERASE_SEL
                    || preset_mode == PresetMode::COPY_SRC
                    || preset_mode == PresetMode::COPY_DEST
                )
                {
                    if (!blink_state)
                        SetSmtLedFloat(25 - i, 0.f, 0.f, 0.f);
                    else if(i == 15)
                        SetSmtLedFloat(25 - i, 0.f, 0.f, 0.f);
                    else if(presets_->IsValid(i))
                        SetSmtLedFloat(25 - i, .4f * purple[0], .4f * purple[1], .4f * purple[2]);
                    else if (
                        preset_mode == PresetMode::SAVE_SEL
                        || preset_mode == PresetMode::COPY_DEST
                    )
                    {
                        SetSmtLedFloat(25 - i, .4f, .4f, .4f);
                    }
                    else
                        SetSmtLedFloat(25 - i, 0.f, 0.f, 0.f);
                }
                else if(i == 15) // defaults slot: always pink so it reads as
                    //selectable-but-different (white above when it is the active slot)
                    SetSmtLedFloat(25 - i, pink[0], pink[1], pink[2]);
                else if(presets_->slotValid[i-1])
                        SetSmtLedFloat(25 - i, purple[0], purple[1], purple[2]);
                else
                    SetSmtLedFloat(25 - i, 0.f, 0.f, 0.f);
            }

            // banks
            SetSmtLedFloat(0, 0.f, 0.f, 0.f);
            SetSmtLedFloat(1, 0.f, 0.f, 0.f);
            int oct = engine_->getOctave();
            int mode = oct == 1 ? 1 : 0;

            if(!no_sd_card_ && oct != 0)
                SetSmtLedFloat(mode, orange[0], orange[1], orange[2]);
            else
                SetSmtLedFloat(mode, 0.f, 0.f, 0.f);

            // ========   send the data   =========
            fill_led_data();
        }


        bool OnEncoderTurned(uint16_t encoderID,
                             int16_t turns,
                             uint16_t stepsPerRevolution) override
        {
            uint8_t page = knob_page[encoderID];
            float inc = turns * kEncoderCoarseStep;

            // we're receiving a knob position via CC
            if(stepsPerRevolution > 0)
                return false; // fall through to normalpage

            float r, g, b;
            if(preset_mode == PresetMode::NONE)
            {
                if(encoderID == 0)
                {
                    if(page == 0) // stepped pitch
                    {
                        pre_quantized_amount += inc;
                        pre_quantized_amount = fclamp(pre_quantized_amount, 0.f, 1.f);
                        //Quantize here:
                        float normalizedValue = (pre_quantized_amount - .5f) * 2.f;
                        int semitoneShift = round(normalizedValue * 12);
                        float quantized_shift = (semitoneShift / 12.f) * .5f + .5f;
                        enc_values[0][0] = quantized_shift;
                        engine_->setGlobalPitch(enc_values[0][0]);
                    }
                    else if(page == 1) // wavetable select
                    {
                        // two detents advance one table - one
                        //detent per table is too twitchy, and this makes landing
                        //on a table feel intentional. Accumulator carries partial
                        //turns.
                        table_detent_acc += turns;
                        while(table_detent_acc >= 2 || table_detent_acc <= -2)
                        {
                            int8_t dir = table_detent_acc > 0 ? 1 : -1;
                            engine_->nextTable(dir, false);
                            wavetable_idx = fclamp(wavetable_idx + dir * kEncoderWtStep, 0.f, 1.f);
                            table_detent_acc -= 2 * dir;
                        }
                    }

                    //DumpValuePresets();
                }
                else if (encoderID == 1)
                {
                    if(page == 0) // coarse attack
                    {
                        enc_values[0][1] = fclamp(enc_values[0][1] + turns * kEncoderEnvCoarseStep, 0.f, 1.f);
                        engine_->setAttack(enc_values[0][1]);
                    }
                    else // pitch LFO rate
                    {
                        pitch_lfo_rate += inc;
                        pitch_lfo_rate = fclamp(pitch_lfo_rate, 0.f, 1.f);
                        engine_->setPitchLfoRate(pitch_lfo_rate);
                    }
                }
                else if (encoderID == 2) // filter LFO rate
                {
                    if(page == 0) // coarse release
                    {
                        enc_values[0][2] = fclamp(enc_values[0][2] + turns * kEncoderEnvCoarseStep, 0.f, 1.f);
                        engine_->setRelease(enc_values[0][2]);
                    }
                    else // filter LFO rate
                    {
                        filter_lfo_rate += inc;
                        filter_lfo_rate = fclamp(filter_lfo_rate, 0.f, 1.f);
                        engine_->setLfoRate(filter_lfo_rate);
                    }
                }
                else if (encoderID == 3)
                {
                    if(page == 0) // Delay
                    {
                        del_time += inc;
                        del_time = fclamp(del_time, 0.f, 1.f);
                        engine_->setDelayTime(del_time);
                    }
                    else // MMF
                    {
                        res += inc;
                        res = fclamp(res, 0.f, 1.f);
                        engine_->setMasterResonance(res);
                    }
                }
                else if(encoderID == 4)
                {
                    clock_manager_->changeDiv(turns);
                }
                else if(encoderID == 5)
                {
                    final_comp += inc;
                    final_comp = fclamp(final_comp, 0.f, 1.f);
                    engine_->setFinalComp(final_comp);
                }
            }


            return true;
        }

        void DumpValuePresets(uint8_t slot)
        {
            presets_->SetValue(enc_values[0][0], slot, 0); //pitch
            presets_->SetValue(static_cast<float>(engine_->getCycle()), slot, 1); //wavetable cycle
            presets_->SetValue(static_cast<float>(engine_->getTable()), slot, 2); //wavetable file
            presets_->SetValue(enc_values[0][1], slot, 3); //attack
            presets_->SetValue(enc_values[1][1], slot, 4); //lfo depth
            presets_->SetValue(enc_values[0][2], slot, 5); //release
            presets_->SetValue(enc_values[1][2], slot, 6); //lfo rate
            presets_->SetValue(enc_values[1][3], slot, 7); //cutoff position (page 2)
            presets_->SetValue(pitch_lfo_rate, slot, 8);
            presets_->SetValue(enc_values[0][3], slot, 9); //delay position (page 1)
            presets_->SetValue(res, slot, 10); //resonance
            presets_->SetValue(filter_lfo_rate, slot, 11);
            presets_->SetValue(del_time, slot, 12); //delay time
            presets_->SetValue(static_cast<float>((engine_->getPitchLfoOn() ? 1 : 0)
                                                | (engine_->getFilterLfoOn() ? 2 : 0)), slot, 13);

        }

        void SetVoiceSlot(size_t slot)
        {
            engine_->setVoiceSlot(slot);

            size_t mode = 0;
            size_t bank = 0;

            if(slot == 15 || !presets_->IsValid(slot))
            {
                // slot 15 is the DEFAULTS slot (TEMPO
                //SetVoiceSlot slot==14 idiom) - a COMPLETE default patch, not just
                //the first pages. Also used for any empty slot.
                enc_values[0][0] = enc_defaults[0][0];
                enc_values[0][1] = enc_defaults[0][1];
                enc_values[0][2] = enc_defaults[0][2];
                enc_values[1][0] = enc_defaults[1][0];
                enc_values[1][1] = enc_defaults[1][1];
                enc_values[1][2] = enc_defaults[1][2];
                enc_values[0][3] = enc_defaults[0][3];
                enc_values[1][3] = enc_defaults[1][3];

                engine_->setCycle(0, true);
                engine_->nextTable(0, true);
                wavetable_idx = 0.f;

                res = .63f;
                del_time = .4f;
                pitch_lfo_rate = .58f;
                filter_lfo_rate = .58f;
                engine_->setPitchLfoOn(true);
                engine_->setFilterLfoOn(true);
            }
            else
            {
                enc_values[0][0] = presets_->GetValue(mode, bank, slot, 0); //pitch

                uint8_t cyc = static_cast <int> (presets_->GetValue(mode, bank, slot, 1));
                engine_->setCycle(cyc, true); //cycle
                enc_values[1][0] = cyc * kEncoderCycleStep;
                
                uint8_t tab = static_cast <int> (presets_->GetValue(mode, bank, slot, 2)); //table
                engine_->nextTable(tab, true);
                wavetable_idx = tab * kEncoderWtStep;

                enc_values[0][1] = presets_->GetValue(mode, bank, slot, 3); //attack
                enc_values[1][1] = presets_->GetValue(mode, bank, slot, 4); //lfo depth
                enc_values[0][2] = presets_->GetValue(mode, bank, slot, 5); //release
                enc_values[1][2] = presets_->GetValue(mode, bank, slot, 6); //lfo rate

                enc_values[1][3] = presets_->GetValue(mode, bank, slot, 7); //cutoff pos (page 2)
                pitch_lfo_rate = presets_->GetValue(mode, bank, slot, 8);
                enc_values[0][3] = presets_->GetValue(mode, bank, slot, 9); //delay pos (page 1)
                res = presets_->GetValue(mode, bank, slot, 10); //resonance
                filter_lfo_rate = presets_->GetValue(mode, bank, slot, 11);
                del_time = presets_->GetValue(mode, bank, slot, 12); //delay time

                int t = static_cast<int>(round(presets_->GetValue(mode, bank, slot, 13)));
                engine_->setPitchLfoOn(t & 1);
                engine_->setFilterLfoOn(t & 2);
            }

            //do I actually need to do this?
            engine_->setGlobalPitch(enc_values[0][0]);
            engine_->setAttack(enc_values[0][1]);
            engine_->setPitchLfoDepth(enc_values[1][1]);
            engine_->setLfoDepth(enc_values[1][2]);
            engine_->setPitchLfoRate(pitch_lfo_rate);
            engine_->setLfoRate(filter_lfo_rate);
            engine_->setRelease(enc_values[0][2]);
            engine_->setMasterCutoff(enc_values[1][3]);
            engine_->setDelayFeedback(enc_values[0][3]);
            engine_->setMasterResonance(res);
            engine_->setDelayTime(del_time);
        }

        bool OnButton(uint16_t buttonID,
                uint8_t numberOfPresses,
                bool isRetriggering) override
        {
            if(isRetriggering)
                return true;

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


            // Each encoder's switch here resets that knob's current page
            case static_cast<uint16_t>(Hardware::SwId::ENC_4_SW): // pitch knob
            {
                const int page = knob_page[0];
                pitch_reset = rising;

                if(rising)
                {
                    if(page == 0)
                    {
                        enc_values[0][0] = enc_defaults[0][0];
                        engine_->setGlobalPitch(.5f);
                        pre_quantized_amount = .5f;
                    }
                    else if(rising && page == 1)
                    {
                        enc_values[1][0] = enc_defaults[1][0];
                        engine_->setCycle(0, true);
                    }

                    //DumpValuePresets();
                    SetPthLedFloat(1, 1.f, 1.f, 1.f);
                }
            break;
            }

            case ENC_5_SW:
            {
                tempo_reset = rising;
                if (rising) {
                    clock_manager_->resetDiv();
                    clock_manager_->changeTempo(320);
                    enc_values[0][4] = .5f; // (320 - 160) / 320
                }
                break;
            }

            case static_cast<uint16_t>(Hardware::SwId::ENC_1_SW): // attack knob
            {
                att_reset = rising;
                if(rising)
                {
                    const int page = knob_page[1];
                    enc_values[page][1] = enc_defaults[page][1];
                    if(page == 0)
                    {
                        engine_->setAttack(enc_values[0][1]);
                    }
                    else
                    {
                        engine_->setPitchLfoDepth(enc_values[1][1]);
                        pitch_lfo_rate = .58f;
                        engine_->setPitchLfoRate(pitch_lfo_rate);
                    }
                }
            break;
            }

            case static_cast<uint16_t>(Hardware::SwId::ENC_2_SW):
            {
                dec_reset = rising;
                if(rising)
                {
                    const int page = knob_page[2];
                    enc_values[page][2] = enc_defaults[page][2];
                    if(page == 0)
                    {
                        engine_->setRelease(enc_values[0][2]);
                    }
                    else
                    {
                        engine_->setLfoDepth(enc_values[1][2]);
                        filter_lfo_rate = .58f;
                        engine_->setLfoRate(filter_lfo_rate);
                    }
                }
            break;
            }

            case static_cast<uint16_t>(Hardware::SwId::ENC_6_SW): // volume
                vol_reset = rising;
                if(rising)
                {
                    const int page = knob_page[5];
                    enc_values[page][5] = enc_defaults[page][5];
                    if(page == 0)
                        engine_->setGain(enc_values[0][5]);
                    else
                        engine_->setPan(enc_values[1][5]);
                    final_comp = 0.f;
                    engine_->setFinalComp(final_comp);
                }
            break;

            case static_cast<uint16_t>(Hardware::SwId::ENC_3_SW): // magic wand
            {
                fx_reset = rising;
                if(rising)
                {
                    enc_values[0][3] = enc_defaults[0][3];
                    enc_values[1][3] = enc_defaults[1][3];

                    engine_->setDelayFeedback(enc_values[0][3]);
                    engine_->setMasterCutoff(enc_values[1][3]);

                    res = .63f;
                    del_time = .4f;

                    engine_->setMasterResonance(res);
                    engine_->setDelayTime(del_time);
                }
            }

            case static_cast<uint16_t>(Hardware::SwId::SW_TOG): // toggle (no longer used, here for safety)
                break;

            case static_cast<uint16_t>(Hardware::SwId::KEY_26): // chompi
            {
                chompi_key_pressed = rising;

                if(rising && selected_slot != kSlotNone)
                {
                    if(preset_mode == PresetMode::SAVE_SEL)
                    {
                        engine_->stopAllVoices();

                        blink_startt = System::GetNow();

                        preset_mode = PresetMode::SAVING;
                        DumpValuePresets(selected_slot);
                        presets_->Save(selected_slot);
                        SetVoiceSlot(selected_slot);
                    }
                    else if(preset_mode == PresetMode::COPY_DEST)
                    {
                        engine_->stopAllVoices();

                        blink_startt = System::GetNow();

                        preset_mode = PresetMode::COPYING;
                        presets_->Copy(0, cs_bank, copy_src, 0, 0, selected_slot);
                        SetVoiceSlot(selected_slot);
                    }
                    else if(preset_mode == PresetMode::ERASE_SEL)
                    {
                        // if a voice is playing that slot, stop it
                        engine_->stopAllVoices();

                        preset_mode = PresetMode::ERASING;
            
                        blink_startt = System::GetNow();

                        presets_->Invalidate(selected_slot);
                        SetVoiceSlot(15);
                    }
                }
                return false;
                break;
            }

            case static_cast<uint16_t>(Hardware::SwId::KEY_16):
            case static_cast<uint16_t>(Hardware::SwId::KEY_17):
            {                
                if(rising && !no_sd_card_)
                {
                    if(buttonID == static_cast<uint16_t>(Hardware::SwId::KEY_16))
                        engine_->setOctave(-1);
                    else
                        engine_->setOctave(1);
                }
                else if(!rising)
                    return false; // note off falls through

            }
            break;

            case static_cast<uint16_t>(Hardware::SwId::KEY_18): // fall through
            case static_cast<uint16_t>(Hardware::SwId::KEY_19): // fall through
            case static_cast<uint16_t>(Hardware::SwId::KEY_20):
            {
                if(!rising)
                    return false; // note off falls through

                if(buttonID == static_cast<uint16_t>(Hardware::SwId::KEY_18))
                    seq_->setGate(.1f);
                else if(buttonID == static_cast<uint16_t>(Hardware::SwId::KEY_19))
                    seq_->setGate(.5f);
                else
                    seq_->setGate(1.f);

                break;
            }


            case static_cast<uint16_t>(Hardware::SwId::KEY_21):
            {
                if (rising) {
                    engine_->setPitchLfoOn(!engine_->getPitchLfoOn());
                }
                break;
            }
            case static_cast<uint16_t>(Hardware::SwId::KEY_22):
            {
                if(rising)
                {
                    engine_->setFilterLfoOn(!engine_->getFilterLfoOn());
                }
                break;
            }


            case static_cast<uint16_t>(Hardware::SwId::KEY_23): // erase
            {
                if(rising && !no_sd_card_)
                {
                    if(preset_mode == PresetMode::NONE)
                        preset_mode = PresetMode::ERASE_SEL;
                    else if(preset_mode == PresetMode::ERASE_SEL)
                        preset_mode = PresetMode::NONE;
                    else
                        break;

                    selected_slot = kSlotNone;
                }
                else if(!rising)
                    return false; // note off falls through

                break;
            }

            case static_cast<uint16_t>(Hardware::SwId::KEY_24): // copy
            {
                if(rising && !no_sd_card_)
                {                    
                    if(preset_mode == PresetMode::NONE)
                        preset_mode = PresetMode::COPY_SRC;
                    else if(preset_mode == PresetMode::COPY_SRC 
                            || preset_mode == PresetMode::COPY_DEST)
                        preset_mode = PresetMode::NONE;
                    else
                        break;

                    copy_src = kSlotNone;
                    selected_slot = kSlotNone;
                    ss_bank = kSlotNone;
                    cs_bank = kSlotNone;

                }
                else if(!rising)
                    return false; // note off falls through

                break;
            }

            case static_cast<uint16_t>(Hardware::SwId::KEY_25): // save
            {
                if(rising && !no_sd_card_)
                {
                    if(preset_mode == PresetMode::NONE)
                        preset_mode = PresetMode::SAVE_SEL;
                    else if(preset_mode == PresetMode::SAVE_SEL)
                        preset_mode = PresetMode::NONE;
                    else
                        break;

                    ss_bank = kSlotNone;
                    selected_slot = kSlotNone;
                }                
                else if(!rising)
                    return false; // note off falls through

                break;
            }

            // white keys and play/pause
            default:
                if (rising)
                {
                    size_t slot_req = KeyToSlot(buttonID);
                    if(buttonID == 33 || buttonID == 34)
                        slot_req = 16;

                    if(slot_req == kSlotNone)
                    {
                        // do nothing
                    }
                    else if(slot_req == 16)
                    {
                        if (preset_mode == PresetMode::COPY_DEST && copy_src != 16)
                        {
                            selected_slot = slot_req;
                        }
                    }
                    else if(
                        preset_mode == PresetMode::COPY_SRC
                        && presets_->IsValid(slot_req)
                    )
                    {
                        copy_src = slot_req;
                        preset_mode = PresetMode::COPY_DEST;
                    }
                    else if(preset_mode == PresetMode::COPY_DEST && copy_src != slot_req
                            && slot_req != 15) // slot 15 is defaults, never a copy target
                    {
                        selected_slot = slot_req;
                    }
                    else if(preset_mode == PresetMode::NONE 
                            && (slot_req == 15 || presets_->slotValid[slot_req - 1]) // It would be better if these used getters
                            && !no_sd_card_)
                    {
                        selected_slot = slot_req;
                        SetVoiceSlot(selected_slot);
                    }
                    else if(
                        preset_mode == PresetMode::ERASE_SEL 
                        && slot_req != 15
                        && presets_->slotValid[slot_req - 1]
                    )
                    {
                        selected_slot = slot_req;
                    }
                    else if(preset_mode == PresetMode::SAVE_SEL && slot_req != 15)
                    {
                        selected_slot = slot_req;
                    }
                }
                else if(buttonID < 29) // white keys, no play / pause
                {
                    return false; // allow releasing notes in shift menu
                }
                break;
            }

            return true;
        }

        void OnFocusGained() override
        {
            // the half-step accumulator can't run independently from the
            // fine-tune value. Get it from the live value every time shift opens.
            pre_quantized_amount = enc_values[0][0];

            fx_reset = false;
            pitch_reset = false;
            att_reset = false;
            dec_reset = false;
            vol_reset = false;
            tempo_reset = false;
            chompi_key_pressed = true;

            copy_src = kSlotNone;
            preset_mode = PresetMode::NONE;
        }

        inline void SetSwitchState(bool state) { switch_state = state; }

        bool IsClosable()
        { 
            if(
                System::GetNow() - blink_startt > 1250
                && ((preset_mode == PresetMode::SAVING)    // (finished saving OR
                || (preset_mode == PresetMode::COPYING)    // finished copying OR
                || (preset_mode == PresetMode::ERASING)        // finished erasing OR
                || (preset_mode == PresetMode::NONE && !chompi_key_pressed))        // did nothing
            )
            {
                return true;
            }

            return false;
        }

        /** Maps a physical key's Hardware::SwId-numbered buttonID to its preset slot
         *  number (1-14), or kSlotNone if that key isn't a preset-slot key at all */
        size_t const KeyToSlot(size_t buttonID)
        {
            if (buttonID == 7)
                return kSlotNone;
            else if (buttonID < 12)
                return buttonID - 6;
            else if (buttonID < 15)
                return kSlotNone;
            else if (buttonID == 15)
                return 1;
            else if (buttonID < 21)
                return buttonID - 10;
            else if (buttonID < 24)
                return kSlotNone;
            else if (buttonID < 29)
                return buttonID - 13;
    
            return kSlotNone;
        }

        bool no_sd_card_ = false;
        inline void NoSDCard() { no_sd_card_ = true; }

    private:
        myEngine *engine_;
        Sequencer *seq_;
        clockManager *clock_manager_;
        Hardware *hw_;
        PresetManager* presets_;
        float** enc_values;
        const float** enc_defaults;
        uint8_t* knob_page;

        bool quantized_pitch_;

        const float* key_color;

        bool chompi_key_pressed = false;
        bool blink_state = true;
        uint32_t blink_startt;
        uint32_t last_blink;

        float res, del_time;
        int8_t table_detent_acc = 0;
        float pitch_lfo_rate, filter_lfo_rate;
        float pre_quantized_amount;
        float final_comp;
        float wavetable_idx;

        bool fx_reset = false;
        bool pitch_reset = false;
        bool att_reset = false;
        bool dec_reset = false;
        bool vol_reset = false;
        bool tempo_reset = false;

        uint8_t selected_slot = kSlotNone;
        uint8_t ss_bank = kSlotNone;
        uint8_t copy_src = kSlotNone;
        uint8_t cs_bank = kSlotNone;
        bool switch_state;

        PresetMode preset_mode = PresetMode::NONE;
    };
} // namespace chompi
