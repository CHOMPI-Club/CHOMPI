// Derived from Electrosmith DSP source.

#pragma once
#include "BasicMMF.h"

using namespace chompi;

/** TODO: replace Tone and ATone with something cheap with resonance*/
class DjFilter
{
  public:
    void Init(float samplerate)
    {
        sr_ = samplerate;

        feedback_filt_llp_.Init(samplerate);
        feedback_filt_rlp_.Init(samplerate);
        feedback_filt_lhp_.Init(samplerate);
        feedback_filt_rhp_.Init(samplerate);

        feedback_filt_llp_.SetMode(BasicMMF::Mode::Lowpass);
        feedback_filt_rlp_.SetMode(BasicMMF::Mode::Lowpass);
        feedback_filt_lhp_.SetMode(BasicMMF::Mode::Highpass);
        feedback_filt_rhp_.SetMode(BasicMMF::Mode::Highpass);

        feedback_filt_llp_.SetFreq(.99f);
        feedback_filt_rlp_.SetFreq(.99f);
        feedback_filt_lhp_.SetFreq(0.f);
        feedback_filt_rhp_.SetFreq(0.f);

        feedback_filt_llp_.SetRes(.6f);
        feedback_filt_rlp_.SetRes(.6f);
        feedback_filt_lhp_.SetRes(.6f);
        feedback_filt_rhp_.SetRes(.6f);
    }
    
    void Process(float in_l, float in_r, float *out_l, float* out_r)
    {
        daisysp::fonepole(lp_, lp_target_, .0002f);
        daisysp::fonepole(hp_, hp_target_, .0002f);

        feedback_filt_llp_.SetFreq(lp_);
        feedback_filt_rlp_.SetFreq(lp_);

        feedback_filt_lhp_.SetFreq(hp_);
        feedback_filt_rhp_.SetFreq(hp_);

        if(hp_ > .8f)
        {
            const float param = 5.f * (1.f - cutoff_);
            feedback_filt_lhp_.SetRes(res_ * param);
            feedback_filt_rhp_.SetRes(res_ * param);
        }

        float filt_l = feedback_filt_llp_.Process(in_l);
        float filt_r = feedback_filt_rlp_.Process(in_r);
        
        filt_l = feedback_filt_lhp_.Process(filt_l);
        filt_r = feedback_filt_rhp_.Process(filt_r);
        
        *out_l = filt_l;
        *out_r = filt_r;
    }

    void SetControl(float cutoff) 
    { 
        cutoff_ = cutoff;
        lp_target_ = daisysp::fclamp(.01f + cutoff_ * 2.f, 0.f, .98f); //these have to be limited
        lp_target_ = lp_target_ * lp_target_ * lp_target_;

        hp_target_ = daisysp::fclamp((cutoff_ * 1.9f) - 1.f, 0.f, .9f);
        hp_target_ = hp_target_ * hp_target_ * hp_target_;
    }
    float GetControl() { return cutoff_; }

    void SetRes(float res) 
    {
        res *= .95f;
        res_ = res;
        feedback_filt_llp_.SetRes(res);
        feedback_filt_rlp_.SetRes(res);

        feedback_filt_lhp_.SetRes(res);
        feedback_filt_rhp_.SetRes(res);
    }

    float sr_;
    BasicMMF   feedback_filt_llp_, feedback_filt_rlp_;
    BasicMMF   feedback_filt_lhp_, feedback_filt_rhp_;
    float lp_min, lp_max, hp_min, hp_max;
    float cutoff_, res_;
    float lp_, lp_target_;
    float hp_, hp_target_;
};