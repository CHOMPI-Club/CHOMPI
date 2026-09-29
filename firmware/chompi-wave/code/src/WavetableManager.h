/** @file WavetableManager.h
 *  @brief Wavetable storage and playback
 *
 * wavetableLoader loads wavetable info from SD card on bootup, then
 * queues the reading of those files for the FileStreamingManager.
 *
 *  A wavetable file on disk holds 33 single-cycle waveforms, (2048) samples each,
 *  and cycling within a table just walks that file's
 *  33 rows. This number is arbitrary and could be changed.
 */
#pragma once
#include "fatfs.h"
#include "FileStreamingManager.h"
#include <vector>
#include <string>
#include <algorithm>
#include <cstring>

#define MAX_SAMPLES_PER_CYCLE 2048
#define CYCLES 33

class wavetable {
    public:
    wavetable() {};
    ~wavetable() {};

    void Init(float sampleRate) {
        sampleRate_ = sampleRate;

        phaseAccumulator = 0.f;
        tuningWord = 0.f;
        
        curCycle = 0;
    }

    // Pulls out one sample
    float PopSample() {
        size_t idx;
        idx = static_cast<int>(phaseAccumulator);
        float frac = phaseAccumulator - idx;

        float sample1 = wavetableMemory_[curCycle][idx];
        float sample2 = wavetableMemory_[curCycle][(idx + 1) % MAX_SAMPLES_PER_CYCLE];

        //Linear interpolate
        float currentSample = sample1 + frac * (sample2 - sample1);
        float output;
        if (isCrossfading) {
            // Old table - read through lastBase_ so a fade can span a TABLE swap
            float last1 = lastBase_[lastCycle][idx];
            float last2 = lastBase_[lastCycle][(idx + 1) % MAX_SAMPLES_PER_CYCLE];
            float lastSample = last1 + frac * (last2 - last1);

            float fadeAmt = std::min(static_cast<float>(fadeCounter) / fadeLength, 1.0f);
            output = (1.0f - fadeAmt) * lastSample + fadeAmt * currentSample;
            fadeCounter++;
            if (fadeCounter >= fadeLength) {
                isCrossfading = false;
            }
        } else {
            output = currentSample;
        }

        phaseAccumulator += tuningWord;
        if (phaseAccumulator > MAX_SAMPLES_PER_CYCLE) {
            phaseAccumulator -= MAX_SAMPLES_PER_CYCLE;
        }

        return output;
    }

    // Converts a target pitch into the phase accumulator step size
    void setFrequency(float frequency) {
        tuningWord = (frequency * MAX_SAMPLES_PER_CYCLE) / sampleRate_;
    }

    void cycleThroughTable(int8_t direction, bool direct) {
        // Rapid knob turns interrupt the 960-sample crossfade. Restarting it from zero with the
        // half reached frame as the new source produces a click, so a change starting
        // early (<50%) keeps the original source and fade position and only changes 
        // the destination. >=50% restarts from zero.
        bool retarget = isCrossfading
                        && (static_cast<float>(fadeCounter) / fadeLength) < .5f;

        if (!retarget) {
            // Get the last sample before switch
            lastCycle = curCycle;
            lastBase_ = wavetableMemory_; // fade within this table
        }

        if (direct) {
            curCycle = direction;
            if (curCycle < 0 || curCycle > CYCLES - 1) {
                curCycle = 0;
            }
        }
        else {
            curCycle += direction;
            if (curCycle < 0) {
                curCycle = 0;
            }
            if (curCycle > CYCLES - 1) {
                curCycle = CYCLES - 1;
            }
        }
        isCrossfading = true;
        if (!retarget) {
            fadeCounter = 0;
        }
    }

    void setTable(float (*base)[MAX_SAMPLES_PER_CYCLE]) {
        if (base == wavetableMemory_) {
            return;
        }
        bool retarget = isCrossfading
                        && (static_cast<float>(fadeCounter) / fadeLength) < .5f;
        if (!retarget) {
            lastCycle = curCycle;
            lastBase_ = wavetableMemory_;
            fadeCounter = 0;
        }
        wavetableMemory_ = base;
        isCrossfading = true;
    }

    float (*wavetableMemory_)[MAX_SAMPLES_PER_CYCLE];
    float (*lastBase_)[MAX_SAMPLES_PER_CYCLE]; //outgoing table during a crossfade
    float phaseAccumulator;
    float tuningWord;
    float sampleRate_;
    int16_t curCycle;
    int16_t lastCycle;

    bool isCrossfading = false;
    int fadeCounter = 0;
    static const int fadeLength = 960;
    float lastSample = 0.f;

};

class wavetableLoader {
    public:
    wavetableLoader() {};
    ~wavetableLoader() {};

    void Init(FATFS *fs, float (*wavetableMemory)[MAX_SAMPLES_PER_CYCLE], float sampleRate) {
        wavetableMemory_ = wavetableMemory;
        fs_ = fs;
        index = 0;

        loadNamesFromSD();
    }

    void loadNamesFromSD() {
        DIR dir;
        FILINFO fno;
        FRESULT res;

        res = f_opendir(&dir, "/");
        if (res == FR_OK) {
            while (true) {
                res = f_readdir(&dir, &fno);
                if (res != FR_OK || fno.fname[0] == 0) {
                    break;
                }

                if (strstr(fno.fname, ".wav") && fno.fname[0] != '.') {//Because we love Mac users :)
                    wavetable_names.push_back(fno.fname);
                }
            }
            f_closedir(&dir);
            std::sort(wavetable_names.begin(), wavetable_names.end());
            numWavetables = wavetable_names.size();
        }
    }

    void loadAllToMemory() {
        int8_t n = numWavetables < kMaxPreload ? (int8_t)numWavetables : kMaxPreload;
        for (int8_t t = 0; t < n; ++t) {
            FileRequest openReq(FileRequest::Type::OPEN, &testFile, wavetable_names[t].c_str(), 0, nullptr, this, nullptr);
            file_manager->request_fifo.PushBack(openReq);

            FileRequest seekReq(FileRequest::Type::SEEK, &testFile, nullptr, 136, nullptr, this, nullptr);
            file_manager->request_fifo.PushBack(seekReq);

            FileRequest readReq(FileRequest::Type::MASS_READ, &testFile, nullptr, 2048 * 4 * 33, &test_read_samps, this, wavetableMemory_[t * 33]);
            file_manager->request_fifo.PushBack(readReq);
        }
    }

    static const int8_t kMaxPreload = 7;

    enum TableStatus : int8_t { TABLE_EMPTY = 0, TABLE_LOADED, TABLE_FAILED };
    int8_t table_status[kMaxPreload] = {0, 0, 0, 0, 0, 0, 0};
    int8_t load_cursor = 0;

    void markSetupResult(bool ok) {
        if (load_cursor < kMaxPreload && !ok) {
            table_status[load_cursor] = TABLE_FAILED;
        }
    }

    bool currentLoadFailed() {
        return load_cursor < kMaxPreload && table_status[load_cursor] == TABLE_FAILED;
    }

    void markReadResult(bool ok) {
        if (load_cursor < kMaxPreload) {
            if (table_status[load_cursor] != TABLE_FAILED) {
                table_status[load_cursor] = ok ? TABLE_LOADED : TABLE_FAILED;
            }
            if (table_status[load_cursor] == TABLE_FAILED) {
                //FAILED means SILENT: wipe whatever partial read landed in the
                //region so a failed table plays zeros
                memset(&wavetableMemory_[load_cursor * 33][0], 0,
                       (size_t)33 * MAX_SAMPLES_PER_CYCLE * sizeof(float));
            }
            load_cursor++;
        }
    }

    void selectTable(int8_t direction, bool direct) {
        int8_t n = numPreloaded();
        if (direct) {
            if (direction < 0 || direction + 1 > n) {
                index = 0;
            }
            else {
                index = direction;
            }
        }
        else {
            if (index + direction < 0 || index + direction + 1 > n) {
                return;
            }
            else {
                index += direction;
            }
        }
    }

    //base row of the currently selected table's preloaded region
    float (*tableBase())[MAX_SAMPLES_PER_CYCLE] {
        return &wavetableMemory_[index * 33];
    }

    int8_t numPreloaded() {
        return numWavetables < kMaxPreload ? (int8_t)numWavetables : kMaxPreload;
    }

    int getIdx() {
        return index;
    }

    void setFileManager(FileStreamingManager *fm){
        file_manager = fm;
    }

    FATFS *fs_;
    std::vector<std::string> wavetable_names;
    int16_t numWavetables;
    int16_t index;
    float (*wavetableMemory_)[MAX_SAMPLES_PER_CYCLE];
    UINT bytesRead;
    FileStreamingManager *file_manager;
    FIFO<int16_t, kMaxFileStreamingSamps> test_read_samps;
    FIL testFile;
    float testArray[2048];

};