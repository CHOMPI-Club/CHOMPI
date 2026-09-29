/** @file ui.h
 *  @brief UI built on libDaisy's daisy::UI page system. Each screen/mode of the UI
 *  (normal play, the menu, test mode, boot animation etc.) is a UiPage subclass
 *
 *  Pages are opened and closed here and the UI events get generated and sent to
 *  the active page.
 */
#pragma once
#include "hardware.h"
#include "ui_utils.h"
#include "NormalPage.h"
#include "MenuPage.h"
#include "TestPage.h"
#include "BootPage.h"
#include "NoSDPage.h"
#include "RainbowWavePage.h"
#include "subtractiveEngine.h"
#include "Sequencer.h"
#include "clockManager.h"
// #include "StereoDelayEffect.h"

namespace chompi
{

    // Default value for each of the 6 knobs on the normal page
    static const float enc_defaults[3][6] = {
        {.5f, 0.f, 0.f, .5f, .5f, .84f}, // page 1  [3]=delay/reverb centre (bipolar: .5 = dry)
        {.0f, 0.f, 0.f, .5f, 0.f, .5f},   // page 2  [1]=pitch-LFO amt off, [2]=filter-LFO amt off, [3]=filter cutoff centre
        {0.f, 0.f, 0.f, 0.f, 0.f, 0.f},   // page 3  (unused - no encoder has a third page)
    };

    // Remaps MIDI note numbers to the UI key system
    static const uint8_t midi2key[49] = {
        40, 41, 42, 43, 44, 45, 46, 47,      // MIDI 36-43 (low octave, synthetic)
        48, 49, 50, 51,                      // MIDI 44-47
        15, 7, 8, 12, 9, 10, 13, 11,         // MIDI 48-72: the 25 physical keys
        14, 16, 21, 17, 18, 22, 19, 23,
        20, 24, 29, 25, 30, 26, 31, 27, 28,
        52, 53, 54, 55, 56, 57, 58, 59,      // MIDI 73-80 (high octave, synthetic)
        60, 61, 62, 63                       // MIDI 81-84
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
        void Init(clockManager *clock_manager, myEngine *engine, Sequencer *seq, Hardware *hw, PresetManager* pre,
                    uint8_t ch_out)
        {
            hw_ = hw;
            engine_ = engine;
            clock_manager_ = clock_manager;
            presets_manager = pre;
            seq_ = seq;

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

            normal_page_.Init(clock_manager_, engine_, seq_, hw_, enc_rows, 
                def_rows, knob_page, presets_manager, ch_out);
            ui.OpenPage(normal_page_);

            boot_page_.Init(hw_);
            ui.OpenPage(boot_page_);

            menu_page_.Init(clock_manager_, engine_, seq_, hw_, enc_rows, def_rows, knob_page,
                presets_manager);

            test_page_.Init(hw_);

            // The toggle switch (SW_TOG) is a physical switch, not a
            // momentary button. This samples it at boot and creates 
            // an initial press event if it reads down for the majority of samples
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
        // Translates raw debounced hardware state into daisy::UiEventQueue events
        // for whatever page is active.
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
                    engine_->setSwitchState(toggle_state);
                }
                else if (hw_->button_sr.FallingEdge(i)) // The majority of key presses and releases go here
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
                        && toggle_state // shift only when the mode switch is DOWN
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

            // encoder_map remaps physical encoder wiring order to logical knob
            // order. Knobs 0 and 4 get finer resolution (1x per detent) 
            // while the rest move 3x faster per detent, since those don't need 
            // as fine resolution.
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
            // pitch, start, end, att, decay, autoloop, sustainactive, gain, pan
            float defaults[15] = {   enc_defaults[0][0], enc_defaults[0][1], enc_defaults[0][1],
                                    enc_defaults[1][1], enc_defaults[1][1], 1.f,
                                    1.f, enc_defaults[1][0], .5f, 0.f, 0.f, 0.f, 0.f, 0.f};

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
        // Starts a preset save if PresetManager has unsaved changes. In TAPE this happens constantly
        // when you move the knobs, in the other firmwares this only happens when you intentionally save

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


        /** Writes the presets_file buffer to disk over several calls instead of one large blocking
         *  write. Otherwise this would take too long and cause other things to lag. Called from SDCallback() */
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

    inline void BootSelectDefaultSlot() { menu_page_.SetVoiceSlot(15); }

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
        myEngine *engine_;
        clockManager *clock_manager_;
        PresetManager* presets_manager;
        Sequencer *seq_;

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