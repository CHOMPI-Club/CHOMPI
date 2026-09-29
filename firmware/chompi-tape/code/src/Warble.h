#pragma once

using namespace daisysp;

namespace chompi {

class Warble
{
  public:
    Warble() {}

    ~Warble() {}

    uint32_t vinyl_rand()
    {
        static uint32_t randval = 1;
        uint32_t        val     = (1103515245 * randval + 12345) % 2147483648;
        randval                 = val;
        return val;
    }

    void Init(float samplerate)
    {
        del_l_.Init();
        del_l_.SetDelay(10u);
        del_r_.Init();
        del_r_.SetDelay(10u);
        freq_ctrl_ = 0.f;
        mix_ = mix_target_ = 0.f;
    }
    void
    Process(float in_left, float in_right, float *out_left, float *out_right)
    {
        const float dv2_31 = 4.656612873077392578125e-10;
        const float onedsr = 2.082725e-5;
        if((float)vinyl_rand() * dv2_31 < onedsr * freq_ctrl_)
        {
            lstart_ = lend_;
            lend_   = 100.f + ((vinyl_rand() * dv2_31) * 880.f);
            coeff_  = (vinyl_rand() * dv2_31) * 0.0001f;
        }
        fonepole(l_, lend_, coeff_);
        del_l_.Write(in_left);
        del_r_.Write(in_right);
        del_l_.SetDelay(l_);
        del_r_.SetDelay(l_);

        daisysp::fonepole(xfade, xfade_target, .001f);
        daisysp::fonepole(mix_, mix_target_, .001f);
        *out_left  = mix_ * (del_l_.Read() - in_left)  + in_left;
        *out_right = mix_ * (del_r_.Read() - in_right) + in_right;
    }

    /** adjusts how often warbles happen. Should be above 0 */
    void SetFreq(float val) 
    {
        mix_target_ = val;
        freq_ctrl_= val * 30.f + .1f;
    }

  private:
    static constexpr float kDefaultFreqScalar = 1.75f;
    DelayLine<float, 1024> del_l_, del_r_;
    uint8_t                done_;
    float                  lstart_, lend_, l_, coeff_;
    float                  freq_ctrl_;
    float mix_, mix_target_;

    float xfade_target, xfade;
};
} // namespace chompi