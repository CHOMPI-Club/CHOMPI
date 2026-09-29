/** @file Sequencer.h
 *  @brief Step sequencer uses clock manager timer to check when to advance, and uses
 *  UI pages to get new inputs.
 */
#pragma once

#include "subtractiveEngine.h"
#include "clockManager.h"

class Sequencer {
    public:
    static constexpr uint8_t kMaxSeqLen = 32;
    Sequencer() {};
    ~Sequencer() {};

    void Init(myEngine *engine, clockManager *clock_manager, Hardware *hw) {

        engine_ = engine;
        clock_manager_ = clock_manager;
        hw_ = hw;

        playing = false;
        playButtonOn = false;
        recording = false;
        recordButtonOn = false;
        isFirstRun = true;
        sequenceLength = 0;
        currentIdx = 0;
        muted = false;
        last_step_muted = false;
        loop_action_taken = false;
        seq_full_hit = false;
        cleared_all = false;
        left_lights = true;
        last_midi_note_ = 60;
        gate_frac = .5f;
        gate_stopped = false;

    }

    void insertNextKey(KeyRequest req) {
        if (sequenceLength < kMaxSeqLen) {
            mySequence[sequenceLength] = req;
            sequenceLength++;
        }
        else {
            seq_full_hit = true; // UI blinks the loop LED 4x
        }
    }

    void insertRest() { // Insert blank step
        if (sequenceLength < kMaxSeqLen) {
            sequenceLength++;
        }
        else {
            seq_full_hit = true;
        }
    }

    void clearSequence() {
        stopPlayback();
        for (int i = 0; i < kMaxSeqLen; ++i) {
            mySequence[i] = KeyRequest();
        }
        sequenceLength = 0;
        playing = false;
        recording = false;
    }

    void clearLastStep() {
        if (sequenceLength > 0) {
            // Check if we are deleting the currently playing note
            if (currentIdx == sequenceLength - 1 && mySequence[currentIdx].type_ != KeyRequest::Type::DUMMY) {
                KeyRequest removeReq = mySequence[currentIdx];
                removeReq.type_ = KeyRequest::Type::STOP;
                removeReq.src_ = KeyRequest::Source::SEQUENCER;
                engine_->request_fifo.PushBack(removeReq);
                hw_->queueMidiNote(midi_out_channel, last_midi_note_, 127, NoteOff);
            }
    
            sequenceLength--;
            mySequence[sequenceLength] = KeyRequest();
    
            // Ensure currentIdx doesn't go out of bounds
            if (currentIdx >= sequenceLength) {
                currentIdx = 0;
            }
        }
    }

    void stopPlayback() {
        hw_->queueMidiTransport(false);
        KeyRequest removeReq = mySequence[currentIdx];
        removeReq.type_ = KeyRequest::Type::STOP;
        removeReq.src_ = KeyRequest::Source::SEQUENCER;
        engine_->request_fifo.PushBack(removeReq);
        hw_->queueMidiNote(midi_out_channel, last_midi_note_, 127, NoteOff);
        currentIdx = 0;
        isFirstRun = true;
    }

    void startPlayback() {
        playing = true;
    }

    void playButton(bool rising) {
        togglePlaying(rising);
        if (rising) {
            last_button_press = System::GetNow();
            playButtonOn = true;
        }
        else {
            playButtonOn = false;
        }
        
    }

    void loopButton(bool rising) {
        if (rising) {
            last_button_press = System::GetNow();
            recordButtonOn = true;
            loop_action_taken = false;
        }
        else {
            recordButtonOn = false;
            if (!loop_action_taken) {
                toggleRecording(true);
            }
        }
    }

    bool checkReset() {
        if (System::GetNow() - last_button_press > 1250) {
            if (playButtonOn && recordButtonOn && sequenceLength > 0) {
                clearSequence();
                cleared_all = true;      // lets the UI run the erase animation
                loop_action_taken = true; //  the release must not re-arm recording
                last_button_press = System::GetNow();  // Reset timer after execution
                return true;
            }
            else if (!playButtonOn && recordButtonOn && sequenceLength > 0) {
                clearLastStep();
                loop_action_taken = true; // the release must not toggle record
                last_button_press = System::GetNow();  // Reset timer here too
                return true;
            }
        }
        return false;
    }

    // Flags for the UI (sequence-full blink + erase animation)
    bool showSequenceFull() {
        bool f = seq_full_hit;
        seq_full_hit = false;
        return f;
    }

    bool showClearedAll() {
        bool f = cleared_all;
        cleared_all = false;
        return f;
    }

    void setMuted(bool m) {
        muted = m;
    }

    // black keys 18/19/20 = 10% / 50% / 100% of the
    // step interval. Not stored in presets, its a global performance setting.
    void setGate(float frac) {
        gate_frac = frac;
    }

    float getGate() {
        return gate_frac;
    }

    void checkAndPop() {
        if (sequenceLength > 0) { // Only run if there is a sequence
            if (isFirstRun) { // First run needs different behavior
                if (!muted && mySequence[0].type_ != KeyRequest::Type::DUMMY) {
                    KeyRequest addReq = mySequence[currentIdx];
                    addReq.type_ = KeyRequest::Type::START;
                    addReq.src_ = KeyRequest::Source::SEQUENCER;
                    engine_->request_fifo.PushBack(addReq);
                    {
                        int n = static_cast<int>(addReq.transpose_nn_) + 60 + 12 * engine_->getOctave();
                        if(n < 0) n = 0; else if(n > 127) n = 127;
                        last_midi_note_ = static_cast<uint8_t>(n);
                    }
                    hw_->queueMidiNote(midi_out_channel, last_midi_note_, 127, NoteOn);
                }
                last_step_muted = muted;
                gate_stopped = false; // gate re-armed for the new step
                clock_manager_->setNow();
                isFirstRun = false;
                return; // On first run, just do this then leave
            }
            if (clock_manager_->checkIntervalExpired()) {
                KeyRequest removeReq = mySequence[currentIdx];
                // A note the gate already cut off shouldn't stop twice
                if (removeReq.type_ != KeyRequest::Type::DUMMY && !last_step_muted && !gate_stopped) {
                    removeReq.type_ = KeyRequest::Type::STOP;
                    removeReq.src_ = KeyRequest::Source::SEQUENCER;
                    engine_->request_fifo.PushBack(removeReq);
                    hw_->queueMidiNote(midi_out_channel, last_midi_note_, 127, NoteOff);
                }
                getNewIdx();
                KeyRequest addReq = mySequence[currentIdx];
                if (!muted && addReq.type_ != KeyRequest::Type::DUMMY) {
                    addReq.type_ = KeyRequest::Type::START;
                    addReq.src_ = KeyRequest::Source::SEQUENCER;
                    engine_->request_fifo.PushBack(addReq);
                    {
                        int n = static_cast<int>(addReq.transpose_nn_) + 60 + 12 * engine_->getOctave();
                        if(n < 0) n = 0; else if(n > 127) n = 127;
                        last_midi_note_ = static_cast<uint8_t>(n);
                    }
                    hw_->queueMidiNote(midi_out_channel, last_midi_note_, 127, NoteOn);
                }
                last_step_muted = muted; // A suppressed step must not emit a note-off later
                gate_stopped = false; // gate re-armed for the new step
                left_lights = !left_lights; // alternate lights
                clock_manager_->setNow();
            }
            else if (gate_frac < 1.f && !gate_stopped
                     && clock_manager_->checkGateExpired(gate_frac)) {
                KeyRequest removeReq = mySequence[currentIdx];
                if (removeReq.type_ != KeyRequest::Type::DUMMY && !last_step_muted) {
                    removeReq.type_ = KeyRequest::Type::STOP;
                    removeReq.src_ = KeyRequest::Source::SEQUENCER;
                    engine_->request_fifo.PushBack(removeReq);
                    hw_->queueMidiNote(midi_out_channel, last_midi_note_, 127, NoteOff);
                }
                gate_stopped = true; //set even for rests/muted steps so this only runs once
            }
        }
    }

    void getNewIdx() {
        currentIdx++;
        if (currentIdx > sequenceLength - 1) {
            currentIdx = 0;
        }
    }

    void togglePlaying(bool rising) { //Does this need to be its own function? Probably not
        if (rising) {
            playing = !playing;
            if (!playing) {
                stopPlayback();
            }
            else {
                hw_->queueMidiTransport(true);
            }
        }
    }

    bool getPlaying() {
        return playing;
    }

    void toggleRecording(bool rising) {
        if (rising) {
            recording = !recording;
        }
    }

    bool getLeftLights() {
        return left_lights;
    }

    bool getRecording() {
        return recording;
    }

    bool getSequence() {
        return sequenceLength > 0;
    }

    void setMidiChannel(uint8_t ch) {
        midi_out_channel = ch;
    }

    bool muted;
    bool last_step_muted;
    float gate_frac;
    bool gate_stopped;
    bool left_lights;
    bool loop_action_taken;
    bool seq_full_hit;
    bool cleared_all;
    bool playing;
    bool playButtonOn;
    bool recording;
    bool recordButtonOn;
    bool isFirstRun;
    KeyRequest mySequence[kMaxSeqLen];
    uint8_t sequenceLength;
    uint8_t currentIdx;
    uint8_t last_midi_note_;

    private:
    myEngine *engine_;
    clockManager *clock_manager_;
    Hardware *hw_;
    uint32_t last_button_press;
    uint8_t midi_out_channel;
};