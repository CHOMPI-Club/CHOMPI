#include "encoder.h"

using namespace chompi;

void ChompiEncoder::Init(dsy_gpio_pin a,
                   dsy_gpio_pin b,
                   dsy_gpio_pin click,
                   float        update_rate)
{
    last_update_ = daisy::System::GetNow();
    updated_     = false;

    // Init GPIO for A, and B
    hw_a_.pin  = a;
    hw_a_.mode = DSY_GPIO_MODE_INPUT;
    hw_a_.pull = DSY_GPIO_PULLUP;
    hw_b_.pin  = b;
    hw_b_.mode = DSY_GPIO_MODE_INPUT;
    hw_b_.pull = DSY_GPIO_PULLUP;
    dsy_gpio_init(&hw_a_);
    dsy_gpio_init(&hw_b_);
    // Default Initialization for Switch
    sw_.Init(click);
    // Set initial states, etc.
    inc_ = 0;
    a_ = b_ = 0xff;
}

// Bitwise shifts in new reading of pins so that pin needs to consistently
// read one way or the other for a few samples before being treated as a state change
// this rejects noise/switch bouncing.
void ChompiEncoder::Debounce()
{
    // update no faster than 1kHz
    uint32_t now = daisy::System::GetNow();
    updated_     = false;

    if(now - last_update_ >= 1)
    {
        last_update_ = now;
        updated_     = true;

        // Shift Button states to debounce
        a_ = (a_ << 1) | dsy_gpio_read(&hw_a_);
        b_ = (b_ << 1) | dsy_gpio_read(&hw_b_);

        // infer increment direction
        inc_ = 0; // reset inc_ first
        if((a_ & 0x03) == 0x02 && (b_ & 0x03) == 0x00)
        {
            inc_ = 1;
        }
        else if((b_ & 0x03) == 0x02 && (a_ & 0x03) == 0x00)
        {
            inc_ = -1;
        }
    }

    // Debounce built-in switch
    sw_.Debounce();
}

void ChompiEncoder::Debounce(bool a_state, bool b_state)
{
    
    // update no faster than 1kHz
    uint32_t now = daisy::System::GetNow();
    updated_     = false;

    if(now - last_update_ >= 1)
    {
        last_update_ = now;
        updated_     = true;

        // Shift Button states to debounce
        a_ = (a_ << 1) | a_state;
        b_ = (b_ << 1) | b_state;

        // infer increment direction
        inc_ = 0; // reset inc_ first
        if((a_ & 0x07) == 0x04 && (b_ & 0x03) == 0x00)
        {
            inc_ = 1;
        }
        else if((b_ & 0x07) == 0x04 && (a_ & 0x03) == 0x00)
        {
            inc_ = -1;
        }
    }

    // Debounce built-in switch
    // sw_.Debounce();
}
