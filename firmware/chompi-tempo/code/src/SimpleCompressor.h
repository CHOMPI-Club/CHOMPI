#pragma once
#include "daisy.h"

class SimpleCompressor {
    public:
    SimpleCompressor() {};
    ~SimpleCompressor() {};

    void Init() {
        amount_ = 0.f;
        env_ = 0.f;
        gain_ = 1.f;
        threshold_ = 0.1f;
        attack_  = 1.0f - expf(-1.0f / (.01f * 48000.f));
        release_ = 1.0f - expf(-1.0f / (.25f * 48000.f));
        boost_ = 100.f;
    }

    void setAmount(float val) {
        amount_ = val;
    }

    void Process(float *wet_l, float *wet_r, float *dry_l, float *dry_r)
    {
        float dry_mix = ((*dry_l + *dry_r) * 0.5f) * boost_;
        env_ = attack_ * fabsf(dry_mix) + (1.0f - attack_) * env_;

        if(env_ > 1.0f) {
            env_ = 1.0f;
        }

        float target_gain;
        if(env_ <= threshold_) {
            target_gain = 1.0f;
        }
        else {
            float over = (env_ - threshold_) / (1.0f - threshold_);
            if(over > 1.0f)
                over = 1.0f;
            target_gain = 1.0f - over * amount_;
        }

        float coeff = (target_gain < gain_) ? attack_ : release_;
        gain_ = coeff * target_gain + (1.0f - coeff) * gain_;

        *wet_l *= gain_;
        *wet_r *= gain_;
    }

    private:
    float amount_;
    float env_;
    float gain_;
    float attack_;
    float release_;
    float threshold_;
    float boost_;
};