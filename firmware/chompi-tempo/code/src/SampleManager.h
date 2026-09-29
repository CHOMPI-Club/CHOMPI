#pragma once
#include "daisy.h"
#include "daisysp.h"
#include "FileStreamingManager.h"
#include "SampleInfo.h"

#define MAX_SLOTS 14

class sampleManager {
    public:
    sampleManager() {}
    ~sampleManager() {}

    void Init(void *start_addr, FileStreamingManager *fm, float sr) {

        sdram_start_addr_ = start_addr + 7680000; // Skip first ~4MB (960,000 floats) We need to skip over where delay is

        sdram_buff_ptr_ = sdram_start_addr_;
        sdram_buff_end_ = sdram_buff_ptr_ + 1920000; // 10 seconds of stereo 2 byte int16 samples

        sdram_write_ptr_ = sdram_buff_end_; //Preset samples go at the end of buffer

        for (size_t eng = 0; eng < 2; ++eng) {
            for (size_t i = 0; i < MAX_SLOTS; ++i) {
                loadedSamples[eng][i].start = nullptr;
                loadedSamples[eng][i].numSamples = 0;
                loadedSamples[eng][i].name.clear();
                loadedSamples[eng][i].fullPath.clear();
                loadedSamples[eng][i].sampleRate = 0;
                loadedSamples[eng][i].status = SampleStatus::EMPTY;
            }
        }

        file_manager_ = fm;
        file_manager_->Init(sr);

        loadFileInfo();
        bufferFilled = false;
    }
    
    void loadFileInfo() {
        DIR dir;
        FILINFO fno;
        FRESULT res;

        res = f_opendir(&dir, "/Chromatic");
        if (res == FR_OK) {
            while (true) {
                res = f_readdir(&dir, &fno);
                if (res != FR_OK || fno.fname[0] == 0) {
                    break;
                }
                if (strstr(fno.fname, "chroma_a") == fno.fname && strstr(fno.fname, ".wav") && fno.fname[0] != '.') {
                    const char* numberPart = fno.fname + strlen("chroma_a");
                    int slotNum = atoi(numberPart);
                    if (slotNum >= 1 && slotNum <= 14) {
                        loadedSamples[0][slotNum - 1].name = fno.fname;
                        loadedSamples[0][slotNum - 1].fullPath = "/Chromatic/" + loadedSamples[0][slotNum - 1].name;
                        loadedSamples[0][slotNum - 1].status = SampleStatus::LOADING;
                    }
                }
            }
            f_closedir(&dir);
        }

        res = f_opendir(&dir, "/Slice");
        if (res == FR_OK) {
            while (true) {
                res = f_readdir(&dir, &fno);
                if (res != FR_OK || fno.fname[0] == 0) {
                    break;
                }
                if (strstr(fno.fname, "slice_a") == fno.fname && strstr(fno.fname, ".wav") && fno.fname[0] != '.') {
                    const char* numberPart = fno.fname + strlen("slice_a");
                    int slotNum = atoi(numberPart);
                    if (slotNum >= 1 && slotNum <= 14) {
                        loadedSamples[1][slotNum - 1].name = fno.fname;
                        loadedSamples[1][slotNum - 1].fullPath = "/Slice/" + loadedSamples[1][slotNum - 1].name;
                        loadedSamples[1][slotNum - 1].status = SampleStatus::LOADING;
                    }
                }
            }
            f_closedir(&dir);
        }

        res = f_opendir(&dir, "/Buffer");
        if (res == FR_OK) {
            while (true) {
                res = f_readdir(&dir, &fno);
                if (res != FR_OK || fno.fname[0] == 0) {
                    break;
                }
                if (strcmp(fno.fname, "buffer.wav") == 0) {
                        loadedSamples[0][14].name = fno.fname;
                        loadedSamples[0][14].fullPath = "/Buffer/" + loadedSamples[0][14].name;
                        loadedSamples[0][14].status = SampleStatus::LOADING;
                }
            }
            f_closedir(&dir);
        }
    }

    void loadFileData() {
        for (size_t i = 0; i < MAX_SLOTS; ++i) {

            if (!loadedSamples[0][i].name.empty()) {                

                FileRequest openReq(FileRequest::Type::OPEN, &testFile, loadedSamples[0][i].fullPath.c_str(), 0, nullptr, nullptr, nullptr);
                file_manager_->request_fifo.PushBack(openReq);

                FileRequest parseReq(FileRequest::Type::PARSE_HEADER, &testFile, nullptr, 0, nullptr, nullptr, &loadedSamples[0][i]);
                file_manager_->request_fifo.PushBack(parseReq);

                FileRequest readReq(FileRequest::Type::MASS_READ_INT16, &testFile, nullptr, 0, nullptr, sdram_write_ptr_, &loadedSamples[0][i]);
                file_manager_->request_fifo.PushBack(readReq);

                loadedSamples[0][i].start = sdram_write_ptr_;
                sdram_write_ptr_ += 1920000;
            }
        }

        for (size_t i = 0; i < MAX_SLOTS; ++i) {

            if (!loadedSamples[1][i].name.empty()) {

                FileRequest openReq(FileRequest::Type::OPEN, &testFile, loadedSamples[1][i].fullPath.c_str(), 0, nullptr, nullptr, nullptr);
                file_manager_->request_fifo.PushBack(openReq);

                FileRequest parseReq(FileRequest::Type::PARSE_HEADER, &testFile, nullptr, 0, nullptr, nullptr, &loadedSamples[1][i]);
                file_manager_->request_fifo.PushBack(parseReq);

                FileRequest readReq(FileRequest::Type::MASS_READ_INT16, &testFile, nullptr, 0, nullptr, sdram_write_ptr_, &loadedSamples[1][i]);
                file_manager_->request_fifo.PushBack(readReq);

                loadedSamples[1][i].start = sdram_write_ptr_;
                sdram_write_ptr_ += 1920000;
            }
        }

        if (!loadedSamples[0][14].name.empty()) {

            FileRequest openReq(FileRequest::Type::OPEN, &testFile, loadedSamples[0][14].fullPath.c_str(), 0, nullptr, nullptr, nullptr);
            file_manager_->request_fifo.PushBack(openReq);

            FileRequest parseReq(FileRequest::Type::PARSE_HEADER, &testFile, nullptr, 0, nullptr, nullptr, &loadedSamples[0][14]);
            file_manager_->request_fifo.PushBack(parseReq);

            FileRequest readReq(FileRequest::Type::MASS_READ_INT16, &testFile, nullptr, 0, nullptr, sdram_buff_ptr_, &loadedSamples[0][14]);
            file_manager_->request_fifo.PushBack(readReq);

            bufferFilled = true;
            loadedSamples[0][14].start = sdram_buff_ptr_;
        }

    }

    bool checkLoaded() {
        for (size_t i = 0; i < MAX_SLOTS; ++i) {
            for (size_t j = 0; j < 2; ++j) {
                if (loadedSamples[j][i].status == SampleStatus::LOADING) {
                    return false;
                }
            }
        }
        if (loadedSamples[0][14].status == SampleStatus::LOADING) {
            return false;
        }
        return true;
    }

    void saveSlot(size_t slot, size_t engine) {
        WriteFileToCard(15, slot, engine, engine);
        copyRamToRam(15, slot, engine);
        validateSlot(15, slot, engine, engine);
    }

    void deleteSlot(size_t slot, size_t engine) {
        if (engine == 0) {
            delBuffer = "/Chromatic/" + loadedSamples[0][slot - 1].name;
            FileRequest delReq(FileRequest::Type::UNLINK, nullptr, delBuffer.c_str(), 0, nullptr, nullptr, nullptr);
            file_manager_->request_fifo.PushBack(delReq);
        }
        else {
            delBuffer = "/Slice/" + loadedSamples[1][slot - 1].name;
            FileRequest delReq(FileRequest::Type::UNLINK, nullptr, delBuffer.c_str(), 0, nullptr, nullptr, nullptr);
            file_manager_->request_fifo.PushBack(delReq);
        }
        invalidateSlot(slot, engine);
    }

    void copySlot(size_t src, size_t dst, size_t src_engine, size_t dst_engine) {
        WriteFileToCard(src, dst, src_engine, dst_engine);
        copyRamToRam(src, dst, src_engine);
        validateSlot(src, dst, src_engine, dst_engine);
    }

    uint8_t getNumSamples() {
        size_t slots = 0;
        for (size_t i = 0; i < 2; ++i) {
            for (size_t j = 0; j < 14; ++j) {
                if (loadedSamples[i][j].status == SampleStatus::LOADING) {
                    ++slots;
                }
            }
        }
        return slots;
    }

    SampleStatus checkSlotForLEDS(uint8_t slot) {
        return loadedSamples[0][slot].status;
    }

    bool isValidSample(size_t slot, size_t engine) {
        if (slot == 15) {
            return bufferFilled;
        }
        return loadedSamples[engine][slot - 1].numSamples > 0;
    }

    void *getNextSample(size_t slot, size_t engine) {
        if (slot == 14) {
            return loadedSamples[0][14].start;
        }
        else {
            return loadedSamples[engine][slot].start;
        }
    }

    size_t getNextSampleSize(size_t slot, size_t engine) {
        if (slot == 14) {
            return loadedSamples[0][14].numSamples / 4;
        }
        else {
            return loadedSamples[engine][slot].numSamples / 4; //Stereo sample frame size
        }
    }

    void startRecording() {
        sdram_rec_ptr_ = sdram_buff_ptr_;
    }

    void stopRecording() {
        //more file stuff
        int16_t* start = static_cast<int16_t*>(sdram_buff_ptr_);
        int16_t* end   = static_cast<int16_t*>(sdram_rec_ptr_);

        size_t buffSize = (end - start) * 2; // Number of stereo frames

        loadedSamples[0][14].numSamples = buffSize;
        
        bufferFilled = true;
    }

    bool stereoWrite(int16_t l, int16_t r) {
        int16_t* dst = static_cast<int16_t*>(sdram_rec_ptr_);
        if (static_cast<void*>(dst + 2) > sdram_buff_end_) {
            return false;
        }

        dst[0] = l;
        dst[1] = r;

        sdram_rec_ptr_ = static_cast<void*>(dst + 2);

        return true;
    }

    void WriteFileToCard(size_t src, size_t dst, size_t src_engine, size_t dst_engine) {

        if (dst_engine == 0) {
            writeBuffer = "/Chromatic/chroma_a" + std::to_string(dst) + ".wav";
        }
        else {
            writeBuffer = "/Slice/slice_a" + std::to_string(dst) + ".wav";
        }

        FileRequest openReq(FileRequest::Type::OPEN_NEW, &writeFile, writeBuffer.c_str(), 0, nullptr, nullptr, nullptr);
        file_manager_->request_fifo.PushBack(openReq);

        if (src == 15) {
            FileRequest writeReq(FileRequest::Type::MASS_WRITE, &writeFile, nullptr, loadedSamples[0][14].numSamples, nullptr, loadedSamples[0][14].start, &loadedSamples[0][14]);
            file_manager_->request_fifo.PushBack(writeReq);
        }
        else {
            FileRequest writeReq(FileRequest::Type::MASS_WRITE, &writeFile, nullptr, loadedSamples[src_engine][src - 1].numSamples, nullptr, loadedSamples[src_engine][src - 1].start, &loadedSamples[src_engine][src - 1]);
            file_manager_->request_fifo.PushBack(writeReq);
        }

        FileRequest headerReq(FileRequest::Type::HEADER, &writeFile, nullptr, 0, nullptr, nullptr, nullptr);
        file_manager_->request_fifo.PushBack(headerReq);

        FileRequest closeReq(FileRequest::Type::CLOSE, &writeFile, nullptr, 0, nullptr, nullptr, nullptr);
        file_manager_->request_fifo.PushBack(closeReq);
    }

    void fillDefaultSample(float sr) {
        const float volume = .05f;
        const float freq = 220.f;
        const float period = sr / freq;
        const size_t totalFrames = static_cast<size_t>(3 * sr);

        for (size_t i = 0; i < totalFrames; ++i) {
            float t = static_cast<float>(i) / period;
            float tri = (2.0f * fabsf(2.0f * (t - floorf(t + 0.5f))) - 1.0f) * volume;
            int16_t sample = f2s16(tri);
            if (!stereoWrite(sample, sample)) {
                break;
            }
        }
    }

    SampleInfo loadedSamples[2][15];

    private:

    void validateSlot(size_t src, size_t dst, size_t src_engine, size_t dst_engine) {
        if (src == 15) {
            loadedSamples[dst_engine][dst - 1].numSamples = loadedSamples[0][14].numSamples;
        }
        else {
            loadedSamples[dst_engine][dst - 1].numSamples = loadedSamples[src_engine][src - 1].numSamples;
        }
        loadedSamples[dst_engine][dst - 1].start = sdram_write_ptr_;
        if (dst_engine == 0) {
            loadedSamples[0][dst - 1].name = "chroma_a" + std::to_string(dst) + ".wav";
        }
        else {
            loadedSamples[1][dst - 1].name = "slice_a" + std::to_string(dst) + ".wav";
        }
    }

    void invalidateSlot(uint8_t slot, size_t engine) {
        loadedSamples[engine][slot - 1].start = nullptr;
        loadedSamples[engine][slot - 1].numSamples = 0;
    }

    void copyRamToRam(size_t src, size_t dst, size_t engine) {
        sdram_write_ptr_ = sdram_buff_end_ + 1920000 * (dst - 1);
        if (src == 15) {
            memcpy(sdram_write_ptr_, loadedSamples[0][14].start, loadedSamples[0][14].numSamples * sizeof(int16_t) * 2);
        }
        else {
            memcpy(sdram_write_ptr_, loadedSamples[engine][src - 1].start, loadedSamples[engine][src - 1].numSamples * sizeof(int16_t) * 2);
        }
    }

    std::string pathBuffer; //For passing strings to f_open() on file read
    std::string writeBuffer; //For passing strings to f_open() on file write
    std::string delBuffer; //For passing strings to f_unlink() on file delete
    void *sdram_start_addr_;
    void *sdram_write_ptr_; //For writing preset samples to RAM
    void *sdram_buff_ptr_; //Start of RAM buffer for playback
    void *sdram_rec_ptr_; //For writing new samples to RAM buffer
    void *sdram_end_addr_ = (void *)0xc4000000;
    void *sdram_buff_end_;
    bool bufferFilled;

    FileStreamingManager *file_manager_;
    float sample_size;
    FIL testFile;
    FIL writeFile;

};

class samplePlayer {
    public:

    samplePlayer() {}
    ~samplePlayer() {}

    static constexpr size_t kMaxCrossfadeLengthLoop = 256;
    static constexpr size_t kMaxCrossfadeLengthClick = 256;

    void Init(float sr) {
        playbackSampleRate_ = sr;
        reverse = false;
        loop = true;
        globalFrequency = 1.f;
    }

    void setStartPoint(float val) {
        size_t offset = static_cast<size_t>(static_cast<float>(num_samples_) * val);
        start_point_ = offset;
        calculateCrossfade();
    }

    void setEndPoint(float val) {
        size_t reduction = static_cast<size_t>(static_cast<float>(num_samples_) * (1.f - val));
        end_point_ = num_samples_ - reduction;
        if (end_point_ < 1) {
            end_point_ = 1; // Ensure at least 1 to avoid indexing issues
        }
        calculateCrossfade();
    }

    void setFrequency(float freq) {
        cur_key_ = freq;
        updateFrequency();
    }

    void setGlobalFrequency(float freq) {
        globalFrequency = freq;
        updateFrequency();
    }

    void updateFrequency() {
        constexpr float middleC = 440.f;
        tuningWord = (cur_key_ * globalFrequency) / middleC;
        if (reverse) {
            tuningWord = -tuningWord;
        }
        calculateCrossfade();
    }

    void setReverse(bool rev) {
        if (reverse != rev) {
            reverse = rev;
            calculateCrossfade();
        }
        reverse = rev;
    }

    void setLoop(bool l) {
        loop = l;
    }

    void resetPlayer(bool still_running) {
        crossfade_counter_ = 0.f;
        if ((still_running && loop) || (still_running && !loop && isVoiceResettable())) {
            crossfading_ = true;
        }
        else {
            crossfading_ = false;
            if (!reverse) {
                phaseAccumulator = static_cast<float>(start_point_);
            }
            else {
                phaseAccumulator = static_cast<float>(end_point_);
            }
        }
        click_crossfading_ = false;
        click_crossfade_counter_ = 0.f;
    }

    void PopStereoSamps(float* left, float* right) {

        if (sample_memory_ == nullptr) {
            // if we get here, there's a bug
            *left = 0.f;
            *right = 0.f;
            return;
        }

        int16_t* pcm_samples = static_cast<int16_t*>(sample_memory_);
        size_t idx = static_cast<size_t>(phaseAccumulator);
        size_t base, next_base;
        float frac;
    
        if (!reverse) {
            base = idx * 2;
            next_base = base + 2;
            if (idx >= end_point_) {
                if (loop) {
                    base = next_base = start_point_ * 2;
                    phaseAccumulator = static_cast<float>(start_point_);
                }
                else if (!crossfading_) {
                    *left = 0.f;
                    *right = 0.f;
                    return;
                }
            }
            frac = phaseAccumulator - static_cast<float>(idx);
        }
        else {
            base = idx * 2;
            next_base = base - 2;
            if (idx <= start_point_) {
                if (loop) {
                    base = next_base = (end_point_ - 1) * 2;
                    phaseAccumulator = static_cast<float>(end_point_ - 1);
                }
                if (!crossfading_) {
                    *left = 0.f;
                    *right = 0.f;
                    return;
                }
            }
            frac = 1.f - (phaseAccumulator - static_cast<float>(idx));
        }

        float crossfadeEnv = 1.f - crossfade_counter_ / static_cast<float>(kMaxCrossfadeLengthLoop);
        float clickEnv = 1.f - click_crossfade_counter_ / static_cast<float>(kMaxCrossfadeLengthClick);
    
        // Interpolate left channel
        int16_t l0 = pcm_samples[base];
        int16_t l1 = pcm_samples[next_base];
        float l_interp = static_cast<float>(l0) + (static_cast<float>(l1 - l0) * frac);
        *left = s162f(static_cast<int16_t>(l_interp)) * crossfadeEnv * clickEnv;
    
        // Interpolate right channel
        int16_t r0 = pcm_samples[base + 1];
        int16_t r1 = pcm_samples[next_base + 1];
        float r_interp = static_cast<float>(r0) + (static_cast<float>(r1 - r0) * frac);
        *right = s162f(static_cast<int16_t>(r_interp)) * crossfadeEnv * clickEnv;

        if (should_crossfade_  && loop) {
            if (phaseAccumulator > crossfade_start_loop_ && !reverse && !crossfading_ || phaseAccumulator < crossfade_start_loop_ && reverse && !crossfading_) {
                crossfading_ = true;
                crossfade_counter_ = 0.f;
            }
        }
        else if (!loop) {
            if (phaseAccumulator > crossfade_start_click_ && !reverse && !click_crossfading_ || phaseAccumulator < crossfade_start_click_ && reverse && !click_crossfading_) {
                click_crossfading_ = true;
                click_crossfade_counter_ = 0.f;
            }
        }
        if (crossfading_) {
            crossfade_counter_ += fabsf(tuningWord);
            float crossfade_pos;
            if (!reverse) {
                crossfade_pos = static_cast<float>(start_point_) + crossfade_counter_;
            }
            else {
                crossfade_pos = static_cast<float>(end_point_) - crossfade_counter_;
            }
            idx = static_cast<size_t>(crossfade_pos);
            base = idx * 2;
            next_base = base + (!reverse ? 2 : -2);
            frac = crossfade_pos - static_cast<float>(idx);
            if (reverse) {
                frac = 1.f - frac;
            }
            l0 = pcm_samples[base];
            l1 = pcm_samples[next_base];
            l_interp = static_cast<float>(l0) + (static_cast<float>(l1 - l0) * frac);
            *left += s162f(static_cast<int16_t>(l_interp)) * (1.f - crossfadeEnv);

            r0 = pcm_samples[base + 1];
            r1 = pcm_samples[next_base + 1];
            r_interp = static_cast<float>(r0) + (static_cast<float>(r1 - r0) * frac);
            *right += s162f(static_cast<int16_t>(r_interp)) * (1.f - crossfadeEnv);
            if (crossfade_counter_ > static_cast<float>(kMaxCrossfadeLengthLoop)) {
                crossfading_ = false;
                crossfade_counter_ = 0;
                phaseAccumulator = crossfade_pos;
            }
        }
        if (click_crossfading_) {
            click_crossfade_counter_ += fabsf(tuningWord);
            if (click_crossfade_counter_ > static_cast<float>(kMaxCrossfadeLengthClick)) {
                click_crossfading_ = false;
                click_crossfade_counter_ = 0.f;
            }
        }
    
        phaseAccumulator += tuningWord;
    }

    void setSample(void *addr, size_t ns) {
        sample_memory_ = addr;
        num_samples_ = ns;
        start_point_ = 0;
        end_point_ = ns;
        calculateCrossfade();
    }

    void calculateCrossfade() {
        window_size_ = end_point_ - start_point_;
        if (window_size_ > 4800) {
            should_crossfade_ = true;
        }
        else {
            should_crossfade_ = false;
        }

        if (!reverse) {
            crossfade_start_loop_ = static_cast<float>(end_point_ - kMaxCrossfadeLengthLoop);
            crossfade_start_click_ = static_cast<float>(end_point_ - kMaxCrossfadeLengthClick);
        }
        else {
            crossfade_start_loop_ = static_cast<float>(start_point_ + kMaxCrossfadeLengthLoop);
            crossfade_start_click_ = static_cast<float>(start_point_ + kMaxCrossfadeLengthClick);
        }
    }

    bool isVoiceResettable() {
        if (phaseAccumulator < crossfade_start_loop_ && !reverse) {
            return true;
        }
        if (phaseAccumulator > crossfade_start_loop_ && reverse) {
            return true;
        }
        return false;
    }

    void *sample_memory_;
    size_t num_samples_;
    size_t start_point_;
    size_t end_point_;
    float cur_key_;
    float phaseAccumulator;
    float tuningWord;
    float playbackSampleRate_;
    float globalFrequency;
    bool reverse;
    bool loop;

    size_t window_size_;
    bool crossfading_, should_crossfade_;
    float crossfade_start_loop_, crossfade_start_click_;
    bool click_crossfading_;
    float crossfade_counter_;
    float click_crossfade_counter_;

    private:
};