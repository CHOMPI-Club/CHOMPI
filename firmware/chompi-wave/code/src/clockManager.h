/** @file clockManager.h
 *  @brief Tempo/timing source shared by the Sequencer and the MIDI clock output
 *  timer. Tracks the current tempo and step clock division for the sequencer
 *  and reprograms the hardware MIDI-clock timer's period so
 *  MIDI clock stays locked to the same tempo. Also has tap-tempo. TEMPO firmware
 *  has both free-running clock and MIDI synced clock, this only has free clock. This code
 *  was ported from TEMPO.
 */
#pragma once
#include "daisy.h"
#include "daisysp.h"

using namespace daisy;
using namespace daisysp;

constexpr uint32_t tapTempoTimeout = 750; // period between 160BPM notes (as in TEMPO)

/** Keeps track of info related to clock division, like where on transport knob you need
 *  to be for each division, what color its leds should be, etc. */
struct ClockDivMapping {
    int min_enc;
    int max_enc;
    uint8_t div_pos;
    size_t clock_division;
    float r1, g1, b1;
    float r2, g2, b2;
};

constexpr ClockDivMapping freeDivs[] = {
    {0, 12, 0, 24,  0.f, .84f, 1.f, 0.f, 0.f, 0.f},
    {12, 24, 1, 18, 0.f, 1.f, 0.f, 0.f, 0.f, 0.f},
    {24, 36, 2, 12, .1f, .1f, .1f, .1f, .1f, .1f},
    {36, 48, 3, 8, 0.f, 0.f, 0.f, 1.f, .95f, 0.05f},
    {48, 60, 4, 6, 0.f, 0.f, 0.f, 1.f, 0.f, 0.f},
};

class clockManager {

    public:
    clockManager() {};
    ~clockManager() {};

    void Init(TimerHandle *clock_timer, size_t base_freq) {
        clock_timer_ = clock_timer;
        base_freq_ = base_freq * 2; // APB2 timer kernel clock = PCLK2 x2
        prescaler_ = 239;
        tapTempoTimer = 0;
        DivPos = newDivPos = 2;
        freeEncoderCounter = 30;
        deferredDivChange = false;
        changeTempo(320);
    }

    bool checkIntervalExpired() {
        if (System::GetNow() - intervalCounter > calculatedInterval) {
            if (deferredDivChange) { //division changes land at step boundaries
                DivPos = newDivPos;
                deferredDivChange = false;
                changeTempo(current_tempo);
            }
            return true;
        }
        return false;
    }

    // True once the running step is past frac of
    // its interval - the sequencer uses this to cut notes early
    bool checkGateExpired(float frac) {
        return (System::GetNow() - intervalCounter) > calculatedInterval * frac;
    }

    int getTempo() {
        return current_tempo;
    }

    // Recomputes both timing sources for a new tempo (BPM):
    void changeTempo(int tempo) {
        calculatedInterval = (60000.f / tempo)
            * (static_cast<float>(freeDivs[DivPos].clock_division) / 12.f);
        current_tempo = tempo;

        if (clock_timer_) {
            float midi_clock_hz = static_cast<float>(current_tempo) * 12.f / 60.f;
            uint32_t tick_rate = base_freq_ / (prescaler_ + 1);
            uint32_t arr = (tick_rate / midi_clock_hz) - 1;
            clock_timer_->SetPeriod(arr);
        }
    }

    /** Turning the encoder moves freeEncoderCounter through freeDivs
     *  min_enc/max_enc ranges. Crossing a boundary selects the next
     *  division but doesn't apply it until a new note is
     *  supposed to play. An in-progress step doesn't change length and the
     *  counter is re-centered within the new division's range */
    void changeDiv(int turns) {
        freeEncoderCounter += turns;
        if (freeEncoderCounter < 0) {
            freeEncoderCounter = 0;
        }
        else if (freeEncoderCounter > 60) {
            freeEncoderCounter = 60;
        }

        bool change_ = false;
        if (freeEncoderCounter < freeDivs[newDivPos].min_enc) {
            newDivPos--;
            deferredDivChange = true;
            change_ = true;
        }
        else if (freeEncoderCounter > freeDivs[newDivPos].max_enc) {
            newDivPos++;
            deferredDivChange = true;
            change_ = true;
        }

        if (change_) {
            int new_min = freeDivs[newDivPos].min_enc;
            int new_max = freeDivs[newDivPos].max_enc;
            freeEncoderCounter = new_min + (new_max - new_min) / 2;
        }
    }

    void resetDiv() {
        DivPos = newDivPos = 2;
        freeEncoderCounter = 30;
        deferredDivChange = false;
    }

    void fillLEDdata(float* leds) {
        leds[0] = freeDivs[newDivPos].r1;
        leds[1] = freeDivs[newDivPos].g1;
        leds[2] = freeDivs[newDivPos].b1;
        leds[3] = freeDivs[newDivPos].r2;
        leds[4] = freeDivs[newDivPos].g2;
        leds[5] = freeDivs[newDivPos].b2;
    }

    float processTapClock(float enc_value) {
        uint32_t now = System::GetNow();
        float ret;
        if (now - tapTempoTimer > tapTempoTimeout) {
            ret = enc_value;
        }
        else {
            uint32_t t = static_cast<uint32_t>(120000.f / (now - tapTempoTimer)); // 2 beats per tap interval
            t = t > 480 ? 480 : t;
            t = t < 160 ? 160 : t;
            changeTempo(t);
            ret = static_cast<float>(current_tempo - 160) / 320.f;
        }
        tapTempoTimer = now;
        return ret;
    }

    void setNow() {
        intervalCounter = System::GetNow();
    }

    private:
    TimerHandle *clock_timer_ = nullptr;
    size_t base_freq_ = 0;
    uint32_t prescaler_ = 239;
    float calculatedInterval;
    uint32_t intervalCounter;
    uint32_t tapTempoTimer;
    int freeEncoderCounter;
    uint8_t DivPos;
    uint8_t newDivPos;
    bool deferredDivChange;
    uint32_t current_tempo;
};