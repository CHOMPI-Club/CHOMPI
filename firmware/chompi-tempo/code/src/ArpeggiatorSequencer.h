#pragma once
#include "clockManager.h"
#include "SampleEngine.h"
#include "SliceEngine.h"
#include "EngineBase.h"
#include "hardware.h"
#include "OptionsManager.h"

struct NoteInfo {
    int8_t key_;
    float transpose_nn_;
    uint8_t order_;
    uint32_t timestamp_;
    int8_t key_number_;
    bool remove_;
};

constexpr size_t kMaxDeferFrames = 30;
constexpr size_t kNumEngines = 2;

constexpr size_t keyMap[32] = {99, 99, 99, 7, 99, 99, 8, 1, 2, 4, 5, 7, 3, 6, 8, 
                                    0, 9, 11, 12, 14, 16, 10, 13, 15, 17, 19, 21, 23, 24, 18, 20, 22};

enum patternMode {
    SEQUENCE,
    ARP_UP,
    ARP_DOWN,
    ARP_PP,
    ARP_RANDOM
};

enum restMode {
    NONE,
    LAST,
    SECOND_LAST,
    MIDDLE_TWO,
    ONLY_FIRST
};

bool restPatterns[5][20] = {
    {true, true, true, true, true, true, true, true, true, true, true, true, true, true, true, true, true, true, true, true},
    {true, true, true, false, true, true, true, false, true, true, true, false, true, true, true, false, true, true, true, false},
    {true, true, false, false, true, true, false, false, true, true, false, false, true, true, false, false, true, true, false, false},
    {true, false, true, false, true, false, true, false, true, false, true, false, true, false, true, false, true, false, true, false},
    {true, false, true, true, false, true, false, true, true, false, true, false, true, true, false, true, false, true, true, false}
};

class ArpeggiatorSequencer {
    public:
    ArpeggiatorSequencer() {}
    ~ArpeggiatorSequencer() {}

    enum randomEvent {
        PITCH_UP,
        PITCH_DOWN,
        REST
    };

    void Init(BaseEngine **engines, clockManager *clockManager, Hardware *hw, OptionsManager *options) {
        engines_ = engines;
        clock_manager_ = clockManager;
        hw_ = hw;

        play_[0] = play_[1] = false;
        latch_[0] = latch_[1] = false;
        pattern_[0] = pattern_[1] = SEQUENCE;
        rest_pattern_[0] = rest_pattern_[1] = NONE;
        just_changed_pattern_[0] = just_changed_pattern_[1] = false;

        slice_key_mode_ = 0;

        left_lights_ = true;

        engine_type_ = 0;
        sustainMode_[0] = sustainMode_[1] = false;
        latch_key_ = false;

        midi_out_channel_[0] = options->midi_ch_out_chroma;
        midi_out_channel_[1] = options->midi_ch_out_slice;
        transport_out_ = options->transport_type == 0 || options->transport_type == 1;
    }

    void checkAndPop() { //This is going to send extra STOPs but I think its ok
        for (size_t i = 0; i < kNumEngines; ++i) {
            if (clock_edge_[i]) {
                if ((notes[i].size() > 0 && play_[i]) || just_swapped_sequence_[i]) { // Redundant check
                    if (restPatterns[rest_pattern_[i]][restIdx[i]]) {
                        pushKeyRequest(false, i);
                        if (notes[i][curIdx[i]].remove_) {
                            if (notes[i][curIdx[i]].key_ == 28 && engine_type_) {
                                slice_key_mode_ = 0;
                            }
                            notes[i].erase(notes[i].begin() + curIdx[i]);
                        }
                        if (notes[i].size() > 0) {
                            if ((i == 0) || (notes[i][curIdx[i]].key_ != 28) || (slice_key_mode_ != 3) || (!last_slice_played_)) {
                                getNewIdx(false, i);
                                getNewRestIdx(i);
                            }
                            pushKeyRequest(true, i);
                        }
                    }
                    else {
                        pushKeyRequest(false, i);
                        if (notes[i][curIdx[i]].remove_) {
                            notes[i].erase(notes[i].begin() + curIdx[i]);
                        }
                        getNewRestIdx(i);
                    }
                }
                if (i == engine_type_) {
                    left_lights_ = !left_lights_;
                }
                clock_edge_[i] = false;
            }
        }
    }

    void ProcessKeyRequests() {
        for (size_t i = 0; i < kNumEngines; ++i) {
            if (deferring_trig_[i]) {
                defer_frames_[i]++;
                if (defer_frames_[i] >= kMaxDeferFrames) {
                    if (i == 0) {
                        engines_[0]->request_fifo.PushBack(KeyRequest(KeyRequest::Type::START, 
                            out_pitch_[0], notes[0][curIdx[0]].key_, 127.f));

                        deferring_trig_[0] = false;

                        engines_[0]->handleRetrigEnv(false, notes[0][curIdx[0]].key_);
                    }
                    else {
                        engines_[1]->request_fifo.PushBack(KeyRequest(KeyRequest::Type::START, 
                            out_pitch_[1], notes[1][curIdx[1]].key_, 127.f));

                        deferring_trig_[1] = false;

                        engines_[1]->handleRetrigEnv(false, notes[1][curIdx[1]].key_);
                    }
                }
            }
        }
        if (!request_fifo.IsEmpty()) {
            KeyRequest req = request_fifo.PopFront();
            if (latch_[engine_type_]) {
                //seq mode
                if (req.type_ == KeyRequest::Type::START) {
                    bool existingNote = false;
                    for (size_t i = 0; i < notes[engine_type_].size(); ++i) {
                        if (notes[engine_type_][i].transpose_nn_ == req.transpose_nn_) {
                            existingNote = true;
                            if (checkNotePlaying(req.transpose_nn_)) { //need to check that not key28 in slice
                                if (req.key_ == 28 && slice_key_mode_ != 3 && engine_type_) {
                                    handleSliceKey(req);
                                }
                                else {
                                    notes[engine_type_][i].remove_ = true;
                                }
                            }
                            else {
                                if (req.key_ == 28 && engine_type_) {
                                    handleSliceKey(req);
                                }
                                else {
                                    removeKey(req);
                                }
                            }
                            if (sustainMode_[engine_type_]) {
                                req.type_ = KeyRequest::Type::STOP;
                                engines_[engine_type_]->request_fifo.PushBack(req);
                                hw_->queueMidiNote(midi_out_channel_[engine_type_], static_cast<int>(req.transpose_nn_ + 60), 127, NoteOff);
                            }
                            break;
                        }
                    }
                    if (!existingNote) {
                        if (req.key_ == 28 && engine_type_) {
                            handleSliceKey(req);
                        }
                        else {
                            addKey(req);
                            if (pattern_[engine_type_] != SEQUENCE) {
                                sortByKey();
                            }
                        }
                    }
                }
            }
            else {
                //arp mode
                if (req.type_ == KeyRequest::Type::START) {
                    addKey(req);
                    if (pattern_[engine_type_] != SEQUENCE) {
                        sortByKey();
                    }
                }
                else if (req.type_ == KeyRequest::Type::STOP) {
                    for (size_t i = 0; i < notes[engine_type_].size(); ++i) {
                        if (notes[engine_type_][i].key_ == req.key_) {
                            if (i == static_cast<size_t>(curIdx[engine_type_])) {
                                notes[engine_type_][i].remove_ = true;
                            }
                            else {
                                if (i < static_cast<size_t>(curIdx[engine_type_])) {
                                    curIdx[engine_type_]--;
                                }
                                notes[engine_type_].erase(notes[engine_type_].begin() + i);
                            }
                            break;
                        }
                    }
                }
            }
        }
    }

    void pushKeyRequest(bool start, size_t i) {
        if (start) {
            out_pitch_[i] = notes[i][curIdx[i]].transpose_nn_;
            if (i == 1) {
                out_pitch_[i] = 0.f;
            }
            bool event = static_cast<float>(rand()) / static_cast<float>(RAND_MAX) < randomness_[i];
            if (event) {
                size_t event_type = static_cast<randomEvent>(rand() % 2);
                switch (event_type) {
                    case randomEvent::PITCH_UP: {
                        out_pitch_[i] += 12.f;
                    }
                    break;
                    case randomEvent::PITCH_DOWN: {
                        out_pitch_[i] -= 12.f;
                    }
                    break;
                }
            }
            if (!i) {
                if (skip_next_note_[0]) {
                    skip_next_note_[0] = false;
                    return;
                }
                if (notes[0].size() == 1) {
                    deferring_trig_[0] = true;
                    defer_frames_[0] = 0;
                    engines_[0]->handleRetrigEnv(true, notes[0][curIdx[0]].key_);
                }
                else {
                    engines_[0]->request_fifo.PushBack(KeyRequest(KeyRequest::Type::START, 
                        out_pitch_[0], notes[0][curIdx[0]].key_, 127.f));
                }
            }
            else {
                if (skip_next_note_[1]) {
                    skip_next_note_[1] = false;
                    return;
                }
                if (notes[1][curIdx[1]].key_ == 28) {
                    if (slice_key_mode_ == 1) {
                        engines_[1]->setLastSlice(true);
                        last_slice_played_ = true;
                    }
                    else if (slice_key_mode_ == 2) {
                        engines_[1]->setLastSlice(false);
                        last_slice_played_ = false;
                    }
                    else if (slice_key_mode_ == 3 && !last_slice_played_) {
                        engines_[1]->setLastSlice(true);
                        last_slice_played_ = true;
                    }
                    else if (slice_key_mode_ == 3 && last_slice_played_) {
                        engines_[1]->setLastSlice(false);
                        last_slice_played_ = false;
                    }
                }
                if (notes[1].size() == 1) {
                    deferring_trig_[1] = true;
                    defer_frames_[1] = 0;
                    engines_[1]->handleRetrigEnv(true, notes[1][curIdx[1]].key_);
                }
                else {
                    engines_[1]->request_fifo.PushBack(KeyRequest(KeyRequest::Type::START, 
                        out_pitch_[i], notes[1][curIdx[1]].key_, 127.f));
                }
            }
            if (i == engine_type_) {
                size_t interval = (clock_manager_->getInterval() / 1000) * clock_manager_->getDiv(engine_type_);
                size_t now = System::GetNow();
                note_end_time_ = now + interval - 50;
            }
            hw_->queueMidiNote(midi_out_channel_[i], static_cast<int>(notes[i][curIdx[i]].transpose_nn_ + 60), 127, NoteOn);
        }
        else {
            if (!i) {
                if (just_swapped_sequence_[0]) {
                    engines_[0]->request_fifo.PushBack(KeyRequest(KeyRequest::Type::STOP, 
                        tempNote[0].transpose_nn_, tempNote[0].key_, 127.f));
                    
                    just_swapped_sequence_[0] = false;
                    if (!play_[0]) {
                        skip_next_note_[0] = true;
                    }
                }
                else if (just_changed_pattern_[0]) {
                    engines_[0]->request_fifo.PushBack(KeyRequest(KeyRequest::Type::STOP, 
                        tempNote[0].transpose_nn_, patternSwitchNote[0].key_, 127.f));

                    just_changed_pattern_[0] = false;
                }
                else {
                    engines_[0]->request_fifo.PushBack(KeyRequest(KeyRequest::Type::STOP, 
                        notes[0][curIdx[0]].transpose_nn_, notes[0][curIdx[0]].key_, 127.f));
                }
            }
            else {
                if (just_swapped_sequence_[1]) {
                    engines_[1]->request_fifo.PushBack(KeyRequest(KeyRequest::Type::STOP, 
                        0.f, tempNote[1].key_, 127.f));
                    
                    just_swapped_sequence_[1] = false;
                    if (!play_[1]) {
                        skip_next_note_[1] = true;
                    }
                }
                else if (just_changed_pattern_[1]) {
                    engines_[1]->request_fifo.PushBack(KeyRequest(KeyRequest::Type::STOP, 
                        0.f, patternSwitchNote[1].key_, 127.f));
                    
                    just_changed_pattern_[1] = false;
                }
                else {
                    engines_[1]->request_fifo.PushBack(KeyRequest(KeyRequest::Type::STOP, 
                        0.f, notes[1][curIdx[1]].key_, 127.f));
                }
            }
            hw_->queueMidiNote(midi_out_channel_[i], static_cast<int>(notes[i][curIdx[i]].transpose_nn_ + 60), 127, NoteOff);
        }
    }

    void Prepare() {
        ProcessKeyRequests();
        if (play_[0] || play_[1] || just_swapped_sequence_[0] || just_swapped_sequence_[1]) {
            checkAndPop();
        }
        checkSustain();
        checkDecouplePlay();
    }

    void setPlay(bool rising) {
        if (!rising) {
            play_key_ = false;
            if (just_decoupled_) {
                just_decoupled_ = false;
                return;
            }
            if (play_[0] && play_[1] && !just_turned_on_) {
                play_[0] = play_[1] = false;
                stopVoice(2);
                if (clock_manager_->getClockMode() == FREE && transport_out_) {
                    hw_->queueMidiTransport(midi_out_channel_[engine_type_], false);
                }
            }
            if (just_turned_on_) {
                just_turned_on_ = false;
            }
        }
        else {
            if (play_[0] ^ play_[1]) {
                play_[0] = play_[1] = true;
                getNewIdx(true, engine_type_);
                just_turned_on_ = true;
                if (sustainMode_[engine_type_]) {
                    stopEngineVoices(engine_type_);
                    sustainMode_[engine_type_] = false;
                }
                if (clock_manager_->getClockMode() == FREE && transport_out_) {
                    hw_->queueMidiTransport(midi_out_channel_[engine_type_], true);
                }
            }
            else if (!play_[0] && !play_[1]) {
                play_[0] = play_[1] = true;
                if (sustainMode_[engine_type_]) {
                    stopEngineVoices(engine_type_);
                    sustainMode_[engine_type_] = false;
                }
                getNewIdx(true, 0);
                getNewIdx(true, 1);
                last_slice_played_ = false;
                clock_edge_[0] = clock_edge_[1] = false;
                clock_manager_->setNow(0);
                clock_manager_->setNow(1);
                clock_manager_->setNow(2);
                for (size_t i = 0; i < kNumEngines; ++i) {
                    if (notes[i].size()) {
                        pushKeyRequest(true, i);
                    }
                }
                just_turned_on_ = true;
                if (clock_manager_->getClockMode() == FREE && transport_out_) {
                    hw_->queueMidiTransport(midi_out_channel_[engine_type_], true);
                }
            }
            last_play_press_ = System::GetNow();
            play_key_ = true;
        }
    }

    bool getPlay(size_t eng) {
        return play_[eng];
    }

    size_t getPlayType() {
        if (play_[engine_type_]) {
            return 2;
        }
        if (!play_[engine_type_] && play_[engine_type_ ^ 1]) {
            return 1;
        }
        else {
            return 0;
        }
    }

    void setPlayDirect(size_t eng, bool on) {
        play_[eng] = on;
    }

    void setPlayTransport(bool on) {
        if (on) {
            play_[0] = play_[1] = true;
            getNewIdx(true, 0);
            getNewIdx(true, 1);
            clock_edge_[0] = clock_edge_[1] = false;
            for (size_t i = 0; i < kNumEngines; ++i) {
                if (notes[i].size()) {
                    pushKeyRequest(true, i);
                }
            }
        }
        else {
            play_[0] = play_[1] = false;
            stopVoice(2);
        }
    }

    void setEngine(bool g) {
        engine_type_ = g ? 0 : 1;
    }

    bool checkNotePlaying(float req_nn_) {
        if (req_nn_ == notes[engine_type_][curIdx[engine_type_]].transpose_nn_ && play_[engine_type_] && notes[engine_type_].size()) {
            return true;
        }
        return false;
    }

    void setLatch(bool rising) {
        if (rising) {
            latch_key_ = true;
            last_latch_press_ = System::GetNow();
            if (just_set_latch_) {
                just_set_latch_ = false;
                return;
            }
            if (!latch_[engine_type_]) {
                latch_[engine_type_] = true;
                just_set_latch_ = true;
            }
        }
        else {
            latch_key_ = false;
            if (just_set_latch_) {
                just_set_latch_ = false;
                return;
            }
            if (sustainMode_[engine_type_]) {
                sustainMode_[engine_type_] = false;
                latch_[engine_type_] = true;
                stopAllVoices();
            }
            else {
                if (latch_[engine_type_]) {
                    latch_[engine_type_] = false;
                }

                if (!latch_[engine_type_]) {
                    stopVoice(engine_type_);
                    notes[engine_type_].clear();
                    slice_key_mode_ = 0;
                }
                else {
                    if (play_[engine_type_]) {
                        for (size_t i = 0; i < notes[engine_type_].size(); ++i) {
                            if (notes[engine_type_][i].key_ == 28 && engine_type_) {
                                slice_key_mode_ = 1; // If the slice key is already in, change to correct mode
                            }
                        }
                    }
                }
            }
        }
    }

    bool getLatch() {
        return latch_[engine_type_];
    }

    size_t getLatchType(size_t eng) {
        // pack latch as bit1 and sustain as bit0 -> 0..3
        return (static_cast<size_t>(latch_[eng]) << 1)
            | static_cast<size_t>(sustainMode_[eng]);
    }

    void setLatchDirect(size_t eng, size_t state) {
        latch_[eng] = (state >> 1) & 1u;
        sustainMode_[eng] = state & 1u;

        if (sustainMode_[eng]) {
            for (size_t i = 0; i < notes[eng].size(); ++i) {
                float nn = eng == 0 ? notes[eng][i].transpose_nn_ : 0.f;
                engines_[eng]->request_fifo.PushBack(KeyRequest(KeyRequest::Type::START, nn, notes[eng][i].key_, 127.f));

                hw_->queueMidiNote(midi_out_channel_[eng], static_cast<size_t>(notes[eng][i].transpose_nn_ + 60), 127, NoteOn);
            }
        }
    }

    bool getLeftLights() {
        return left_lights_;
    }

    void checkDecouplePlay() {
        if ((System::GetNow() - last_play_press_ > 1000) && (play_key_ && play_[engine_type_])) {
            stopVoice(engine_type_);
            play_[engine_type_] = false;
            just_decoupled_ = true;
            //notes[engine_type_].clear();
        }
    }

    void checkSustain() {
        if ((System::GetNow() - last_latch_press_ > 1000) && (latch_key_ && !play_[engine_type_] && latch_[engine_type_] && !sustainMode_[engine_type_])) {
            sustainMode_[engine_type_] = true;
            just_set_latch_ = true;

            // First start all red keys
            for (size_t i = 0; i < notes[engine_type_].size(); ++i) {
                engines_[engine_type_]->request_fifo.PushBack(KeyRequest(KeyRequest::Type::START, 
                            notes[engine_type_][i].transpose_nn_, notes[engine_type_][i].key_, 127.f));
            }

            // Then get all active non-red keys into red mode
            uint16_t activeKeys[NUM_VOICES] = {255, 255, 255, 255, 255, 255, 255, 255};
            float transpose_nns_[NUM_VOICES] = {0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f};
            engines_[engine_type_]->getActiveKeys(activeKeys, transpose_nns_);
            for (size_t i = 0; i < NUM_VOICES; ++i) {
                if (activeKeys[i] != 255) {
                    NoteInfo info;
                    info.key_ = activeKeys[i];
                    info.order_ = notes[engine_type_].size();
                    info.transpose_nn_ = transpose_nns_[i];
                    info.key_number_ = keyMap[info.key_];
                    info.remove_ = false;
                    notes[engine_type_].push_back(info);
                }
            }
        }
    }

    bool getSustain() {
        return sustainMode_[engine_type_];
    }

    bool getEngineSustain(size_t e) { // I am trying to avoid as much as possible modifying anything that already exists
        return sustainMode_[e];
    }

    void handleSliceKey(KeyRequest req) {
        slice_key_mode_ = (slice_key_mode_ + 1) % 4;
        if (slice_key_mode_ == 1) {
            addKey(req);
        }
        else if (slice_key_mode_ == 0) {
            removeKey(req);
        }
        else if (slice_key_mode_ == 3) {
            last_slice_played_ = false;
        }

    }

    void stopVoice(size_t eng) {
        if (eng == 2) {
            for (size_t i = 0; i < kNumEngines; ++i) {
                engines_[i]->request_fifo.PushBack(KeyRequest(KeyRequest::Type::STOP, 
                    0.f, notes[i][curIdx[i]].key_, 127.f));
                hw_->queueMidiNote(midi_out_channel_[i], static_cast<int>(notes[i][curIdx[i]].transpose_nn_ + 60), 127, NoteOff);
            }
        }
        else {
            engines_[eng]->request_fifo.PushBack(KeyRequest(KeyRequest::Type::STOP, 
                0.f, notes[eng][curIdx[eng]].key_, 127.f));
            hw_->queueMidiNote(midi_out_channel_[eng], static_cast<int>(notes[eng][curIdx[eng]].transpose_nn_ + 60), 127, NoteOff);
        }
    }

    bool isKeyPlaying(size_t key) {
        bool led_on = false;
        if (engines_[engine_type_]->isKeyPlaying(key)) {
            led_on = true;
        }
        if (play_[engine_type_]) {
            volatile size_t next_key = notes[engine_type_][peekNextNote()].key_;
            volatile size_t now = System::GetNow();
            if (key == next_key && now > note_end_time_) {
                led_on = false;
            }
        }
        return led_on;
    }

    void incrementPattern() {
        uint8_t old_pattern_ = pattern_[engine_type_];
        pattern_[engine_type_]++;
        if (pattern_[engine_type_] > 4) {
            pattern_[engine_type_] = SEQUENCE;
        }
        if (old_pattern_ == SEQUENCE) {
            if (!just_changed_pattern_[engine_type_]) {
                just_changed_pattern_[engine_type_] = true;
                patternSwitchNote[engine_type_] = notes[engine_type_][curIdx[engine_type_]];
            }
            sortByKey();
        }
        else if (old_pattern_ == ARP_RANDOM) {
            if (!just_changed_pattern_[engine_type_]) {
                just_changed_pattern_[engine_type_] = true;
                patternSwitchNote[engine_type_] = notes[engine_type_][curIdx[engine_type_]];
            }
            std::sort(notes[engine_type_].begin(), notes[engine_type_].end(), [](const NoteInfo& a, const NoteInfo& b) {
                return a.order_ < b.order_;
            });
        }
    }

    uint8_t getPattern() {
        return pattern_[engine_type_];
    }

    void incrementRestMode() {
        rest_pattern_[engine_type_]++;
        if (rest_pattern_[engine_type_] > 4) {
            rest_pattern_[engine_type_] = NONE;
        }
    }

    uint8_t getRestMode() {
        return rest_pattern_[engine_type_];
    }

    void setPattern(size_t eng, size_t pat) {
        pattern_[eng] = pat;
    }

    void setRestMode(size_t eng, size_t rst) {
        rest_pattern_[eng] = rst;
    }

    bool isKeyInSeq(size_t key, size_t eng) {
        for (size_t i = 0; i < notes[eng].size(); ++i) {
            if (notes[eng][i].key_ == key) {
                return true;
            }
        }
        return false;
    }

    bool isRestPlaying(size_t key) {
        for (size_t i = 0; i < notes[1].size(); ++i) {
            if (notes[1][curIdx[1]].key_ == key) {
                return true;
            }
        }
        return false;
    }

    void getColors(float *leds) {
        if (slice_key_mode_ == 1) {
            leds[0] = 1.f;
            leds[1] = 0.f;
            leds[2] = 0.f;
        }
        else if (slice_key_mode_ == 2) {
            leds[0] = 1.f;
            leds[1] = .95f;
            leds[2] = .05f;
        }
        else if (slice_key_mode_ == 3) {
            leds[0] = 1.f;
            leds[1] = .2f;
            leds[2] = 0.f;
        }
    }

    void setClockEdge(size_t eng) {
        clock_edge_[eng] = true;
    }

    void setSequence(size_t eng, std::vector<NoteInfo> *seq) {
        notes[eng] = *seq;
    }

    std::vector<NoteInfo> *getSequence(size_t eng) {
        return &notes[eng];
    }

    void setTempNote() {
        if (sustainMode_[engine_type_]) {
            stopAllVoices();
            return;
        }
        if (!just_swapped_sequence_[0] && !just_swapped_sequence_[1]) {
            for (size_t i = 0; i < kNumEngines; ++i) {
                if (notes[i].size()) {
                    tempNote[i] = notes[i][curIdx[i]];
                    just_swapped_sequence_[i] = true;
                }
            }
        }
    }

    void checkTempNote() {
        for (size_t i = 0; i < kNumEngines; ++i) {
            if (sustainMode_[i]) {
                just_swapped_sequence_[i] = false;
            }
        }
    }

    void setRandomness(float val, size_t eng) {
        randomness_[eng] = val;
    }

    FIFO<KeyRequest, 64> request_fifo;
    std::vector<NoteInfo> notes[kNumEngines];

    private:

    void getNewIdx(bool getFirstElement, size_t i) {
        if (notes[i].size()) {
            if (getFirstElement) {
                switch (pattern_[i]) {
                    case SEQUENCE: {
                        curIdx[i] = 0;
                    }
                    break;
                    case ARP_UP: {
                        curIdx[i] = 0;
                    }
                    break;
                    case ARP_DOWN: {
                        curIdx[i] = notes[i].size() - 1;
                    }
                    break;
                    case ARP_PP: {
                        curIdx[i] = 0;
                    }
                    break;
                    case ARP_RANDOM: {
                        curIdx[i] = rand() % notes[i].size();
                    }
                    break;
                }
                restIdx[i] = 0;
            }
            else {
                switch (pattern_[i]) {
                    case SEQUENCE: {
                        curIdx[i]++;
                        if (curIdx[i] > notes[i].size() - 1) {
                            curIdx[i] = 0;
                        }
                    }
                    break;
                    case ARP_UP: {
                        curIdx[i]++;
                        if (curIdx[i] > notes[i].size() - 1) {
                            curIdx[i] = 0;
                        }
                    }
                    break;
                    case ARP_DOWN: {
                        curIdx[i]--;
                        if (curIdx[i] < 0) {
                            curIdx[i] = notes[i].size() - 1;
                        }
                    }
                    break;
                    case ARP_PP: {
                        static bool up_;
                        if (up_) {
                            curIdx[i]++;
                            if (curIdx[i] > notes[i].size() - 1) {
                                curIdx[i]--;
                                up_ = false;
                            }
                        }
                        else {
                            curIdx[i]--;
                            if (curIdx[i] < 0) {
                                curIdx[i]++;
                                up_ = true;
                            }
                        }
                    }
                    break;
                    case ARP_RANDOM: {
                        curIdx[i] = rand() % notes[i].size();
                    }
                    break;
                }
            }
        }
    }

    size_t peekNextNote() {
        int32_t next_note = 0;
        switch (pattern_[engine_type_]) {
            case SEQUENCE: {
                next_note = curIdx[engine_type_] + 1;
                if (next_note > notes[engine_type_].size() - 1) {
                    next_note = 0;
                }
            }
            break;
            case ARP_UP: {
                next_note = curIdx[engine_type_] + 1;
                if (next_note > notes[engine_type_].size() - 1) {
                    next_note = 0;
                }
            }
            break;
            case ARP_DOWN: {
                next_note = curIdx[engine_type_] - 1;
                if (next_note < 0) {
                    next_note = notes[engine_type_].size() - 1;
                }
            }
            break;
            case ARP_PP: {
                static bool up_;
                if (up_) {
                    next_note = curIdx[engine_type_] + 1;
                    if (next_note > notes[engine_type_].size() - 1) {
                        next_note--;
                        up_ = false;
                    }
                }
                else {
                    next_note = curIdx[engine_type_] - 1;
                    if (next_note < 0) {
                        next_note++;
                        up_ = true;
                    }
                }
            }
            break;
            case ARP_RANDOM: {
                
            }
            break;
        }
        return next_note;
    }

    void getNewRestIdx(size_t i) {
        restIdx[i]++;
        if (restIdx[i] > 19) {
            restIdx[i] = 0;
        }
    }

    void addKey(KeyRequest req) {
        NoteInfo info_;
        info_.key_ = req.key_;
        info_.order_ = notes[engine_type_].size();
        info_.transpose_nn_ = req.transpose_nn_;
        info_.key_number_ = keyMap[info_.key_];
        info_.remove_ = false;
        notes[engine_type_].push_back(info_);
    }

    void removeKey(KeyRequest req) {
        for (size_t i = 0; i < notes[engine_type_].size(); ++i) {
            if (notes[engine_type_][i].key_ == req.key_) {
                if (i < static_cast<size_t>(curIdx[engine_type_])) {
                    curIdx[engine_type_]--;
                }
                notes[engine_type_].erase(notes[engine_type_].begin() + i);
                break;
            }
        }
    }

    void stopAllVoices() {
        for (size_t eng = 0; eng < kNumEngines; ++eng) {
            for (size_t i = 0; i < notes[eng].size(); ++i) {
                engines_[eng]->request_fifo.PushBack(KeyRequest(KeyRequest::Type::STOP, 
                    0.f, notes[eng][i].key_, 127.f));

                hw_->queueMidiNote(midi_out_channel_[engine_type_], static_cast<int>(notes[engine_type_][i].transpose_nn_ + 60), 127, NoteOff);
            }
        }
    }

    void stopEngineVoices(size_t eng) {
        for (size_t i = 0; i < notes[eng].size(); ++i) {
            engines_[eng]->request_fifo.PushBack(KeyRequest(KeyRequest::Type::STOP, 
                0.f, notes[eng][i].key_, 127.f));

            hw_->queueMidiNote(midi_out_channel_[engine_type_], static_cast<int>(notes[engine_type_][i].transpose_nn_ + 60), 127, NoteOff);
        }
    }

    void sortByKey() {
        std::sort(notes[engine_type_].begin(), notes[engine_type_].end(), [](const NoteInfo& a, const NoteInfo& b) {
            return a.key_number_ < b.key_number_;
        });
    }

    
    NoteInfo tempNote[kNumEngines];
    NoteInfo patternSwitchNote[kNumEngines];
    int8_t curIdx[kNumEngines], restIdx[kNumEngines];
    uint8_t pattern_[kNumEngines], rest_pattern_[kNumEngines];
    BaseEngine **engines_;
    clockManager *clock_manager_;
    Hardware *hw_;
    bool play_[kNumEngines], latch_[kNumEngines];
    bool clock_edge_[kNumEngines];
    uint8_t engine_type_;
    bool sustainMode_[kNumEngines];
    bool latch_key_, play_key_;

    bool deferring_trig_[kNumEngines];
    size_t defer_frames_[kNumEngines];

    size_t note_end_time_;

    uint8_t slice_key_mode_;
    bool last_slice_played_;

    uint8_t midi_out_channel_[kNumEngines];
    bool transport_out_;

    uint32_t last_latch_press_, last_play_press_;

    bool just_decoupled_, just_turned_on_;
    bool just_swapped_sequence_[kNumEngines];
    bool skip_next_note_[kNumEngines];
    bool just_changed_pattern_[kNumEngines];
    bool just_set_latch_;

    float randomness_[kNumEngines];
    float out_pitch_[kNumEngines];

    bool left_lights_;
};
