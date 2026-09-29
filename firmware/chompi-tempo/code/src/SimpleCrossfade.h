#pragma once
#include "daisysp.h"

using namespace chompi;

class Crossfade {

    public:

    enum CrossfadeType {
        THROUGH_ZERO,
        TO_ZERO,
        TO_ONE,
        POINT_TO_POINT
    };

    Crossfade() {};
    ~Crossfade() {};

    void Init(size_t crossfade_samples) {
        crossfade_samples_ = crossfade_samples;
    }

    void Process(float *sig_out, float *sig_in, bool *util) {
        if (!crossfading_) {
            *sig_in = 0.f;
            *sig_out = 1.f;
            return;
        }
        if (type_ == CrossfadeType::POINT_TO_POINT) {
            crossfade_counter_--;
            float env = static_cast<float>(crossfade_counter_) / static_cast<float>(crossfade_samples_);
            *sig_out = env;
            *sig_in = 1.f - env;
            if (crossfade_counter_ == 0) {
                crossfading_ = false;
            }
        }
        else if (type_ == CrossfadeType::THROUGH_ZERO) {
            if (fading_down_) {
                crossfade_counter_--;
                if (crossfade_counter_ == 0) {
                    fading_down_ = false;
                    fading_up_ = true;
                    *util = true;
                }
            }
            else {
                crossfade_counter_++;
                if (crossfade_counter_ == crossfade_samples_) {
                    fading_up_ = false;
                    crossfading_ = false;
                }
            }
            float env = static_cast<float>(crossfade_counter_) / static_cast<float>(crossfade_samples_);
            *sig_out = env;
        }
        else if (type_ == CrossfadeType::TO_ZERO) {
            crossfade_counter_--;
            if (crossfade_counter_ == 0) {
                crossfading_ = false;
                *util = true;
            }
            float env = static_cast<float>(crossfade_counter_) / static_cast<float>(crossfade_samples_);
            *sig_out = env;
        }
        else {
            crossfade_counter_++;
            if (crossfade_counter_ == crossfade_samples_) {
                crossfading_ = false;
                *util = true;
            }
            float env = static_cast<float>(crossfade_counter_) / static_cast<float>(crossfade_samples_);
            *sig_out = env;
        }
    }

    void startFade(CrossfadeType type) {
        crossfading_ = true;
        if (type == CrossfadeType::TO_ONE) {
            crossfade_counter_ = 0;
        }
        else {
            crossfade_counter_ = crossfade_samples_;
        }
        type_ = type;
        if (type_ == CrossfadeType::THROUGH_ZERO) {
            fading_down_ = true;
        }
    }

    void Stop() {
        crossfading_ = false;
    }

    bool isRunning() {
        return crossfading_;
    }

    //private:
    bool crossfading_;
    bool fading_down_, fading_up_;
    size_t crossfade_samples_;
    size_t crossfade_counter_;
    CrossfadeType type_;
};
