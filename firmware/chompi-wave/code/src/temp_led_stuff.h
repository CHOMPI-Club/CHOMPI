/** This is a temporary kludge of stuff necessary to run the
 *  LEDs without the ideal API in libDaisy yet.
 *
 *  CHOMPI's two LED chains (PTH is the through-hole panel LEDs, SMT is
 *  the 25 keybed LEDs, both WS2812-style addressable RGB) are driven by timer
 *  PWM+DMA. Each color bit becomes one PWM pulse whose ON-duration
 *  encodes a 1 or 0 (kOneTime/kZeroTime), in timer ticks. Each chain
 *  needs a few LED-times of "porch" (kPorchSize) at each end sending zero pulses.
 *  The setup creates the correct timing and format for the data signal using the libDaisy timers.
 *  The buffers are set per LED throughout the UI pages Draw() function then formatted
 *  and put in the DMA buffers in fill_led_data() which is called at the end of Draw()
 */
#pragma once
#include "daisy_seed.h"

namespace chompi
{
    /** Global timer refs. */
    daisy::TimerHandle tim3_smt, tim5_pth;
    daisy::TimChannel ledPthPwm, ledSmtPwm;
    daisy::TimerHandle::Config tim3_cfg;
    daisy::TimerHandle::Config tim5_cfg;
    daisy::TimChannel::Config t3chn2_cfg;
    daisy::TimChannel::Config t5chn4_cfg;

    /** now a bunch of consts for the strings of LEDs */
    const int kPorchSize = 6;
    const int kNumPthLeds = 10 + 2 * kPorchSize;
    const int kNumSmtLeds = 25 + 2 * kPorchSize;

    const size_t kOutPthDataSize = kNumPthLeds * 3 * 8;
    const size_t kOutSmtDataSize = kNumSmtLeds * 3 * 8;

    uint8_t led_pth_data[kNumPthLeds][3]; /**< RGB data */
    uint8_t led_smt_data[kNumSmtLeds][3]; /**< RGB data */

    uint32_t DMA_BUFFER_MEM_SECTION
        output_pth_data[kNumPthLeds * 3 * 8]; /**< PWM lengths data, one "duration" per bit */
    uint32_t DMA_BUFFER_MEM_SECTION
        output_smt_data[kNumSmtLeds * 3 * 8]; /**< PWM lengths data, one "duration" per bit */

    /** actually measuring on chompi pre-rework */
    // const int kOneTime = 31;
    // const int kZeroTime = 13;
    // const int kOneTime = 28;
    // const int kZeroTime = 8;

    /** Tweaked for Rev2 hardware */
    const int kOneTime = 20; /**< measured 0.68us */
    const int kZeroTime = 10; /**< measured 0.334us */
    const int kLedResetTime = 1; // reset time in ms, actually ends up being 2x this

    /** map pth led index to chain index */


    void EndOfLeds(void* context);

    /** @brief setup LEDs */
    void LedSetup()
    {
        // zero out the buffers
        std::fill(led_pth_data[0], led_pth_data[0] + kNumPthLeds * 3, 0);
        std::fill(led_smt_data[0], led_smt_data[0] + kNumSmtLeds * 3, 0);

        std::fill(output_pth_data, output_pth_data + kNumPthLeds * 3 * 8, 0);
        std::fill(output_smt_data, output_smt_data + kNumSmtLeds * 3 * 8, 0);

        /** Config */
        tim3_cfg.periph = daisy::TimerHandle::Config::Peripheral::TIM_3;
        tim5_cfg.periph = daisy::TimerHandle::Config::Peripheral::TIM_5;
        tim3_cfg.dir = daisy::TimerHandle::Config::CounterDir::UP;
        tim5_cfg.dir = daisy::TimerHandle::Config::CounterDir::UP;

        /** Init */
        tim3_smt.Init(tim3_cfg);
        tim5_pth.Init(tim5_cfg);

        /** Generate period for timer
         *  This is a marvelously useful little tidbit that should be put into a TimerHandle function or something.
         */
        uint32_t prescaler = 8;
        uint32_t tickspeed = (daisy::System::GetPClk2Freq() * 2) / prescaler;
        uint32_t target_pulse_freq = 833332; /**< 1.2 microsecond symbol length */
        uint32_t period = (tickspeed / target_pulse_freq) - 1;
        tim3_smt.SetPrescaler(prescaler - 1); /**< ps=0 is divide by 1 and so on.*/
        tim3_smt.SetPeriod(period);
        tim5_pth.SetPrescaler(prescaler - 1); /**< ps=0 is divide by 1 and so on.*/
        tim5_pth.SetPeriod(period);

        /** TIM3 Ch1 is for another set:
         *  All keyboard LEDs (looks like K1 - K28 in sequence)
         */
        t3chn2_cfg.tim = &tim3_smt;
        t3chn2_cfg.chn = daisy::TimChannel::Config::Channel::TWO;
        t3chn2_cfg.mode = daisy::TimChannel::Config::Mode::PWM;
        t3chn2_cfg.polarity = daisy::TimChannel::Config::Polarity::HIGH;
        t3chn2_cfg.pin = daisy::seed::D18;

        /** TIM5_CH4 is for another
         *  For the 10 PTH leds
         */
        t5chn4_cfg.tim = &tim5_pth;
        t5chn4_cfg.chn = daisy::TimChannel::Config::Channel::FOUR;
        t5chn4_cfg.mode = daisy::TimChannel::Config::Mode::PWM;
        t5chn4_cfg.polarity = daisy::TimChannel::Config::Polarity::HIGH;
        t5chn4_cfg.pin = daisy::seed::D16;

        /** and finally initialize */

        ledPthPwm.Init(t5chn4_cfg);
        ledSmtPwm.Init(t3chn2_cfg);

        ledSmtPwm.Start();
        ledSmtPwm.StartDma(output_smt_data, kOutSmtDataSize, EndOfLeds, (void *)&ledSmtPwm);
        // ledPthPwm.Start();
    }

    /** buff expects that 8 elements are available for the one 8-bit color val*/
    void populate_bits(uint8_t color_val, uint32_t *buff)
    {
        for (int i = 0; i < 8; i++)
        {
            buff[i] = (color_val & (1 << (7 - i))) > 0 ? kOneTime : kZeroTime;
        }
    }

    void populate_off(uint32_t *buff)
    {
        for (int i = 0; i < 8; i++)
        {
            buff[i + 8 * 0] = 0;
            buff[i + 8 * 1] = 0;
            buff[i + 8 * 2] = 0;
        }
    }

    void fill_led_data()
    {
        /** TODO fix these to be accurate for necessary timing */
        for (int i = 0; i < kNumPthLeds; i++)
        {
            auto data_index = i * 3 * 8;

            if(i < kPorchSize || i >= (kNumPthLeds - kPorchSize))
            {
                populate_off(&output_pth_data[data_index]);
            }
            else
            {
                uint8_t g = led_pth_data[i - kPorchSize][1];
                uint8_t r = led_pth_data[i - kPorchSize][0];
                uint8_t b = led_pth_data[i - kPorchSize][2];

                populate_bits(r, &output_pth_data[data_index]);
                populate_bits(g, &output_pth_data[data_index + 8]);
                populate_bits(b, &output_pth_data[data_index + 16]);
            }
        }
        for (int i = 0; i < kNumSmtLeds; i++)
        {
            auto data_index = i * 3 * 8;

            if(i < kPorchSize || i >= (kNumSmtLeds - kPorchSize))
            {
                populate_off(&output_smt_data[data_index]);
            }
            else
            {
                uint8_t g = led_smt_data[i - kPorchSize][1];
                uint8_t r = led_smt_data[i - kPorchSize][0];
                uint8_t b = led_smt_data[i - kPorchSize][2];

                populate_bits(g, &output_smt_data[data_index]);
                populate_bits(r, &output_smt_data[data_index + 8]);
                populate_bits(b, &output_smt_data[data_index + 16]);
            }
        }
    }

   void SetPthLed(int index, uint8_t r, uint8_t g, uint8_t b)
    {
        led_pth_data[index][0] = r / 11;
        led_pth_data[index][1] = g / 11;
        led_pth_data[index][2] = b / 11;
    }
    void SetSmtLed(int index, uint8_t r, uint8_t g, uint8_t b)
    {
        led_smt_data[index][0] = r / 4;
        led_smt_data[index][1] = g / 4;
        led_smt_data[index][2] = b / 4;
    }

    void SetPthLedFloat(int index, float r, float g, float b)
    {
        SetPthLed(index, r * 255, g * 255, b * 255);
    }
    void SetSmtLedFloat(int index, float r, float g, float b)
    {
        SetSmtLed(index, r * 255, g * 255, b * 255);
    }

    void EndOfLeds(void *context)
    {
        daisy::TimChannel *pwm = (daisy::TimChannel *)context;

        // restart from the top
        if(pwm->GetConfig().chn == daisy::TimChannel::Config::Channel::FOUR) // PTH stop, start SMT
        {
            pwm->SetPwm(0);
            ledSmtPwm.Start();
            ledSmtPwm.StartDma(output_smt_data, kOutSmtDataSize, EndOfLeds, (void *)&ledSmtPwm);
        }
        else // channel TWO
        {
            pwm->SetPwm(0);
            ledPthPwm.Start();
            ledPthPwm.StartDma(output_pth_data, kOutPthDataSize, EndOfLeds, (void *)&ledPthPwm);
        }

        // pwm->SetPwm(0);
        // pwm->Stop();
    }

    // ======== helper functions for color crossfading ========
    // TODO: condense this to one RGB thing with the daisy::Color rather than 3 calls
    float color_xfade(float start, float end, float idx)
    {
        return (1.f - idx) * start + idx * end;
    }

    float color_triple_xfade(float start, float mid, float end, float idx)
    {
        if(idx < .5f)
        {
            idx *= 2.f;
            return color_xfade(start, mid, idx);
        }
        else
        {
            idx = (idx - .5f) * 2.f;
            return color_xfade(mid, end, idx);
        }
    }

    float color_quad_xfade(float start, float mid1, float mid2, float end, float idx)
    {
        if(idx < .33f)
        {
            idx *= 3.f;
            return color_xfade(start, mid1, idx);
        }
        else if(idx < .66f)
        {
            idx = (idx - .33f) * 3.f;
            return color_xfade(mid1, mid2, idx);
        }
        else
        {
            idx = (idx - .66f) * 3.f;
            return color_xfade(mid2, end, idx);
        }
    }
} // namespace chompi