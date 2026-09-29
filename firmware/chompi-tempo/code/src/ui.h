#pragma once
#include "hardware.h"
#include "ui_utils.h"
#include "NormalPage.h"
#include "MenuPage.h"
#include "TestPage.h"
#include "BootPage.h"
#include "NoSDPage.h"
#include "RainbowWavePage.h"
#include "SampleEngine.h"
#include "SliceEngine.h"
#include "ArpeggiatorSequencer.h"
#include "clockManager.h"
#include "FxEngine.h"
#include "StateSaver.h"
#include "OptionsManager.h"
// #include "StereoDelayEffect.h"

namespace chompi
{

    static const float enc_defaults[3][6] = {
        {.83f, 0.f, 1.f, .5f, .5f, .6f}, // page 1
        {.6f, 0.f, 0.f, .5f, 0.f, .75f},   // page 2
        {.5f, 0.f, 0.f, .5f, 0.f, 0.f},   // page 3
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
        void Init(fxEngine *fx, clockManager *clock_manager, ArpeggiatorSequencer *arpSeq, sampleManager *sManager, 
                Hardware *hw, PresetManager* pre, bool pitch_shift_quant, 
                BaseEngine **engines, StateSaver *stateSaver, OptionsManager *options)
        {
            hw_ = hw;
            sManager_ = sManager;
            clock_manager_ = clock_manager;
            presets_manager = pre;
            arpSeq_ = arpSeq;


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

            normal_page_.Init(fx, clock_manager_, arpSeq_, engines, hw_, enc_rows, 
                def_rows, knob_page, presets_manager, options);
            ui.OpenPage(normal_page_);

            boot_page_.Init(hw_, sManager_);
            ui.OpenPage(boot_page_);

            menu_page_.Init(fx, clock_manager_, hw_, arpSeq_, enc_rows, def_rows, knob_page,
                presets_manager, pitch_shift_quant, stateSaver, engines, options);

            test_page_.Init(hw_);

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

            if (normal_page_.getRecordReset()) {
                menu_page_.resetAfterRecording();
                normal_page_.resetRecordReset();
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
                        && normal_page_.IsActive()
                        && !boot_page_.IsActive()
                        && !rainbow_page_.IsActive()
                        && !test_page_.IsActive()
                        && toggle_state)
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

        FIL fptr_pre;
        char fname[16];
        static const size_t kPreFileSize = 4096;
        char presets_file[kPreFileSize]; // too big? not big enough?

        void InitFromPresets()
        {
            float defaults[11] = {  enc_defaults[0][0], enc_defaults[1][0], enc_defaults[2][0],
                                    enc_defaults[0][1], enc_defaults[1][1],
                                    enc_defaults[0][2], enc_defaults[1][2],
                                    .5f, 0.f, 1.f, 1.f};

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
    daisy::UiEventQueue event_queue;
    private:
        daisy::UI ui;
        Hardware *hw_;
        sampleManager *sManager_;
        clockManager *clock_manager_;
        PresetManager* presets_manager;
        ArpeggiatorSequencer *arpSeq_;

        bool toggle_state;

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