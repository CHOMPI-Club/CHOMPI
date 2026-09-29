#pragma once
#include "daisysp.h"
#include "clockManager.h"

constexpr float delayDivs[9] = {1.f/8.f, 1.f/6.f, 1.f/4.f, 1.f/3.f, 3.f/8.f, 1.f/2.f, 3.f/4.f, 1.f, 2.f};
constexpr size_t frozenDelayIntervals[9] = {6, 8, 12, 16, 18, 24, 36, 48, 96};
constexpr uint8_t kMaxGrains = 12;
constexpr uint32_t kMaxCrossfadeSamps = 256;
constexpr uint32_t kMaxEventCrossfadeSamps = 1024;

constexpr float delayColors[9][3] = {{0.f, 1.f, 0.f}, {1.f, 0.f, 0.f}, {.2f, 1.f, .2f}, {1.f, .3f, .1f}, {.4f, 1.f, .4f}, {1.f, .75f, .1f}, {.6f, 1.f, .6f}, {1.f, 1.f, .3f}, {.8f, 1.f, .8f}};

// This file is not compiled, it is just here if you want to try it out instead of the default delay

namespace chompi {
    void getSample(float *buffer, float read_head, float *out_l, float *out_r, size_t buffer_size) {
        if (read_head < 0.f) {
            read_head += static_cast<float>(buffer_size);
        }
        size_t i0 = static_cast<int>(read_head);
        if (i0 >= buffer_size) {
            i0 -= buffer_size;
        }
        size_t i1 = (i0 + 1);
        if (i1 >= buffer_size) {
            i1 -= buffer_size;
        }
        float frac = read_head - static_cast<float>(i0);

        // Read interleaved stereo
        float a_l = buffer[i0 * 2];
        float b_l = buffer[i1 * 2];
        *out_l += (a_l + frac * (b_l - a_l));

        float a_r = buffer[i0 * 2 + 1];
        float b_r = buffer[i1 * 2 + 1];
        *out_r += (a_r + frac * (b_r - a_r));
    }

    float fast_rsqrt(float x) {
        union { float f; uint32_t i; } conv;
        conv.f = x;
        conv.i = 0x5f3759dfU - (conv.i >> 1);
        float y = conv.f;
        // one Newton step to improve accuracy
        y = y * (1.5f - 0.5f * x * y * y);
        return y;
    }

    float fast_sqrt(float x) {
        if (x <= 0.f) return 0.f;
        return x * fast_rsqrt(x); // sqrt(x) ≈ x * (1/sqrt(x))
    }

    static float half_sine_table[kMaxCrossfadeSamps];

    void InitHalfSineTable() {
        for(size_t i = 0; i  < kMaxCrossfadeSamps; ++i) {
            float t = static_cast<float>(i) / static_cast<float>(kMaxCrossfadeSamps - 1);
            half_sine_table[i] = sinf(PI_F * t); // sin(pi * t)
        }
    }

    float half_sine_lut(float t) {
        float idx = t * (kMaxCrossfadeSamps - 1);
        int i = static_cast<int>(idx);
        float frac = idx - static_cast<float>(i);
        float a = half_sine_table[i];
        float b = half_sine_table[i+1];
        return a + frac * (b - a);
    }
};

class Grain {
    public:
    Grain() {};
    ~Grain() {};

    void Init(float *buffer_pointer, uint32_t buffer_size) {
        buff_ = buffer_pointer;
        buffer_size_ = buffer_size;
        playback_speed_ = 1.f;
        pan_ = 0.f;
        active_ = false;
    }

    void Process(float *out_l, float *out_r) {
        if (active_) {

            float amp = chompi::half_sine_lut(grain_counter_ / grain_size_);
            float panL = chompi::fast_sqrt(0.5f * (1.f - pan_));
            float panR = chompi::fast_sqrt(0.5f * (1.f + pan_));

            float sig_l = 0.f;
            float sig_r = 0.f;

            chompi::getSample(buff_, read_head_, &sig_l, &sig_r, buffer_size_);
            *out_l += sig_l * polyFactor * amp * panL;
            *out_r += sig_r * polyFactor * amp * panR;

            grain_counter_ += playback_speed_;

            if (grain_counter_ > grain_size_) {
                active_ = false;
            }

            read_head_ += playback_speed_;
            if (read_head_ >= static_cast<float>(buffer_size_)) {
                read_head_ -= static_cast<float>(buffer_size_);
            }

        }
        else {
            *out_l += 0.f;
            *out_r += 0.f;
        }
    }

    void Trigger(size_t grain_size, float pos, float speed, float pan) {
        read_head_ = pos;
        grain_size_ = grain_size;
        playback_speed_ = speed;
        pan_ = pan;
        grain_counter_ = 0.f;
        active_ = true;
    }

    void setHead(float pos) {
        read_head_ = pos;
    }

    bool isActive() {
        return active_;
    }

    private:
    float *buff_;
    float read_head_;
    uint32_t buffer_size_;
    float grain_size_;
    float playback_speed_;
    float pan_;
    float grain_counter_;

    const float polyFactor = .75;

    bool active_;
    bool frozen_;
};

class delayVoice {
    public:
    delayVoice() {};
    ~delayVoice() {};

    enum delayEvent {
        RETRIG,
        REVERSE,
        PITCH_UP,
        PITCH_DOWN,
        NONE
    };

    void Init(float *buffer, size_t buffer_size) {
        buffer_ = buffer;
        buffer_size_ = buffer_size;
        crossfade_env_ = 1.f;
        curEvent = nextEvent = NONE;
    }

    void updateTempo(float delay_samples, uint32_t write_head) {
        if (!div_crossfade_) {
            delay_samples_ = delay_samples;
        }
        write_head_ = write_head;
    }

    void startFadeIn() {
        if (fading_in_ || fading_out_ || active_) {
            return;
        }
        active_ = true;
        fading_in_ = true;
        event_crossfade_counter_ = 0;
        curEvent = nextEvent;
        nextEvent = NONE; // Until otherwise changed
        read_head_ = write_head_ - delay_samples_;
    }

    void startFadeOut() {
        if (fading_in_ || fading_out_ || !active_) {
            return;
        }
        fading_out_ = true;
        event_crossfade_counter_ = kMaxEventCrossfadeSamps;
    }

    void setNextEvent(size_t event) {
        nextEvent = static_cast<delayEvent>(event);
    }

    void setDivCrossfade(float new_read_head) {
        if (active_) {
            div_read_head_ = new_read_head;
            div_crossfade_counter_ = kMaxCrossfadeSamps;
            div_crossfade_ = true;
            testIdx = 0;
        }
        else {
            read_head_ = new_read_head;
        }
    }

    void Read(float *out_l, float *out_r) {

        if (!active_) {
            *out_l += 0.f;
            *out_r += 0.f;
            return;
        }

        crossfade_env_ = 1.f;
        div_env_ = 1.f;

        if (fading_in_) {
            event_crossfade_counter_++;
            if (event_crossfade_counter_ > kMaxEventCrossfadeSamps) {
                event_crossfade_counter_ = kMaxEventCrossfadeSamps;
                fading_in_ = false;
            }
            float phase = static_cast<float>(event_crossfade_counter_) / static_cast<float>(kMaxEventCrossfadeSamps);
            crossfade_env_ = sinf(phase * PI_F * 0.5f);
        }
        else if (fading_out_) {
            event_crossfade_counter_--;
            if (event_crossfade_counter_ < 1) {
                fading_out_ = false;
                active_ = false;
            }
            float phase = static_cast<float>(event_crossfade_counter_) / static_cast<float>(kMaxEventCrossfadeSamps);
            crossfade_env_ = sinf(phase * PI_F * 0.5f);
        }

        switch (curEvent) {
            case NONE: {
                read_head_ = write_head_ - delay_samples_;
            }
            break;
            case RETRIG: {
                read_head_ = static_cast<float>(write_head_) - delay_samples_ - delay_samples_ * .125; // Might have to do target
            }
            break;
            case REVERSE: {
                read_head_ -= 1.f;
            }
            break;
            case PITCH_UP: {
                read_head_ += 2.f;
            }
            break;
            case PITCH_DOWN: {
                read_head_ += .5f;
            }
            break;
        };
        if (read_head_ < 0.f) {
            read_head_ += static_cast<float>(buffer_size_);
        }
        if (read_head_ >= static_cast<float>(buffer_size_)) {
            read_head_ -= static_cast<float>(buffer_size_);
        }

        float sig_l = 0.f;
        float sig_r = 0.f;

        if (div_crossfade_) {
            div_crossfade_counter_--;
            div_env_ = static_cast<float>(div_crossfade_counter_) / static_cast<float>(kMaxCrossfadeSamps);
            if (div_crossfade_counter_ == 0) {
                read_head_ = div_read_head_;
                div_crossfade_ = false;
                div_env_ = 1.f;
            }
            samps_since_crossfade = 0;
        }

        samps_since_crossfade++;

        chompi::getSample(buffer_, read_head_, &sig_l, &sig_r, buffer_size_);
        *out_l += sig_l * crossfade_env_ * div_env_;
        *out_r += sig_r * crossfade_env_ * div_env_;

        if (div_crossfade_) {

            switch (curEvent) {
                case NONE: {
                    div_read_head_ += 1.f;
                }
                break;
                case RETRIG: {
                    div_read_head_ += 1.f;
                }
                break;
                case REVERSE: {
                    div_read_head_ -= 1.f;
                }
                break;
                case PITCH_UP: {
                    div_read_head_ += 2.f;
                }
                break;
                case PITCH_DOWN: {
                    div_read_head_ += .5f;
                }
                break;
            }
            if (div_read_head_ < 0.f) {
                div_read_head_ += static_cast<float>(buffer_size_);
            }
            if (div_read_head_ >= static_cast<float>(buffer_size_)) {
                div_read_head_ -= static_cast<float>(buffer_size_);
            }

            float c_sig_l = 0.f;
            float c_sig_r = 0.f;
            chompi::getSample(buffer_, div_read_head_, &c_sig_l, &c_sig_r, buffer_size_);
            *out_l += c_sig_l * crossfade_env_ * (1.f - div_env_);
            *out_r += c_sig_r * crossfade_env_ * (1.f - div_env_);

            env_tester[testIdx] = div_env_;
            reg_tester[testIdx] = read_head_;
            new_tester[testIdx] = div_read_head_;
            testIdx++;
        }
    }

    float *buffer_;
    size_t buffer_size_;
    uint32_t write_head_;
    
    float read_head_;
    float crossfade_env_;
    float delay_samples_;
    delayEvent curEvent, nextEvent;
    uint32_t event_crossfade_counter_;
    bool active_;
    bool fading_in_, fading_out_;

    float env_tester[kMaxCrossfadeSamps + 4];
    float reg_tester[kMaxCrossfadeSamps + 4];
    float new_tester[kMaxCrossfadeSamps + 4];
    size_t testIdx;
    volatile size_t samps_since_crossfade = 0;

    float div_read_head_;
    bool div_crossfade_;
    size_t div_crossfade_counter_;
    float div_env_;
};

class granularDelay {
    public:
    granularDelay() {};
    ~granularDelay() {};

    enum delayEvent {
        RETRIG,
        REVERSE,
        PITCH_UP,
        PITCH_DOWN,
        NONE
    };

    void Init(float *buff, float *frozen_buff, size_t delaySize, clockManager *clock_manager) {
        buffer_ = buff;
        frozen_buffer_ = frozen_buff;
        buffer_size_ = delaySize;

        clock_manager_ = clock_manager;

        write_head_ = 0;
        delay_samples_ = delay_samples_target_ = 72000.f;

        knob_position_ = .5f;
        alt_control_ = .15f;
        delay_div_position_ = 8;

        setFeedback(.3f);

        wet_amt_ = wet_amt_target_ = 0.f;

        curIdx = 0;
        nextIdx = 1;
        myVoices[curIdx].active_ = true;

        for (size_t i = 0; i < kMaxGrains; ++i) {
            myGrains[i].Init(buff, buffer_size_);
        }

        for (size_t i = 0; i < 2; ++i) {
            myVoices[i].Init(buffer_, buffer_size_);
        }

        chompi::InitHalfSineTable();
    }

    void write(float in_l, float in_r) {
        if (lock_buffer_) {
            return;
        }
        size_t idx = write_head_ * 2;
        buffer_[idx] = in_l + cur_sig_l_;
        buffer_[idx + 1] = in_r + cur_sig_r_;

        write_head_ = (write_head_ + 1) % buffer_size_;
    }

    void read(float* out_l, float* out_r)
    {
        if (delay_on_) {
            processDelay(out_l, out_r);
        }
        else if (granular_on_) {
            processGrains(out_l, out_r);
        }
        else {
            *out_l = *out_r = 0.f;
        }
    }

    void processDelay(float* out_l, float* out_r) {

        fonepole(wet_amt_, wet_amt_target_, .001f);

        *out_l = *out_r = 0.f; // Need to fix this

        float crossfadeEnv = 1.f;
        float divEnv = 1.f;

        size_t t_ = clock_manager_->getTempo();
        size_t new_interval = 8;
        if (knob_position_ < .4) {
            new_interval = static_cast<size_t>(knob_position_ * 20.0f + 0.5f); // 0–0.4 maps to 0–8
        }

        if (new_interval != delay_div_position_) {
            if (lock_buffer_) {
                float samples_per_tick = (48000.f * 60.f) / (t_ * 12.f);
                float phase_offset = frozen_read_head_ - static_cast<float>(clock_pulse_counter_[delay_div_position_]) * samples_per_tick;
                crossfade_read_head_ = static_cast<float>(clock_pulse_counter_[new_interval]) * samples_per_tick + phase_offset;
                div_crossfade_ = true;
                div_counter_ = kMaxCrossfadeSamps;
            }
            else {
                float new_delay_samps = (60000.f / t_) * 4.f * delayDivs[new_interval] * 48.f;
                float new_read_head = write_head_ - new_delay_samps;
                if (new_read_head < 0.f) {
                    new_read_head += static_cast<float>(buffer_size_);
                }
                for (size_t i = 0; i < 2; ++i) {
                    myVoices[i].setDivCrossfade(new_read_head);
                }
                delay_samples_ = new_delay_samps;
            }
        }

        delay_div_position_ = new_interval;
        delay_samples_target_ = (60000.f / t_) * 4.f * delayDivs[delay_div_position_] * 48.f;
        fonepole(delay_samples_, delay_samples_target_, .001f);

        if (lock_buffer_) {

            float read_end_ = static_cast<float>(frozen_write_head) - delay_samples_;
            if(read_end_ < 0.f) {
                read_end_ += buffer_size_;
            }
            frozen_read_head_+= 1.f;
            if (frozen_read_head_ >= static_cast<float>(buffer_size_)) {
                frozen_read_head_ -= static_cast<float>(buffer_size_);
            }

            for (size_t i = 0; i < 9; ++i) {
                if (clock_pulse_counter_[i] >= frozenDelayIntervals[i]) {
                    clock_pulse_counter_[i] = 0;
                    if (i == delay_div_position_) {
                        crossfading_down_ = true;
                        crossfade_counter_ = kMaxCrossfadeSamps;
                    }
                }
            }

            frozen_sample_counter_++;

            if (crossfading_down_) {
                if (crossfade_counter_ == 0) {
                    crossfading_down_ = false;
                    frozen_read_head_ = read_end_;
                    frozen_sample_counter_ = 0;
                    crossfading_up_ = true;
                    crossfadeEnv = 0.f;
                }
                else {
                    crossfade_counter_--;
                    crossfadeEnv = static_cast<float>(crossfade_counter_) / static_cast<float>(kMaxCrossfadeSamps);
                }
            }
            else if (crossfading_up_) {
                crossfade_counter_++;
                crossfadeEnv = static_cast<float>(crossfade_counter_) / static_cast<float>(kMaxCrossfadeSamps);
                if (crossfade_counter_ == kMaxCrossfadeSamps) {
                    crossfading_up_ = false;
                }
            }

            if (div_crossfade_) {
                divEnv = static_cast<float>(div_counter_) / static_cast<float>(kMaxCrossfadeSamps);
            }

            float sig_l = 0.f;
            float sig_r = 0.f;

            chompi::getSample(frozen_buffer_, frozen_read_head_, &sig_l, &sig_r, buffer_size_);
            *out_l = sig_l * crossfadeEnv * wet_amt_ * divEnv;
            *out_r = sig_r * crossfadeEnv * wet_amt_ * divEnv;

            if (div_crossfade_) {
                float c_sig_l = 0.f;
                float c_sig_r = 0.f;
                chompi::getSample(frozen_buffer_, crossfade_read_head_, &c_sig_l, &c_sig_r, buffer_size_);

                *out_l += c_sig_l * crossfadeEnv * wet_amt_ * (1.f - divEnv);
                *out_r += c_sig_r * crossfadeEnv * wet_amt_ * (1.f - divEnv);

                div_counter_--;
                if (div_counter_ == 0) {
                    frozen_read_head_ = crossfade_read_head_;
                    div_crossfade_ = false;
                }
                crossfade_read_head_ += 1.f;
                if (crossfade_read_head_ > static_cast<float>(buffer_size_)) {
                    crossfade_read_head_ -= static_cast<float>(buffer_size_);
                }
            }

            cur_sig_l_ = 0.f;
            cur_sig_r_ = 0.f;

        }
        else {
            for (size_t i = 0; i < 2; ++i) {
                myVoices[i].updateTempo(delay_samples_, write_head_);
            }
            size_t div = clock_manager_->getClockDivPos(0);
            float clock_mult = 1.f;
            clock_interval_ = 1;
            if (div > 2){
                clock_interval_ = 2;
                clock_mult = 2.f;
            }

            if (clock_edge_) {
                clock_counter_++;
                if (clock_counter_ >= clock_interval_) {
                    event_type_[0] = event_type_[1];
                    bool random_event = static_cast<float>(rand()) / static_cast<float>(RAND_MAX) < (0.5f * alt_control_);
                    if (random_event) {
                        curIdx = nextIdx;
                        nextIdx = (curIdx + 1) % 2;
                        event_type_[1] = static_cast<delayEvent>(rand() % 4);
                        myVoices[curIdx].setNextEvent(event_type_[1]);
                    }
                    else {
                        event_type_[1] = delayEvent::NONE;
                        if (event_type_[0] != NONE) {
                            curIdx = nextIdx;
                            nextIdx = (curIdx + 1) % 2;
                            myVoices[curIdx].setNextEvent(4);
                        }
                    }
                    clock_counter_ = 0;
                }
                clock_edge_ = false;
            }

            if (event_type_[0] != NONE || event_type_[1] != NONE) {
                myVoices[curIdx].startFadeIn(); // They manage themselves
                myVoices[nextIdx].startFadeOut();
            }

            // Read interleaved stereo
            for (size_t i = 0; i < 2; ++i) {
                myVoices[i].Read(out_l, out_r);
            }

            *out_r *= wet_amt_;
            *out_l *= wet_amt_;

            cur_sig_l_ = *out_l * delay_feedback_amt_;
            cur_sig_r_ = *out_r * delay_feedback_amt_;

            size_t idx = frozen_write_head * 2;
            frozen_buffer_[idx] = *out_l;
            frozen_buffer_[idx + 1] = *out_r;
            frozen_write_head = (frozen_write_head + 1) % buffer_size_;
        }
    }

    void processGrains(float* out_l, float* out_r) {
        float sig_l = 0.f;
        float sig_r = 0.f;

        delay_samples_target_ = 48000.f;
        fonepole(delay_samples_, delay_samples_target_, .001f);
        fonepole(wet_amt_, wet_amt_target_, .001f);

        size_t clock_pulses = knob_position_ > .83f ? 3 : 6;
        if (grain_pulse_counter >= clock_pulses) {
            float r = static_cast<float>(rand()) / static_cast<float>(RAND_MAX);
            float norm = (knob_position_ - 0.55f) / (1.0f - 0.55f);
            float prob = 0.1f + norm * (0.9f - 0.1f);
            if (knob_position_ > .83f) {
                norm = (knob_position_ - .83f) / (1.f - .83f);
                prob = 0.3f + norm * (0.75f - 0.3f);
            }
            if (r < prob) {
                for (size_t i = 0; i < kMaxGrains; ++i) {
                    if (!myGrains[i].isActive()) {
                        float sp = randomSpeed();
                        size_t sz = randomSize(sp);
                        myGrains[i].Trigger(sz, randomHead(sz, sp), sp, randomPan());
                        break;
                    }
                }
            }
            grain_pulse_counter = 0;
        }

        for (size_t i = 0; i < kMaxGrains; ++i) {
            myGrains[i].Process(&sig_l, &sig_r);
        }

        *out_l = sig_l * wet_amt_;
        *out_r = sig_r * wet_amt_;

        cur_sig_l_ = *out_l * grain_feedback_amt_;
        cur_sig_r_ = *out_r * grain_feedback_amt_;
    }

    size_t randomInterval() {
        float norm = (knob_position_ - 0.55f) / 0.45f;

        float min_us = 15000.f;
        float max_us = 400000.f;

        float base = max_us - norm * (max_us - min_us);
        float jitter = base * 0.2f * ((rand() % 2001 - 1000) / 1000.f); // random float in [-0.2, 0.2]
        float result = base + jitter;

        return static_cast<size_t>(fmaxf(result, min_us));
    }

    size_t randomSize(float speed) {
        float norm = (knob_position_ - 0.55f) / 0.45f;

        // Define grain size bounds in samples (e.g., 30ms to 200ms at 48kHz)
        const float min_samples = 0.25f * 48000.f;
        const float max_samples = 1.f * 48000.f;

        float base = min_samples + (1.f - norm) * (max_samples - min_samples);
        float jitter = base * 0.2f * ((rand() % 2001 - 1000) / 1000.f); // random float in [-0.2, 0.2]
        float size = base + jitter;
        fclamp(size, min_samples, max_samples);

        if (speed <= .5f) {
            size *= .5f;
        }

        return static_cast<size_t>(size);
    }

    size_t randomHead(size_t grainSize, float grainSpeed){
        float min_offset, max_offset;

        min_offset = static_cast<float>(grainSize) * grainSpeed;

        if (grainSpeed >= 2.f) {
            max_offset = 96000.f;
        }
        else {
            max_offset = delay_samples_;
        }

        float range = fmaxf(max_offset - min_offset, 1.f);
        float offset = min_offset + (rand() / (float)RAND_MAX) * range;
        int32_t read_start = static_cast<int32_t>(write_head_) - static_cast<int32_t>(max_offset);
        if (read_start < 0) {
            read_start += buffer_size_;  // wrap around
        }

        return (write_head_ - static_cast<size_t>(offset)) % buffer_size_;
    }

    float randomSpeed() {
        float r = static_cast<float>(rand()) / static_cast<float>(RAND_MAX);

        if (alt_control_ == 0.0f) {
            return 1.0f;
        }

        // Adjust the threshold for switching based on alt_control_
        float threshold1 = 1.0f - alt_control_ * .5f;
        float threshold2 = threshold1 + alt_control_ * 0.25f;

        if (r < threshold1) {
            return 1.0f;
        } else if (r < threshold2) {
            return .5f;
        } else {
            return 2.f;
        }
    }

    float randomPan() {
        float effective_range = 0.5f + (alt_control_ * 0.5f); // Range: [0.5, 1.0]
        float r = ((rand() % 2001 - 1000) / 1000.f); // [-1.0, 1.0]
        return r * effective_range;
    }

    void setFeedback(float val) {
        grain_feedback_amt_ = val * .6f;
        delay_feedback_amt_ = val;
    }

    void setMainControl(float val) {
        if (val < .45f) {
            if (val < .45 && val > .4) {
                wet_amt_target_ = 9.f - val * 20.f; // .45 to .4 mapped to 0 to 1
            }
            else {
                wet_amt_target_ = 1.f;
            }
            delay_on_ = true;
            granular_on_ = false;
            // delay
        }
        else if (val > .55) {
            // granular
            if (val < .6 && val > .55) {
                wet_amt_target_ = val * 20.f - 11.f;
            }
            else {
                wet_amt_target_ = 1.f;
            }
            delay_on_ = false;
            granular_on_ = true;
        }
        else {
            wet_amt_target_ = 0.f;
            delay_on_ = false;
            granular_on_ = false;
        }
        knob_position_ = val;
    }

    void setAltControl(float val) {
        alt_control_ = val;
    }

    void toggleBufferLock() {
        lock_buffer_ = !lock_buffer_;
        if (lock_buffer_) {
            calculateCrossfadePos();
        }
        frozen_read_head_ = frozen_write_head - delay_samples_;
        if (frozen_read_head_ < 0.f) {
            frozen_read_head_ += static_cast<float>(buffer_size_);
        }
        frozen_sample_counter_ = 0;
        for (size_t i = 0; i < 9; ++i) {
            clock_pulse_counter_[i] = 0;
        }
    }

    void setBufferLock(bool t) {
        lock_buffer_ = t;
    }

    void getColors(float *colors) {
        if (knob_position_ > .5f) {
            float idx = (knob_position_ - .5f) * 2.f;
            colors[0] = color_xfade(.14f, 0.f, idx);
            colors[1] = color_xfade(1.f, 0.f, idx);
            colors[2] = color_xfade(.92f, 1.f, idx);
        }
        else {
            float lock_pos = 1.f;
            if (lock_buffer_ && knob_position_ < .45) {
                lock_pos = static_cast<float>(frozen_sample_counter_) / delay_samples_;
                lock_pos = fclamp(lock_pos, 0.f, 1.f);
            }
            if (knob_position_ > .4f) {
                float idx = (.5f - knob_position_) * 10.f;
                colors[0] = color_xfade(.14f, delayColors[8][0], idx) * lock_pos;
                colors[1] = color_xfade(1.f, delayColors[8][1], idx) * lock_pos;
                colors[2] = color_xfade(.92f, delayColors[8][2], idx) * lock_pos;
            }
            else {
                colors[0] = delayColors[delay_div_position_][0] * lock_pos;
                colors[1] = delayColors[delay_div_position_][1] * lock_pos;
                colors[2] = delayColors[delay_div_position_][2] * lock_pos;
            }
        }
    }

    void calculateCrossfadePos() {
        crossfade_start_ = (frozen_write_head - kMaxCrossfadeSamps) % buffer_size_;
    }

    void setClockEdge() {
        clock_edge_ = true;
    }

    void setClockPulse() {
        if (granular_on_) {
            grain_pulse_counter++;
        }
        if (!lock_buffer_) {
            return;
        }
        for (size_t i = 0; i < 9; ++i) {
            clock_pulse_counter_[i]++;
        }
    }

    Grain myGrains[kMaxGrains];
    delayVoice myVoices[2];

    private:
    float *buffer_;
    uint32_t write_head_;
    uint32_t buffer_size_;
    float delay_samples_, delay_samples_target_;
    float cur_sig_l_, cur_sig_r_;

    float *frozen_buffer_;
    uint32_t frozen_write_head;
    float frozen_read_head_;
    uint32_t crossfade_start_;
    uint32_t crossfade_counter_;
    bool crossfading_down_, crossfading_up_;
    size_t frozen_sample_counter_;
    size_t clock_pulse_counter_[9];
    size_t curDiv_;

    bool div_crossfade_;
    float crossfade_read_head_;
    float div_counter_;

    float wet_amt_, wet_amt_target_;
    bool delay_on_, granular_on_;
    bool lock_buffer_;
    float grain_feedback_amt_;
    float delay_feedback_amt_;

    uint32_t grainIntervalTimer_;
    uint32_t grainInterval_;
    uint32_t pulseCounter_;
    bool trigger_window_;
    size_t grain_pulse_counter;

    float knob_position_;
    float alt_control_;
    size_t delay_div_position_;

    clockManager *clock_manager_;

    bool clock_edge_;
    delayEvent event_type_[2] = {NONE, NONE};
    uint8_t curIdx, nextIdx;
    uint8_t clock_counter_, clock_interval_;
};