#include "hardware.h"
#include "temp_led_stuff.h"

namespace chompi
{
    class NoSDPage : public daisy::UiPage
    {
    public:
        void Init() {}

        void Draw(const daisy::UiCanvasDescriptor &canvasDescriptor) override
        {
            uint32_t now = System::GetNow();
            
            if(now - last_blink_time > 400)
            {
                last_blink_time = now;
                blink = !blink;

                for(int i = 0; i < kNumPthLeds; i++)
                {
                    SetPthLedFloat(i, blink * 1.f, 0.f, 0.f);
                }

                for(int i = 0; i < kNumSmtLeds; i++)
                {
                    SetSmtLedFloat(i, blink * 1.f, 0.f, 0.f);
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

    private:
        uint32_t last_blink_time;
        bool blink;
    };
} // namespace chompi
