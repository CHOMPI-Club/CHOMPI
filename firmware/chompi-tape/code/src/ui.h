#include "hardware.h"
#include "ui_utils.h"
#include "NormalPage.h"
#include "MenuPage.h"
#include "TestPage.h"
#include "BootPage.h"
#include "NoSDPage.h"
#include "RainbowWavePage.h"
#include "DSPEngine.h"
#include "FileCopier.h"
// #include "StereoDelayEffect.h"

namespace chompi
{

    static const float enc_defaults[3][6] = {
        {.83f, 0.f, 1.f, 0.f, .75f, .84f}, // page 1
        {.704f, 0.f, 0.f, 0.f, 0.f, .75f},   // page 2
        {0.f, 0.f, 0.f, .5f, 0.f, 0.f},   // page 3
    };

    static const uint8_t midi2key[49] = {
        44, 45, 46, 47, 48, 49, 50, 51, 52, 53, 54, 55,
        32, 33, 34, 35, 36, 37, 38, 39, 40, 41, 42, 43,
        15, 7, 8, 12, 9, 10, 13, 11,
        14, 16, 21, 17, 18, 22, 19, 23,
        20, 24, 29, 25, 30, 26, 31, 27, 28
    };

    static const uint8_t encoder_map[6] = {1, 2, 3, 0, 4, 5};
    static const uint32_t kMaxFileWriteChunkSize = 1024;

    enum CanvasIds
    {
        canvasLedDisplay = 0,
        NUM_CANVASES
    };

    enum UiMode
    {
        MIDI,
    };

    class UserInterface
    {
    public:
        void Init(Hardware *hw, Engine *fx, FileCopier* copier, PresetManager* pre,
                    uint8_t ch_in, uint8_t ch_out, bool pitch_shift_quant, bool split_delay)
        {
            hw_ = hw;
            fx_ = fx;
            manager = fx_->GetFileManager();
            presets_manager = pre;

            midi_in_ch = ch_in;

            InitFromPresets();

            /** Describe UI special controls - if any */
            daisy::UI::SpecialControlIds specialControlIds; /**< None here */
            /** Canvas Descriptor */
            daisy::UiCanvasDescriptor ledDisplayDescriptor;
            ledDisplayDescriptor.id_ = canvasLedDisplay;
            ledDisplayDescriptor.handle_ = nullptr;
            ledDisplayDescriptor.updateRateMs_ = 16; /**< 30Hz */
            ledDisplayDescriptor.clearFunction_ = ClearLeds;
            ledDisplayDescriptor.flushFunction_ = FlushLeds;

            /** Init */
            ui.Init(event_queue,
                    specialControlIds,
                    {ledDisplayDescriptor},
                    canvasLedDisplay);

            normal_page_.Init(hw_, fx_, copier, enc_rows, 
                def_rows, knob_page, presets_manager, ch_out, pitch_shift_quant, split_delay);
            ui.OpenPage(normal_page_);

            boot_page_.Init(hw_, fx_);
            ui.OpenPage(boot_page_);

            menu_page_.Init(hw_, fx_, copier, enc_rows, def_rows, knob_page,
                presets_manager, pitch_shift_quant, split_delay);

            test_page_.Init(hw_, fx_);

            uint16_t state = 0;
            for (int i = 0; i < 512; i++)
            {
                hw->ProcessAllControls();
                daisy::System::DelayUs(500);
                state += !hw_->button_sr.State(static_cast<int>(Hardware::SwId::SW_TOG));
            }

            // state should be debounced
            if (state >= 480)
                event_queue.AddButtonPressed(static_cast<int>(Hardware::SwId::SW_TOG), 1);
        }

        inline bool InTestMode() { return test_page_.IsActive(); }
        void TestMode()
        {
            ui.OpenPage(test_page_);
            normal_page_.SetInitIgnore(false);
        }

        void NoSDCard()
        {
            menu_page_.NoSDCard();
        }

        void NoSDCardAnimation(bool run)
        {
            if(run)
            {
                ui.ClosePage(menu_page_);
                ui.ClosePage(test_page_);
                ui.OpenPage(no_sd_page_);
            }
            else
                ui.ClosePage(no_sd_page_);
        }

        void RainbowWave() 
        { 
            if(!test_page_.IsActive()) 
                ui.OpenPage(rainbow_page_);
        }
        inline bool InRainbows() { return rainbow_page_.IsActive(); }

        void StopBootAnimation()
        {
            ui.ClosePage(boot_page_);
        }

        bool key_cc[2];
        void ProcessMidi()
        {
            daisy::MidiEvent event;
            while(hw_->GetMidi(event))
            {
                // only accept input on channel 1
                if(event.channel != midi_in_ch)
                    continue;

                switch(event.type)
                {
                    case NoteOn:
                    {
                        int key = event.data[0];
                        key -= 24;
                        if(key > 48|| key < 0)
                            break;

                        if(fx_->GetVoiceMode() == VoiceMode::CUBBI)
                        {
                            size_t slot = KeyToSlot(midi2key[key]);
                            if(slot == kSlotNone)
                                break;

                            normal_page_.OpenCubbiSlot(slot);
                        }
 
                        if(!test_page_.IsActive()) {
                            fx_->request_fifo.PushBack(KeyRequest(KeyRequest::Type::START, 
                                key - 36, midi2key[key], event.data[1] + 1));

                            if(fx_->GetLooperRecordArm())
                                fx_->ToggleLooperRecord();
                        }
                        else {
                            event_queue.AddButtonPressed(midi2key[key], 1, true);
                        }
                    }
                    break;
                    case NoteOff:
                    {
                        int key = event.data[0];
                        key -= 24;
                        if(key > 48|| key < 0)
                            break;

                        fx_->request_fifo.PushBack(KeyRequest(KeyRequest::Type::STOP, 
                            key - 36, midi2key[key], event.data[1] + 1));

                        if (test_page_.IsActive()) {
                            event_queue.AddButtonReleased(midi2key[key]);
                        }
                    }
                    break;
                    case ControlChange:
                    {
                        if(menu_page_.IsActive())
                            break;

                        uint8_t cc = event.data[0];
                        uint8_t val = event.data[1];

                        if(cc >= 20 && cc < 26)
                        {
                            uint8_t knob = cc - 20;

                            if(knob == 4 && !fx_->IsLooperPlaying())
                                break;

                            event_queue.AddEncoderTurned(knob, val, 1);
                        }
                        else if(cc == 26 || cc == 27)
                        {
                            const uint8_t idx = cc - 26;

                            const bool last = key_cc[idx];

                            // top 1/3 is high, bottom 1/3 is low, middle 1/3 is dead zone
                            if(val > 84)
                                key_cc[idx] = true;
                            else if(val < 42)
                                key_cc[idx] = false;

                            if(!last && key_cc[idx]) // rising edge
                            {
                                event_queue.AddButtonPressed(cc + 7, 1, true);
                            }
                            else if(last && !key_cc[idx]) // falling edge
                            {
                                event_queue.AddButtonReleased(cc + 7);
                            }
                        }
                    }
                    default:
                    break;
                }
            }
        }

        inline bool GetToggleState() { return toggle_state; }

        uint32_t last_force_off;
        void GenerateEvents()
        {

            /** 
                Hack to fix stuck notes for now.
                Every so often sends another message to turn off a voice that has a key assigned
                if that key is in the off state 

                Interferes with midi on :(
            */
            // const uint32_t now = System::GetNow();
            // if(now - last_force_off > 50)
            // {
            //     for(size_t i = 0; i < kMaxPoly; i++)
            //     {
            //         const int key = fx_->GetPlayingKey(i);
            //         if(!hw_->button_sr.State(key))
            //         {
            //             fx_->request_fifo.PushBack(KeyRequest(KeyRequest::Type::STOP, 0, key, 127.f));
            //         }
            //     }

            //     last_force_off = now;
            // }


            toggle_state = hw_->GetToggleState();

            if(menu_page_.IsActive() && menu_page_.IsClosable())
            {
                ui.ClosePage(menu_page_);
                normal_page_.ResetSmtLeds();
            }

            if(test_page_.IsClosable() && test_page_.IsActive())
            {
                ui.ClosePage(test_page_);
                normal_page_.ResetSmtLeds();
            }

            if(rainbow_page_.IsClosable() && rainbow_page_.IsActive())
            {
                ui.ClosePage(rainbow_page_);
                normal_page_.ResetSmtLeds();
            }


            for (int i = 0; i < static_cast<int>(Hardware::SwId::SR_LAST); i++)
            {
                if (i == ENC_5_SW)
                    continue; // skip this one
                else if(i == static_cast<int>(Hardware::SwId::SW_TOG))
                {
                    // this should be smoothed
                    normal_page_.SetSwitchState(toggle_state);
                    test_page_.SetSwitchState(toggle_state);
                    menu_page_.SetSwitchState(toggle_state);
                }
                else if (hw_->button_sr.FallingEdge(i))
                {
                    // chompi key changes page to MenuPage
                    if (i == static_cast<int>(Hardware::SwId::KEY_26))
                    {
                        // normal_page_.RefreshLeds();
                    }
                    event_queue.AddButtonReleased(i);
                }
                else if (hw_->button_sr.RisingEdge(i))
                {
                    // chompi key changes page to MenuPage
                    if (i == static_cast<int>(Hardware::SwId::KEY_26)
                        && toggle_state
                        && normal_page_.IsActive()
                        && !boot_page_.IsActive()
                        && !rainbow_page_.IsActive()
                        && !test_page_.IsActive())
                    {
                        // normal_page_.CacheLeds();
                        ui.OpenPage(menu_page_);             
                    }
                    event_queue.AddButtonPressed(i, 1);
                }
            }

            if (hw_->enc[4].FallingEdge())
                event_queue.AddButtonReleased(ENC_5_SW);
            else if (hw_->enc[4].RisingEdge())
                event_queue.AddButtonPressed(ENC_5_SW, 1);

            for (int i = 0; i < static_cast<int>(Hardware::EncoderId::ENC_LAST); i++)
            {
                int inc = hw_->enc[i].Increment();
                if (inc == 1 || inc == -1)
                {
                    uint8_t enc = encoder_map[i];

                    if(enc == 0 || enc == 4) // pitch knobs are finer
                        event_queue.AddEncoderTurned(enc, inc, 0);
                    else
                        event_queue.AddEncoderTurned(enc, inc * 3, 0);
                }
            }
        }

        FileStreamingManager* manager;
        FIL fptr_pre;
        char fname[32];
        static const size_t kPreFileSize = 8192;
        char presets_file[kPreFileSize]; // too big? not big enough?

        // pitch, start, end, att, decay, autoloop, sustainactive, gain, pan
        const float defaults[9] = {   enc_defaults[0][0], enc_defaults[0][1], enc_defaults[0][2],
                                enc_defaults[1][1], enc_defaults[1][2], 1.f,
                                1.f, enc_defaults[1][0], .5f};

        void InitFromPresets()
        {
            presets_manager->Init(defaults);

            strcpy(fname, "presets.json");

            FRESULT fres;
            fres = f_open(&fptr_pre, fname, (FA_OPEN_ALWAYS | FA_WRITE | FA_READ));

            if(fres == FR_OK)
            {
                UINT br;
                fres = f_read(&fptr_pre, presets_file, kPreFileSize, &br);
                if(fres == FR_OK)
                {
                    presets_manager->Parse(presets_file, kPreFileSize);
                    fres = f_lseek(&fptr_pre, 0);
                    fres = f_write(&fptr_pre, presets_file, strlen(presets_file), nullptr);
                    fres = f_truncate(&fptr_pre);
                    fres = f_sync(&fptr_pre);
                }
            }
            // else?


        }

        void DoEvents() { ui.Process(); }

        uint32_t file_len = 0;
        uint8_t write_stage = 0;
        // for some reason this causes clicking if you optimize it higher than this...
        void TestPresets()
        {
            if(write_stage == 0) // start chunked write
            {
                PresetManager::Result res = presets_manager->WriteWholeFile(presets_file, kPreFileSize);
                if(res == PresetManager::Result::OK)
                {
                    write_stage = 1;
                }    
            }
        }


        void WritePresets() __attribute__((optimize("-O0")))
        {
            if(write_stage == 0)
            {
                // do nothing
            }
            else if(write_stage == 1)
            {
                file_len = strlen(presets_file);

                char name[32];
                strcpy(name, "presets_temp.json");
                f_open(&fptr_pre, name, (FA_OPEN_ALWAYS | FA_WRITE | FA_READ));

                f_lseek(&fptr_pre, 0);

                write_stage = 2;
            }
            else if(f_tell(&fptr_pre) < file_len && write_stage == 2) // write the next chunk
            {
                uint32_t write_size = kMaxFileWriteChunkSize;
                if(f_tell(&fptr_pre) + write_size >= file_len)
                    write_size = file_len - f_tell(&fptr_pre);

                f_write(&fptr_pre, &presets_file[f_tell(&fptr_pre)], write_size, nullptr);
                f_sync(&fptr_pre);

                if(f_tell(&fptr_pre) >= file_len)
                    write_stage = 3;
            }
            else if(write_stage == 3)
            {
                f_truncate(&fptr_pre);
                f_sync(&fptr_pre);

                char from[32];
                char to[32];

                strcpy(from, "presets_temp.json");
                strcpy(to, "presets.json");

                f_unlink(to);
                f_rename(from, to);

                write_stage = 0;
            }
        }

        // inline void TrigClear() { normal_page_.TrigClear(); }

    inline void TestPowerCable(bool cable) { test_page_.SetPowerCable(cable); }
    inline void TestBMC(bool good) { test_page_.SetBMCGood(good); }

    // public so we can check IsActive from main
    BootPage boot_page_;
    NormalPage normal_page_;
    MenuPage menu_page_;
    TestPage test_page_;
    NoSDPage no_sd_page_;
    RainbowPage rainbow_page_;
    private:
        daisy::UiEventQueue event_queue;
        daisy::UI ui;
        Hardware *hw_;
        Engine *fx_;
        PresetManager* presets_manager;

        bool toggle_state;

        uint8_t midi_in_ch;

        // this way both UI pages can interact with it
        float enc_values[3][6];
        float* enc_rows[3] = {enc_values[0], enc_values[1], enc_values[2]};

        const float* def_rows[3] = {enc_defaults[0], enc_defaults[1], enc_defaults[2]};

        uint8_t knob_page[6] = {0, 0, 0, 0, 0, 0};

        // Effect*                                   fx_;
        // PotListener                               listener;
        // daisy::PotMonitor<PotListener, KNOB_LAST> pot_monitor;
    };

} // namespace chompi