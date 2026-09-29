#pragma once
#include "daisy.h"

struct SavedState {
    float enc_values[2][4];
    float filter;
    float redux;
    float pan;
    float loop;
    float sustain;
    float randomness;
    float feedback;
    float out_gain;
    float comp;
    float in_gain;
    size_t sample_slot;
    size_t monitor_mode;
    size_t pattern;
    size_t rest_pattern;
    size_t clock_div;
    bool play;
    size_t latch_state;
    float arp_randomness;
    std::vector<NoteInfo> sequence;
};

class StateSaver {
    public:
    StateSaver() {}
    ~StateSaver() {}

    enum StateSlot {
        ChromaticA,
        ChromaticB,
        SliceA,
        SliceB,
        LAST
    };

    enum State {
        A,
        B
    };

    void Init() {
        states_[SliceA].loop = 0.f;
        states_[SliceB].loop = 0.f;

        curState = A;
    }

    State getState() {
        return curState;
    }

    void setState(bool a) {
        if (a) {
            curState = A;
        }
        else {
            curState = B;
        }
    }

    void Save(size_t slot, SavedState *st, std::vector<NoteInfo> *seq) {
        for (size_t page = 0; page < 2; ++page) {
            for (size_t enc = 0; enc < 4; ++enc) {
                states_[slot].enc_values[page][enc] = st->enc_values[page][enc];
            }
        }
        states_[slot].filter = st->filter;
        states_[slot].pan = st->pan;
        states_[slot].redux = st->redux;
        states_[slot].loop = st->loop;
        states_[slot].sustain = st->sustain;
        states_[slot].randomness = st->randomness;
        states_[slot].feedback = st->feedback;
        states_[slot].out_gain = st->out_gain;
        states_[slot].comp = st->comp;
        states_[slot].in_gain = st->in_gain;
        states_[slot].sample_slot = st->sample_slot;
        states_[slot].monitor_mode = st->monitor_mode;
        states_[slot].pattern = st->pattern;
        states_[slot].rest_pattern = st->rest_pattern;
        states_[slot].clock_div = st->clock_div;
        states_[slot].play = st->play;
        states_[slot].latch_state = st->latch_state;
        states_[slot].arp_randomness = st->arp_randomness;
        if (seq != nullptr) {
            states_[slot].sequence = *seq;
        }

        size_t opposite_slot = slot > 1 ? slot - 2 : slot + 2;
        states_[opposite_slot].enc_values[0][3] = st->enc_values[0][3];
        states_[opposite_slot].randomness = st->randomness;
        states_[opposite_slot].feedback = st->feedback;
        states_[opposite_slot].out_gain = st->out_gain;
        states_[opposite_slot].comp = st->comp;
        states_[opposite_slot].in_gain = st->in_gain;
        states_[opposite_slot].monitor_mode = st->monitor_mode;
    }

    SavedState *Recall(size_t slot) {
        return &states_[slot];
    }

    void Copy(State st) {
        size_t src_chromatic = static_cast<size_t>(st);
        size_t src_slice = static_cast<size_t>(st) + 2;

        size_t dst_chromatic = src_chromatic ^ 1;
        size_t dst_slice = src_slice ^ 1;

        states_[dst_chromatic] = states_[src_chromatic];
        states_[dst_slice] = states_[src_slice];
    }

    void setPlay(size_t st, bool play) {
        states_[st].play = play;
    }

    private:
    SavedState states_[StateSlot::LAST];
    State curState;
};