#pragma once
#include "SampleManager.h"
#include "granularDelay.h"
#include "reverb.h"
#include "SimpleCompressor.h"

static constexpr float kMicGain = 5.f;
static constexpr float kLineInGain = 8.f;
static constexpr float kFinalGain = 2.f;

enum engineSelection {
    CHROMATIC,
    SLICE
};

class fxEngine {
    public:
    fxEngine() {}
    ~fxEngine() {}

    void Init(sampleManager *sample_manager, float sample_rate, granularDelay *delay, Reverb *reverb, size_t init_monitor_pos) {

        engine_ = CHROMATIC;
        record = false;
        in_source = InputSource::MIC;

        sample_manager_ = sample_manager;

        delay_ = delay;
        reverb_ = reverb;

        reverb_->SetAmount(0.f);
        reverb_->SetInputGain(.3f);
        reverb_->SetLowpass(1.f);
        comp_.Init();

        input_env_follower.Init();
        dcblock_mic_in_.Init(sample_rate);
        dcblock_line_in_l_.Init(sample_rate);
        dcblock_line_in_r_.Init(sample_rate);
        mic_filter_.Init(sample_rate);

        lim_hp_l_.Init();
        lim_hp_r_.Init();
        lim_line_l_.Init();
        lim_line_r_.Init();

        reverb_amt_ = reverb_amt_target_ = 0.f;
        reverb_boost_ = reverb_boost_target_ = 0.f;

        fx_position_ = .5f;

        gain_ = gain_target_ = .6f;
        ingain_ = ingain_target_ = .75;

        saturate_amt_ = saturate_amt_target_ = 1.f;
        saturate_makeup_ = saturate_makeup_target_ = 1.f;

        chroma_dry_amt_ = chroma_dry_amt_target_ = 1.f;
        chroma_wet_amt_ = chroma_wet_amt_target_ = 1.f;
        slice_dry_amt_ = slice_dry_amt_target_ = 1.f;
        slice_wet_amt_ = slice_wet_amt_target_ = 1.f;

        resamp_env_ = resamp_env_target_ = 1.f;

        monitor_mode_ = init_monitor_pos;

        output_env_follower.Init();

        sample_rate_ = sample_rate;

    }

    void Process(const float *const *in, float **out, size_t size, float **chroma_buff, float **slice_buff) {

        float *outl = out[0];
        float *outr = out[1];

        float monitor[2][size];
        std::fill(&monitor[0][0], &monitor[1][size], 0.f);

        std::fill(&dry_buffer_[0][0], &dry_buffer_[0][0] + 96, 0.f);
        std::fill(&wet_buffer_[0][0], &wet_buffer_[0][0] + 96, 0.f);

        if(monitor_mode_ == 1) // Both
        {
            if(in_source == InputSource::MIC) {
                ApplyMicMonitorPositionTwo(in, dryPtr, wetPtr, size, &monitor[0][0]);
            }
            else if(in_source == InputSource::LINE_IN) {
                ApplyLineMonitorPositionTwo(in, dryPtr, wetPtr, size, &monitor[0][0]);
            }
        }
        else if (monitor_mode_ == 2 && input_monitor && in_source == InputSource::MIC)
        {
            ApplyMicMonitor(in, wetPtr, size, &monitor[0][0]);
        }

        float delay_l = 0.f;
        float delay_r = 0.f;

        for (size_t i = 0; i < size; ++i) {
            fonepole(chroma_wet_amt_, chroma_wet_amt_target_, .001f);
            fonepole(chroma_dry_amt_, chroma_dry_amt_target_, .001f);
            fonepole(slice_wet_amt_, slice_wet_amt_target_, .001f);
            fonepole(slice_dry_amt_, slice_dry_amt_target_, .001f);
            fonepole(reverb_amt_, reverb_amt_target_, .001f);
            fonepole(reverb_boost_, reverb_boost_target_, .001f);

            wet_buffer_[0][i] += chroma_buff[0][i] * chroma_wet_amt_ + slice_buff[0][i] * slice_wet_amt_;
            wet_buffer_[1][i] += chroma_buff[1][i] * chroma_wet_amt_ + slice_buff[1][i] * slice_wet_amt_;
            dry_buffer_[0][i] += chroma_buff[0][i] * chroma_dry_amt_ + slice_buff[0][i] * slice_dry_amt_;
            dry_buffer_[1][i] += chroma_buff[1][i] * chroma_dry_amt_ + slice_buff[1][i] * slice_dry_amt_;

            delay_->write(wet_buffer_[0][i], wet_buffer_[1][i]);
            delay_->read(&delay_l, &delay_r);

            reverb_->SetAmount(reverb_amt_ * reverb_amt_ * .8f);
            reverb_->SetTime(reverb_amt_);
            reverb_->SetLowpass(reverb_amt_ * .55f + .4f);
            reverb_->SetDiffusion(reverb_amt_ * .6f);

            delay_l += wet_buffer_[0][i] * reverb_boost_;
            delay_r += wet_buffer_[1][i] * reverb_boost_;
            reverb_->Process(&delay_l, &delay_r);

            comp_.Process(&delay_l, &delay_r, &dry_buffer_[0][i], &dry_buffer_[1][i]);

            outl[i] = delay_l + dry_buffer_[0][i];
            outr[i] = delay_r + dry_buffer_[1][i];
        }

        std::copy(out[0], out[0] + size, out[2]); // Copy here after FX
        std::copy(out[1], out[1] + size, out[3]);

        if (monitor_mode_ == 0 && input_monitor) {
            if (in_source == InputSource::MIC) {
                ApplyMicMonitor(in, out, size, &monitor[0][0]);
            }
            if (in_source == InputSource::LINE_IN) {
                ApplyLineMonitor(in, out, size, &monitor[0][0]);
            }
        }
        else if (monitor_mode_ == 2) {
            ApplyLineMonitor(in, out, size, &monitor[0][0]);
        }

        if (in_source == InputSource::RESAMPLE) {
            for (size_t i = 0; i < size; ++i) {

                fonepole(resamp_env_, resamp_env_target_, .001f);

                out[0][i] = monitor[0][i] = resamp_env_ * out[0][i];
                out[1][i] = monitor[1][i] = resamp_env_ * out[1][i];

                input_env_follower.Process((monitor[0][i] + monitor[1][i]) * .2f);

                if (monitor_mode_ == 1) {
                    out[2][i] = out[0][i];
                    out[3][i] = out[1][i];
                }

                monitor[0][i] *= 10.f;
                monitor[1][i] *= 10.f; // Because it doesn't get boosted at the end like the output signal
            }
        }

        ApplyEnvelopeFollower(size, &monitor[0][0]);

        if (record) {
            for (size_t i = 0; i < size; i++)
            {
                const int16_t inl = f2s16(monitor[0][i]);
                const int16_t inr = f2s16(monitor[1][i]);

                if(!sample_manager_->stereoWrite(inl, inr)) {
                    StopRecording();
                }
            }
        }
        
    }

    void ApplyOutputFX(const float *const *in, float **out, size_t size) {
        for (size_t i = 0; i < size; ++i) {
            //gain and compress
            fonepole(final_lim_, final_lim_target_, .001f);
            fonepole(saturate_amt_, saturate_amt_target_, .001f);
            fonepole(saturate_makeup_, saturate_makeup_target_, .001f);
            fonepole(gain_, gain_target_, .001f);

            const float thresh = 1.f / (10.f * final_lim_ + 4.f);
            const float ratio = 1.f + final_lim_ * final_lim_ * 7.f;
            const float makeup = .9f + final_lim_ * .6f;
            const float pregain = 7.f * final_lim_ + 1.f;
            out[0][i] = lim_hp_l_.ProcessComp(out[0][i], pregain, thresh, ratio, makeup);
            out[1][i] = lim_hp_r_.ProcessComp(out[1][i], pregain, thresh, ratio, makeup);
            out[2][i] = lim_line_l_.ProcessComp(out[2][i], pregain, thresh, ratio, makeup);
            out[3][i] = lim_line_r_.ProcessComp(out[3][i], pregain, thresh, ratio, makeup);

            out[0][i] = daisysp::SoftClip(saturate_amt_ * out[0][i]);
            out[1][i] = daisysp::SoftClip(saturate_amt_ * out[1][i]);
            out[2][i] = daisysp::SoftClip(saturate_amt_ * out[2][i]);
            out[3][i] = daisysp::SoftClip(saturate_amt_ * out[3][i]);
            
            out[0][i] *= saturate_makeup_;
            out[1][i] *= saturate_makeup_;
            out[2][i] *= saturate_makeup_;
            out[3][i] *= saturate_makeup_;

            out[0][i] *= gain_ * kFinalGain;
            out[1][i] *= gain_ * kFinalGain;
            out[2][i] *= gain_ * kFinalGain;
            out[3][i] *= gain_ * kFinalGain;

            output_env_follower.Process((out[0][i] + out[1][i]));
        }
    }

    void ApplyEnvelopeFollower(size_t size, float* monitor)
    {
        for (size_t i = 0; i < size; i++)
            input_env_follower.Process((monitor[i] + monitor[size + i]) * .8f);
    }

    float getVUSampleInput() {
        return input_env_follower.GetLastSamp();
    }

    void ApplyMicMonitor(const float* const* in, float **out, size_t size, float* monitor)
    {
        for (size_t i = 0; i < size; i++)
        {
            fonepole(ingain_, ingain_target_, .001f);
            float sig = dcblock_mic_in_.Process(in[0][i] * ingain_ * kMicGain);
            sig = mic_filter_.Process(sig);

            monitor[i] += sig;
            monitor[size + i] += sig;

            out[0][i] += sig * .125f;
            out[1][i] += sig * .125f;
        }
    }

    void ApplyLineMonitor(const float* const* in, float **out, size_t size, float* monitor)
    {
        for (size_t i = 0; i < size; i++)
        {
            fonepole(ingain_, ingain_target_, .001f);
            const float sigl = dcblock_line_in_l_.Process(in[2][i] * ingain_ * kLineInGain);
            const float sigr = dcblock_line_in_r_.Process(in[3][i] * ingain_ * kLineInGain);

            monitor[i] += sigl;
            monitor[size + i] += sigr;

            out[0][i] += sigl * .125f;
            out[1][i] += sigr * .125f;
        }
    }

    void ApplyMicMonitorPositionTwo(const float* const* in, float **dry, float **wet, size_t size, float* monitor) {
        for (size_t i = 0; i < size; i++) {
            fonepole(ingain_, ingain_target_, .001f);
            float sig = dcblock_mic_in_.Process(in[0][i] * ingain_ * kMicGain);
            sig = mic_filter_.Process(sig);

            monitor[i] += sig;
            monitor[size + i] += sig;

            dry[0][i] += sig * .125f * chroma_dry_amt_;
            dry[1][i] += sig * .125f * chroma_dry_amt_;

            wet[0][i] += sig * .125f * chroma_wet_amt_;
            wet[1][i] += sig * .125f * chroma_wet_amt_;
        }
    }

    void ApplyLineMonitorPositionTwo(const float* const* in, float **dry, float **wet, size_t size, float* monitor) {
        for (size_t i = 0; i < size; i++) {
            fonepole(ingain_, ingain_target_, .001f);
            const float sigl = dcblock_line_in_l_.Process(in[2][i] * ingain_ * kLineInGain);
            const float sigr = dcblock_line_in_r_.Process(in[3][i] * ingain_ * kLineInGain);

            monitor[i] += sigl;
            monitor[size + i] += sigr;


            dry[0][i] += sigl * .125f * chroma_dry_amt_;
            dry[1][i] += sigr * .125f * chroma_dry_amt_;

            wet[0][i] += sigl * .125f * chroma_wet_amt_;
            wet[1][i] += sigr * .125f * chroma_wet_amt_;
        }
    }

    void setGranularMain(float val) {
        delay_->setMainControl(val);
        if (val > .55f) {
            float norm = (val - .55f) / (1.f - .55f);
            reverb_amt_target_ = .25f + .65f * powf(norm, .5f);
            if (val > .6f) {
                reverb_boost_target_ = .3f;
            }
            else {
                reverb_boost_target_ = 0.f;
            }
        }
        else {
            reverb_amt_target_ = 0.f;
            reverb_boost_target_ = 0.f;
        }
        fx_position_ = val;
    }

    void setGranularAlt(float val) {
        delay_->setAltControl(val);
    }

    void setGranularMix(float val, size_t eng) {
        if (eng == CHROMATIC) {
            if (val >= .5f) {
                chroma_dry_amt_target_ = 2.f - val * 2.f;
                chroma_wet_amt_target_ = 1.f;
            }
            else {
                chroma_dry_amt_target_ = 1.f;
                chroma_wet_amt_target_ = val * 2.f;
            }
        }
        else {
            if (val >= .5f) {
                slice_dry_amt_target_ = 2.f - val * 2.f;
                slice_wet_amt_target_ = 1.f;
            }
            else {
                slice_dry_amt_target_ = 1.f;
                slice_wet_amt_target_= val * 2.f;
            }
        }
    }

    void setGranularFeedback(float val) {
        delay_->setFeedback(val);
        float norm = 0.f;
        if (val > .6f) {
            norm = (val - .6f) / (1.f - .6f);
            comp_.setAmount(norm);
        }
        comp_.setAmount(norm);
    }

    void toggleGranularFreeze() {
        delay_->toggleBufferLock();
    }

    void setBufferFreeze(bool t) {
        delay_->setBufferLock(t);
    }

    void getColors(float *colors) {
        delay_->getColors(colors);
    }

    void setGain(float amount) {
        gain_target_ = amount;
    }

    void setInputGain(float gain) {
        ingain_target_ = gain;
        if (input_monitor && in_source == InputSource::RESAMPLE) {
            resamp_env_target_ = ingain_target_;
        }
    }

    void setFinalComp(float amount) {
        if (amount < .5f) {
            saturate_amt_target_ = 1.f;
            saturate_makeup_target_ = 1.f;
            final_lim_target_ = amount * 1.4f;
        }
        else {
            float val = logf(3.4f * (amount - .5f) + 1.f);
            saturate_amt_target_ = val * 100.f + 1.f;
            if (saturate_amt_target_ < 5.f) {
                saturate_makeup_target_ = .25f;
            }
            else if (saturate_amt_target_ < 15.f) {
                saturate_makeup_target_ = .13f;
            }
            else if (saturate_amt_target_ < 24.f) {
                saturate_makeup_target_ = .085f;
            }
            else if (saturate_amt_target_ < 30.f) {
                saturate_makeup_target_ = .072f;
            }
            else if (saturate_amt_target_ < 40.f) {
                saturate_makeup_target_ = .065f;
            }
            else if (saturate_amt_target_ < 52.f) {
                saturate_makeup_target_ = .058f;
            }
            else if (saturate_amt_target_ < 60.f) {
                saturate_makeup_target_ = .05f;
            }
            else if (saturate_amt_target_ < 70.f) {
                saturate_makeup_target_ = .046f;
            }
            else if (saturate_amt_target_ < 75.f) {
                saturate_makeup_target_ = .044f;
            }
            else if (saturate_amt_target_ < 80.f) {
                saturate_makeup_target_ = .044f;
            }
            else if (saturate_amt_target_ < 85.f) {
                saturate_makeup_target_ = .042f;
            }
            else if (saturate_amt_target_ < 88.f) {
                saturate_makeup_target_ = .04f;
            }
            else if (saturate_amt_target_ < 94.f) {
                saturate_makeup_target_ = .04f;
            }
            else if (saturate_amt_target_ < 96.f) {
                saturate_makeup_target_ = .04f;
            }
            else {
                saturate_makeup_target_ = .04f;
            }
            final_lim_target_ = .7f;
        }
    }

    float getVUSample() {
        return output_env_follower.GetLastSamp();
    }

    void setEngine(engineSelection e) {
        engine_ = e;
    }

    size_t getEngine() {
        return engine_;
    }

    void SetInputMonitor(bool monitor) {
        input_monitor = monitor;
        if (in_source == InputSource::RESAMPLE) {
            resamp_env_target_ = monitor ? ingain_target_ : 1.f;
        }
    }

    void incrementMonitorMode() {
        monitor_mode_ = (monitor_mode_ + 1) % 3;
    }

    void setMonitorMode(size_t m) {
        monitor_mode_ = m;
    }

    size_t getMonitorMode() {
        return monitor_mode_;
    }

    void StartNewRecording() {
        record = true;
        sample_manager_->startRecording();
    }

    void StopRecording() {
        record = false;
        sample_manager_->stopRecording();
    }

    bool getRecording() {
        return record;
    }

    void SetInputSource(InputSource source) {
        in_source = source;
    }

    uint8_t getInputSource() {
        return static_cast<uint8_t>(in_source);
    }

    private:
    sampleManager *sample_manager_;
    granularDelay *delay_;
    Reverb *reverb_;
    SimpleCompressor comp_;

    engineSelection engine_;
    bool input_monitor;
    bool record;
    InputSource in_source;
    EnvFollower input_env_follower;
    daisysp::DcBlock dcblock_mic_in_;
    daisysp::DcBlock dcblock_line_in_l_;
    daisysp::DcBlock dcblock_line_in_r_;
    MicFilter mic_filter_;
    float ingain_, ingain_target_;

    float chroma_wet_amt_, chroma_wet_amt_target_;
    float chroma_dry_amt_, chroma_dry_amt_target_;
    float slice_wet_amt_, slice_wet_amt_target_;
    float slice_dry_amt_, slice_dry_amt_target_;

    float sample_rate_;
    size_t monitor_mode_;

    float fx_position_;

    float dry_buffer_[2][48];
    float wet_buffer_[2][48];
    float *dryPtr[2] = { dry_buffer_[0], dry_buffer_[1] };
    float *wetPtr[2] = { wet_buffer_[0], wet_buffer_[1] };

    float final_lim_, final_lim_target_;
    float saturate_amt_, saturate_amt_target_;
    float saturate_makeup_, saturate_makeup_target_;
    float gain_, gain_target_;
    float resamp_env_, resamp_env_target_;
    float reverb_amt_, reverb_amt_target_;
    float reverb_boost_, reverb_boost_target_;
    chompi::Limiter lim_hp_l_;
    chompi::Limiter lim_hp_r_;
    chompi::Limiter lim_line_l_;
    chompi::Limiter lim_line_r_;
    EnvFollower output_env_follower;
};