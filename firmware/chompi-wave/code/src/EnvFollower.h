/** @file EnvFollower.h
 *  @brief A simple asymmetric peak/envelope follower used to drive the VU
 *  LED meter on the gain knob (see engine.getVUSample() Read from NormalPage's
 *  Draw()) Not used in the actual audio signal path, just a visual meter.
 */
#pragma once

namespace chompi {
class EnvFollower
{
    public:

        EnvFollower() {}
        ~EnvFollower() {}

        void Init()
        {
            last_samp_ = 0.f;
            b_up_ = .5f;
            b_down_ = .9993f;

            b_ = b_up_;
            g_ = 1.f - b_;
        }  

        void Process(float samp)
        {
            samp = fabsf(samp);
            samp = daisysp::fclamp(samp, 0.f, 1.f);

            b_ = samp > last_samp_ ? b_up_ : b_down_;
            g_ = 1.f - b_;

            last_samp_ = samp * g_ + last_samp_ * b_;
        }

        // also kicks up the gain a bunch and then clips
        inline float GetLastSamp()
        { 
            float vu_sample = last_samp_;
            vu_sample *= 5.f;
            vu_sample += 0.0f;
            vu_sample = vu_sample > 1.f ? 1.f : vu_sample;

            return vu_sample;
        }

    private:
        float last_samp_, g_, b_;
        float b_up_, b_down_;
};
} // namespace chompi