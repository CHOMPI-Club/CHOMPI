/** DSPEngine
 *  Core DSP for sampling engine, looping engine, and additional DSP
 */
#pragma once
#include "daisy.h"
#include "FileStreamingManager.h"
#include "SampleReader.h"
#include "LooperEngine.h"
#include "daisysp.h"
#include "DJFilter.h"
#include "Warble.h"
#include "PresetManager.h"
#include "EnvFollower.h"
#include "MicFilter.h"
#include "reverb.h"
#include "RamBuffer.h"
#include "Limiter.h"
#include "InterpolatedDelayLine.h"
#include <algorithm>

using namespace daisy;
static constexpr size_t kMaxDelayTime = 48128 * 2; // stereo, > 1 seconds at 48kHz

static constexpr float kLineOutGain = .3f;
static constexpr float kHpGain = .2f;
static constexpr float kMicGain = 5.f;
static constexpr float kLineInGain = 3.f;
static constexpr size_t kMaxPoly = 7;

namespace daisy
{
    enum class VUTarget
    {
        VU_INPUT = 0,
        VU_OUTPUT,
        VU_LAST
    };

    enum class InputSource
    {
        MIC = 0,
        LINE_IN,
        RESAMPLE,
        LAST,
    };

    enum class MonitorMode
    {
        HP = 0,
        BOTH,
        SEND_RET,
        LAST,
    };

    enum class VoiceMode
    {
        JAMMI,
        CUBBI,
        LAST,
    };

    struct KeyRequest
    {
        enum class Type
        {
            START,
            STOP,
            DUMMY,
        };

        Type type_;
        float transpose_nn_;
        int key_;
        float vel_;

        /** constructor for full request data */
        KeyRequest(Type type,
                    float transpose_nn,
                    int key,
                    float vel
                  )
            : type_(type),
                transpose_nn_(transpose_nn),
                key_(key),
                vel_(vel)
        {
        }

        /** Empty, invalid request */
        KeyRequest()
            : type_(Type::DUMMY),
                transpose_nn_(0.f),
                key_(0),
                vel_(127.f)
        {
        }


    };

    // This shouldn't live here, but it's visible where it's needed, so...
    static const uint8_t kSlotNone = 100;
    static size_t const KeyToSlot(size_t buttonID)
    {
        if (buttonID == 7)
            return kSlotNone;
        else if (buttonID < 12)
            return buttonID - 6;
        else if (buttonID < 15)
            return kSlotNone;
        else if (buttonID == 15)
            return 1;
        else if (buttonID < 21)
            return buttonID - 10;
        else if (buttonID < 24)
            return kSlotNone;
        else if (buttonID < 29)
            return buttonID - 13;

        return kSlotNone;
    }


    /** @brief core engine for running entire modules audio
     *
     *  For now now abstraction for various sampling modes,
     *  possibly that can all be handled in the UI.
     *
     *  This will build out CHOMPI Mode and add the looping engine, and other fixed engines
     */
    class Engine
    {
    public:
        Engine() {}
        ~Engine() {}

        FileStreamingManager* GetFileManager()
        {
            return &file_manager;
        }

        void Init(
            float samplerate, 
            daisysp::Reverb* reverb, 
            chompi::InterpolatedDelayLine::AudioSample* del,
            RamBufferMemory* loop_buff,
            RamBufferMemory* chompi_buff,
            bool latch,
            bool tape_slew,
            MonitorMode mon_mode)
        {
            voice_mode = VoiceMode::JAMMI;
            latest_voice = 0;
            monitor_mode = mon_mode;

            input_env_follower.Init();
            output_env_follower.Init();

            for (size_t i = 0; i < size_t(VoiceMode::LAST); i++)
                bank[i] = 0;

            reverb_ = reverb;

            reverb_->Init(samplerate);
            reverb_->SetAmount(0.f);
            reverb_->SetInputGain(.3f);
            reverb_->SetLowpass(1.f);

            del_.Init(del, kMaxDelayTime);
            del_.SetDelay(kMaxDelayTime * .5f);

            SetDelayTime(.5f);
            dly_time_ = dly_time_target_;

            mic_filter_.Init(samplerate);

            filter_.Init(samplerate);
            filter_.SetControl(.5f);
            cutoff_target_ = .5f;
            res_ = res_target_ = 0.f;

            SetSaturate(0.f);

            mgain_ = mgain_target_ = .8f;

            warble_.Init(samplerate);
            warble_.SetFreq(.1f);
            
            dcblock_mic_in_.Init(samplerate);
            dcblock_line_in_l_.Init(samplerate);
            dcblock_line_in_r_.Init(samplerate);
            dcblock_fx_l_.Init(samplerate);
            dcblock_fx_r_.Init(samplerate);

            file_manager.Init(samplerate);

            fx_pre_loop = true;
            fx_env_ = fx_env_target_ = 1.f;
            final_lim_ = 0.f;

            resamp_env_ = resamp_env_target_ = 1.f;

            /* voices */
            for (size_t i = 0; i < kMaxPoly; i++)
            {
                chompi_voice[i].Init(file_manager, samplerate, chompi_buff);
            }
            chompi_writer.Init(chompi_buff);
            record = false;
            record_latch = latch;

            SetVoiceMode(VoiceMode::JAMMI);
            SetBank(0);
            SetVoiceSlot(15, true);

            /* looper */
            looper.Init(samplerate, loop_buff, tape_slew);

            /** Final output compressors */
            lim_hp_l_.Init();
            lim_hp_r_.Init();
            lim_line_l_.Init();
            lim_line_r_.Init();
        }

        // fill chompi buffer with 2 second long cosine
        void FillDefaultSample(float samplerate)
        {
            float rads_l = 1.f;
            float rads_r = 0.f;
            float detune = 1.01f;
            float inc_r = .5f * 523.2511f * (TWOPI_F / samplerate) ; // hz -> increment in rads
            float inc_l = .5f * 523.2511f * detune * (TWOPI_F / samplerate) ;
            float envelope;

            for(size_t i = 0; i < 3 * samplerate; i++)
            {
                if (i > 24000) {
                    float decay_rate = 0.00007195578f;
                    envelope = expf(-decay_rate * (i - 24000));
                }
                else {
                    envelope = 1.f;
                }
                
                const size_t end = (4 * samplerate) - 2000;
                float gain = 1.f;
                if(i < 2000)
                    gain = i / 2000.f;
                else if(i > end)
                    gain = 1.f - (i - end) / 2000.f;

                rads_l += inc_l;
                if(rads_l >= TWOPI_F)
                    rads_l -= TWOPI_F;

                rads_r += inc_r;
                if(rads_r >= TWOPI_F)
                    rads_r -= TWOPI_F;

                float tri_l = 2.0f * fabsf(rads_l / TWOPI_F - 0.5f) - 1.0f;
                float tri_r = 2.0f * fabsf(rads_r / TWOPI_F - 0.5f) - 1.0f;
                // sum and output
                const int16_t sum_l = f2s16(gain * .4f * tri_l * envelope);
                const int16_t sum_r = f2s16(gain * .4f * tri_r * envelope);
                chompi_writer.StereoWrite(sum_l, sum_r, false, true);
            }
        }

        void ApplyFx(float* outl, float* outr, size_t size)
        {
            for(size_t i = 0; i < size; i++)
            {
                // slew controls at audio rate
                fonepole(cutoff_, cutoff_target_, .001f);
                fonepole(res_, res_target_, .001f);
                fonepole(saturate_amt_, saturate_amt_target_, .001f);

                // input gain
                fonepole(fx_env_, fx_env_target_, .001f);
                outl[i] *= fx_env_;
                outr[i] *= fx_env_;

                filter_.SetControl(cutoff_);
                filter_.SetRes(res_);

                outl[i] = dcblock_fx_l_.Process(outl[i]);
                outr[i] = dcblock_fx_r_.Process(outr[i]);

                // filter
                filter_.Process(outl[i], outr[i], &outl[i], &outr[i]);

                // then clip
                outl[i] = daisysp::SoftClip(saturate_amt_ * outl[i]); 
                outr[i] = daisysp::SoftClip(saturate_amt_ * outr[i]); 

                // reduce amplitude to prevent LUFs from blowing up
                const float gain = 1.f - daisysp::SoftClip(.4f * (saturate_amt_ - 1.f)) * .7f;
                outl[i] *= gain;
                outr[i] *= gain;

                // wow and flutter
                warble_.Process(outl[i], outr[i], &outl[i], &outr[i]);
            }

            // delay, in its own block to optimize SDRAM access
            for(size_t i = 0; i < size; i++)
            {
                // fonepole and set controls
                // dly_amt_ is a duplicate of reverb_amt_, but we want to fonepole it in this loop
                fonepole(dly_feedback_, dly_feedback_target_, .001f);
                fonepole(dly_time_, dly_time_target_, .001f);
                fonepole(dly_amt_, dly_amt_target_, .001f); 
                del_.SetDelay(dly_time_); 

                // read
                const float del_vol = dly_feedback_ < .2f ? dly_feedback_ * 5.f : 1.f;
                InterpolatedDelayLine::AudioSample del_read = del_.Read();
                const float delsig_l = s162f(del_read.l) * del_vol;
                const float delsig_r = s162f(del_read.r) * del_vol;

                // write
                const float mono_sum = (outl[i] + outr[i]) * .5f;
                const float del_in = mono_sum + delsig_r * powf(dly_feedback_, .7f);
                const InterpolatedDelayLine::AudioSample del_write = {int16_t(f2s16(del_in)), int16_t(f2s16(delsig_l))};
                del_.Write(del_write);

                // dry/wet mix
                const float wet_mix = dly_feedback_ > .25f ? .5f : 2.f * dly_feedback_; // quickly to 50%
                const float dry_mix = dly_feedback_ > .83f ? .5f : (1 - .6f * dly_feedback_); // slowly to 50%

                outl[i] = outl[i] * dry_mix + delsig_l * wet_mix;
                outr[i] = outr[i] * dry_mix + delsig_r * wet_mix;
            }

            // reverb, in its own block to optimize SDRAM access
            for(size_t i = 0; i < size; i++)
            {
                // fonepole and set controls
                fonepole(reverb_amt_, reverb_amt_target_, .001f);
                fonepole(reverb_time_, reverb_time_target_, .001f);
                reverb_->SetAmount(reverb_amt_ * reverb_amt_ * .8f);
                reverb_->SetTime(reverb_time_);
                reverb_->SetLowpass(reverb_amt_ * .6f + .4f);
                reverb_->SetDiffusion(reverb_amt_ * .6f);

                reverb_->Process(&outl[i], &outr[i]);
            }
        }

        /** Apply the total monitor signal to the input envelope follower */
        void ApplyEnvelopeFollower(size_t size, float* monitor)
        {
            for (size_t i = 0; i < size; i++)
                input_env_follower.Process((monitor[i] + monitor[size + i]) * .8f);
        }

        /** add the mic monitor into both outputs, 
            also add it to the monitor buffer, which will be used to feed the env follower at the end
        */
        void ApplyMicMonitor(const float* const* in, float **out, size_t size, float* monitor)
        {
            for (size_t i = 0; i < size; i++)
            {
                float sig = dcblock_mic_in_.Process(in[0][i] * ingain_ * kMicGain);
                sig = mic_filter_.Process(sig);

                monitor[i] += sig;
                monitor[size + i] += sig;

                out[0][i] += sig;
                out[1][i] += sig;
            }
        }

        /** add the line in monitor into both outputs, 
            also add it to the monitor buffer, which will be used to feed the env follower at the end
        */
        void ApplyLineMonitor(const float* const* in, float **out, size_t size, float* monitor)
        {
            for (size_t i = 0; i < size; i++)
            {
                const float sigl = dcblock_line_in_l_.Process(in[2][i] * ingain_ * kLineInGain);
                const float sigr = dcblock_line_in_r_.Process(in[3][i] * ingain_ * kLineInGain);

                monitor[i] += sigl;
                monitor[size + i] += sigr;

                out[0][i] += sigl;
                out[1][i] += sigr;
            }
        }

        bool looper_reset = false;
        bool CheckReset()
        {
            if(looper_reset)
            {
                looper_reset = false;
                return true;
            }

            return false;
        }

        float old_fx_outl, old_fx_outr;
        void Process(const float *const *in, float **out, size_t size)
        {            
            // we'll use out[0] and out[1] as our working space, 
            // then copy to out[2] and out[3] at the end (and apply gain settings)
            std::fill(out[0], out[0] + size, 0.f);
            std::fill(out[1], out[1] + size, 0.f);

            /** TODO: debug why this is happening */
            if(record != true && record != false)
                record = false;

            for (size_t voice = 0; voice < kMaxPoly; voice++)
                // cache the first block of samples as needed
                chompi_voice[voice].CacheSamples();


            /** voice read*/
            for (size_t voice = 0; voice < kMaxPoly; voice++)
            {
                if (chompi_voice[voice].IsPlaying() || chompi_voice[voice].deferred_trig)
                {
                    for (size_t i = 0; i < size; i++)
                    {
                        float aol = 0.f;
                        float aor = 0.f;

                        chompi_voice[voice].PopStereoSamps(&aol, &aor);
                        out[0][i] += aol;
                        out[1][i] += aor;
                    }
                }
            }

            for(size_t i = 0; i < size; i++)
            {
                out[0][i] = daisysp::SoftClip(out[0][i]);
                out[1][i] = daisysp::SoftClip(out[1][i]);
            }

            // add the monitor to the working space pre-FX
            float monitor[2][size];
            std::fill(&monitor[0][0], &monitor[1][size], 0.f);

            if(monitor_mode == MonitorMode::BOTH)
            {
                if(in_source == InputSource::MIC)
                    ApplyMicMonitor(in, out, size, &monitor[0][0]);
                else if(in_source == InputSource::LINE_IN)
                    ApplyLineMonitor(in, out, size, &monitor[0][0]);
            }
            else if (monitor_mode == MonitorMode::SEND_RET && input_monitor && in_source == InputSource::MIC)
            {
                ApplyMicMonitor(in, out, size, &monitor[0][0]);
            }


            /** looper read + write */
            looper.CheckRecordReady();
            if(looper.CheckReset())
                looper_reset = true;

            //fx_xfade_pre
            if(fx_pre_loop)
                ApplyFx(out[0], out[1], size);

            looper.Process(out[0], out[1], size);

            if(!fx_pre_loop)
                ApplyFx(out[0], out[1], size);

            if(fx_env_ < .01f)
            {
                fx_pre_loop = !fx_pre_loop;
                fx_env_target_ = 1.f;
            }

            //working space (HP) copy to line out
            std::copy(out[0], out[0] + size, out[2]);
            std::copy(out[1], out[1] + size, out[3]);

            // add the dry monitor to the HPs only
            if (monitor_mode == MonitorMode::HP && input_monitor)
            {
                if (in_source == InputSource::MIC)
                    ApplyMicMonitor(in, out, size, &monitor[0][0]);
                if (in_source == InputSource::LINE_IN)
                    ApplyLineMonitor(in, out, size, &monitor[0][0]);
            }
            else if (monitor_mode == MonitorMode::SEND_RET)
            {
                ApplyLineMonitor(in, out, size, &monitor[0][0]);
            }

            // apply resample gain as appropriate (and envelope in/out of that situation)
            if(in_source == InputSource::RESAMPLE)
            {
                for(size_t i = 0; i < size; i++)
                {
                    daisysp::fonepole(resamp_env_, resamp_env_target_, .001f);

                    out[0][i] = monitor[0][i] = resamp_env_ * out[0][i];
                    out[1][i] = monitor[1][i] = resamp_env_ * out[1][i];
        
                    input_env_follower.Process((monitor[0][i] + monitor[1][i]) * .2f);

                    if(monitor_mode == MonitorMode::BOTH)
                    {
                        out[2][i] = out[0][i];
                        out[3][i] = out[1][i];
                    }
                }
            }
            else
            {
                ApplyEnvelopeFollower(size, &monitor[0][0]);
            }


            /** voice write */
            if (record)
            {
                for (size_t i = 0; i < size; i++)
                {
                    const int16_t inl = f2s16(monitor[0][i]);
                    const int16_t inr = f2s16(monitor[1][i]);

                    chompi_writer.StereoWrite(inl, inr, false, true);
                }
            }

            if(record && chompi_writer.WriteFullLength())
                StopRecording();

            /** main gain control */
            for(size_t i = 0; i < size; i++)
            {
                fonepole(mgain_, mgain_target_, .001f);
                fonepole(ingain_, ingain_target_, .001f);
                fonepole(final_lim_, final_lim_target_, .001f);

                // apply headphone and main gain
                out[0][i] *= kHpGain * mgain_;
                out[1][i] *= kHpGain * mgain_;

                // apply lineout and main gain
                out[2][i] *= kLineOutGain * mgain_;
                out[3][i] *= kLineOutGain * mgain_;

                // compress
                const float thresh = 1.f / (10.f * final_lim_ + 4.f);
                const float ratio = 1.f + final_lim_ * final_lim_ * 7.f;
                const float makeup = .9f + final_lim_ * .6f;
                const float pregain = 7.f * final_lim_ + 1.f;
                out[0][i] = lim_hp_l_.ProcessComp(out[0][i], pregain, thresh, ratio, makeup);
                out[1][i] = lim_hp_r_.ProcessComp(out[1][i], pregain, thresh, ratio, makeup);
                out[2][i] = lim_line_l_.ProcessComp(out[2][i], pregain, thresh, ratio, makeup);
                out[3][i] = lim_line_r_.ProcessComp(out[3][i], pregain, thresh, ratio, makeup);

                output_env_follower.Process((out[0][i] + out[1][i]));
            }
        }

        inline void SetInputMonitor(bool monitor) 
        {
            input_monitor = monitor;
            
            if(in_source == InputSource::RESAMPLE)
                resamp_env_target_ = monitor ? ingain_target_ : 1.f;
        }

        inline float GetVUSample(VUTarget target)
        { 
            if(target == VUTarget::VU_INPUT)
                return input_env_follower.GetLastSamp();
            else if(target == VUTarget::VU_OUTPUT)
                return output_env_follower.GetLastSamp();

            return 0.f;
        }

        /** @brief Opens a new file and enables recording for a file
         *  @param slot position in the bank to record the sample (ignored for now)
         *
         *  @todo add delay/preprocessing for declicking
         */
        void StartNewRecording(int slot)
        {
            /** if the looper is overdubbing, stop it */
            if(looper.IsRecording())
                looper.ToggleRecord();

            /** if it was the first recording, stop it overdubbing :\ */
            if(looper.IsRecording())
                looper.ToggleRecord();

            /** Send open file request and toggle record flag */
            char name_buffer[32];
            strcpy(name_buffer, "temp_rec.wav");

            // char name_buffer_dbl[32];
            // strcpy(name_buffer_dbl, "temp_rec_double.wav");

            /** For now we only need to record to the first-voices file..
             *  since all voices will be using the same file for playback
             */
            chompi_writer.Reset();
            record = true;
        }

        /** @brief stops recording, and syncs file
         *  @todo add delay/preprocessing for declicking (fade, etc.)
         */
        void StopRecording()
        {
            for(size_t i = 0; i < kMaxPoly; i++)
            {
                chompi_voice[i].RestoreDefaults();
                chompi_voice[i].StopPlaying();
                chompi_voice[i].CloseFile();

                SetVoiceMode(VoiceMode::JAMMI);
                SetVoiceSlot(15, false);
            }

            record = false;
        }
        inline bool Recording() const { return record; }

        /**
         * @brief Resets the assignment of keyboard keys to voices used in voice stealing algorithm.
         *        Call this when we change voice modes, banks, or slots 
         *        (except for some cases involving the chompi buffer)
         */
        inline void ResetVoiceKeys()
        {
            StopAllVoices();

            for(size_t i = 0; i < kMaxPoly; i++)
                playing_key[i] = 0;
        }

        /** @brief Voice stealing algorithm.
         *  Starts playback of sample in slot at desired playback ratio
         *  @param transpose_nn MIDI note number transposition to play w/ middle C being 0
         */
        void StartPlayback(float transpose_nn, int key, float vel)
        {
            /** Is this key already playing? */
            int free_idx = -1;
            for(size_t i = 0; i < kMaxPoly; i++)
            {
                if(playing_key[i] == key)
                {
                    free_idx = i;
                    break;
                }
            }

            /** Otherwise, take over an available voice with no key assigned */
            if (free_idx < 0)
            {
                for (size_t i = 0; i < kMaxPoly; i++)
                {
                    if (!chompi_voice[i].IsPlaying() && playing_key[i] == 0)
                    {
                        free_idx = i;
                        break;
                    }
                }
            }

            /** Otherwise, take the first available voice we can get */
            if (free_idx < 0)
            {
                for (size_t i = 0; i < kMaxPoly; i++)
                {
                    if (!chompi_voice[i].IsPlaying())
                    {
                        free_idx = i;
                        break;
                    }
                }
            }

            /** No available voice, take over oldest */
            if (free_idx < 0)
            {
                free_idx = 0;
                uint32_t oldest_time = play_start_time[0];
                for(size_t i = 1; i < kMaxPoly; i++)
                {
                    if(play_start_time[i] <= oldest_time)
                    {
                        free_idx = i;
                        oldest_time = play_start_time[i];
                    }
                }
            }

            // we're only using one file for now. That gets opened on init, or on record end
            uint32_t now = System::GetNow();
            play_start_time[free_idx] = now;

            const bool copy_occurred = chompi_voice[free_idx].GetCopyOccurred();
            if(voice_mode == VoiceMode::CUBBI)
            {
                char name_buffer[32];
                char name_buffer_dbl[32];
                size_t slot = KeyToSlot(key);
                if(slot == kSlotNone)
                    return;

                if(!file_exists[1][bank[int(voice_mode)]][slot - 1])
                    return; // no file in that slot

                // reopen the file if it's not a retrigger, or we just switched modes or banks on this voice
                if(slot != 15 && 
                    (playing_key[free_idx] != key 
                        || !chompi_voice[free_idx].GetCubbiMode()
                        || chompi_voice[free_idx].GetBank() != bank[int(voice_mode)]
                        || copy_occurred
                    ))
                {
                    GetFileNameForSlot(slot, bank[int(voice_mode)], voice_mode, name_buffer);
                    GetFileNameForSlot(slot, bank[int(voice_mode)], voice_mode, name_buffer_dbl, true);
                    chompi_voice[free_idx].OpenFile(name_buffer, name_buffer_dbl, true); 
                    chompi_voice[free_idx].SetSlot(slot); 
                    chompi_voice[free_idx].SetUsingRam(false, true); 
                }
                else if(slot == 15)
                    chompi_voice[free_idx].SetUsingRam(true, true); 

                cubbi_slot_ = slot;
                latest_voice = free_idx;
                SetGlobalPitch(fabsf(cubbi_pitch));
                SetReverse(cubbi_pitch < 0.f);
                SetStartPoint(cubbi_start);
                SetEndPoint(cubbi_end);
                SetAttack(cubbi_attack);
                SetDecay(cubbi_decay);
                SetAutoLoop(cubbi_autoloop);
                SetSustainActive(cubbi_sustain);
                SetGain(cubbi_gain);
                SetPan(cubbi_pan);
            }

            playing_key[free_idx] = key;

            // hacks to fix issue with cubbi voices not changing samples sometimes
            chompi_voice[free_idx].SetCubbiMode(voice_mode == VoiceMode::CUBBI);
            chompi_voice[free_idx].SetBank(bank[int(voice_mode)]);

            chompi_voice[free_idx].SetVelocity(vel);

            if(voice_mode == VoiceMode::JAMMI)
                chompi_voice[free_idx].SetVarispeed(MidiNoteToPlaybackRatio(transpose_nn));
            else if(voice_mode == VoiceMode::CUBBI)
                chompi_voice[free_idx].SetVarispeed(1.f);


            chompi_voice[free_idx].StartPlaying();
        }

        void StopPlayback(int key)
        {
            for(size_t i = 0; i < kMaxPoly; i++)
            {
                if(playing_key[i] == key)
                {
                    chompi_voice[i].StopPlaying();
                    // chompi_voice[i].JumpToStart();
                }
            }
        }

        void ProcessKeyReqs()
        {
            if (!request_fifo.IsEmpty())
            {
                KeyRequest req = request_fifo.PopFront();
                
                if (req.type_ == KeyRequest::Type::START)
                    StartPlayback(req.transpose_nn_, req.key_, req.vel_);
                else if (req.type_ == KeyRequest::Type::STOP)
                    StopPlayback(req.key_);
            }
        }

        /** Do SD Card stuff
            return true if there are more requests to complete
        */
        bool ProcessFileRequests()
        {
            file_manager.ProcessRequests();
            return !file_manager.request_fifo.IsEmpty();
        }

        void Prepare()
        {
            ProcessKeyReqs();
        }

        bool IsKeyPlaying(int key)
        {
            for(size_t i = 0; i < kMaxPoly; i++)
            {
                if (playing_key[i] == key && chompi_voice[i].IsPlaying())
                {
                    return true;
                }
            }

            return false;
        }

        void SetAttack(float val)
        {
            val = powf(val, 3.f) + .01f;

            if(voice_mode == VoiceMode::CUBBI)
            {
                chompi_voice[latest_voice].SetAttack(val);
            }
            else
            {
                for(size_t i = 0; i < kMaxPoly; i++)
                    chompi_voice[i].SetAttack(val);            
            }
        }

        void SetDecay(float val)
        {
            val = powf(val, 3.f) + .01f;

            if(voice_mode == VoiceMode::CUBBI)
            {
                chompi_voice[latest_voice].SetDecay(val);
            }
            else
            {
                for(size_t i = 0; i < kMaxPoly; i++)
                    chompi_voice[i].SetDecay(val);            
            }
        }

        void SetStartPointForce(float val)
        {
            if(voice_mode == VoiceMode::CUBBI)
            {
                chompi_voice[latest_voice].SetStartPointForce(val);
            }
            else
            {
                for(size_t i = 0; i < kMaxPoly; i++)
                {
                    chompi_voice[i].SetStartPointForce(val);
                }
            }
        }

        void SetEndPointForce(float val)
        {
            if(voice_mode == VoiceMode::CUBBI)
            {
                chompi_voice[latest_voice].SetEndPointForce(val);
            }
            else
            {
                for(size_t i = 0; i < kMaxPoly; i++)
                {
                    chompi_voice[i].SetEndPointForce(val);
                }
            }   
        }

        bool SetStartPoint(float val)
        {
            bool ret = false;
            if(voice_mode == VoiceMode::CUBBI)
            {
                ret = chompi_voice[latest_voice].SetStartPoint(val);
            }
            else
            {
                for(size_t i = 0; i < kMaxPoly; i++)
                    ret = chompi_voice[i].SetStartPoint(val);            
            }

            return ret;
        }

        bool SetEndPoint(float val)
        {
            bool ret = false;
            if(voice_mode == VoiceMode::CUBBI)
            {
                ret = chompi_voice[latest_voice].SetEndPoint(val);
            }
            else
            {
                for(size_t i = 0; i < kMaxPoly; i++)
                    ret = chompi_voice[i].SetEndPoint(val);            
            }

            return ret;
        }

        void SetAutoLoop(bool loop)
        {
            if(voice_mode == VoiceMode::CUBBI)
            {
                chompi_voice[latest_voice].SetAutoLoop(loop);
            }
            else
            {
                for(size_t i = 0; i< kMaxPoly; i++)
                {
                    chompi_voice[i].SetAutoLoop(loop);
                }
            }        
        }

        void ToggleAutoLoop()
        {
            for(size_t i = 0; i< kMaxPoly; i++)
            {
                chompi_voice[i].ToggleAutoLoop();
            }
        }

        bool GetAutoLoop() { return chompi_voice[latest_voice].GetAutoLoop(); }

        void ResetGlobalPitchQuant()
        {
            fifth = false;
            encoder_chunk = 0.f;
        }

        // returns the value for the encoder tracking
        float SetGlobalPitchQuantized(int16_t turns, float enc_pos)
        {
            encoder_chunk += turns * .25f;
            if(encoder_chunk >= 1.f || encoder_chunk <= -1.f)
            {
                // get current semi
                encoder_chunk = round(encoder_chunk);

                float pitch = GetGlobalPitch();
                float orig_pitch = pitch;

                // snap to fifths and octaves
                // calculate the consts via 2^(x/12) e.g. 2^(-5/12) for down 5 semis
                float mul;
                if(!GetReverse())
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
                        ToggleReverse();
                    }
                    // we were already in the zone, and we're headed over the middle
                    else if((GetReverse() && turns > 0) || (!GetReverse() && turns < 0))
                    {
                        fifth = !fifth;
                        pitch = orig_pitch;
                        ToggleReverse();
                    }
                }

                SetGlobalPitch(pitch);

                if(pitch < .5f) // 0 - .33
                    pitch = (pitch - .01) * 0.673469f;
                else if (pitch < 1.f ) // .33 - .66
                    pitch = (pitch - .5f) * .66f + .33f;
                else // .66 - 1
                    pitch = (pitch - 1.f) * .34f + .66f;
                
                return GetReverse() ? (1.f - pitch) * .5f : pitch * .5f + .5f;
            }

            return enc_pos;
        }

        /** unquantized pitch
            * curved s.t.
            *  0.f - .33f = .01x - .5x
            *  .33f - .66f = .5x - 1x
            *  .66f - 1.f = 1x - 2x
        */
        void SetGlobalPitchFree(float val)
        {
            val = val < .5f ? (.5f - val) * -2.f : (val - .5f) * 2.f; // 1 - 0 - 1

            float pitch;
            float inv = val < 0.f ? -1.f : 1.f;
            if(fabsf(val) < .33f) // .01x - .5x
                pitch = val * 1.484848f + .01f * inv;
            else if (fabsf(val) < .66f ) // .5x - 1x
                pitch = (val - .33f * inv) * 1.515151 + .5f * inv;
            else // 1x - 2x
                pitch = (val - .66 * inv) * 2.941176 + 1.f * inv;

            SetGlobalPitch(fabsf(pitch));
            SetReverse(pitch < 0.f);
        }

        void SetGlobalPitch(float val)
        {
            if(voice_mode == VoiceMode::CUBBI)
            {
                chompi_voice[latest_voice].SetGlobalPitch(val);
            }
            else
            {
                for(size_t i = 0; i < kMaxPoly; i++)
                    chompi_voice[i].SetGlobalPitch(val);            
            }
        }
        inline float GetGlobalPitch() { return chompi_voice[latest_voice].GetGlobalPitch(); }

        void SetGain(float val)
        {
            if(voice_mode == VoiceMode::CUBBI)
            {
                chompi_voice[latest_voice].SetGain(val);
            }
            else
            {
                for(size_t i = 0; i < kMaxPoly; i++)
                    chompi_voice[i].SetGain(val);            
            }
        }

        void SetPan(float val)
        {
            if(voice_mode == VoiceMode::CUBBI)
            {
                chompi_voice[latest_voice].SetPan(val);
            }
            else
            {
                for(size_t i = 0; i < kMaxPoly; i++)
                    chompi_voice[i].SetPan(val);            
            }            
        }

        float GetPan() 
        {
            if(voice_mode == VoiceMode::CUBBI)
                return chompi_voice[latest_voice].GetPan();

            return chompi_voice[0].GetPan();            
        }

        void SetReverse(bool rev)
        {
            if(voice_mode == VoiceMode::CUBBI)
            {
                chompi_voice[latest_voice].SetReverse(rev);
            }
            else
            {
                for(size_t i = 0; i < kMaxPoly; i++)
                    chompi_voice[i].SetReverse(rev);            
            }
        }
        void ToggleReverse() { SetReverse(!chompi_voice[latest_voice].GetReverse()); }
        inline bool GetReverse() { return chompi_voice[latest_voice].GetReverse(); }



        void SetSustainActive(bool sus)
        {
            if(voice_mode == VoiceMode::CUBBI)
            {
                chompi_voice[latest_voice].SetSustainActive(sus);
            }
            else
            {
                for(size_t i = 0; i< kMaxPoly; i++)
                {
                    chompi_voice[i].SetSustainActive(sus);
                }
            }        
        }

        void ToggleSustainActive() 
        {
            for(size_t i = 0; i < kMaxPoly; i++)
            {
                chompi_voice[i].ToggleSustainActive();
            }
        }
        inline bool GetSustainActive() { return chompi_voice[latest_voice].GetSustainActive(); }

        void SetInputGain(float gain) 
        {
            ingain_target_ = gain; 
         
            if(input_monitor && in_source == InputSource::RESAMPLE)
                resamp_env_target_ = ingain_target_;
        }
        
        inline void SetMainGain(float gain) { mgain_target_ = gain; }
        inline void SetFinalComp(float comp) { final_lim_target_ = comp; }
        inline float GetFinalComp() { return final_lim_target_; }

        inline void SetReverb(float val) { dly_amt_target_ = reverb_amt_target_ = 1.3f * logf(val + 1.f); }
        inline void SetDelayFeedback(float val) { dly_feedback_target_ = val * .9f; }
        inline void SetDelayTime(float val) {
            dly_time_target_ = .99f * powf(val, 3.f) * kMaxDelayTime + 450;

            reverb_time_target_ = val;
            reverb_time_target_ = fclamp(reverb_time_target_, .05, .97);
        }

        inline void SetWarble(float val) { warble_.SetFreq(val); }

        inline void SetFilter(float val) { cutoff_target_ = val; }
        inline void SetFilterResonance(float val) { res_target_ = val; }
        inline void SetSaturate(float val) 
        { 
            val = logf(1.7f * val + 1.f); 
            saturate_amt_target_ = val * 13.f + 1.f; 
            // saturate_amt_target_ *= .5f; 
        
        }

        inline bool GetLooperRecordArm() { return looper.GetRecordArm(); }
        inline bool GetLooperIsEmpty() { return looper.GetIsEmpty(); }
        inline float GetLooperPosition() { return looper.GetPosition(); }
        inline float GetLooperPitch() { return looper.GetPitch(); }

        inline void LooperRecordButton(bool rising) { looper.RecordButton(rising); }
        inline void LooperPlayButton(bool rising) { looper.PlayButton(rising); }

        inline void ToggleLooperRecord() { looper.ToggleRecord(); }
        inline void ToggleLooperPlaying() { looper.TogglePlaying(); }
        inline bool GetLooperReverse() { return looper.GetReverse(); }

        void ResetLooperPitchQuant()
        {
            looper_fifth = false;
            looper_encoder_chunk = 0.f;
        }

        float SetLooperPitchQuantized(int16_t turns, float enc_pos)
        {
            if (IsLooperPlaying())
            {
                looper_encoder_chunk += turns * .25f;
                if(looper_encoder_chunk >= 1.f || looper_encoder_chunk <= -1.f)
                {
                    // get current semi
                    looper_encoder_chunk = round(looper_encoder_chunk);

                    float pitch = GetLooperPitch();
                    float orig_pitch = pitch;

                    // snap to fifths and octaves
                    // calculate the consts via 2^(x/12) e.g. 2^(-5/12) for down 5 semis
                    float mul;
                    if(pitch > 0.f)
                    {
                        if(looper_fifth)
                            mul = looper_encoder_chunk < 0 ? .667419927085f : 1.33483985417f;
                        else
                            mul = looper_encoder_chunk < 0 ? .749153538438f : 1.49830707688f;
                    }
                    else
                    {
                        if(looper_fifth)
                            mul = looper_encoder_chunk < 0 ? 1.33483985417f : .667419927085f;
                        else
                            mul = looper_encoder_chunk < 0 ? 1.49830707688f : .749153538438f;                            
                    }

                    // snap to next note, return of out of bounds
                    pitch *= mul;
                    if(pitch > 2.f || pitch < -2.f)
                        return enc_pos;

                    looper_fifth = !looper_fifth;
                    looper_encoder_chunk = 0.f;

                    // handle reverse
                    if(pitch < .0625 && pitch > -.0625)
                    {
                        // we weren't already in the turn-around zone
                        if(orig_pitch > .0625 || orig_pitch < -.0625)
                        {
                            looper_fifth = !looper_fifth;
                            pitch = orig_pitch;
                            pitch *= -1.f;
                        }
                        // we were already in the zone, and we're headed over the middle
                        else if((GetLooperReverse() && turns > 0) || (!GetLooperReverse() && turns < 0))
                        {
                            looper_fifth = !looper_fifth;
                            pitch = orig_pitch;
                            pitch *= -1.f;
                        }
                    }

                    SetLooperPitch(pitch);

                    return pitch * .25f + .5f;

                }
            }

            return enc_pos;
        }

        inline void SetLooperPitchFree(float val)
        {
            SetLooperPitch(val * 4.f - 2.f); // -2 - 2            
        }

        inline void SetLooperPitch(float val) { looper.SetPitch(val); }
        inline void SetLooperScrub(float scrub) { looper.SetScrub(scrub); }
        inline float GetLooperScrub() { return looper.GetScrub(); }

        bool AnyVoicesPlaying() 
        {
            for(size_t i = 0; i < kMaxPoly; i++)
            { 
                if(chompi_voice[i].IsPlaying())
                    return true;
            }

            return false;
        }

        inline bool IsLooperPlaying() { return looper.IsPlaying(); };
        inline bool IsLooperRecording() { return looper.IsRecording(); };
        inline bool IsLooperFirstRecording() { return looper.IsFirstRecording(); };
        inline bool IsLooperRecordArmed() { return looper.GetRecordArm(); };

        inline bool GetRecordLatch() { return record_latch; }

        inline void IncrementLooperDubGain(float gain) { looper.IncrementDubGain(gain); }
        inline float GetLooperDubGain() { return looper.GetDubGain(); }

        inline void LooperOpenFile() { looper.OpenFile(); }

        inline int GetBank() { return bank[int(voice_mode)];}
        inline size_t GetVoiceBank() 
        {
            if(voice_mode == VoiceMode::CUBBI)
                return bank[int(voice_mode)];
            
            return voice_bank_; 
        }
        inline void IncrementBank() 
        {
            bank[int(voice_mode)] = (bank[int(voice_mode)] + 1) % 5;

            if(voice_mode == VoiceMode::CUBBI)
                ResetVoiceKeys();
        }
        inline void SetBank(size_t b) 
        {
            bank[int(voice_mode)] = b % 5;

            if(voice_mode == VoiceMode::CUBBI)
                ResetVoiceKeys();
        }

        daisy::FIFO<KeyRequest, 32> request_fifo;

        /** @brief populates expected filename for a given slot
         *  @param slot position in the bank
         *  @param bank bank number
         *  @param name string to fill with filename
         *
         *  name must be a buffer of at least 16 bytes to fit the name:
         *  - "chompi_xy.wav" where xy is a letter-number combo indicating bank/slot
         */
        static void GetFileNameForSlot(int slot, int b, VoiceMode m, char *name, bool dbl = false)
        {
            char bankchar;
            switch(b)
            {
                case 0:
                    bankchar = 'a';
                    break;
                case 1:
                    bankchar = 'b';
                    break;
                case 2:
                    bankchar = 'c';
                    break;
                case 3:
                    bankchar = 'd';
                    break;
                default:
                    bankchar = 'e';
            }

            char mode[10];

            if(m == VoiceMode::JAMMI)
                strcpy(mode, "jammi");
            else if(m == VoiceMode::CUBBI)
                strcpy(mode, "cubbi");

            char dbl_suffix[10];
            if(dbl)
                strcpy(dbl_suffix, "_double");
            else
                dbl_suffix[0] = '\0';

            if (name)
            {
                sprintf(name, "%s_%c%1d%s.wav", mode, bankchar, slot, dbl_suffix);
            }
        }

        char fname[32];
        char fname_double[32];
        uint8_t erasing = 0;
        void EraseStart(uint8_t target, int b, VoiceMode m)
        {
            SetFileExists(target - 1, b, m, false);

            GetFileNameForSlot(target, b, m, fname);
            GetFileNameForSlot(target, b, m, fname_double, true);

            FileRequest req(FileRequest::Type::UNLINK, nullptr, fname, 0, nullptr, this);
            file_manager.request_fifo.PushBack(req);

            FileRequest double_req(FileRequest::Type::UNLINK, nullptr, fname_double, 0, nullptr, this);
            file_manager.request_fifo.PushBack(double_req);
            erasing += 2;
        }

        inline void EraseFinished()
        { 
            if(erasing != 0)
                erasing--; 
        }

        inline bool IsErasing() { return erasing != 0; }

        inline void SetAllCopyOccurred()
        {
            for(size_t i = 0; i < kMaxPoly; i++)
                chompi_voice[i].SetCopyOccurred();
        }

        void UpdateFileExists()
        {
            for(size_t m = 0; m < 2; m++)
            {
                for(size_t b = 0; b < 5; b++)
                {
                    for(size_t s = 0; s < 15; s++)
                    {
                        if (s >= 14)
                        {
                            file_exists[m][b][s] = true;
                        }
                        else
                        {
                            GetFileNameForSlot(s + 1, b, VoiceMode(m), fname);
                            file_exists[m][b][s] = f_stat(fname, nullptr) == FR_OK;
                        }
                    }
                }
            }
        }

        inline void SetFileExists(size_t slot, size_t bank, VoiceMode mode, bool exists)
        {
            file_exists[int(mode)][bank][slot] = exists;
        }

        inline bool GetFileExists(size_t slot)
        { 
            return file_exists[int(voice_mode)][GetBank()] [slot];
        }

        inline size_t GetVoiceSlot()
        { 
            if(voice_mode == VoiceMode::JAMMI)
                return voice_slot_;
            
            return cubbi_slot_;
        }

        // these should really be in a struct defined in the presets file
        float cubbi_pitch;
        float cubbi_start;
        float cubbi_end;
        float cubbi_attack;
        float cubbi_decay;
        bool cubbi_autoloop;
        bool cubbi_sustain;
        float cubbi_gain;
        float cubbi_pan;

        void OpenCubbiSlot(float pitch, float start, float end, float attack, float decay, bool autoloop, bool sustain, float gain, float pan)
        {
            cubbi_pitch = pitch;
            cubbi_start = start;
            cubbi_end = end;
            cubbi_attack = attack;
            cubbi_decay = decay;
            cubbi_autoloop = autoloop;
            cubbi_sustain = sustain;
            cubbi_gain = gain;
            cubbi_pan = pan;
        }

        void SetVoiceSlot(size_t idx, bool click)
        {
            voice_slot_ = idx;
            
            if(voice_mode == VoiceMode::JAMMI)
            {
                voice_bank_ = bank[int(voice_mode)];
            }

            char name_buffer[32];
            GetFileNameForSlot(idx, bank[int(voice_mode)], voice_mode, name_buffer);

            char name_buffer_dbl[32];
            GetFileNameForSlot(idx, bank[int(voice_mode)], voice_mode, name_buffer_dbl, true);

            for(size_t i = 0; i < kMaxPoly; i++)
            {
                if(idx != 15)
                {
                    chompi_voice[i].OpenFile(name_buffer, name_buffer_dbl, true);
                }

                chompi_voice[i].SetSlot(idx); 
                chompi_voice[i].SetCubbiMode(voice_mode == VoiceMode::CUBBI);
                chompi_voice[i].SetUsingRam(idx == 15, click);
            }
        }

        inline VoiceMode GetVoiceMode () { return voice_mode; }

        inline void SetVoiceMode(VoiceMode m)
        {
            if(voice_mode != m)
                ResetVoiceKeys();

            voice_mode = m;
        }

        void StopAllVoices(size_t idx = 100)
        {
            for(size_t i = 0; i < kMaxPoly; i++)
            {
                if(idx == 100 || idx == chompi_voice[i].GetSlot())
                    chompi_voice[i].Choke();
            }
        }

        // otherwise, they are post looper
        void SetFxPreLooper(bool pre)
        {
            if(pre != fx_pre_loop)
            {
                looper.FXEnvelope();
                fx_env_target_ = 0.f;
            }
        }

        inline bool GetFxPreLooper() { return fx_pre_loop; }

        inline InputSource GetInputSource() { return in_source; }

        void SetInputSource(InputSource source)
        {
            in_source = source;
        }

        inline void IncrementMonitorMode() { monitor_mode = MonitorMode( (int(monitor_mode) + 1) % int(MonitorMode::LAST) ); }
        inline MonitorMode GetMonitorMode() { return monitor_mode; }

        inline bool IsManagerEmpty() __attribute__((optimize("-O0"))) { return file_manager.request_fifo.IsEmpty(); }

        // helper for stuck key hack
        inline int GetPlayingKey(size_t idx) { return playing_key[idx]; }

    private:
        /** @brief Converts a MIDI note number to a ratio of playback speed
         *  @param nn number of MIDI notes to transpose above or below original speed
         *
         *  @todo handle as a value from 0-127 w/ Middle C being 0
         *  @todo handle w/ lookup table to improve performance
         */
        float MidiNoteToPlaybackRatio(int nn) { return pow(2.f, nn * 0.08333333f); } // .0833 = 1/12

        /** the looper */
        LooperEngine looper;

        float encoder_chunk = 0.f; // chunk up the quantized pitch controls
        float looper_encoder_chunk = 0.f;
        bool fifth, looper_fifth;
        
        /** env followers for VU meters */
        EnvFollower input_env_follower;
        EnvFollower output_env_follower;

        /** Sampling Bits */
        daisy::FileStreamingManager file_manager;
        daisy::FileSampleReader chompi_voice[kMaxPoly];
        RamBuffer chompi_writer;

        chompi::Limiter lim_hp_l_;
        chompi::Limiter lim_hp_r_;
        chompi::Limiter lim_line_l_;
        chompi::Limiter lim_line_r_;



        bool record, record_latch;
        bool input_monitor;
        int playing_key[kMaxPoly];

        VoiceMode voice_mode;

        bool file_exists[2][5][15]; // mode, bank, slot
        size_t voice_slot_;
        size_t cubbi_slot_;

        // in jammi mode, which bank is currently active?
        // we can change bank pages without selecting a new slot in that bank
        size_t voice_bank_;

        uint32_t play_start_time[kMaxPoly];

        float ingain_, ingain_target_;
        float resamp_env_, resamp_env_target_;
        float mgain_, mgain_target_;
        float final_lim_, final_lim_target_;

        /** FX */
        MicFilter mic_filter_;
        DjFilter filter_;
        daisysp::Reverb* reverb_;
        chompi::InterpolatedDelayLine del_;
        Warble warble_;
        daisysp::DcBlock dcblock_mic_in_;
        daisysp::DcBlock dcblock_line_in_l_;
        daisysp::DcBlock dcblock_line_in_r_;
        daisysp::DcBlock dcblock_fx_l_;
        daisysp::DcBlock dcblock_fx_r_;

        size_t latest_voice;

        float dly_time_, dly_time_target_;
        float dly_feedback_, dly_feedback_target_;
        float reverb_amt_, reverb_amt_target_;
        float reverb_time_, reverb_time_target_;
        float dly_amt_, dly_amt_target_;
        float saturate_amt_, saturate_amt_target_;
        float cutoff_, cutoff_target_;
        float res_, res_target_;
        
        bool fx_pre_loop;
        float fx_env_, fx_env_target_;

        int bank[int(VoiceMode::LAST)];

        InputSource in_source;
        MonitorMode monitor_mode;

        /** No copy, no assign */
        Engine(const Engine &);
        void operator=(const Engine &);
    };

} // namespace chompi