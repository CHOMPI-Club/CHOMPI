#pragma once
#include "daisy_seed.h"
#include "daisysp.h"
#include "fatfs.h"
#include "RamBuffer.h"
#include "Limiter.h"

namespace daisy
{

    class FileSampler
    {
    public:
        void Init(float sr, RamBufferMemory* buff, bool tape_slew)
        {
            ram_buff.Init(buff);
            sr_ = sr;
            varispeed_factor = 1.f;
            scrub_ =  1.f;
            reverse_ = false;
            
            tape_slew_ = tape_slew;

            lim_l_.Init();
            lim_r_.Init();
        }

        void ReadJumpStart()
        {
            ReadJump(reverse_ ? ram_buff.GetSize() : 0);
        }

        void WriteJumpStart()
        {
            if(reverse_)
                WriteJump(ram_buff.GetSize());
            else
                WriteJump(0);
        }

        inline void ReadJump(uint32_t pos = 0) { ram_buff.SetReadHead(pos); }

        void WriteJump(size_t pos)
        {
            ram_buff.SetWriteHead(pos);
        }

        /** Marks current buffer as invalid, and requests new data at beginning of file */
        void JumpTo(uint32_t pos = 0, bool clear_read = true)
        {
            ram_buff.SetWriteHead(pos);
            ram_buff.SetReadHead(pos);
        }

        void JumpToStart()
        {
            ReadJumpStart();
            WriteJumpStart();
        }

        inline void Reset() { reset = true; }    
        inline bool IsResetting() { return reset; }

        uint16_t write_left_a, write_right_a;
        void ReadyToRecord()
        {
            write_left_a = write_right_a = 0;
        }

        int16_t last_l, last_r;

        /** returns true if a sample was popped */
        bool old_rec = false;
        void PopStereoSamps(int16_t inl, int16_t inr, int16_t *out_l, int16_t *out_r, bool recording, bool playing)
        {
            if(!old_rec && recording) // rising edge
            {
                input_env_dec = .001f;
                old_samps_l.Clear();
                old_samps_r.Clear();
                rev_pushback = true; // poorly named var imo
                ram_buff.SetWriteHead(ram_buff.GetReadHead());
            }
            else if(!recording) // falling edge
            {
                input_env_dec = -.001f;
            }

            old_rec = recording;

            {
                float target = playing ? varispeed_factor : scrub_target_;
                if(reset)
                    target = 0.f;

                
                daisysp::fonepole(scrub_, target, tape_slew_ ? .0001f : .01f);
                scrub_ = daisysp::fclamp(scrub_, -2.f, 2.f);

                if((scrub_ < 0.f && target < 0.f) || (scrub_ > 0.f && target > 0.f))
                {
                    // rev_check = 512;
                    if(SetReverse(scrub_ < 0.f))
                    {
                        *out_l = last_l;
                        *out_r = last_r;
                        return;
                    }
                }

                if(ram_buff.GetSize() == 0)
                {
                    reset = false;
                }

                const float abs_scrub = fabsf(scrub_);
                if(reset && abs_scrub < 0.05f)
                {
                    *out_l = last_l * reset_env;
                    *out_r = last_r * reset_env;
                    reset_env -= .0001f;
                    if(reset_env <= 0.f)
                    {
                        loop_env = 0.f;
                        loop_env_dec = loop_env_dec_val; // ~5ms
                        scrub_ = scrub_target_ = 0.f;
                        SetVarispeed(1.f, true);
                        ram_buff.Reset();
                        ForceSetScrub(1.f);
                        old_samps_l.Clear();
                        old_samps_r.Clear();
                        *out_l = *out_r = 0;
                        reset = false;
                        reset_env = 1.f;
                    }

                    return;
                }
                else if(!reset)
                {
                    reset_env += .005f;

                    if(reset_env > 1.f)
                        reset_env = 1.f;
                }

                old_rpos_frac = rpos_frac_;
                rpos_frac_ += abs_scrub;
                if(turn_period_count_ > kTurnPeriodTimeout)
                {
                    if(!tape_slew_)
                    {
                        if(turn_count_ > 0.f)
                            turn_count_ = 5.f * varispeed_factor;
                        else if(turn_count_ < 0.f)
                            turn_count_ = -5.f * varispeed_factor;
                    }
                    
                    scrub_target_ = turn_count_ * .2f;
                    turn_count_ = 0.f;
                    turn_period_count_ = 0;
                }
                else
                    turn_period_count_++;

                if (rpos_frac_ >= 1.f)
                {
                    /** increment that shouldn't be susceptible to f32 precision loss w/ long files*/
                    const uint32_t samp_stride_ = (uint32_t)rpos_frac_;

                    rpos_frac_ -= samp_stride_;

                    for (uint32_t i = 0; i < samp_stride_; i++)
                    {
                        al = bl;
                        ar = br;
                        ram_buff.StereoRead(&bl, &br, reverse_);

                        if(ram_buff.ReadLoop(reverse_))
                        {
                            ReadJumpStart();
                        }
                        else if(ram_buff.GetRemainingRead(reverse_) <= 480 && loop_env_dec > 0.f)
                        {
                            loop_env_dec = -1.f * loop_env_dec_val;
                        }                    

                        // stop a junk sample from being pushed after changing directions
                        if(!rev_pushback)
                        {
                            old_samps_l.PushBack(al);
                            old_samps_r.PushBack(ar);
                        }
                        rev_pushback = false;

                        loop_env += loop_env_dec;
                        const bool ote = (reverse_ && ram_buff.GetReadHead() > (ram_buff.GetSize() - 50))
                                         || (!reverse_ && ram_buff.GetReadHead() < 50);
                        if(loop_env < 0.f && ote)
                        {
                            loop_env = 0.f;
                            loop_env_dec = loop_env_dec_val;
                        }
                    }

                    const float num_samps = old_samps_l.GetNumElements() - 1.f;
                    float pos = (1.f - old_rpos_frac);
                    const float delta = 1.f / (num_samps + rpos_frac_ - old_rpos_frac);
                    while(old_samps_l.GetNumElements() > 1) // leave one sample on the stack
                    {
                        int32_t write_left = old_samps_l.PopFront() * dub_gain_;
                        int32_t write_right = old_samps_r.PopFront() * dub_gain_;

                        input_env += input_env_dec;
                        input_env = daisysp::fclamp(input_env, 0.f, 1.f);

                        if(recording || input_env > .01f)
                        {
                            daisysp::fonepole(dub_gain_, dub_gain_target_, .0001f);

                            const float lerp_val = pos * delta;
                            write_left += (old_inl + (inl - old_inl) * lerp_val) * input_env;
                            write_right += (old_inr + (inr - old_inr) * lerp_val) * input_env;

                            write_left  = f2s16(lim_l_.ProcessHard(s162f(write_left)));
                            write_right = f2s16(lim_r_.ProcessHard(s162f(write_right)));

                            pos += 1.f;
                            ram_buff.StereoWrite(write_left, write_right, reverse_, false);

                            if(ram_buff.WriteLoop(reverse_))
                            {
                                WriteJumpStart();
                            }
                        }
                        else if(ram_buff.GetSize())
                            ram_buff.AdvanceWrite(reverse_);
                    }
                }

                loop_env = daisysp::fclamp(loop_env, 0.f, 1.f);

                rev_env += rev_env_dec; // rev_env_dec should be positive at this point
                rev_env = daisysp::fclamp(rev_env, 0.f, 1.f);

                /** linear interpolation */
                const float tl = al + (bl - al) * rpos_frac_;
                const float tr = ar + (br - ar) * rpos_frac_;

                old_inl = inl;
                old_inr = inr;

                // Amp env
                *out_l = static_cast<int16_t>(tl * loop_env * rev_env * reset_env);
                last_l = *out_l;
                *out_r = static_cast<int16_t>(tr * loop_env * rev_env * reset_env);
                last_r = *out_r;
            }
        }

        // only used for first recording. no varispeed
        inline void PushStereoSamps(int16_t l, int16_t r) { ram_buff.StereoWrite(l, r, reverse_, true); }

        /** @brief sets the rate at which to read from the file
         *  @param speed a value relative to original speed
         *      (e.g. 1.0 for original, 2.0 for double, 0.5 for half)
         *      TODO: negative values will be reverse (unless we setup a diff. mechanism for that).
         */
        void SetVarispeed(float speed, bool force = false)
        {
            varispeed_factor = speed;

            if(force)
            {
                SetReverse(speed < 0.f);
            }
        }

        inline float GetVarispeed() { return varispeed_factor; }

        // reverse functions only used internally (except getter)
        bool SetReverse(bool rev) 
        { 
            const bool change = rev != reverse_;
            if(change)
            {
                old_samps_l.Clear();
                old_samps_r.Clear();
                
                rev_pushback = true;
                WriteJump(ram_buff.GetReadHead());
    
                reverse_ = rev;
            }

            return change;
        }
        inline bool GetReverse() { return reverse_; }


        inline void ForceSetScrub(float s) { scrub_ = s; }
        inline void SetScrub(float addl) { turn_count_ += addl; }
        inline float GetScrub() { return scrub_; }

        inline uint32_t GetReadTell() { return ram_buff.GetReadHead(); }
        inline uint32_t GetReadSize() { return ram_buff.GetSize(); }

        inline bool WriteFullLength() { return ram_buff.WriteFullLength(); }

        void IncrementDubGain(float increment, bool force)
        {
            dub_gain_target_ += increment;
            dub_gain_target_ = daisysp::fclamp(dub_gain_target_, 0.f, 1.f);

            if(force)
                dub_gain_ = dub_gain_target_;
        }
        inline float GetDubGain() { return dub_gain_target_; }

        void ResetRamBuff() { ram_buff.Reset(); }

        RamBuffer ram_buff;
        volatile bool reset;
        float reset_env = 1.f;

        /** Compression for overdub feedback loop */
        chompi::Limiter lim_l_;
        chompi::Limiter lim_r_;

        /** Varispeed handling */
        float varispeed_factor;
        float rpos_frac_;

        /** reverse handling */
        bool reverse_;

        /** scrubbing */
        float scrub_, scrub_target_;
        uint32_t turn_period_count_;
        const size_t kTurnPeriodTimeout = 6000; // 1/8 second
        float turn_count_ = 0.f;

        /** cached varispeed data */
        int16_t old_inl = 0;
        int16_t old_inr = 0;
        float old_rpos_frac = 0.f;

        FIFO<int16_t, 32> old_samps_l;
        FIFO<int16_t, 32> old_samps_r;

        int16_t al, bl = 0;
        int16_t ar, br = 0;

        /** Envelope to avoid clicking over the end */
        float loop_env = 0.f;
        const float loop_env_dec_val = .00464f;
        float loop_env_dec = loop_env_dec_val;

        float input_env;
        float input_env_dec = .001f;

        float rev_env = 1.f;
        float rev_env_dec = .001f;
        float rev_pushback = true;

        float dub_gain_ = 1.f;
        float dub_gain_target_ = 1.f;

        float sr_;

        bool tape_slew_;
    };

} // namespace daisy