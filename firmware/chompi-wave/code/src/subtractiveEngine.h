/** @file subtractiveEngine.h
 *  @brief Synth engine
 *
 *  Note calls come in as KeyRequests from MIDI-in, the keyboard keys, or the Sequencer.
 */
#pragma once
#include "daisy.h"
#include "daisysp.h"
#include "DJFilter.h"
#include "WavetableManager.h"
#include "EnvFollower.h"
#include "limiter.h"
#include "reverb.h"
#include "InterpolatedDelayLine.h"
#include <cmath>

#define NUM_VOICES 8

static constexpr size_t kMaxDelayTime = 48128 * 2; // stereo, > 1 seconds at 48kHz

using namespace daisy;
using namespace daisysp;

// This shouldn't live here, but it's visible where it's needed, so...
static const uint8_t kSlotNone = 100;

struct KeyRequest
{
    enum class Type
    {
        START,
        STOP,
        DUMMY,
    };

    enum class Source
    {
        USER,
        SEQUENCER,
    };

    Type type_;
    float transpose_nn_;
    int key_;
    float vel_;
    Source src_;

    /** constructor for full request data */
    KeyRequest(Type type,
                float transpose_nn,
                int key,
                float vel,
                Source src = Source::USER
              )
        : type_(type),
            transpose_nn_(transpose_nn),
            key_(key),
            vel_(vel),
            src_(src)
    {
    }

    /** Empty, invalid request */
    KeyRequest()
        : type_(Type::DUMMY),
            transpose_nn_(0.f),
            key_(0),
            vel_(127.f),
            src_(Source::USER)
    {
    }


};

class subtractiveVoice {
    public:

    subtractiveVoice() {};
    ~subtractiveVoice() {};

    void Init(float sample_rate, float *filter_lfo_val, float *pitch_lfo_mult) {

        wt.Init(sample_rate);

        amp_env.Init(sample_rate);
        amp_env.SetSustainLevel(1.f); //This won't change

        cutoff_position = .5f;
        filter_.Init(sample_rate);
        filter_.SetControl(.5f);
        filter_.SetRes(0.f);

        filter_lfo_val_ = filter_lfo_val;
        pitch_lfo_mult_ = pitch_lfo_mult;

        activeFromUser = false;
        activeFromSequencer = false;
    }

    void Process(float *sigl, float *sigr) {
        
        wt.setFrequency(frequency * *pitch_lfo_mult_);

        float samp = (wt.PopSample() * .2f * velocity) * amp_env.Process(gate);
        float templ; // These don't need to be zero initialized because they are
        float tempr; // directly assigned values, it doesn't matter if they start as garbage data

        //filter LFO value is 0 when toggled off - additive, same math as the old LFO mode
        filter_.SetControl(cutoff_position + *filter_lfo_val_);
        
        filter_.Process(samp, samp, &templ, &tempr);
        *sigl += templ; // sigl/sigr need to be zero initialized because they are added to, not
        *sigr += tempr; //assigned a value. They need a starting value or they will be garbage data

        return;
    }

    void setFrequency(float freq) {
        wt.setFrequency(freq);
        frequency = freq;
    }

    int prioroty;
    float frequency;
    int key;
    float nn;
    bool gate;
    float cutoff_position;
    DjFilter filter_;
    Adsr amp_env;
    wavetable wt;
    bool activeFromUser;
    bool activeFromSequencer;
    float velocity = 1.f;

    private:
    float *filter_lfo_val_;
    float *pitch_lfo_mult_;

};

class myEngine {
    public:

    myEngine() {};
    ~myEngine() {};

    FIFO<KeyRequest, 64> request_fifo;

    subtractiveVoice myVoices[NUM_VOICES];

    void Init(float sample_rate, InterpolatedDelayLine::AudioSample* del, daisysp::Reverb* reverb, wavetableLoader *wtLoader) {
        for (int i = 0; i < NUM_VOICES; ++i) {
            myVoices[i].Init(sample_rate, &filter_lfo_val, &pitch_lfo_mult);
            myVoices[i].key = -1;
            myVoices[i].prioroty = i + 1;
            myVoices[i].wt.wavetableMemory_ = wtLoader->wavetableMemory_;
            myVoices[i].wt.lastBase_ = wtLoader->wavetableMemory_;
        }

        filterLfo.Init(sample_rate);
        filterLfo.SetWaveform(Oscillator::WAVE_TRI);
        filterLfo.SetAmp(0.f);
        pitchLfo.Init(sample_rate);
        pitchLfo.SetWaveform(Oscillator::WAVE_TRI);
        pitchLfo.SetAmp(0.f);
        // Quarter-cycle offset: at identical rates the two LFOs would otherwise run
        // phase-locked from boot and feel like a single modulation source
        pitchLfo.PhaseAdd(.25f);
        setMasterResonance(.63f);
        setLfoRate(.58f);
        setPitchLfoRate(.58f);

        globalFrequency = .5f;

        del_.Init(del, kMaxDelayTime);
        del_.SetDelay(kMaxDelayTime * .5f);
        setDelayTime(.5f);
        delay_time = delay_time_target;
        reverb_time = reverb_time_target;

        dcblock_fx_l_.Init(sample_rate);
        dcblock_fx_r_.Init(sample_rate);

        reverb_ = reverb;
        wtLoader_ = wtLoader;

        octaveOffset = 0;

        reverb_->Init(sample_rate);
        reverb_->SetAmount(0.f);
        reverb_->SetInputGain(.3f);
        reverb_->SetLowpass(1.f);

        output_env_follower.Init();

        saturate_amt_ = saturate_amt_target_ = 1.f;
        saturate_makeup_ = saturate_makeup_target_ = 1.f;

        lim_hp_l_.Init();
        lim_hp_r_.Init();
        lim_line_l_.Init();
        lim_line_r_.Init();

        pan = pan_target = .5f;

        voice_slot_ = 15;

        file_manager.Init(sample_rate);
        wtLoader_->setFileManager(&file_manager);
    }

    void Prepare() {
        ProcessKeyReqs();
    }

    void stopAllVoices() {
        for (size_t i = 0; i < NUM_VOICES; ++i) {
            myVoices[i].gate = false;
        }
    }

    void Process(const float *const *in, float **out, size_t size) {

        std::fill(out[0], out[0] + size, 0.f);
        std::fill(out[1], out[1] + size, 0.f);

        // Fill from wavetable
        for (size_t i = 0; i < size; ++i) {
            filter_lfo_val = filter_lfo_on ? filterLfo.Process() : 0.f;
            pitch_lfo_mult = pitch_lfo_on
                ? powf(2.f, pitchLfo.Process() * (2.f / 12.f)) // full depth = +/- 2 semitones
                : 1.f;

            float sigl = 0.f;
            float sigr = 0.f;
                for (int voice = 0; voice < NUM_VOICES; ++voice) {
                    myVoices[voice].Process(&sigl, &sigr); // Mono for now
                }
            out[0][i] = sigl / (float)NUM_VOICES;
            out[1][i] = sigr / (float)NUM_VOICES;
        }

        ApplyFX(out[0], out[1], size);

        // Every setting has a target and a real used value so you can directly set
        // it in the UI and then smoothly reach it over many samples. fonepole() is 
        // a one-pole smoothing filter that slews the live value toward its target 
        // over ~1ms so knob turns don't produce audible zipper/stepping artifacts.
        for (size_t i = 0; i < size; ++i) {
            //gain and compress
            fonepole(final_lim_, final_lim_target_, .001f);
            fonepole(saturate_amt_, saturate_amt_target_, .001f);
            fonepole(saturate_makeup_, saturate_makeup_target_, .001f);
            fonepole(gain, gain_target, .001f);

            out[0][i] *= gain;
            out[1][i] *= gain;

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

            output_env_follower.Process((out[0][i] + out[1][i]));
        }


        for (size_t i = 0; i < size; ++i) {
            //pan
            fonepole(pan, pan_target, .001f);

            out[0][i] *= (1.f - pan) * 2.f;
            out[1][i] *= pan * 2.f;

            out[2][i] = out[0][i];
            out[3][i] = out[1][i];
        }

        return;
    }

    void ApplyFX(float* outl, float* outr, size_t size) {

        //Apply FX
        for (size_t i = 0; i < size; ++i) {
            outl[i] = dcblock_fx_l_.Process(outl[i]);
            outr[i] = dcblock_fx_r_.Process(outr[i]);
        }

        for (size_t i = 0; i < size; ++i) {

            fonepole(delay_amount, delay_amount_target, .001f);
            fonepole(delay_feedback, delay_feedback_target, .001f);
            fonepole(reverb_amount, reverb_amount_target, .001f);
            fonepole(delay_time, delay_time_target, .001f);

            del_.SetDelay(delay_time); 

            float del_vol = delay_feedback < .2f ? delay_feedback * 5.f : 1.f;

            InterpolatedDelayLine::AudioSample del_read = del_.Read();

            float delsig_l = s162f(del_read.l) * del_vol;
            float delsig_r = s162f(del_read.r) * del_vol;

            float mono_sum = (outl[i] + outr[i]) * .5f;
            float del_in = mono_sum + delsig_r * powf(delay_feedback, .7f);
            InterpolatedDelayLine::AudioSample del_write = {int16_t(f2s16(del_in)), int16_t(f2s16(delsig_l))};
            del_.Write(del_write);

            float wet_mix = delay_amount > .25f ? .5f : 2.f * delay_amount;
            float dry_mix = delay_amount > .83f ? .5f : (1 - .6f * delay_amount);

            outl[i] = outl[i] * dry_mix + delsig_l * wet_mix;
            outr[i] = outr[i] * dry_mix + delsig_r * wet_mix;

        }

        for (size_t i = 0; i < size; ++i) {
            fonepole(reverb_time, reverb_time_target, .001f);
            reverb_->SetAmount(reverb_amount * reverb_amount * .8f);
            reverb_->SetTime(reverb_time);
            reverb_->SetLowpass(reverb_amount * .6f + .4f);
            reverb_->SetDiffusion(reverb_amount * .6f);

            reverb_->Process(&outl[i], &outr[i]);
        }
    }

    void setGlobalPitch(float amount) {
        //.5 = no change, 0.f = octave down, 1.f = octave up
        globalFrequency = amount;
        for (int i = 0; i < NUM_VOICES; ++i) {
            myVoices[i].setFrequency(keyToFrequency(myVoices[i].nn));
        }

    }

    void setOctave(int value) {
        octaveOffset += value;
        if (octaveOffset > 1) {
            octaveOffset = 1;
        }
        else if (octaveOffset < -1) {
            octaveOffset = -1;
        }
        for (int i = 0; i < NUM_VOICES; ++i) {
            myVoices[i].setFrequency(keyToFrequency(myVoices[i].nn));
        }
    }

    int getOctave() {
        return octaveOffset;
    }

    void setAttack(float amount) {
        // Max attack time 5 seconds for now
        // May want to offset lowest value, not sure if its clicking
        // Is it changing the timing of the key presses? Every 5th note the gate is stunted?
        if (amount < .008) {
            amount = .001;
        }
        for (int i = 0; i < NUM_VOICES; ++i) {
            myVoices[i].amp_env.SetAttackTime(amount * 5.f, 1.f);
        }
    }

    void setRelease(float amount) {
        // Max release time
        if (amount < .008) {
            amount = .005;
        }
        for (int i = 0; i < NUM_VOICES; ++i) {
            myVoices[i].amp_env.SetReleaseTime(amount);
        }
    }



    void setLfoDepth(float amount) {
        filterLfo.SetAmp(amount);
    }

    void setLfoRate(float amount) {
        filterLfo.SetFreq(.14f * powf(65.41f / .14f, amount));
    }

    void setCycle(int8_t direction, bool direct) {
        for (size_t i = 0; i < NUM_VOICES; ++i) {
            myVoices[i].wt.cycleThroughTable(direction, direct);
        }
    }

    uint8_t getCycle() {
        return myVoices[0].wt.curCycle;
    }

    void nextTable(int8_t direction, bool direct) {
        wtLoader_->selectTable(direction, direct);
        for (size_t i = 0; i < NUM_VOICES; ++i) {
            myVoices[i].wt.setTable(wtLoader_->tableBase());
        }
    }

    int getTable() {
        return wtLoader_->getIdx();
    }

    void setGain(float amount) {
        gain_target = amount;
    }

    // This isn't a good way to do it, but I couldn't find a curve/formula that worked nicely
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

    bool isKeyPlaying(int key) {
        for (int i = 0; i < NUM_VOICES; ++i) {
            if (myVoices[i].key == key && myVoices[i].amp_env.IsRunning()) {
                return true;
            }
        }
        return false;
    }

    float getVUSample() {
        return output_env_follower.GetLastSamp();
    }

    void ProcessKeyReqs() {
            if (!request_fifo.IsEmpty())
            {
                KeyRequest req = request_fifo.PopFront();
                
                if (req.type_ == KeyRequest::Type::START) {
                    StartPlayback(req.transpose_nn_, req.key_, req.vel_, req.src_);
                }
                else if (req.type_ == KeyRequest::Type::STOP) {
                    StopPlayback(req.key_, req.src_);
                }
            }
    }

    void setPitchLfoOn(bool on) { pitch_lfo_on = on; }
    void setFilterLfoOn(bool on) { filter_lfo_on = on; }
    bool getPitchLfoOn() { return pitch_lfo_on; }
    bool getFilterLfoOn() { return filter_lfo_on; }

    void setPitchLfoDepth(float amount) {
        pitchLfo.SetAmp(amount);
    }

    void setPitchLfoRate(float amount) {
        //same retuned exponential map as the filter LFO rate below
        pitchLfo.SetFreq(.14f * powf(65.41f / .14f, amount));
    }

    void setMasterCutoff(float amount) {
        master_cutoff = amount; //Needed??
        for (int i = 0; i < NUM_VOICES; ++i) {
            myVoices[i].cutoff_position = amount;
        }
    }

    void setMasterResonance(float amount) {
        amount = fclamp(amount, 0.f, .99f); //This has to be limited
        master_resonance = amount;
        for (int i = 0; i < NUM_VOICES; ++i) {
            myVoices[i].filter_.SetRes(amount);
        }
    }

    void setDelayFeedback(float amount) {
        if (amount < .5f) {
            amount = 1 - amount * 2.f;
            delay_feedback_target = amount * .9;
            delay_amount_target = 1.3f * logf(amount + 1.f);
            reverb_amount_target = 0.f;
        }
        else {
            amount = (amount - .5f) * 2.f;
            reverb_amount_target = 1.3f * logf(amount + 1.f);
            delay_feedback_target = 0.f;
            delay_amount_target = 0.f;
        }

        
    }

    void setDelayTime(float amount) {
        delay_time_target = .99f * powf(amount, 3.f) * kMaxDelayTime + 450;

        reverb_time_target = fclamp(amount, .05f, .97f);
    }

    void setPan(float amount) {
        pan_target = amount;
    }

    void setSwitchState(bool state) {
        switch_state = state;
    }

    bool getSwitchState() {
        return switch_state;
    }

    void setVoiceSlot(uint8_t slot) {
        voice_slot_ = slot;
    }

    int getVoiceSlot() {
        return voice_slot_;
    }

    bool ProcessFileRequests()
    {
        file_manager.ProcessRequests();
        return !file_manager.request_fifo.IsEmpty();
    }

    bool checkLoaded() {
        return file_manager.request_fifo.IsEmpty();
    }

    private:

    /** Assigns a note to one of the subtractiveVoice slots, stealing an
     *  existing voice if all are busy. Each voice has a prioroty so you always
     *  take the voice that was played longest ago if you need a new note. Prefers unused
     *  (gate off, not playing) notes to stealing used notes.
     */
    void StartPlayback(float transpose_nn, int key, float vel, KeyRequest::Source src) {
        // Find if the key is already being played
        int idx = -1;
        for (int i = 0; i < NUM_VOICES; ++i) {
            if (myVoices[i].key == key) {
                idx = i;
                break;
            }
        }
        
        // Retrigger a voice already playing
        if (idx != -1) {
            int p;
            myVoices[idx].gate = true;
            myVoices[idx].velocity = vel * 0.00787401f; // vel/127 (TAPE's constant)
            p = myVoices[idx].prioroty;
            myVoices[idx].prioroty = 0;  // Reset priority for this voice
            if (src == KeyRequest::Source::USER) {
                myVoices[idx].activeFromUser = true;
            }
            else {
                myVoices[idx].activeFromSequencer = true;
            }
            // Shift other priorities
            for (int i = 0; i < NUM_VOICES; ++i) {
                if (myVoices[i].prioroty < p) {
                    myVoices[i].prioroty++;
                }
            }
        }
        // If the key isn't playing, take the lowest priority voice with gate off, or the oldest if all gates are on
        else {
            int voiceToSteal = -1;
            int oldestVoice = -1;
            int maxPriority = -1;

            for (int i = 0; i < NUM_VOICES; ++i) {
                // Find the lowest priority voice with the gate off
                if (!myVoices[i].gate && myVoices[i].prioroty > maxPriority) {
                    voiceToSteal = i;
                    maxPriority = myVoices[i].prioroty;
                }
                // Track the oldest voice (highest priority) in case all gates are on
                if (myVoices[i].prioroty == NUM_VOICES) {
                    oldestVoice = i;
                }
            }

            // If all voices are playing (gates on), steal the oldest one
            if (voiceToSteal == -1) {
                voiceToSteal = oldestVoice;
            }

            // Assign the new note to the selected voice
            if (voiceToSteal != -1) {
                myVoices[voiceToSteal].setFrequency(keyToFrequency(transpose_nn));
                myVoices[voiceToSteal].key = key;
                myVoices[voiceToSteal].velocity = vel * 0.00787401f; // = vel/127
                myVoices[voiceToSteal].nn = transpose_nn;
                myVoices[voiceToSteal].gate = true;
                myVoices[voiceToSteal].prioroty = 0;  // Reset priority
                if (src == KeyRequest::Source::USER) {
                    myVoices[voiceToSteal].activeFromUser = true;
                }
                else {
                    myVoices[voiceToSteal].activeFromSequencer = true;
                }
            }

            // Increase the priority for all other voices
            for (int i = 0; i < NUM_VOICES; ++i) {
                if (i != voiceToSteal) {
                    myVoices[i].prioroty++;
                }
            }
        }
    }


    /** Only actually stops the note once both sources have released it.
     *  If the sequencer and the user trigger the same key, letting
     *  go of the physical key doesn't cut off a note if the sequencer is still holding it
     *  and vice versa. */
    void StopPlayback(int key, KeyRequest::Source src) {
        for (int i = 0; i < NUM_VOICES; ++i) {
            if (myVoices[i].key == key) {
                if (src == KeyRequest::Source::USER) {
                    myVoices[i].activeFromUser = false;
                }
                else {
                    myVoices[i].activeFromSequencer = false;
                }
                if (!myVoices[i].activeFromUser && !myVoices[i].activeFromSequencer) {
                    myVoices[i].gate = false;
                }
                break;
            }
        }
    }

    float keyToFrequency(int transpose_nn) {

        const float referenceFrequency = 440.f;
        const int referenceKey = 57;
        
        // Calculate the note frequency based on transpose_nn
        float baseFrequency = referenceFrequency * pow(2.f, ((transpose_nn + 36 - referenceKey + (octaveOffset * 12)) / 12.f));
        
        // Calculate the fine-tuning amount
        float octaveShift = pow(2.f, (globalFrequency - 0.5f) * 2.f); // -1 octave at 0, +1 octave at 1
        
        // Return the adjusted frequency
        return baseFrequency * octaveShift;
    }

    float globalFrequency;
    int octaveOffset;
    daisysp::Oscillator pitchLfo;
    float filter_lfo_val = 0.f;  
    float pitch_lfo_mult = 1.f;
    bool pitch_lfo_on = true;
    bool filter_lfo_on = true;
    InterpolatedDelayLine del_;
    daisysp::Reverb *reverb_;
    wavetableLoader *wtLoader_;
    EnvFollower output_env_follower;
    chompi::Limiter lim_hp_l_;
    chompi::Limiter lim_hp_r_;
    chompi::Limiter lim_line_l_;
    chompi::Limiter lim_line_r_;
    daisysp::Oscillator filterLfo;
    float final_lim_, final_lim_target_;
    bool switch_state;
    float master_cutoff;
    float master_resonance;
    float delay_feedback, delay_feedback_target;
    float delay_amount, delay_amount_target;
    float saturate_amt_, saturate_amt_target_;
    float saturate_makeup_, saturate_makeup_target_;
    float reverb_amount, reverb_amount_target;
    float reverb_time, reverb_time_target;
    float delay_time, delay_time_target;
    daisysp::DcBlock dcblock_fx_l_, dcblock_fx_r_;
    float gain, gain_target;
    float pan, pan_target;
    uint8_t voice_slot_;

    daisy::FileStreamingManager file_manager;

};