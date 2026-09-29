#pragma once
#include "daisy.h"
#include "daisysp.h"

using namespace daisy;
using namespace daisysp;

enum clockMode {
    FREE,
    SYNC
};

struct ClockDivMapping {
    int min_enc;
    int max_enc;
    uint8_t div_pos;
    size_t clock_division;
    float r1, g1, b1;
    float r2, g2, b2;
};

constexpr ClockDivMapping syncDivs[] = {
    {0, 12, 0, 48, 0.f, .84f, 1.f, 0.f, 0.f, 0.f},                        //med_blue
    {12, 24, 1, 24, 0.f, 1.f, 0.f, 0.f, 0.f, 0.f},                        //green
    {24, 36, 2, 12, .1f, .1f, .1f, .1f, .1f, .1f},                        //dim white
    {36, 48, 3, 6, 0.f, 0.f, 0.f, 1.f, .95f, 0.05f},                      //yellow
    {48, 60, 4, 3, 0.f, 0.f, 0.f, 1.f, 0.f, 0.f},                         //red
};

constexpr ClockDivMapping freeDivs[] = {
    {0, 12, 0, 24,  0.f, .84f, 1.f, 0.f, 0.f, 0.f},
    {12, 24, 1, 18, 0.f, 1.f, 0.f, 0.f, 0.f, 0.f},
    {24, 36, 2, 12, .1f, .1f, .1f, .1f, .1f, .1f},
    {36, 48, 3, 8, 0.f, 0.f, 0.f, 1.f, .95f, 0.05f},
    {48, 60, 4, 6, 0.f, 0.f, 0.f, 1.f, 0.f, 0.f},
};

constexpr uint32_t tapTempoTimeout = 750; // period between 160BPM notes

class clockManager {

    public:
    clockManager() {};
    ~clockManager() {};

    float freeClockMults[5] = {2.f, 1.5f, 1.f, .66f, .5f};
    float syncClockMults[5] = {4.f, 2.f, 1.f, .5f, .25f};

    void Init(TimerHandle *clock_timer, size_t base_freq) {
        mode_ = FREE;

        clock_timer_ = clock_timer;
        base_freq_ = base_freq * 2;
        prescaler_ = 239;

        SyncDivPos[0] = SyncDivPos[1] = FreeDivPos[0] = FreeDivPos[1] = 2;
        newSyncDivPos[0] = newSyncDivPos[1] = newFreeDivPos[0] = newFreeDivPos[1] = 2;
        MIDIclockDiv[0] = MIDIclockDiv[1] = 12;

        AvgIdx = 0;
        intervalUsSync = 320000;
        sync_tempo = 187;

        MIDIcounter[0] = MIDIcounter[1] = 0;
        intervalCounter[0] = intervalCounter[1] = 0;
        calculatedInterval[0] = calculatedInterval[1] = calculatedInterval[2] = 12;

        current_tempo = 320; //Initial clock tempo 320 bpm
        changeTempo(0);

        // Middle position ticks number
        freeEncoderCounter[0] = freeEncoderCounter[1] = syncEncoderCounter[0] = syncEncoderCounter[1] = 30;
    }

    bool checkIntervalExpired(size_t eng) {
        if (mode_ == FREE && intervalCounter[eng] >= calculatedInterval[eng]) {
            if (eng == 2) {
                for (size_t i = 0; i < 2; ++i) {
                    if (deferredDivChange[i]) {
                        FreeDivPos[i] = newFreeDivPos[i];
                        calculatedInterval[i] = freeDivs[FreeDivPos[i]].clock_division;
                        deferredDivChange[i] = false;
                        setNow(i);
                    }
                }
            }
            setNow(eng);
            return true;
        }
        else if (mode_ == SYNC && MIDIcounter[eng] >= MIDIclockDiv[eng]) {
            MIDIcounter[eng] = 0;
            return true;
        }
        return false;
    }

    int getTempo() {
        if (mode_ == FREE) {
            return current_tempo;
        }
        else {
            return sync_tempo;
        }
    }

    size_t getInterval() {
        if (mode_ == FREE) {
            return intervalUsFree;
        }
        else {
            return intervalUsSync + syncDelayBoost;
        }
    }

    float getDiv(size_t eng) {
        if (mode_ == FREE) {
            return freeClockMults[FreeDivPos[eng]];
        }
        else {
            return syncClockMults[SyncDivPos[eng]];
        }
    }

    void incrementCounters() {
        intervalCounter[0]++;
        intervalCounter[1]++;
        intervalCounter[2]++;
    }

    void toggleClockMode() {
        mode_ = !mode_;
    }

    int getClockMode() {
        return mode_;
    }

    int getClockDivPos(size_t eng) {
        if (mode_ == SYNC) {
            return SyncDivPos[eng];
        }
        else {
            return FreeDivPos[eng];
        }
    }

    void fillLEDdata(float *leds, size_t eng) {
        if (mode_ == SYNC) {
            leds[0] = syncDivs[SyncDivPos[eng]].r1;
            leds[1] = syncDivs[SyncDivPos[eng]].g1;
            leds[2] = syncDivs[SyncDivPos[eng]].b1;
            leds[3] = syncDivs[SyncDivPos[eng]].r2;
            leds[4] = syncDivs[SyncDivPos[eng]].g2;
            leds[5] = syncDivs[SyncDivPos[eng]].b2;
        }
        else {
            leds[0] = freeDivs[newFreeDivPos[eng]].r1;
            leds[1] = freeDivs[newFreeDivPos[eng]].g1;
            leds[2] = freeDivs[newFreeDivPos[eng]].b1;
            leds[3] = freeDivs[newFreeDivPos[eng]].r2;
            leds[4] = freeDivs[newFreeDivPos[eng]].g2;
            leds[5] = freeDivs[newFreeDivPos[eng]].b2;
        }
    }

    void changeTempo(int turns) {
        current_tempo += turns;
        if (current_tempo > 480) {
            current_tempo = 480;
        }
        else if (current_tempo < 160) {
            current_tempo = 160;
        }

        intervalUsFree = 60000000 / current_tempo;

        float midi_clock_hz = static_cast<float>(current_tempo) * 12.f / 60.f;
        uint32_t tick_rate = base_freq_ / (prescaler_ + 1);
        uint32_t arr = (tick_rate / midi_clock_hz) - 1;

        clock_timer_->SetPeriod(arr);
    }

    void setTempo(size_t t) {
        current_tempo = t;

        intervalUsFree = 60000000 / current_tempo;

        float midi_clock_hz = static_cast<float>(current_tempo) * 12.f / 60.f;
        uint32_t tick_rate = base_freq_ / (prescaler_ + 1);
        uint32_t arr = (tick_rate / midi_clock_hz) - 1;

        clock_timer_->SetPeriod(arr);
    }

    void changeDiv(int turns, size_t eng) {
        if (mode_ == SYNC) {
            syncEncoderCounter[eng] += turns;
            if (syncEncoderCounter[eng] < 0) {
                syncEncoderCounter[eng] = 0;
            }
            else if (syncEncoderCounter[eng] > 60) {
                syncEncoderCounter[eng] = 60;
            }

            bool change_ = false;
            if (syncEncoderCounter[eng] < syncDivs[SyncDivPos[eng]].min_enc) {
                SyncDivPos[eng]--;
                change_ = true;
            }
            else if (syncEncoderCounter[eng] > syncDivs[SyncDivPos[eng]].max_enc) {
                SyncDivPos[eng]++;
                change_ = true;
            }

            if (change_) {
                size_t new_min = syncDivs[SyncDivPos[eng]].min_enc;
                size_t new_max = syncDivs[SyncDivPos[eng]].max_enc;
                syncEncoderCounter[eng] = new_min + (new_max - new_min) / 2;
                MIDIclockDiv[eng] = syncDivs[SyncDivPos[eng]].clock_division;
            }
        }
        else {
            freeEncoderCounter[eng] += turns;
            if (freeEncoderCounter[eng] < 0) {
                freeEncoderCounter[eng] = 0;
            }
            else if (freeEncoderCounter[eng] > 60) {
                freeEncoderCounter[eng] = 60;
            }

            bool change_ = false;
            if (freeEncoderCounter[eng] < freeDivs[newFreeDivPos[eng]].min_enc) {
                newFreeDivPos[eng]--;
                deferredDivChange[eng] = true;
                change_ = true;
            }
            else if (freeEncoderCounter[eng] > freeDivs[newFreeDivPos[eng]].max_enc) {
                newFreeDivPos[eng]++;
                deferredDivChange[eng] = true;
                change_ = true;
            }

            if (change_) {
                size_t new_min = freeDivs[newFreeDivPos[eng]].min_enc;
                size_t new_max = freeDivs[newFreeDivPos[eng]].max_enc;
                freeEncoderCounter[eng] = new_min + (new_max - new_min) / 2;
            }
        }
    }

    void setDiv(size_t eng, size_t div) {
        if (mode_ == FREE) {
            newFreeDivPos[eng] = div;
        }
        else {
            MIDIclockDiv[eng] = div;
        }
        deferredDivChange[eng] = true;
    }

    void setNow(size_t eng) {
        if (mode_ == FREE) {
            intervalCounter[eng] = 0;
        }
        else {
            MIDIcounter[eng] = 0;
        }
    }

    void processMidiClock() {
        // Exponential smoothing factor (0 < alpha <= 1)
        constexpr float alpha = 0.1f;

        AvgIdx++;
        // increment MIDI counters for existing logic
        MIDIcounter[0]++;
        MIDIcounter[1]++;
    }

    void averageMidiClock() {
        constexpr float alpha = 0.1f;
        
        size_t now = System::GetTick();

        if (AvgIdx == 0) {
            last_time_ = now;
        }
        if (AvgIdx >= 24) {
            size_t inst_interval = (now - last_time_) / (System::GetTickFreq() / 1000000);
            intervalUsSync_f += alpha * ((float)(inst_interval >> 1) - intervalUsSync_f);
            intervalUsSync = (size_t)(intervalUsSync_f + 0.5f);
            AvgIdx = 0;
            sync_tempo = 60000000 / intervalUsSync;

            int32_t d = (int32_t)intervalUsSync - 151520;
            if (d < 0) {
                d = 0;
            }
            syncDelayBoost = (size_t)((138 * d * d) / 1000000000 + (194 * d) / 10000 + 3900);
        }
    }

    float processTapClock(float enc_value) {
        uint32_t now = System::GetNow();
        float ret;
        if (now - tapTempoTimer > tapTempoTimeout) {
            ret = enc_value;
        }
        else {
            current_tempo = static_cast<uint32_t>(120000.f / (now - tapTempoTimer)); // 2 beats per tap interval
            current_tempo = current_tempo > 480 ? 480 : current_tempo;

            changeTempo(0);

            ret = static_cast<float>(current_tempo - 160) / 320.f;
        }
        tapTempoTimer = now;
        return ret;
    }

    int mode_;

    private:
    float calculatedInterval[2 + 1];

    size_t intervalCounter[2 + 1];
    uint8_t MIDIcounter[2 + 1];

    uint8_t MIDIclockDiv[2 + 1];

    uint8_t SyncDivPos[2];
    uint8_t FreeDivPos[2];
    uint8_t newSyncDivPos[2];
    uint8_t newFreeDivPos[2];

    uint32_t current_tempo;
    uint32_t sync_tempo;
    float noteTime[2];

    int32_t syncEncoderCounter[2];
    int32_t freeEncoderCounter[2];

    size_t intervalUsFree, intervalUsSync;
    float intervalUsSync_f;
    size_t syncDelayBoost = 0;

    size_t last_time_;
    size_t AvgIdx;

    uint32_t MIDIclockTimer;

    uint32_t tapTempoTimer;

    uint32_t clockPulseCounter;
    float clockPulseInterval;

    TimerHandle *clock_timer_;
    size_t base_freq_;
    size_t prescaler_;

    bool deferredDivChange[2];
};