#pragma once
#include "daisy_seed.h"
#include "daisysp.h"
#include "fatfs.h"
#include "FileStreamingManager.h"
#include "Control/adsr.h"
#include "RamBuffer.h"

namespace daisy
{

    static const uint16_t kMaxClickSamps = 300; // 6ish ms
    static const uint16_t kMinLoopLen = kMaxFileStreamingSamps / 2;

    /** @brief Sampler that uses a file on external media to store the data
     *  This uses a .wav file as the source to be able to be saved/edited
     *  offline as well.
     *
     *  Basic Usage:
     *  1. Initialize with a reference to a FileStreamingManager
     *  2. Call OpenFile with a filename.
     *     a. If the file does not exist it will be created for reading/writing
     *     b. If the file does exist it will be opened for reading/writing.
     *  3. Inside the audio callback, calling PopStereoSamples will fill
     *      two samples of audio data from the file
     *  4. Inside the audio callback, calling PushStereoSamples will queue
     *      two samples of audio data to be written
     *  5. Within the main() infinite loop, call the Process function to
     *      handle fulfilling all of the actual File I/O in the background.
     *
     */
    class FileSampleReader
    {
    public:
        void Init(FileStreamingManager &manager, float sr, RamBufferMemory* buff)
        {
            manager_ = &manager;
            read_requests_ = 0;
            sr_ = sr;

            ram_buff.Init(buff);

            env_.Init(sr);
            env_.SetSustainLevel(1.f);

            RestoreDefaults();

            using_ram = false;
        }

        void SetVelocity(float vel) { velocity = vel * 0.00787401f; }

        void SetAutoLoop (bool loop) { auto_loop = loop; }
        void ToggleAutoLoop () { auto_loop = !auto_loop; }
        bool GetAutoLoop() { return auto_loop; }

        void SetSustainActive(bool sus) { sustain = sus; }
        bool GetSustainActive() { return sustain; }
        void ToggleSustainActive() { sustain = !sustain; }

        inline void SetUsingRam(bool use, bool click) 
        {
            if(using_ram)
            {
                is_buffered = false;
                read_samps.Clear();
            }

            using_ram_target = use;
            if(click)
            {
                CalculateClickSamps();
                deferred_trig = env_gate_ || cubbi_mode;
            }

            note_changed = true;

            if(!env_.IsRunning())
            {
                using_ram = using_ram_target;
            }
        }

        inline void SetStartPointForce(float val) { fstart_ = val; }

        bool SetStartPoint(float val) 
        { 
            if(!reverse_ && fstart_ != val)
            {
                is_buffered = false;
            }

            uint32_t new_start_point = 0;
            uint32_t end_point = 0;
            if(using_ram)
            {
                new_start_point = val * ram_buff.GetSize();
                new_start_point -= (new_start_point % 2);
                new_start_point /= 2;

                end_point = fend_ * ram_buff.GetSize();
                end_point -= (end_point % 2);
                end_point /= 2;
            }
            else
            {
                new_start_point = val * (f_size(&fptr_read) - sizeof(WAV_FormatTypeDef));
                new_start_point -= (new_start_point % 4);
                new_start_point /= 4; // convert bytes to samples

                end_point = fend_ * (f_size(&fptr_read) - sizeof(WAV_FormatTypeDef));
                end_point -= (end_point % 4);
                end_point /= 4; // convert bytes to samples
            }

            bool ret = false;

            // we're smashing the start point into the play head region, which can cause clicks
            if(reverse_ && !using_ram &&
                f_tell(&fptr_read) - read_samps.GetNumElements() * 2 
                    < new_start_point + kMaxFileStreamingSamps * 2)
            {
                // do nothing
            }
            else if(end_point - new_start_point < kMinLoopLen || new_start_point >= end_point) // loop would be too short
            {
                // do nothing
            }
            else
            {
                fstart_ = val;
                ret = true;
            }

            return ret;
        }

        inline uint32_t GetStartPoint() 
        {
            if(using_ram)
            {
                uint32_t start_point = fstart_ * ram_buff.GetSize();
                return start_point - (start_point % 2);
            }
            else
            {
                uint32_t start_point = fstart_ * (f_size(&fptr_read) - sizeof(WAV_FormatTypeDef)) + sizeof(WAV_FormatTypeDef);
                return start_point - (start_point % 4);
            }
        }

        inline void SetEndPointForce(float val) { fend_ = val; }

        bool SetEndPoint(float val) 
        { 
            if(reverse_ && fend_ != val)
            {
                is_buffered = false;
            }

            uint32_t new_end_point = 0;
            uint32_t start_point = 0;
            if(using_ram)
            {
                new_end_point = val * ram_buff.GetSize();
                new_end_point -= (new_end_point % 2);
                new_end_point /= 2;

                start_point = fstart_ * ram_buff.GetSize();
                start_point -= (start_point % 2);
                start_point /= 2;
            }
            else
            {
                new_end_point = val * (f_size(&fptr_read) - sizeof(WAV_FormatTypeDef));
                new_end_point -= (new_end_point % 4);
                new_end_point /= 4; // convert bytes to samples

                start_point = fstart_ * (f_size(&fptr_read) - sizeof(WAV_FormatTypeDef));
                start_point -= (start_point % 4);
                start_point /= 4; // convert bytes to samples
            }
            
            bool ret = false;

            // we're smashing the end point into the play head region, which can cause clicks
            if(!reverse_ && !using_ram &&
                f_tell(&fptr_read) + kMaxFileStreamingSamps > new_end_point)
            {
                // do nothing
            }
            else if(new_end_point - start_point < kMinLoopLen || start_point >= new_end_point) // loop would be too short
            {
                // do nothing
            }
            else
            {
                fend_ = val;
                ret = true;
            }

            return ret;
        }

        inline uint32_t GetEndPoint() 
        {
            if(using_ram)
            {
                uint32_t end_point = fend_ * ram_buff.GetSize();
                return end_point - (end_point % 2);
            }
            else
            {
                uint32_t end_point = fend_ * (f_size(&fptr_read) - sizeof(WAV_FormatTypeDef)) + sizeof(WAV_FormatTypeDef);
                return end_point - (end_point % 4);
            }
        }


        void ResetEnv()
        {
            env_.Init(sr_);
            env_.SetSustainLevel(1.f);
            env_.SetAttackTime(att_time_, 0.f); // linear 1ms to 5s
            env_.SetReleaseTime(rel_time_); // linear 1ms to 5s            
            env_gate_ = false;
        }

        /** Sustain, autoloop, pitch, reverse,
            startpoint, endpoint, attack, decay,
            gain, and pan
        */
        void RestoreDefaults()
        {
            auto_loop = true;
            sustain = true;

            SetGlobalPitch(1.f);
            SetVarispeed(1.f);
            SetReverse(false);

            SetStartPoint(0.f);
            SetEndPoint(1.f);

            SetAttack(.01f);
            SetDecay(.01f);

            // force gain and pan
            gain_ = gain_target_ = .704f;
            pan_raw_ = .5f;
            pan_l_ = pan_l_target_ = 1.f;
            pan_r_ = pan_r_target_ = 1.f;
        }

        /** Mapping: 0-1 = 0x-2x  */
        void SetGain(float val)
        { 
            gain_target_ = 2.f * val * val + .01f;

            if(!IsPlaying())
                gain_ = gain_target_;
        }

        /** Mapping: 0-1 = L-R with .5 being center */
        inline void SetPan(float val)
        {
            pan_raw_ = daisysp::fclamp(val, 0.f, 1.f);
            pan_r_target_ = fminf(2.f * pan_raw_, 1.f);
            pan_l_target_ = fminf(2.f - 2.f * pan_raw_, 1.f);

            if(!IsPlaying())
            {
                pan_l_ = pan_l_target_;
                pan_r_ = pan_r_target_;
            }
        }

        inline float GetPan()  { return pan_raw_; }

        void SetAttack(float val)
        {
            att_time_ = val * 20.f + .001f;
            env_.SetAttackTime(att_time_, 0.f); // linear 1ms to 5s
        }

        void SetDecay(float val)
        {
            rel_time_ = val * 4.f + .001f;
            env_.SetReleaseTime(rel_time_); // linear 1ms to 5s            
        }

        /** Marks current buffer as invalid, and requests new data at beginning of file 
         *  Works relative to start point
        */
        void JumpTo(uint32_t pos = 0)
        {
            if(using_ram)
            {
                ram_buff.SetReadHead(pos);
            }
            else
            {
                FileRequest readjump_req(FileRequest::Type::SEEK, &fptr_read, nullptr, pos, nullptr, this);
                manager_->request_fifo.PushBack(readjump_req);
                seek_requests_++;
            }
        }

        /** Jump to the beginning of the sample + the start point
         *  Does sample math to deal with reverse playback
        */
        void JumpToStart()
        {
            if(reverse_)
            {
                JumpTo(GetEndPoint());
                last_read_size_ = 0;
            }
            else
            {
                JumpTo(GetStartPoint());
            }
        }

        void OpenFile(const char* filename, const char* filename_dbl, bool reset)
        {
            if(fname_rx != filename)
                strcpy(fname_rx, filename);
            if(fname_rx_dbl != filename_dbl)
                strcpy(fname_rx_dbl, filename_dbl);

            bool open_double = varispeed_target * global_pitch > 1.5f;
            double_speed = open_double;

            if(f_size(&fptr_read) != 0)
            {
                FileRequest new_close(FileRequest::Type::CLOSE, &fptr_read, nullptr, 0, nullptr, this);
                manager_->request_fifo.PushBack(new_close);
            }

            if(open_double)
            {
                FileRequest new_req_rx(FileRequest::Type::OPEN, &fptr_read, fname_rx_dbl, 0, nullptr, this);
                manager_->request_fifo.PushBack(new_req_rx);
            }
            else
            {
                FileRequest new_req_rx(FileRequest::Type::OPEN, &fptr_read, fname_rx, 0, nullptr, this);
                manager_->request_fifo.PushBack(new_req_rx);
            }


            if(reset)
            {
                if(env_.IsRunning())
                {
                    CalculateClickSamps();
                    deferred_jump = true;
                }
                else
                {
                    click_env = 1.f;
                    click_dec = .002f;
                    click_clear = 0;
                    is_buffered = false;
                    read_samps.Clear();
                }
            }
            else
            {
                click_env = 1.f;
                click_dec = .002f;
                click_clear = 0;
            }

            open_requests_++;
        }

        void CloseFile()
        {
            FileRequest close_req_rx(FileRequest::Type::CLOSE, &fptr_read, nullptr, 0, nullptr, this);
            manager_->request_fifo.PushBack(close_req_rx);
        }

        // call this post file load. recalculates end point and so on
        void FileOpened()
        {
            SetStartPoint(fstart_);
            SetEndPoint(fend_);
 
            if(open_requests_ != 0)
                open_requests_--;
        }

        /** Reads the next data available in the local FIFO */
        int16_t PopSample() { return 0; }

        void PopStereoSamps(float *l, float *r)
        {
            float env_sig = env_.Process(env_gate_);

            if(open_requests_ == 0 && deferred_jump)
            {
                deferred_jump = false;
                JumpToStart();
            }

            if(env_.GetCurrentSegment() == daisysp::ADSR_SEG_DECAY && !sustain)
            {
                env_gate_ = false;
            }

            if(note_changed || pitch_changed)
            {
                CheckDoubleSpeed(pitch_changed);
                note_changed = pitch_changed = false;
            }

            daisysp::fonepole(gain_, gain_target_, .001f);
            daisysp::fonepole(pan_l_, pan_l_target_, .001f);
            daisysp::fonepole(pan_r_, pan_r_target_, .001f);

            if(pan_l_ < .01f && pan_l_target_ < .01f)
                pan_l_ = 0.f;

            if(pan_r_ < .01f && pan_r_target_ < .01f)
                pan_r_ = 0.f;

            if (((read_samps.GetNumElements() < 16 && !using_ram) 
                || env_sig < .001f)
                && !deferred_trig
            )
            {
                *l = last_l * rev_env;
                *r = last_r * rev_env;
            }
            else
            {
                /** handle with varispeed (positive only) */
                float inc = varispeed_factor * global_pitch;

                if(double_speed_ctr > 0 && click_clear > 0)
                    inc = old_speed;
                
                if(double_speed_ctr == 1 && double_speed)
                    inc = (inc - (1.f - rpos_frac_)) / 2.f + (1.f - rpos_frac_);
                else if(double_speed_ctr == 1 && !double_speed)
                {
                    const float rem = (1.f - rpos_frac_);
                    const float over = std::fmaxf(0.f, (inc * .5f)  - rem);
                    inc = rem + over * 2.f;
                }
                else if((double_speed_ctr > 1 && !double_speed) || (double_speed_ctr == 0 && double_speed))
                    inc *= 0.5f;

                rpos_frac_ += inc;
                if (rpos_frac_ >= 1.f)
                {
                    /** increment that shouldn't be susceptible to f32 precision loss w/ long files*/
                    uint32_t samp_stride = (uint32_t)rpos_frac_;
                    rpos_frac_ -= samp_stride;

                    for (uint32_t i = 0; i < samp_stride; i++)
                    {
                        // click envelope (for when the voice interrupts itself)
                        click_env += click_dec;

                        if(click_dec < 0.f)
                        {
                            if(click_env <= 0.f || (env_sig < .01f && deferred_trig))
                            {
                                click_dec = .004f;

                                if(deferred_trig)
                                {
                                    env_.Retrigger(false); // deferred env retrigger
                                    env_gate_ = true;
                                    deferred_trig = false;
                                }
                                
                                if(using_ram)
                                {
                                    click_clear = 0;
                                    JumpToStart();
                                }

                                read_samps.PopFrontMany(click_clear);
                                varispeed_counter = 0;
                                varispeed_factor = varispeed_target;

                                if(!using_ram)
                                    click_clear = 0;
                            }
                        }
                        click_env = daisysp::fclamp(click_env, 0.f, 1.f);
                        click_clear -= 2;
                        if(click_clear <= 0)
                        {
                            using_ram = using_ram_target;
                            click_clear = 0;
                        }

                        // loop envelope (for when the voice loops over the end)
                        if(loop_env_delay == 0)
                        {
                            loop_env += loop_env_dec;
                            if(loop_env < 0.f)
                            {
                                loop_env = 0.f;
                                loop_env_dec = fabsf(loop_env_dec);

                                if(using_ram && auto_loop)
                                    JumpToStart();
                            }
                        }
                        else
                        {
                            loop_env_delay--;
                        }
                        loop_env = daisysp::fclamp(loop_env, 0.f, 1.f);

                        rev_env += rev_env_dec;
                        if(rev_env < 0.f)
                        {
                            rev_env_dec = fabsf(rev_env_dec);
                        }
                        rev_env = daisysp::fclamp(rev_env, 0.f, 1.f);
                        rev_wait = rev_wait >= 2 ? 2 : rev_wait + 1;

                        varispeed_counter = varispeed_counter == 0 ? 0 : varispeed_counter - 1;
                        if(varispeed_counter == 0)
                            varispeed_factor = varispeed_target;

                        // update samples, B become A, and A get popped from FIFO
                        read_left_a_ = read_left_b_;
                        read_right_a_ = read_right_b_;

                        if(using_ram)
                        {
                            ram_buff.StereoRead(&read_left_b_, &read_right_b_, reverse_);
                        }
                        else
                        {
                            read_left_b_ = read_samps.PopFront();
                            read_right_b_ = read_samps.PopFront();
                        }
                        double_speed_ctr = double_speed_ctr == 0 ? 0 : double_speed_ctr - 1;
                    }
                }

                if(rev_wait == 2)
                {
                    /** linear interpolation */
                    float tl = read_left_a_ + (read_left_b_ - read_left_a_) * rpos_frac_;
                    float tr = read_right_a_ + (read_right_b_ - read_right_a_) * rpos_frac_;                

                    *l = s162f(tl) * env_sig * click_env * loop_env * rev_env * velocity * gain_ * pan_l_;
                    *r = s162f(tr) * env_sig * click_env * loop_env * rev_env * velocity * gain_ * pan_r_;
                    last_l = *l;
                    last_r = *r;
                }
                else
                {
                    *l = last_l * rev_env;
                    *r = last_r * rev_env;
                }

            }

            // Stop playing if we're not auto-looping
            if(OTE() && !auto_loop && env_.IsRunning() && seek_requests_ == 0) 
            {
                ResetEnv();
            }
            else if(using_ram && 
                        (  (reverse_ && ram_buff.GetReadHead() <= GetStartPoint() + kMaxClickSamps)
                        || (!reverse_ && ram_buff.GetReadHead() >= GetEndPoint() - kMaxClickSamps)
                        || (ram_buff.ReadLoop(reverse_)) )
                    )
            {
                loop_env_dec = -2.f / kMaxClickSamps;
            }
            /** if read_samps is < 1/4 full, generate read request */
            else if (read_samps.GetNumElements() < kMaxFileStreamingSamps / 4
                        && read_requests_ == 0 
                        && seek_requests_ == 0
                        && open_requests_ == 0
                        && f_size(&fptr_read) != 0
                        &&!using_ram)
            {
                // how many samples is the FIFO short by?
                uint32_t block_missing = (kMaxFileStreamingSamps - read_samps.GetNumElements()) * sizeof(int16_t);

                bool jump = false;
                if (reverse_)
                {
                    int new_pos = f_tell(&fptr_read); // current position
                    new_pos -= last_read_size_; // previous read position
                    
                    // move the new_pos head back. Check if it's before the file start
                    if(new_pos - int(block_missing) <= int(GetStartPoint()))
                    {
                        // PASSED THE EXIT, JUMP NOW!!
                        if(new_pos <= int(GetStartPoint()))
                        {
                            if(auto_loop)
                            {
                                JumpTo(GetEndPoint());
                                last_read_size_ = 0;
                            }
                            return;
                        }

                        block_missing = abs(int(new_pos) - int(GetStartPoint()));
                        new_pos = GetStartPoint();
                        jump = true;
                    }
                    else if (int(new_pos) - int(block_missing) < 0)
                    {
                        block_missing = new_pos;
                        new_pos = 0;
                        jump = true;
                    }
                    else
                    {
                        new_pos -= block_missing; // new position
                    }

                    // jump to the new read head location
                    JumpTo(new_pos);
                }
                else
                {
                    if(block_missing + f_tell(&fptr_read) >= GetEndPoint())
                    {
                        // PASSED THE EXIT, JUMP NOW!!
                        if(f_tell(&fptr_read) >= GetEndPoint())
                        {
                            if(auto_loop)
                                JumpTo(GetStartPoint());
                            return;
                        }

                        block_missing = GetEndPoint() - f_tell(&fptr_read);
                        jump = true;
                    }
                    else if(block_missing + f_tell(&fptr_read) >= f_size(&fptr_read))
                    {
                        block_missing = f_size(&fptr_read) - f_tell(&fptr_read);
                        jump = true;
                    }
                }

                // request the samples from that spot
                last_read_size_ = block_missing;
                FileRequest::Type type = reverse_ ? FileRequest::Type::REV_READ : FileRequest::Type::READ;
                FileRequest new_req(type,
                                    &fptr_read,
                                    nullptr,
                                    block_missing,
                                    &read_samps,
                                    this);
                manager_->request_fifo.PushBack(new_req);
                read_requests_++;

                if(jump)
                {
                    if(read_samps.IsEmpty())
                    {
                        loop_env_dec = -.0042f; // ~ 5ms
                        loop_env = 0.f;
                    }
                    else
                    {
                        const size_t max_samps = 480; // ~10ms stereo samples
                        size_t total = read_samps.GetNumElements() / 2 + block_missing / 4;
                        loop_env_delay = total > max_samps ? total - max_samps : 0;
                        total = total > max_samps ? max_samps : total;
                        loop_env_dec = -1.f / float(total);
                    }                    

                    if(reverse_ && auto_loop)
                    {
                        JumpTo(GetEndPoint());
                        last_read_size_ = 0;
                    }
                    else if(auto_loop)
                    {
                        JumpTo(GetStartPoint());
                    }                
                }
            }
        }

        bool OTE()
        {
            if(deferred_jump)
                return false;

            // need envelope down still, since they can edit the start/end points to a click spot
            bool ret = false;
            if(reverse_)
            {
                if(using_ram)
                    ret = ram_buff.GetReadHead() <= GetStartPoint();
                else
                    ret = f_tell(&fptr_read) <= (GetStartPoint() + 2 * kMaxFileStreamingSamps + 4) 
                        && read_samps.GetNumElements() < 16;
            }
            else
            {
                if(using_ram)
                    ret = ram_buff.GetReadHead() >= GetEndPoint();
                else
                    ret = f_tell(&fptr_read) >= (GetEndPoint() - 4) && read_samps.GetNumElements() < 16;
            }
    
            return ret;
        }


        /** is_knob tells us whether the knob is being turned, or a new key is pressed */
        void CheckDoubleSpeed(bool is_knob)
        {
            if(using_ram && using_ram_target)
            {
                double_speed = false;
                return;
            }

            if(is_knob && double_speed_ctr > 0)
                return;

            bool to_double = varispeed_target * global_pitch > 1.5f && !double_speed;
            bool to_normal = varispeed_target * global_pitch <= 1.5f && double_speed;
            if(to_double || to_normal)
            {
                int pos = sizeof(WAV_FormatTypeDef);

                if(is_knob)
                {
                    if(f_tell(&fptr_read) > sizeof(WAV_FormatTypeDef))
                    {
                        pos = (f_tell(&fptr_read) - sizeof(WAV_FormatTypeDef));

                        if(reverse_)
                        {
                            pos -= last_read_size_;
                            if(pos < 0)
                                pos += (f_size(&fptr_read) - sizeof(WAV_FormatTypeDef));
                        }

                        if(to_double)
                        {
                            if(pos % 8 == 0)
                            {
                                if(!reverse_)
                                {
                                    // drop last sample
                                    read_samps.Remove(read_samps.GetNumElements() - 1);
                                    read_samps.Remove(read_samps.GetNumElements() - 1);
                                }
                            }
                            else
                                pos += pos % 8;
                        }

                        pos = to_double ? pos / 2 : pos * 2;

                        if(!reverse_ && !to_double)
                            pos -= 4;

                        pos += sizeof(WAV_FormatTypeDef);

                        // last_read_size_ = to_double ? last_read_size_ / 2 : last_read_size_ * 2;
                        // last_read_size_ -= last_read_size_ % 4;
                        last_read_size_ = 0;
                    }
                }

                CloseFile();
                OpenFile(fname_rx, fname_rx_dbl, !is_knob);
                JumpTo(pos);

                if(click_clear > 0)
                    double_speed_ctr = click_clear < kMaxClickSamps ? click_clear / 2 : kMaxClickSamps / 2;
                else
                    double_speed_ctr = read_samps.GetNumElements() / 2 + 1;

                if(reverse_ && to_double && 
                    f_size(&fptr_read) - sizeof(WAV_FormatTypeDef) % 8 != 0) // not on 2x grid
                {
                    double_speed_ctr++;                    
                }
            }
        }

        /** Quick hacks to fix a wrong voice bug*/
        bool cubbi_mode = false;
        void SetCubbiMode(bool mode) { cubbi_mode = mode; }
        bool GetCubbiMode() { return cubbi_mode; }

        uint8_t bank = 0;
        void SetBank(uint8_t b) { bank = b; }
        uint8_t GetBank() { return bank; }

        uint8_t slot = 15;
        void SetSlot(uint8_t s) { slot = s; }
        uint8_t GetSlot()
        {
            return slot;
        }

        /** @brief sets the rate at which to read from the file
         *  @param speed a value relative to original speed
         *      (e.g. 1.0 for original, 2.0 for double, 0.5 for half)
         *      TODO: negative values will be reverse (unless we setup a diff. mechanism for that).
         */
        inline void SetVarispeed(float speed)
        {
            if(double_speed_ctr == 0)
                old_speed = varispeed_factor * global_pitch;
    
            if(varispeed_factor == 0.f || !env_.IsRunning())
            {
                varispeed_factor = speed;
                varispeed_target = speed;
                varispeed_counter = 0;
            }
            else
            {
                varispeed_target = speed;
                varispeed_counter = read_samps.GetNumElements() / 2;
            }

            note_changed = true;
        }

        inline void SetGlobalPitch(float speed)
        {
            if(double_speed_ctr == 0)
                old_speed = varispeed_factor * global_pitch;
        
            global_pitch = speed;
            pitch_changed = true;
        }
        inline float GetGlobalPitch() { return global_pitch; }

        void SetReverse(bool rev) 
        { 
            if(reverse_ != rev)
            {
                is_buffered = false;

                reverse_ = rev;
                if(!read_samps.IsEmpty() && IsPlaying())
                {
                    rev_env_dec = -2.f / read_samps.GetNumElements();
                    // rev_env = 1.f;
                }
            }
        }
        inline bool GetReverse () { return reverse_; }

        void DecrementSeekRequests()
        {
            if(seek_requests_ > 0)
                seek_requests_--;
        }


        inline size_t GetReadRequests() { return read_requests_; }
        inline size_t GetSeekRequests() { return seek_requests_; }

        void DecrementReadRequests()
        {
            if(read_requests_ > 0)
                read_requests_--;
        }

        void CorrectRequestCounts()
        {
            read_requests_ = manager_->GetNumReqs(this, FileRequest::Type::READ);
            read_requests_ += manager_->GetNumReqs(this, FileRequest::Type::REV_READ);

            seek_requests_ = manager_->GetNumReqs(this, FileRequest::Type::SEEK);
        }

        void CalculateClickSamps()
        {
            deferred_trig = true;
            click_dec = .002f;
            click_clear = 0;

            if(using_ram && env_.IsRunning())
            {
                click_dec = -2.f / kMaxClickSamps;
                click_clear = kMaxClickSamps;                
            }
            else if(!read_samps.IsEmpty())
            {
                click_dec = -2.f / kMaxClickSamps;
                click_clear = read_samps.GetNumElements();

                if(click_clear < kMaxClickSamps)
                {
                    click_dec = -2.f / read_samps.GetNumElements(); // calculate a short env
                }
            }
        }

        bool deferred_jump;
        bool deferred_trig;
        void StartPlaying() {
            if((read_requests_ != 0 || seek_requests_ != 0) && open_requests_ == 0)
            {
                manager_->ClearRequests(this);
                read_requests_ = 0;
                seek_requests_ = 0;
                open_requests_ = 0;
            }

            const bool is_running = env_.IsRunning();

            if(is_running && click_clear == 0)
            {
                CalculateClickSamps();
                if(!using_ram)
                    JumpToStart();
            }
            else if(!is_running)
            {
                click_env = 0.f;
                click_dec = .01f;
                click_clear = 0;
                rev_wait = 0;
                last_l = last_r = 0.f;

                env_.Retrigger(false);

                if(!is_buffered)
                {
                    deferred_jump = true;
                    read_samps.Clear();
                }
                // if we buffered the first block, don't jump to start!

                env_gate_ = true;
            }

            // Hack fixes issue with short samples being silent on second button press
            loop_env_delay = 0;
            if(loop_env_dec < 0.f)
                loop_env_dec *= -1.f;

            is_buffered = false;
        }
        
        inline void StopPlaying() {  env_gate_ = false; deferred_trig = false; }

        inline void Choke()
        {
            SetDecay(.01f);
            env_gate_ = false;
            CalculateClickSamps();
            deferred_trig = false;
        }


        // just checks the value without doing other stuff
        inline bool IsPlaying()
        {
            return env_.IsRunning() || env_gate_;
        }

        bool is_buffered = false;
        uint32_t last_cache;
        inline void CacheSamples() {             
            const bool run = env_.IsRunning() || env_gate_;
            const uint32_t now = System::GetNow();

            if(!run && !is_buffered && open_requests_ == 0
                && f_size(&fptr_read) != 0 && !using_ram_target && now - last_cache > 2000)
            {
                last_cache = now;
                read_samps.Clear();

                // TODO: this can get bad if we have the wrong double/not double file loaded
                FileRequest::Type type;
                if(reverse_)
                {
                    JumpTo(GetEndPoint() - kMaxFileStreamingSamps * 2);
                    type = FileRequest::Type::REV_READ;
                }
                else
                {
                    JumpTo(GetStartPoint());
                    type = FileRequest::Type::READ;
                }

                last_read_size_ = kMaxFileStreamingSamps * 2;
                FileRequest new_req(type,
                                    &fptr_read,
                                    nullptr,
                                    kMaxFileStreamingSamps * 2,
                                    &read_samps,
                                    this);
                manager_->request_fifo.PushBack(new_req);
                read_requests_++;
                is_buffered = true;
            }
            
            if(!run)
            {
                using_ram = using_ram_target;
            }
        }

        inline size_t GetSize() { return ram_buff.GetSize(); }

        FileStreamingManager *manager_;
        FIFO<int16_t, kMaxFileStreamingSamps> read_samps;

        FIL fptr_read;
        char fname_rx[32];
        char fname_rx_dbl[32];
        size_t read_requests_;
        size_t seek_requests_;
        size_t open_requests_;
        size_t last_read_size_;

        /** 2x speed file */
        bool double_speed;
        uint32_t double_speed_ctr;
        float old_speed;
        bool note_changed, pitch_changed;

        /** Varispeed handling */
        float varispeed_factor, varispeed_target;
        float global_pitch;
        uint32_t varispeed_counter;

        bool reverse_;
        float rev_env = 1.f;
        float rev_env_dec = .01f;
        uint8_t rev_wait;

        float gain_, gain_target_;
        float pan_raw_, pan_l_, pan_l_target_, pan_r_, pan_r_target_;

        float rpos_frac_;

        /** audio-cache for varispeed playback */
        int16_t read_left_a_, read_left_b_;
        int16_t read_right_a_, read_right_b_;

        bool env_gate_;
        float click_env = 1.f;
        float click_dec = .01f;

        float loop_env = 1.f;
        float loop_env_dec = .01f;
        size_t loop_env_delay = 0;

        float last_l, last_r;

        bool copy_occurred = false;
        inline void SetCopyOccurred() { copy_occurred = true; }

        /** check if this voice has not been played since a copy to a cubbi/jammi slot occurred.
            If so, we must force reload the file under cubbi mode.
            (i.e. we're not retriggering a voice that already has the correct file loaded).
        */
        inline bool GetCopyOccurred() 
        { 
            const bool ret = copy_occurred;
            copy_occurred = false;
            return ret;
        }

        /** DSP objects */
        float sr_;
        daisysp::Adsr env_;
        float att_time_, rel_time_;
        bool auto_loop, sustain;
        float fstart_, fend_;

        int click_clear;

        float velocity = 1.f;

        /** Chompi buffer*/
        RamBuffer ram_buff;
        bool using_ram, using_ram_target;
    };

} // namespace daisy