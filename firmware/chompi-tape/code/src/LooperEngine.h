/** DSPEngine
 *  Core DSP for sampling engine, looping engine, and additional DSP
 */
#pragma once
#include "Sampler.h"
#include "RamBuffer.h"
using namespace daisy;

static const uint32_t kRecordClearTimeout = 2000;
static const uint32_t kButtonTimeout = 10;
namespace daisy
{

    /** @brief Logic for main looper engine */
    class LooperEngine
    {
    public:
        LooperEngine() {}
        ~LooperEngine() {}

        void Init(float sr, RamBufferMemory* loop_buff, bool tape_slew)
        {
            looper.Init(sr, loop_buff, tape_slew);

            record = false;
            first_record = true;
            record_target = false;
            record_arm = false;
            playing = false;

            char name_buffer[32];
            sprintf(name_buffer, "looper.wav");
            looper.Reset();

            fx_env_ = fx_env_target_ = 1.f;
        }

        void FXEnvelope()
        {
            if(IsFirstRecording() && !IsRecording() && !looper.IsResetting())
                return;

            fx_env_target_ = 0.f;
        }

        void Process(float* out_l, float* out_r, size_t size)
        {
            if(IsFirstRecording() && !IsRecording() && !looper.IsResetting())
                return;

            // looper.PerBlock(playing);

            if(fx_env_ < .01f)
                fx_env_target_ = 1.f;

            for(size_t i = 0; i < size; i++)
            {
                daisysp::fonepole(fx_env_, fx_env_target_, .001f);

                if(!first_record || looper.IsResetting())
                {
                    int16_t aol = 0;
                    int16_t aor = 0;

                    looper.PopStereoSamps(f2s16(out_l[i] * fx_env_), f2s16(out_r[i] * fx_env_), &aol, &aor, record, playing);

                    out_l[i] += s162f(aol) * fx_env_;
                    out_r[i] += s162f(aor) * fx_env_;
                }
                else if(!reset)
                {
                    looper.PushStereoSamps(f2s16(out_l[i] * fx_env_), f2s16(out_r[i] * fx_env_));
                }

                if(looper.WriteFullLength())
                {
                    ToggleRecord(); // overdub mode
                    ToggleRecord(); // play mode
                }
            }
        }

        inline bool IsPlaying() { return playing && !first_record; }
        inline bool IsFirstRecording() { return first_record; }

        void IncrementDubGain(float gain) { looper.IncrementDubGain(gain, first_record && !record); }
        inline float GetDubGain() { return looper.GetDubGain(); }

        bool CheckReset()
        {
            if(!looper.IsResetting() && !record_btn && !play_btn)
                reset = false;

            uint32_t elapsed = System::GetNow() - last_button_press;            
            if(elapsed >= kButtonTimeout)
            {
                if(play_btn && !record_btn && !reset && !toggle_play && (record || IsPlaying())) // rising edge and recording or playing
                    TogglePlaying();
                if(record_btn && !play_btn && !reset && !toggle_record)
                    ToggleRecord();            
            }
            if(elapsed >= kRecordClearTimeout)
            {
                if(!record_btn && play_btn && !jump_to_start && !IsPlaying())
                {
                    looper.JumpToStart();
                    jump_to_start = true;
                    return false;
                }
                else if(record_btn && play_btn && !reset)
                {
                    Reset();
                    return true;
                }
            }

            return false;
        }

        void CheckRecordReady()
        {
            if(record_target)
            {
                record = true;
                record_target = false;
                looper.ReadyToRecord();

                if(IsFirstRecording())
                    looper.ResetRamBuff();
            }

            if(play_target)
            {
                playing = true;
                play_target = false;
            }
        }

        bool IsRecording()
        {
            return record;
        }

        inline void SetPitch(float val) { looper.SetVarispeed(val); }
        inline void SetReverse(bool rev) { looper.SetReverse(rev); }
        inline void SetScrub(float scrub) { looper.SetScrub(scrub); }
        inline float GetScrub() { return looper.GetScrub(); }
        inline bool GetReverse() { return looper.GetReverse(); }

        void RecordButton(bool rising)
        {

            record_btn = rising;
            toggle_record = false;
            last_button_press = System::GetNow();

            if(looper.IsResetting() || reset || record_arm)
                return; // short circuit situations
            else if(record_btn && play_btn && first_record && !record)
                record_arm = true;
        }

        void PlayButton(bool rising)
        {
            // falling edge and not recording
            if(play_btn && !rising && !jump_to_start && !record && !toggle_play)
                TogglePlaying();

            toggle_play = false;

            jump_to_start = false;

            play_btn = rising;
            last_button_press = System::GetNow();
         
            if(looper.IsResetting() || reset || record_arm)
                return; // short circuit situations
            else if(record_btn && play_btn && first_record && !record)
                record_arm = true;
        }

        void ToggleRecord(bool play_pressed = false)
        {
            toggle_record = true;
            record_arm = false;
            if(record)
            {
                // don't go into overdub from new record if play is pressed
                if(play_pressed || !first_record)
                {
                    record_target = record = false;
                }

                if(first_record) // reload, then overdub
                {
                    looper.JumpToStart();
                }

                first_record = false;
                play_target = true;
                playing = false;
            }
            else
            {
                if(first_record)
                {
                    looper.WriteJump(sizeof(WAV_FormatTypeDef));
                }
                if (!first_record)
                {
                    playing = true;
                }
                record_target = true;
            }
        }

        void TogglePlaying()
        {
            toggle_play = true;

            if(first_record && !record)
                return; // do nothing on empty looper and not recording

            if(record)
            {
                ToggleRecord(true);
            }
            else
            {
                playing = !playing;
            }
        }

        inline bool GetRecordArm() { return record_arm; }
        inline bool GetIsEmpty() { return !record && first_record; }
        
        float GetPosition() { 
            float tell = static_cast<float>(looper.GetReadTell());
            float size = static_cast<float>(looper.GetReadSize());
            return tell / size;
        }

        float GetPitch()
        {
            return looper.GetVarispeed();
        }

        inline bool GetReset() { return reset; }

        void Reset()
        {
            looper.Reset();
            reset = true;

            record = false;
            first_record = true;
            record_target = false;
            record_arm = false;
            playing = false;
        }

        void OpenFile()
        {
            first_record = false;
            playing = false;
            record = false;
            record_arm = false;
            record_target = false;

            looper.SetVarispeed(1.f, true);
            looper.JumpTo(0);
            looper.ForceSetScrub(0.f);
        }

    private:
        FileSampler looper;
        bool record, first_record, record_target;
        bool playing, play_target;
        uint32_t loop_end;
        uint32_t last_button_press;
        bool record_btn, play_btn;
        bool toggle_record, toggle_play;
        bool record_arm;
        bool reset, jump_to_start;

        float fx_env_, fx_env_target_;
    };
}