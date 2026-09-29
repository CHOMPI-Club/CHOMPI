/** @file RainbowWavePage.h
 *  @brief One-time rainbow LED intro animation UiPage at startup. Draws
 *  a rainbow on the UI.
 */
#include "hardware.h"
#include "temp_led_stuff.h"

namespace chompi
{
    class RainbowPage : public daisy::UiPage
    {
    public:
        void Init() {}

        // roygbivr (roll over at end for programming ease)
        const int reds[8] = {255, 255, 255, 0, 0, 75, 238, 255};
        const int greens[8] = {0, 146, 255, 255, 0, 0, 130, 0};
        const int blues[8] = {0, 0, 0, 0, 255, 130, 238, 0};

        const float kPthStep = 6.f / kNumPthLeds;
        const float kSmtStepBlack = 6.f / 10;
        const float kSmtStepWhite = 6.f / 15;

        float idx = 0.f;
        const float inc = .1f;

        float gain = 0.f;

        uint32_t startt;
        bool down = false;

        float fade = 1.f;
        void Draw(const daisy::UiCanvasDescriptor &canvasDescriptor) override
        {
            uint32_t now = System::GetNow();
            
            if(now - last_blink_time > 1)
            {
                last_blink_time = now;

                fade -= .01f;
                if(fade > 0.f)
                {
                    for(size_t i = 0; i < kNumPthLeds; i++)
                    {
                        led_pth_data[i][0] = led_pth_data[i][0] * fade;
                        led_pth_data[i][1] = led_pth_data[i][1] * fade;
                        led_pth_data[i][2] = led_pth_data[i][2] * fade;
                    }

                    for(size_t i = 0; i < kNumSmtLeds; i++)
                    {
                        led_smt_data[i][0] = led_smt_data[i][0] * fade;
                        led_smt_data[i][1] = led_smt_data[i][1] * fade;
                        led_smt_data[i][2] = led_smt_data[i][2] * fade;
                    }
                }

                else
                {
                    if(startt == 0)
                        startt = now;

                    if(now - startt > 1500)
                    {
                        down = true;
                        startt = now;
                    }

                    idx += inc;
                    if(idx >= 7.f)
                        idx -= 7.f;

                    if(!down)
                        gain = gain < 1.f ? gain + .02f : gain;
                    else
                        gain = gain > 0.f ? gain - .02f : gain;

                    float fidx = idx + 1.5f;
                    if(fidx >= 7.f)
                        fidx -= 7.f;

                    for(int i = 0; i < kNumPthLeds; i++)
                    {
                        const size_t floor = fidx;
                        const size_t ceil = floor + 1;
                        const float frac = fidx - int(fidx);

                        float fgain = daisysp::fclamp((gain * 10.f) - (9.f - i), 0.f, 1.f);
                        if(down)
                            fgain = daisysp::fclamp((gain * 10.f) - i, 0.f, 1.f);
                        fgain *= fgain;

                        uint8_t r = frac * (reds[ceil] - reds[floor]) + reds[floor];
                        uint8_t g = frac * (greens[ceil] - greens[floor]) + greens[floor];
                        uint8_t b = frac * (blues[ceil] - blues[floor]) + blues[floor];

                        fidx += kPthStep;
                        if(fidx >= 7.f)
                            fidx -= 7.f;

                        SetPthLed(i, fgain * r, fgain * g, fgain * b);
                    }

                    fidx = idx;
                    for(int i = 0; i < 10; i++)
                    {
                        const size_t floor = fidx;
                        const size_t ceil = floor + 1;
                        const float frac = fidx - int(fidx);

                        float fgain = daisysp::fclamp((gain * 10.f) - (9.f - i), 0.f, 1.f);
                        if(down)
                            fgain = daisysp::fclamp((gain * 10.f) - i, 0.f, 1.f);
                        fgain *= fgain;

                        uint8_t r = frac * (reds[ceil] - reds[floor]) + reds[floor];
                        uint8_t g = frac * (greens[ceil] - greens[floor]) + greens[floor];
                        uint8_t b = frac * (blues[ceil] - blues[floor]) + blues[floor];

                        fidx += kSmtStepBlack;
                        if(fidx >= 7.f)
                            fidx -= 7.f;

                        SetSmtLed(i, fgain * r, fgain * g, fgain * b);
                    }

                    fidx = idx;
                    for(int i = 0; i < 15; i++)
                    {
                        const size_t floor = fidx;
                        const size_t ceil = floor + 1;
                        const float frac = fidx - int(fidx);

                        float fgain = daisysp::fclamp((gain * 15.f) - (14.f - i), 0.f, 1.f);
                        if(down)
                            fgain = daisysp::fclamp((gain * 15.f) - i, 0.f, 1.f);
                        fgain *= fgain;

                        uint8_t r = frac * (reds[ceil] - reds[floor]) + reds[floor];
                        uint8_t g = frac * (greens[ceil] - greens[floor]) + greens[floor];
                        uint8_t b = frac * (blues[ceil] - blues[floor]) + blues[floor];

                        fidx += kSmtStepWhite;
                        if(fidx >= 7.f)
                            fidx -= 7.f;

                        SetSmtLed(24 - i, fgain * r, fgain * g, fgain * b);
                    }
                }
            }

            // ========   send the data   =========
            fill_led_data();
        }

        bool OnEncoderTurned(uint16_t encoderID,
                             int16_t turns,
                             uint16_t stepsPerRevolution) override
        {
            return true; // do nothing
        }

        bool OnButton(uint16_t buttonID,
                uint8_t numberOfPresses,
                bool isRetriggering) override
        {
            return true; // do nothing
        }

        void OnFocusGained() override {}

        bool IsClosable() { return down && System::GetNow() - startt > 1000; }

    private:
        uint32_t last_blink_time;
        bool blink;
    };
} // namespace chompi
