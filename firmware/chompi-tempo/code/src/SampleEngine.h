#pragma once
#include "EngineBase.h"
#include "SampleManager.h"
#include "daisysp.h"
#include "daisy.h"
#include "MicFilter.h"
#include "Effects/sampleratereducer.h"
#include "hardware.h"

enum class InputSource
{
    MIC = 0,
    LINE_IN,
    RESAMPLE,
    LAST,
};

class sampleVoice {
    public:

    sampleVoice() {}
    ~sampleVoice() {}

    void Init(float sample_rate) {
        player_.Init(sample_rate);
        amp_env.Init(sample_rate);
        amp_env.SetSustainLevel(1.f);
        amp_env.SetAttackTime(.005f);
        amp_env.SetReleaseTime(.005f);

        sustain = true;
    }

    void setFrequency(float freq) {
        player_.setFrequency(freq);
    }

    void Process(float *sigl, float *sigr) {
        float l = 0.f;
        float r = 0.f;
        if (amp_env.IsRunning()) {
            player_.PopStereoSamps(&l, &r);
        }
        if (!sustain && amp_env.GetCurrentSegment() == daisysp::ADSR_SEG_DECAY) {
            gate = false;
        }
        float amp = amp_env.Process(gate);
        *sigl += l * amp;
        *sigr += r * amp;
    }

    int prioroty;
    float frequency;
    int key;
    float nn;
    bool gate;
    float cutoff_position;
    DjFilter filter_;
    Adsr amp_env;
    Adsr filt_env;
    bool activeFromUser;
    bool activeFromSequencer;
    bool sustain;

    samplePlayer player_;

    private:
};

class sampleEngine : public BaseEngine {
    public:

    sampleVoice myVoices[NUM_VOICES];

    sampleEngine() {}
    ~sampleEngine() {}

    void Init(sampleManager *sample_manager, float sample_rate) override {
        sample_manager_ = sample_manager;

        for (size_t i = 0; i < NUM_VOICES; ++i) {
            myVoices[i].Init(sample_rate);
            myVoices[i].key = -1;
            myVoices[i].prioroty = i + 1;
        }

        lim_l_.Init();
        lim_r_.Init();

        filter_.Init(sample_rate);

        rd_l_.Init();
        rd_l_.SetFreq(1.f);
        rd_r_.Init();
        rd_r_.SetFreq(1.f);

        setMasterCutoff(.5f);
        setMasterRes(0.f);

        reverse = fifth = false;
        encoder_chunk = 0;

        global_pitch_ = 1.f;
        sample_vol_ = sample_vol_target_ = .6f;
        masterCutoff = .5f;
        saturate_amt_ = saturate_amt_target_ = 1.f;

        pan_ = pan_target_ = .5f;

        stored_release_ = .005f;

        reduce_ = false;

        loop = sustain = true;

        voice_slot_ = 1;
    }

    void Process(const float *const *in, float **out, size_t size) override {
        for (size_t i = 0; i < size; ++i) {
            fonepole(sample_vol_, sample_vol_target_, .001f);
            fonepole(pan_, pan_target_, .001f);
            float sigl = 0.f;
            float sigr = 0.f;
            for (size_t voice = 0; voice < NUM_VOICES; ++voice) {
                myVoices[voice].Process(&sigl, &sigr);
            }
            float pan_l = (1.f - pan_) * 2.f;
            float pan_r = pan_ * 2.f;
            out[0][i] = sigl * .125f * sample_vol_ * pan_l;
            out[1][i] = sigr * .125f * sample_vol_ * pan_r;
        }

        float *outl = out[0];
        float *outr = out[1];

        for (size_t i = 0; i < size; ++i) {
            if (reduce_) {
                outl[i] = rd_l_.Process(outl[i]);
                outr[i] = rd_r_.Process(outr[i]);
            }
            else {
                rd_l_.Process(outl[i]);
                rd_r_.Process(outr[i]);
            }
        }

        filter_.SetControl(master_cutoff_);
        for (size_t i = 0; i < size; ++i) {
            filter_.Process(outl[i], outr[i], &outl[i], &outr[i]);
        }
    }

    void Prepare() override {
        ProcessKeyReqs();
    }

    void ProcessKeyReqs() {
        while (!request_fifo.IsEmpty())
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
            if (transpose_nn != myVoices[idx].nn) {
                myVoices[idx].setFrequency(keyToFrequency(transpose_nn));
                myVoices[idx].nn = transpose_nn;
            }
            myVoices[idx].player_.resetPlayer(myVoices[idx].amp_env.IsRunning());
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
                myVoices[voiceToSteal].nn = transpose_nn;
                myVoices[voiceToSteal].gate = true;
                myVoices[voiceToSteal].player_.resetPlayer(myVoices[voiceToSteal].amp_env.IsRunning());
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
        const float referenceFrequency = 440.f; // your base (A4)
        
        // treat transpose_nn=0 as "no shift", +1 = up one semitone, -1 = down one semitone
        return referenceFrequency * pow(2.f, transpose_nn / 12.f);
    }

    void setGlobalPitchFree(float val) override {
        val = val < .5f ? (.5f - val) * -2.f : (val - .5f) * 2.f; // 1 - 0 - 1

        float pitch;
        float inv = val < 0.f ? -1.f : 1.f;
        if(fabsf(val) < .33f) // .01x - .5x
            pitch = val * 1.484848f + .01f * inv;
        else if (fabsf(val) < .66f ) // .5x - 1x
            pitch = (val - .33f * inv) * 1.515151 + .5f * inv;
        else // 1x - 2x
            pitch = (val - .66 * inv) * 2.941176 + 1.f * inv;

        global_pitch_ = pitch;
        reverse = pitch < 0.f;
        for (size_t i = 0; i < NUM_VOICES; ++i) {
            myVoices[i].player_.setReverse(pitch < 0.f);
            myVoices[i].player_.setGlobalFrequency(fabs(pitch));
        }
    }

    float setGlobalPitchQuantized(int16_t turns, float enc_pos) override {
        encoder_chunk += turns * .25f;
            if(encoder_chunk >= 1.f || encoder_chunk <= -1.f) {
                // get current semi
                encoder_chunk = round(encoder_chunk);

                float pitch = global_pitch_;
                float orig_pitch = pitch;

                // snap to fifths and octaves
                // calculate the consts via 2^(x/12) e.g. 2^(-5/12) for down 5 semis
                float mul;
                if(!reverse)
                {
                    if(fifth)
                        mul = encoder_chunk < 0 ? .667419927085f : 1.33483985417f;
                    else
                        mul = encoder_chunk < 0 ? .749153538438f : 1.49830707688f;
                }
                else
                {
                    if(fifth)
                        mul = encoder_chunk < 0 ? 1.33483985417f : .667419927085f;
                    else
                        mul = encoder_chunk < 0 ? 1.49830707688f : .749153538438f;                            
                }

                // jump, then do nothing if we've gone over the end
                pitch *= mul;
                if(pitch > 2.f || pitch < -2.f)
                    return enc_pos;

                fifth = !fifth;
                encoder_chunk = 0.f;

                // handle direction change
                if(pitch < .0625)
                {
                    // we weren't already in the turn-around zone
                    if(orig_pitch > .0625)
                    {
                        fifth = !fifth;
                        pitch = orig_pitch;
                        reverse = !reverse;
                        for (size_t i = 0; i < NUM_VOICES; ++i) {
                            myVoices[i].player_.setReverse(reverse);
                        }
                    }
                    // we were already in the zone, and we're headed over the middle
                    else if((reverse && turns > 0) || (!reverse && turns < 0))
                    {
                        fifth = !fifth;
                        pitch = orig_pitch;
                        reverse = !reverse;
                        for (size_t i = 0; i < NUM_VOICES; ++i) {
                            myVoices[i].player_.setReverse(reverse);
                        }
                    }
                }

                for (size_t i = 0; i < NUM_VOICES; ++i) {
                    myVoices[i].player_.setGlobalFrequency(fabs(pitch));
                }
                global_pitch_ = pitch;

                if(pitch < .5f) // 0 - .33
                    pitch = (pitch - .01) * 0.673469f;
                else if (pitch < 1.f ) // .33 - .66
                    pitch = (pitch - .5f) * .66f + .33f;
                else // .66 - 1
                    pitch = (pitch - 1.f) * .34f + .66f;
                
                return reverse ? (1.f - pitch) * .5f : pitch * .5f + .5f;
            }

        return enc_pos;
    }

    void resetGlobalPitchQuant() override {
        encoder_chunk = 0.f;
        fifth = false;
    }

    bool isKeyPlaying(size_t key) override {
        for (int i = 0; i < NUM_VOICES; ++i) {
            if (myVoices[i].key == key && myVoices[i].amp_env.IsRunning()) {
                return true;
            }
        }
        return false;
    }

    void setSample(size_t slot) override {
        for (size_t i = 0; i < NUM_VOICES; ++i) {
            myVoices[i].player_.setSample(sample_manager_->getNextSample(slot, 0), sample_manager_->getNextSampleSize(slot, 0));
        }
        voice_slot_ = slot + 1;
    }

    void saveFileToSlot(size_t slot) override {
        sample_manager_->saveSlot(slot, 0);
    }

    void deleteSlot(size_t slot) override {
        sample_manager_->deleteSlot(slot, 0);
    }

    void copySlot(size_t src, size_t dst, size_t src_engine, size_t dst_engine) override {
        sample_manager_->copySlot(src, dst, src_engine, dst_engine);
    }

    bool isValidSample(size_t slot) override {
        return sample_manager_->isValidSample(slot, 0);
    }

    void setAttack(float amount) override {
        // if (amount < .008) {
        //     amount = .001;
        // }
        float curved = powf(amount, 3.f);
        float attack = (0.001f + curved * (1.f - 0.001f)) * 5.f;
        for (int i = 0; i < NUM_VOICES; ++i) {
            myVoices[i].amp_env.SetAttackTime(attack, 1.f);
        }
    }

    void setRelease(float amount) override {
        if (amount < .008) {
            amount = .005;
        }
        for (int i = 0; i < NUM_VOICES; ++i) {
            myVoices[i].amp_env.SetReleaseTime(amount);
        }
        stored_release_ = amount;
    }

    void setStartPoint(float val) override {
        for (size_t i = 0; i < NUM_VOICES; ++i) {
            myVoices[i].player_.setStartPoint(val);
        }
    }

    void setEndPoint(float val) override {
        for (size_t i = 0; i < NUM_VOICES; ++i) {
            myVoices[i].player_.setEndPoint(val);
        }
    }

    void setMasterCutoff(float amount) override {
        if (amount > .45 && amount < .55) {
            //dead zone
            master_cutoff_ = .5;
            setMasterRes(0.f);
        }
        else {
            master_cutoff_ = amount < .45 ? amount * 1.111111111f : (amount - .1) * 1.111111111f; //scale back
            master_cutoff_ = fclamp(master_cutoff_, 0.f, 1.f);
            if ((amount > .4 && amount < .45) || (amount > .55 && amount < .6)) {
                setMasterRes(fabsf(0.5f - amount) * 9.f - 0.45f);
            }
            else {
                setMasterRes(.45f);
            }
        }
    }

    void setMasterRes(float amount) override {
        amount = fclamp(amount, 0.f, .99f); //This has to be limited
        filter_.SetRes(amount);
    }

    void setPan(float amount) override {
        pan_target_ = amount;
    }

    void setSampleReducer(float amount) override {
        if (amount > 0.f) {
            amount = (1.0f - amount) * 0.45f;
            amount = fclamp(amount, .01f, 1.f);
            rd_l_.SetFreq(amount);
            rd_r_.SetFreq(amount);
            reduce_ = true;
        }
        else {
            reduce_ = false;
        }
    }

    void setSampleVolume(float amount) override {
        sample_vol_target_ = amount;
    }

    uint8_t getVoiceSlot() override {
        return voice_slot_;
    }

    void setLoop(float amount) override {
        if (amount > .5) {
            if (loop == false) {
                for (size_t i = 0; i < NUM_VOICES; ++i) {
                    myVoices[i].player_.setLoop(true);
                }
                loop = true;
            }
        }
        else {
            if (loop == true) {
                for (size_t i = 0; i < NUM_VOICES; ++i) {
                    myVoices[i].player_.setLoop(false);
                }
                loop = false;
            }
        }
    }

    void setSustain(float amount) override {
        if (amount > .5f) {
            if (sustain == false) {
                for (size_t i = 0; i < NUM_VOICES; ++i) {
                    myVoices[i].sustain = true;
                }
                sustain = true;
            }
        }
        else {
            if (sustain == true) {
                for (size_t i = 0; i < NUM_VOICES; ++i) {
                    myVoices[i].sustain = false;
                }
            }
            sustain = false;
        }
    }

    void handleRetrigEnv(bool on, size_t key) override {
        size_t idx = 0;
        for (size_t i = 0; i < NUM_VOICES; ++i) {
            if (myVoices[i].key == key) {
                idx = i;
                break;
            }
        }
        if (on) {
            myVoices[idx].amp_env.SetReleaseTime(.005);
        }
        else {
            myVoices[idx].amp_env.SetReleaseTime(stored_release_);
        }
    }

    void getActiveKeys(uint16_t *activeKeys, float *nns) override {
        size_t j = 0;
        for (size_t i = 0; i < NUM_VOICES; ++i) {
            if (myVoices[i].gate) {
                activeKeys[j] = myVoices[i].key;
                nns[j] = myVoices[i].nn;
                ++j;
            }
        }
    }

    float chokeVoice(size_t voice) override {
        if (myVoices[voice].gate) {
            myVoices[voice].gate = false;
            return myVoices[voice].nn;
        }
        return -99.f;
    }

    void setLastSlice(bool t) {
        return;
    }

    float masterCutoff;
    float encoder_chunk;

    private:

    chompi::Limiter lim_l_;
    chompi::Limiter lim_r_;
    DjFilter filter_;
    SampleRateReducer rd_l_, rd_r_;
    float pan_, pan_target_;

    float saturate_amt_, saturate_amt_target_;
    float sample_vol_, sample_vol_target_;
    float global_pitch_;
    sampleManager *sample_manager_;
    uint8_t voice_slot_;
    bool reverse, fifth;
    bool loop, sustain;
    bool reduce_;
    float master_cutoff_;

    float stored_release_;
    
};