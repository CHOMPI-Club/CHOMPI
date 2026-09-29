#include "hardware.h"
#include "temp_led_stuff.h"

namespace chompi
{
    class BootPage : public daisy::UiPage
    {
    public:

        const int reds[8] = {255, 255, 255, 0, 0, 75, 238, 255};
        const int greens[8] = {0, 146, 255, 255, 0, 0, 130, 0};
        const int blues[8] = {0, 0, 0, 0, 255, 130, 238, 0};

        float idx = 0.f;
        const float inc = .1f;
        float gain = 0.f;

        uint32_t startt;
        bool down = false;

        const float kPthStep = 6.f / 22.f;

        void Init(Hardware* hw, sampleManager *sm)
        {
            hw_ = hw;
            sm_ = sm;
            RandomColors();
            expected_samples_ = sm_->getNumSamples();
        }

        void RandomColors()
        {
            r = System::GetNow() % 66;
            g = System::GetNow() % 53;
            b = System::GetNow() % 36;

            r = r / 66.f;
            g = g / 53.f;
            b = b / 36.f;
        }

        void Draw(const daisy::UiCanvasDescriptor &canvasDescriptor) override
        {

            uint32_t now = System::GetNow();

            if(startt == 0) {
                startt = now;
            }

            if(now - startt > 1500) {
                down = true;
                startt = now;
            }

            idx += inc;
            if(idx >= 7.f) {
                idx -= 7.f;
            }

            if(!down) {
                gain = gain < 1.f ? gain + .02f : gain;
            }
            else {
                gain = gain > 0.f ? gain - .02f : gain;
            }

            float fidx = idx + 1.5f;
            if(fidx >= 7.f) {
                fidx -= 7.f;
            }

            const size_t floor = fidx;
            const size_t ceil = floor + 1;
            const float frac = fidx - int(fidx);

            float fgain = daisysp::fclamp((gain * 10.f) - (9.f), 0.f, 1.f);

            if(down) {
                fgain = daisysp::fclamp((gain * 10.f), 0.f, 1.f);
            }
            fgain *= fgain;

            uint8_t r = frac * (reds[ceil] - reds[floor]) + reds[floor];
            uint8_t g = frac * (greens[ceil] - greens[floor]) + greens[floor];
            uint8_t b = frac * (blues[ceil] - blues[floor]) + blues[floor];

            fidx += kPthStep;

            if(fidx >= 7.f) {
                fidx -= 7.f;
            }

            for(size_t i = 1; i < kNumPthLeds; i++)
            {
                SetPthLedFloat(i, 0.f, 0.f, 0.f);
            }
            SetPthLed(0, fgain * r, fgain * g, fgain * b);

            for(size_t i = 0; i < 15; i++)
            {
                SampleStatus status = sm_->checkSlotForLEDS(i);
                if (status == SampleStatus::LOADED) {
                    SetSmtLedFloat(24 - i, 1.f, 1.f, 1.f);
                }
                else if (status == SampleStatus::FAILED) {
                    SetSmtLedFloat(24 - i, 1.f, 0.f, 0.f);
                }
                else {
                    SetSmtLedFloat(24 - i, 0.f, 0.f, 0.f);
                }
                
            }

            // ========   send the data   =========
            fill_led_data();
        }

        bool OnEncoderTurned(uint16_t encoderID,
                             int16_t turns,
                             uint16_t stepsPerRevolution) override
        {
            return false; // do nothing
        }

        bool OnButton(uint16_t buttonID,
                uint8_t numberOfPresses,
                bool isRetriggering) override
        {
            return false; // do nothing
        }

    private:
        Hardware* hw_;
        sampleManager *sm_;
        float r = 0.f;
        float g = 0.f;
        float b = 0.f;
        float bright = 0.f;
        float bright_inc = .01f;
        uint8_t expected_samples_;
    };
} // namespace chompi
