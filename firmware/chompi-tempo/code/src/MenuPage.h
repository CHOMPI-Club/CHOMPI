#include "hardware.h"
#include "StateSaver.h"
#include "temp_led_stuff.h"

constexpr uint8_t kSlotNone = 100;

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

        void Init(fxEngine *fx, clockManager *clock_manager, Hardware *hw, ArpeggiatorSequencer *arpSeq, 
                    float** enc_arr, const float** def_arr, uint8_t* page, PresetManager* pre, bool ps_quant, 
                    StateSaver *stateSaver, BaseEngine **engines, OptionsManager *options)
        {
            hw_ = hw;
            engines_ = engines;
            fx_ = fx;
            clock_manager_ = clock_manager;
            arpSeq_ = arpSeq;
            presets_ = pre;
            state_ = stateSaver;
            enc_values = enc_arr;
            enc_defaults = def_arr;
            knob_page = page;
            
            midi_channel[0] = options->midi_ch_out_chroma;
            midi_channel[1] = options->midi_ch_out_slice;
            quantized_pitch_ = ps_quant;

            input_toggled_ = false;

            key_color = &purple[0];

            last_blink = System::GetNow();

            chompi_key_pressed = false;

            preset_mode = PresetMode::NONE;

            pre_quantized_amount = .5;
            window_encoder_counter = 0;

            final_comp = 0.f;
            randomness = 0.f;
            feedback = .3f;
            atk = rel = 0.f;
            pan = .5f;
            sustain = 1.f;
            loop = 1.f;
            reduce = 0.f;
            arp_randomness = 0.f;

            fx_->setGranularFeedback(feedback);

            InitStatesFromDefault();
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

            if (now - transport_blink > 125 && transport_blinking) {
                transport_blink_state = !transport_blink_state;
                transport_blink = now;
                if (now - transport_blink_timer > 500) {
                    transport_blink_state = false;
                    transport_blinking = false;
                }
            }

            if (now - transport_timer > 1000 && transport_pressed) {
                transport_pressed = false;
                clock_manager_->toggleClockMode();
                transport_blink_state = true;
                transport_blink = transport_blink_timer = System::GetNow();
                transport_blinking = true;
            }

            if (now - copy_time > 1000 && copy_pressed) {
                StateSaver::State src = state_->getState();
                saveState(fx_->getEngine(), state_->getState());
                size_t dst = ((fx_->getEngine() << 1) | state_->getState()) ^ 2;
                state_->setPlay(dst, arpSeq_->getPlay(fx_->getEngine() ^ 1));
                state_->Copy(src);
                copy_pressed = false;
                copy_blinking = true;
                copy_blink_state = true;
                copy_blink_time = last_copy_blink = now;
            }

            if (copy_blinking) {
                if (now - copy_blink_time < 1000) {
                    if (now - last_copy_blink > 125) {
                        copy_blink_state = !copy_blink_state;
                        last_copy_blink = now;
                    }
                }
                else {
                    copy_blinking = false;
                    copy_blink_state = false;
                }
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
                if (transport_blink_state) {
                    if (clock_manager_->getClockMode() == clockMode::SYNC) {
                        SetPthLedFloat(5, purple[0], purple[1], purple[2]);
                        SetPthLedFloat(6, purple[0], purple[1], purple[2]);
                    }
                    else {
                        SetPthLedFloat(5, pink[0], pink[1], pink[2]);
                        SetPthLedFloat(6, pink[0], pink[1], pink[2]);
                    }
                }
                else {
                    SetPthLedFloat(5, arp_randomness, arp_randomness, arp_randomness);
                    SetPthLedFloat(6, arp_randomness, arp_randomness, arp_randomness);
                }
            }

            else if (preset_mode == PresetMode::COPY_SRC || preset_mode == PresetMode::COPY_DEST)
            {          
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
                SetPthLedFloat(7, 0.f, 0.f, 0.f);
                SetPthLedFloat(8, 0.f, 0.f, 0.f);                
            }

            uint8_t ptn = arpSeq_->getPattern();

            switch (ptn) {
                case SEQUENCE: {
                    r = teal[0];
                    g = teal[1];
                    b = teal[2];
                }
                break;
                case ARP_UP: {
                    r = green[0];
                    g = green[1];
                    b = green[2];
                }
                break;
                case ARP_DOWN: {
                    r = yellow[0];
                    g = yellow[1];
                    b = yellow[2];
                }
                break;
                case ARP_PP: {
                    r = orange[0];
                    g = orange[1];
                    b = orange[2];
                }
                break;
                case ARP_RANDOM: {
                    r = red[0];
                    g = red[1];
                    b = red[2];
                }
                break;
            }

            SetPthLedFloat(led_map[33], r, g, b);

            ptn = arpSeq_->getRestMode();

            switch (ptn) {
                case NONE: {
                    r = teal[0];
                    g = teal[1];
                    b = teal[2];
                }
                break;
                case LAST: {
                    r = green[0];
                    g = green[1];
                    b = green[2];
                }
                break;
                case SECOND_LAST: {
                    r = yellow[0];
                    g = yellow[1];
                    b = yellow[2];
                }
                break;
                case MIDDLE_TWO: {
                    r = orange[0];
                    g = orange[1];
                    b = orange[2];
                }
                break;
                case ONLY_FIRST: {
                    r = red[0];
                    g = red[1];
                    b = red[2];
                }
                break;
            }

            SetPthLedFloat(led_map[34], r, g, b);

            // shift encoder display
            if(chompi_key_pressed)
            {
                // FX
                if(fx_reset)
                {
                    SetPthLedFloat(4, 1.f, 1.f, 1.f);
                }
                else if(knob_page[3] == 0) // random
                {
                    SetPthLedFloat(4, randomness, randomness, randomness);
                }
                else // feedback
                {
                    const float thresh = 0.4f;
                    if (feedback <= thresh) {
                        r = g = b = feedback;
                    } else {
                        float t = (feedback - thresh) / (1.f - thresh); // 0..1
                        float mix_r = (1.f - t) + t * purple[0];
                        float mix_g = (1.f - t) + t * purple[1];
                        float mix_b = (1.f - t) + t * purple[2];
                        r = mix_r * feedback;
                        g = mix_g * feedback;
                        b = mix_b * feedback;
                    }
                    SetPthLedFloat(4, r, g, b);
                }


                // headphone os gain
                if (input_toggled_) {
                    switch (fx_->getMonitorMode()) {
                        case 0:
                            r = orange[0];
                            g = orange[1];
                            b = orange[2];
                            break;
                        case 1:
                            r = blue[0];
                            g = blue[1];
                            b = blue[2];
                            break;
                        case 2:
                            r = yellow[0];
                            g = yellow[1];
                            b = yellow[2];
                        break;
                    }
                }
                else {
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
                    float idx = enc_values[0][0] < .5f ? enc_values[0][0] * 2.f : (1.f - enc_values[0][0]) * 2.f; // 0 - 1 - 0
                    r = color_quad_xfade(med_blue[0], green[0], yellow[0], red[0], idx);
                    g = color_quad_xfade(med_blue[1], green[1], yellow[1], red[1], idx);
                    b = color_quad_xfade(med_blue[2], green[2], yellow[2], red[2], idx);
                }
                else if(knob_page[0] == 1)
                {
                    r = color_triple_xfade(teal[0], blue[0], green[0], pan);
                    g = color_triple_xfade(teal[1], blue[1], green[1], pan);
                    b = color_triple_xfade(teal[2], blue[2], green[2], pan);
                }
                else {
                    r = color_xfade(purple[0] * .1f, purple[0], reduce);
                    g = color_xfade(purple[0] * .1f, purple[0], reduce);
                    b = color_xfade(purple[0] * .1f, purple[0], reduce);
                }

                SetPthLedFloat(1, r, g, b);

                if (knob_page[1] == 0) {
                    r = color_xfade(yellow[0], orange[0], enc_values[0][1]);
                    g = color_xfade(yellow[1], orange[1], enc_values[0][1]);
                    b = color_xfade(yellow[2], orange[2], enc_values[0][1]);
                }
                else {
                    r = loop > .5f ? med_blue[0] : 0.f;
                    g = loop > .5f ? med_blue[1] : 0.f;
                    b = loop > .5f ? med_blue[2] : 0.f;
                }

                SetPthLedFloat(2, r, g, b);

                if (knob_page[2] == 0) {
                    r = color_xfade(orange[0], red[0], enc_values[0][2]);
                    g = color_xfade(orange[1], red[1], enc_values[0][2]);
                    b = color_xfade(orange[2], red[2], enc_values[0][2]);
                }
                else {
                    r = sustain > .5f ? med_blue[0] : 0.f;
                    g = sustain > .5f ? med_blue[1] : 0.f;
                    b = sustain > .5f ? med_blue[2] : 0.f;
                }

                SetPthLedFloat(3, r, g, b);
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

            StateSaver::State st = state_->getState();
            // FX pre / post looper
            if (st == StateSaver::State::A) {
                SetSmtLedFloat(5, yellow[0], yellow[1], yellow[2]);
                if (copy_blink_state) {
                    SetSmtLedFloat(6, green[0], green[1], green[2]);
                }
                else {
                    SetSmtLedFloat(6, 0.f, 0.f, 0.f);
                }
            }
            else {
                if (copy_blink_state) {
                    SetSmtLedFloat(5, green[0], green[1], green[2]);
                }
                else {
                    SetSmtLedFloat(5, 0.f, 0.f, 0.f);
                }
                SetSmtLedFloat(6, yellow[0], yellow[1], yellow[2]);
            }
        
            // Input select
            int led_sel = 2;
            led_sel += fx_->getInputSource();
            SetSmtLedFloat(2, 0.f, 0.f, 0.f);
            SetSmtLedFloat(3, 0.f, 0.f, 0.f);
            SetSmtLedFloat(4, 0.f, 0.f, 0.f);
            SetSmtLedFloat(led_sel, pink[0], .7f * pink[1], .7f * pink[2]);

            // white keys
            for (size_t i = 1; i < 16; i++)
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
                else if(engines_[fx_->getEngine()]->getVoiceSlot() == i && preset_mode == PresetMode::NONE)
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
                    else if(preset_mode == PresetMode::ERASE_SEL && i == 15)
                        SetSmtLedFloat(25 - i, 0.f, 0.f, 0.f);
                    else if (i == 15 && engines_[0]->isValidSample(i))
                        SetSmtLedFloat(25 - i, .4f * pink[0], .4f * pink[1], .4f * pink[2]);
                    else if(engines_[0]->isValidSample(i) && fx_->getEngine() == CHROMATIC)
                        SetSmtLedFloat(25 - i, .4f * purple[0], .4f * purple[1], .4f * purple[2]);
                    else if (engines_[1]->isValidSample(i) && fx_->getEngine() == SLICE)
                        SetSmtLedFloat(25 - i, .4f * med_green[0], .4f * med_green[1], .4f * med_green[2]);
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
                else if (i == 15 && engines_[0]->isValidSample(i))
                        SetSmtLedFloat(25 - i, pink[0], pink[1], pink[2]);
                else if(engines_[0]->isValidSample(i) && fx_->getEngine() == CHROMATIC)
                        SetSmtLedFloat(25 - i, purple[0], purple[1], purple[2]);
                else if (engines_[1]->isValidSample(i) && fx_->getEngine() == SLICE)
                        SetSmtLedFloat(25 - i, med_green[0], med_green[1], med_green[2]);
                else
                    SetSmtLedFloat(25 - i, 0.f, 0.f, 0.f);
            }

            // banks
            SetSmtLedFloat(0, 0.f, 0.f, 0.f);
            SetSmtLedFloat(1, 0.f, 0.f, 0.f);
            int e = fx_->getEngine();

            if(!no_sd_card_) {
                SetSmtLedFloat(e, orange[0], orange[1], orange[2]);
            }

            // ========   send the data   =========
            fill_led_data();
        }


        bool OnEncoderTurned(uint16_t encoderID,
                             int16_t turns,
                             uint16_t stepsPerRevolution) override
        {
            uint8_t page = knob_page[encoderID];
            float inc = turns * kEncoderCoarseStep;

            if (encoderID == 0 && page == 0 && !quantized_pitch_) {
                inc = turns * kEncoderFineStep;
            }

            // we're receiving a knob position via CC
            if(stepsPerRevolution > 0)
                return false; // fall through to normalpage

            float r, g, b;
            if(preset_mode == PresetMode::NONE)
            {
                switch (encoderID) {
                    case 0:
                    {
                        if(page == 0) // stepped pitch
                        {
                            if (quantized_pitch_) {
                                enc_values[0][0] = engines_[fx_->getEngine()]->setGlobalPitchQuantized(turns, enc_values[0][0]);
                            }
                            else {
                                enc_values[0][0] += inc;
                                enc_values[0][0] = fclamp(enc_values[0][0], 0.f, 1.f);
                                engines_[fx_->getEngine()]->setGlobalPitchFree(enc_values[0][0]);
                            }
                        }
                        else if(page == 1) // pan
                        {
                            pan += inc;
                            pan = fclamp(pan, 0.f, 1.f);
                            engines_[fx_->getEngine()]->setPan(pan);
                        }
                        else { // sample rate reducer
                            reduce += inc;
                            reduce = fclamp(reduce, 0.f, 1.f);
                            engines_[fx_->getEngine()]->setSampleReducer(reduce);
                        }
                    }
                    break;
                    case 1:
                    {
                        if (page == 0) {
                            //move window
                            float pre_range = enc_values[0][2] - enc_values[0][1];
                            float range = fminf(pre_range, .01f) * turns;
                            enc_values[0][1] += range;
                            enc_values[0][2] += range;
                            enc_values[0][1] = fclamp(enc_values[0][1], 0.f, 1.f - pre_range);
                            enc_values[0][2] = fclamp(enc_values[0][2], pre_range, 1.f);
                            engines_[fx_->getEngine()]->setStartPoint(enc_values[0][1]);
                            engines_[fx_->getEngine()]->setEndPoint(enc_values[0][2]);
                        }
                        else {
                            loop += inc;
                            loop = fclamp(loop, 0.f, 1.f);
                            engines_[fx_->getEngine()]->setLoop(loop);
                        }
                    }
                    break;
                    case 2:
                    {
                        if (page == 0) {
                            //move window
                            window_encoder_counter += turns;
                            if (window_encoder_counter < -6 || window_encoder_counter > 6) {
                                window_encoder_counter = 0;

                                float start = enc_values[0][1];
                                float end = enc_values[0][2];
                                float window = end - start;
                                if (turns < 0) {
                                    enc_values[0][2] = start + window * 0.5f;
                                }
                                else {
                                    enc_values[0][2] = fclamp(start + window * 2.0f, start + kEncoderFineStep, 1.0f);
                                }
                                enc_values[0][2] = fclamp(enc_values[0][2], start + kEncoderFineStep, 1.f);
                                engines_[fx_->getEngine()]->setEndPoint(enc_values[0][2]);
                            }
                        }
                        else {
                            sustain += inc;
                            sustain = fclamp(sustain, 0.f, 1.f);
                            engines_[fx_->getEngine()]->setSustain(sustain);
                        }
                    }
                    break;
                    case 3:
                    {
                        if(page == 0) // Granular randomness
                        {
                            randomness += inc;
                            randomness = fclamp(randomness, 0.f, 1.f);
                            fx_->setGranularAlt(randomness);
                        }
                        else // Chorus
                        {
                            feedback += inc;
                            feedback = fclamp(feedback, 0.f, 1.f);
                            fx_->setGranularFeedback(feedback);
                        }
                    }
                    break;
                    case 4:
                    {
                        arp_randomness += inc;
                        arp_randomness = fclamp(arp_randomness, 0.f, 1.f);
                        arpSeq_->setRandomness(arp_randomness, fx_->getEngine());
                    }
                    break;
                    case 5:
                    {
                        final_comp += inc;
                        final_comp = fclamp(final_comp, 0.f, 1.f);
                        fx_->setFinalComp(final_comp);

                        input_toggled_ = false;
                    }

                }
                // move sample window (both start and end pos)
            }


            return true;
        }

        void DumpValuePresets(uint8_t slot)
        {
            size_t mode = fx_->getEngine();
            presets_->SetValue(enc_values[0][0], mode, slot, 0); // pitch
            presets_->SetValue(enc_values[1][0], mode, slot, 1); // sample volume
            presets_->SetValue(enc_values[2][0], mode, slot, 2); // filter
            presets_->SetValue(enc_values[0][1], mode, slot, 3); // sample start
            presets_->SetValue(enc_values[1][1], mode, slot, 4); // attack
            presets_->SetValue(enc_values[0][2], mode, slot, 5); // sample end
            presets_->SetValue(enc_values[1][2], mode, slot, 6); // release
            presets_->SetValue(pan, mode, slot, 7);              // pan
            presets_->SetValue(reduce, mode, slot, 8);          // sample rate reduction
            presets_->SetValue(loop, mode, slot, 9);            // loop
            presets_->SetValue(sustain, mode, slot, 10);         // sustain
        }

        void SetVoiceSlot(size_t slot)
        {
            size_t mode = fx_->getEngine();
            engines_[fx_->getEngine()]->setSample(slot);

            if(slot == 14)
            {
                enc_values[0][0] = presets_->getDefault(0); // pitch
                enc_values[1][0] = presets_->getDefault(1); // sample volume
                enc_values[2][0] = presets_->getDefault(2); // filter
                enc_values[0][1] = presets_->getDefault(3); // sample start
                enc_values[1][1] = presets_->getDefault(4); // attack
                enc_values[0][2] = presets_->getDefault(5); // sample end
                enc_values[1][2] = presets_->getDefault(6); // release
                pan = presets_->getDefault(7);              // pan
                reduce = presets_->getDefault(8);          // sample rate reduction
                loop = presets_->getDefault(9);            // loop
                sustain = presets_->getDefault(10);         // sustain
            }
            else
            {
                enc_values[0][0] = presets_->GetValue(mode, slot, 0); // pitch
                enc_values[1][0] = presets_->GetValue(mode, slot, 1); // sample volume
                enc_values[2][0] = presets_->GetValue(mode, slot, 2); // filter
                enc_values[0][1] = presets_->GetValue(mode, slot, 3); // sample start
                enc_values[1][1] = presets_->GetValue(mode, slot, 4); // attack
                enc_values[0][2] = presets_->GetValue(mode, slot, 5); // sample end
                enc_values[1][2] = presets_->GetValue(mode, slot, 6); // release
                pan = presets_->GetValue(mode, slot, 7);              // pan
                reduce = presets_->GetValue(mode, slot, 8);          // sample rate reduction
                loop = presets_->GetValue(mode, slot, 9);            // loop
                sustain = presets_->GetValue(mode, slot, 10);         // sustain
            }
            engines_[mode]->setGlobalPitchFree(enc_values[0][0]);
            engines_[mode]->setSampleVolume(enc_values[1][0]);
            engines_[mode]->setMasterCutoff(enc_values[2][0]);
            engines_[mode]->setStartPoint(enc_values[0][1]);
            engines_[mode]->setAttack(enc_values[1][1]);
            engines_[mode]->setEndPoint(enc_values[0][2]);
            engines_[mode]->setRelease(enc_values[1][2]);
            engines_[mode]->setPan(pan);
            engines_[mode]->setSampleReducer(reduce);
            engines_[mode]->setLoop(loop);
            engines_[mode]->setSustain(sustain);
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


            case static_cast<uint16_t>(Hardware::SwId::ENC_4_SW): // pitch knob
            {
                const int page = knob_page[0];
                pitch_reset = rising;

                if(rising)
                {
                    if(page == 0)
                    {
                        enc_values[0][0] = enc_defaults[0][0];
                        engines_[fx_->getEngine()]->setGlobalPitchFree(enc_values[0][0]);
                        engines_[fx_->getEngine()]->resetGlobalPitchQuant();
                        pre_quantized_amount = .5f;
                    }
                    else if(page == 1)
                    {
                        enc_values[1][0] = enc_defaults[1][0];
                        pan = .5f;
                        engines_[fx_->getEngine()]->setSampleVolume(enc_values[1][0]);
                        engines_[fx_->getEngine()]->setPan(pan);
                    }
                    else {
                        enc_values[2][0] = enc_defaults[2][0];
                        reduce = 0.f;
                        engines_[fx_->getEngine()]->setMasterCutoff(enc_values[2][0]);
                        engines_[fx_->getEngine()]->setSampleReducer(reduce);
                    }

                    //DumpValuePresets();
                    SetPthLedFloat(1, 1.f, 1.f, 1.f);
                }
            break;
            }

            // reset the looper pitch via fall through
            case ENC_5_SW: {
                if (rising) {
                    transport_pressed = true;
                    transport_timer = System::GetNow();
                }
                else {
                    transport_pressed = false;
                }
            }
            break;

            case static_cast<uint16_t>(Hardware::SwId::ENC_1_SW): // attack knob
            {
                if (knob_page[1] == 0) {
                    enc_values[0][1] = enc_defaults[0][1];
                    engines_[fx_->getEngine()]->setStartPoint(enc_values[0][1]);
                }
                else {
                    enc_values[1][1] = enc_defaults[1][1];
                    engines_[fx_->getEngine()]->setAttack(enc_values[1][1]);
                }
            }
            break;

            case static_cast<uint16_t>(Hardware::SwId::ENC_2_SW): // decay knob
            {
                if (knob_page[2] == 0) {
                    enc_values[0][2] = enc_defaults[0][2];
                    engines_[fx_->getEngine()]->setEndPoint(enc_values[0][2]);
                }
                else {
                    enc_values[1][2] = enc_defaults[1][2];
                    engines_[fx_->getEngine()]->setRelease(enc_values[1][2]);
                }
            }
            break;

            case static_cast<uint16_t>(Hardware::SwId::ENC_6_SW): // volume
                if(rising)
                {
                    input_toggled_ = true;
                    fx_->incrementMonitorMode();
                }
            break;

            case static_cast<uint16_t>(Hardware::SwId::ENC_3_SW): // magic wand
            {
                fx_reset = rising;
                if(rising)
                {
                    switch (knob_page[3]) {
                        case 0: {
                            enc_values[0][3] = enc_defaults[0][3];
                            fx_->setGranularMain(enc_values[0][3]);
                            randomness = 0.f;
                            fx_->setGranularAlt(randomness);
                        }
                        break;
                        case 1: {
                            enc_values[1][3] = enc_defaults[1][3];
                            fx_->setGranularMix(enc_values[1][3], fx_->getEngine());
                            feedback = .3f;
                            fx_->setGranularFeedback(feedback);
                        }
                        break;
                    }
                    fx_->setBufferFreeze(false);
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

                        blink_startt = System::GetNow();

                        preset_mode = PresetMode::SAVING;
                        DumpValuePresets(selected_slot);
                        presets_->Save(fx_->getEngine(), selected_slot);
                        engines_[fx_->getEngine()]->saveFileToSlot(static_cast<size_t>(selected_slot));
                        engines_[fx_->getEngine()]->setSample(static_cast<size_t>(selected_slot - 1));
                    }
                    else if(preset_mode == PresetMode::COPY_DEST)
                    {

                        blink_startt = System::GetNow();

                        preset_mode = PresetMode::COPYING;
                        DumpValuePresets(selected_slot);
                        presets_->Save(fx_->getEngine(), selected_slot);
                        if (copy_src != selected_slot) {
                            engines_[fx_->getEngine()]->copySlot(copy_src, selected_slot, cs_engine, 0);
                        }
                    }
                    else if(preset_mode == PresetMode::ERASE_SEL)
                    {
                        // if a voice is playing that slot, stop it

                        preset_mode = PresetMode::ERASING;
            
                        blink_startt = System::GetNow();

                        engines_[fx_->getEngine()]->deleteSlot(static_cast<size_t>(selected_slot));
                        engines_[fx_->getEngine()]->setSample(14);
                    }
                }
                return false;
                break;
            }

            case static_cast<uint16_t>(Hardware::SwId::KEY_27): { //Play
                if (rising) {
                    arpSeq_->incrementPattern();
                }
            }
            break;

            case static_cast<uint16_t>(Hardware::SwId::KEY_28): { //Loop
                if (rising) {
                    arpSeq_->incrementRestMode();
                }
            }
            break;

            case static_cast<uint16_t>(Hardware::SwId::KEY_16): // Jammi mode bank, fall through
            case static_cast<uint16_t>(Hardware::SwId::KEY_17): // Cubbi mode bank
            {                
                if(rising && !no_sd_card_)
                {
                    if(buttonID == static_cast<uint16_t>(Hardware::SwId::KEY_16)) { // This will happen repeatedly (bad)
                        fx_->setEngine(CHROMATIC);
                        saveState(1, state_->getState());
                        recallState(0, state_->getState(), false);
                        if (!arpSeq_->getPlay(SLICE) && !arpSeq_->getEngineSustain(1)) {
                            for (size_t i = 0; i < NUM_VOICES; ++i) {
                                float nn = engines_[SLICE]->chokeVoice(i);
                                if (nn != -99.f) {
                                    hw_->queueMidiNote(midi_channel[1], static_cast<int>(nn + 60), 127, NoteOff);
                                }
                            }
                        }
                        arpSeq_->setEngine(true);
                    }
                    else {
                        fx_->setEngine(SLICE);
                        saveState(0, state_->getState());
                        recallState(1, state_->getState(), false);
                        if (!arpSeq_->getPlay(CHROMATIC) && !arpSeq_->getEngineSustain(0)) {
                            for (size_t i = 0; i < NUM_VOICES; ++i) {
                                float nn = engines_[CHROMATIC]->chokeVoice(i);
                                if (nn != -99.f) {
                                    hw_->queueMidiNote(midi_channel[0], static_cast<int>(nn + 60), 127, NoteOff);
                                }
                            }
                        }
                        arpSeq_->setEngine(false);
                    }
                }
                else if(!rising)
                    return false; // note off falls through

            }
            break;

            case static_cast<uint16_t>(Hardware::SwId::KEY_18): // mic in, fall through
            case static_cast<uint16_t>(Hardware::SwId::KEY_19): // aux in, fall through
            case static_cast<uint16_t>(Hardware::SwId::KEY_20): // resample
            {
                if(rising)
                {
                    InputSource source;
                    if(buttonID == static_cast<uint16_t>(Hardware::SwId::KEY_18))
                        source = InputSource::MIC;
                    else if(buttonID == static_cast<uint16_t>(Hardware::SwId::KEY_19))
                        source = InputSource::LINE_IN;
                    else
                        source = InputSource::RESAMPLE;
                    fx_->SetInputSource(source);
                }
                else if(!rising)
                    return false; // note off falls through

                break;
            }


            case static_cast<uint16_t>(Hardware::SwId::KEY_21): // A State
            {
                if (rising) {
                    if (state_->getState()!= StateSaver::State::A) {
                        arpSeq_->setTempNote();
                        saveState(fx_->getEngine(), StateSaver::State::B);
                        size_t dst = ((fx_->getEngine() << 1) | state_->getState()) ^ 0b10;
                        state_->setPlay(dst, arpSeq_->getPlay(fx_->getEngine() ^ 1));
                        recallState(fx_->getEngine(), StateSaver::State::A, true);
                        state_->setState(true);
                        arpSeq_->checkTempNote();
                    }
                    else {
                        copy_time = System::GetNow();
                        copy_pressed = true;
                    }
                }
                else {
                    copy_pressed = false;
                }
                break;
            }
            case static_cast<uint16_t>(Hardware::SwId::KEY_22): // B State
            {
                if(rising)
                {
                    if (state_->getState() != StateSaver::State::B) {
                        arpSeq_->setTempNote();
                        saveState(fx_->getEngine(), StateSaver::State::A);
                        size_t dst = ((fx_->getEngine() << 1) | state_->getState()) ^ 0b10;
                        state_->setPlay(dst, arpSeq_->getPlay(fx_->getEngine() ^ 1));
                        recallState(fx_->getEngine(), StateSaver::State::B, true);
                        state_->setState(false);
                        arpSeq_->checkTempNote();
                    }
                    else {
                        copy_time = System::GetNow();
                        copy_pressed = true;
                    }
                }
                else {
                    copy_pressed = false;
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
                    cs_engine = kSlotNone;

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
                    else if(preset_mode == PresetMode::COPY_SRC && engines_[fx_->getEngine()]->isValidSample(slot_req))
                    {
                        copy_src = slot_req;
                        cs_engine = fx_->getEngine();
                        preset_mode = PresetMode::COPY_DEST;
                    }
                    else if(preset_mode == PresetMode::COPY_DEST)
                    {
                        selected_slot = slot_req;
                    }
                    else if(preset_mode == PresetMode::NONE && engines_[fx_->getEngine()]->isValidSample(slot_req)
                            && !no_sd_card_)
                    {
                        selected_slot = slot_req;
                        SetVoiceSlot(selected_slot - 1);
                    }
                    else if(preset_mode == PresetMode::ERASE_SEL && slot_req != 15 && 
                            engines_[fx_->getEngine()]->isValidSample(slot_req))
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
            fx_reset = false;
            pitch_reset = false;
            chompi_key_pressed = true;

            input_toggled_ = false;

            copy_src = kSlotNone;
            preset_mode = PresetMode::NONE;
        }

        inline void SetSwitchState(bool state) { switch_state = state; }

        bool IsClosable()
        { 
            if(
                System::GetNow() - blink_startt > 1000                              // animation done AND
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

        void InitStatesFromDefault() {
            SavedState default_state;
            for (size_t page = 0; page < 2; ++page) {
                for (size_t enc = 0; enc < 4; ++enc) {
                    default_state.enc_values[page][enc] = enc_values[page][enc];
                }
            }
            default_state.filter = enc_values[2][0];
            default_state.pan = pan;
            default_state.redux = reduce;
            default_state.loop = true;
            default_state.sustain = true;
            default_state.randomness = randomness;
            default_state.feedback = feedback;
            default_state.out_gain = enc_values[0][5];
            default_state.comp = final_comp;
            default_state.in_gain = enc_values[1][5];
            default_state.sample_slot = 15;
            default_state.monitor_mode = fx_->getMonitorMode();
            default_state.pattern = 0;
            default_state.rest_pattern = 0;
            default_state.clock_div = clock_manager_->getClockDivPos(0);
            default_state.play = false;
            default_state.latch_state = 0;
            default_state.arp_randomness = arp_randomness;
            default_state.sequence = {};
            for (size_t i = 0; i < StateSaver::StateSlot::LAST; ++i) {
                state_->Save(i, &default_state, nullptr);
            }
        }

        void saveState(size_t eng, StateSaver::State st) __attribute__((optimize("-O0"))) {
            SavedState myState;
            for (size_t page = 0; page < 2; ++page) {
                for (size_t enc = 0; enc < 4; ++enc) {
                    myState.enc_values[page][enc] = enc_values[page][enc];
                }
            }
            myState.filter = enc_values[2][0];
            myState.pan = pan;
            myState.redux = reduce;
            myState.loop = loop;
            myState.sustain = sustain;
            myState.randomness = randomness;
            myState.feedback = feedback;
            myState.out_gain = enc_values[0][5];
            myState.comp = final_comp;
            myState.in_gain = enc_values[1][5];
            myState.sample_slot = engines_[eng]->getVoiceSlot();
            myState.monitor_mode = fx_->getMonitorMode();
            myState.pattern = arpSeq_->getPattern();
            myState.rest_pattern = arpSeq_->getRestMode();
            myState.clock_div = clock_manager_->getClockDivPos(eng);
            myState.play = arpSeq_->getPlay(eng);
            myState.latch_state = arpSeq_->getLatchType(eng);
            myState.arp_randomness = arp_randomness;
            std::vector<NoteInfo> *seq = arpSeq_->getSequence(eng);
            size_t slot = eng * 2 + static_cast<size_t>(st);
            state_->Save(slot, &myState, seq);
        }

        void recallState(size_t eng, StateSaver::State st, bool apply_params) {
            size_t slot = eng * 2 + static_cast<size_t>(st);
            SavedState *foregroundState = state_->Recall(slot);
            for (size_t page = 0; page < 2; ++page) {
                for (size_t enc = 0; enc < 4; ++enc) {
                    enc_values[page][enc] = foregroundState->enc_values[page][enc];
                }
            }
            enc_values[2][0] = foregroundState->filter;
            reduce = foregroundState->redux;
            pan = foregroundState->pan;
            loop = foregroundState->loop;
            sustain = foregroundState->sustain;
            randomness = foregroundState->randomness;
            feedback = foregroundState->feedback;
            enc_values[0][5] = foregroundState->out_gain;
            final_comp = foregroundState->comp;
            enc_values[1][5] = foregroundState->in_gain;
            arp_randomness = foregroundState->arp_randomness;
            // Maybe a check if anything has actually changed?
            if (apply_params) {
                SavedState *backgroundState = state_->Recall(slot > 1 ? slot - 2 : slot + 2);
                size_t fg_idx = eng;
                size_t bg_idx = eng ^ 1;
                applyEngineParams(fg_idx, foregroundState);
                applyEngineParams(bg_idx, backgroundState);
                fx_->setGranularMain(enc_values[0][3]);
                fx_->setGranularMix(enc_values[1][3], fg_idx);
                fx_->setGranularAlt(randomness);
                fx_->setGranularFeedback(feedback);
                fx_->setGain(enc_values[0][5]);
                fx_->setFinalComp(final_comp);
                fx_->setInputGain(enc_values[1][5]);
                fx_->setMonitorMode(foregroundState->monitor_mode);
                clock_manager_->changeTempo(0);
            }
        }

        void applyEngineParams(size_t eng, SavedState *state) {
            engines_[eng]->setGlobalPitchFree(state->enc_values[0][0]);
            engines_[eng]->setSample(state->sample_slot - 1);
            engines_[eng]->setAttack(state->enc_values[1][1]);
            engines_[eng]->setRelease(state->enc_values[1][2]);
            engines_[eng]->setStartPoint(state->enc_values[0][1]);
            engines_[eng]->setEndPoint(state->enc_values[0][2]);
            engines_[eng]->setMasterCutoff(state->filter);
            engines_[eng]->setPan(state->pan);
            engines_[eng]->setSampleReducer(state->redux);
            engines_[eng]->setSampleVolume(state->enc_values[1][0]);
            engines_[eng]->setLoop(state->loop);
            engines_[eng]->setSustain(state->sustain);
            arpSeq_->setSequence(eng, &state->sequence);
            arpSeq_->setPattern(eng, state->pattern);
            arpSeq_->setRestMode(eng, state->rest_pattern);
            arpSeq_->setPlayDirect(eng, state->play);
            arpSeq_->setLatchDirect(eng, state->latch_state);
            arpSeq_->setRandomness(state->arp_randomness, eng);
            clock_manager_->setDiv(eng, state->clock_div);
        }

        void resetAfterRecording() {
            pan = .5f;
            reduce = 0.f;
            engines_[fx_->getEngine()]->setPan(pan);
            engines_[fx_->getEngine()]->setSampleReducer(reduce);
        }

        bool no_sd_card_ = false;
        inline void NoSDCard() { no_sd_card_ = true; }

    private:
        clockManager *clock_manager_;
        BaseEngine **engines_;
        fxEngine *fx_;
        Hardware *hw_;
        ArpeggiatorSequencer *arpSeq_;
        PresetManager* presets_;
        StateSaver *state_;
        float** enc_values;
        const float** enc_defaults;
        uint8_t* knob_page;

        bool quantized_pitch_;

        bool input_toggled_;

        const float* key_color;

        bool transport_pressed = false;
        uint32_t transport_timer, transport_blink, transport_blink_timer;
        bool transport_blink_state, transport_blinking;

        bool chompi_key_pressed = false;
        bool blink_state = true;
        uint32_t blink_startt;
        uint32_t last_blink;

        bool copy_pressed, copy_blinking, copy_blink_state;
        uint32_t copy_time, copy_blink_time, last_copy_blink;

        int32_t window_encoder_counter;

        uint8_t midi_channel[kNumEngines] = {0, 0};

        float feedback;
        float randomness;
        float atk, rel;
        float pan;
        float loop, sustain;
        float pre_quantized_amount;
        float final_comp;
        float wavetable_idx;
        float reduce;
        float arp_randomness;

        bool fx_reset = false;
        bool pitch_reset = false;

        uint8_t selected_slot = kSlotNone;
        uint8_t copy_src = kSlotNone;
        uint8_t cs_engine = kSlotNone;
        bool switch_state;

        PresetMode preset_mode = PresetMode::NONE;
    };
} // namespace chompi
