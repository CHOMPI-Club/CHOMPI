/** @file chompi_main.cpp
 *  @brief Firmware entry point
 *
 *  CHOMPI (built on the Daisy Seed / STM32H7) has three places code runs, in
 *  order of priority:
 *   1. AudioCallback() - the audio ISR. Runs once per audio block (~24 samples
 *      at 48kHz here)
 *   2. SDCallback() - a lower-priority hardware timer callback for work that's
 *      too slow for the audio ISR but still needs to happen without 
 *      waiting on the main loop. Does one FileStreamingManager 
 *      request per tick so SD reads/writes never block audio. This was more important
 *      in TAPE which is constantly reading from the SD card, but the architecture is
 *      kept the same here.
 *   3. MainLoop() - Lowest priority, handles UI polling, MIDI I/O, and boot-time stuff.
 */
#include "hardware.h"
#include "temp_led_stuff.h"
#include "ui.h"
#include "daisysp.h"
#include "fatfs.h"
#include "diskio.h"
#include "subtractiveEngine.h"
#include "Sequencer.h"
#include "InterpolatedDelayLine.h"
#include "OptionsManager.h"
#include "WavetableManager.h"
#include "clockManager.h"
#include "MidiManager.h"

#define DSY_DTCMRAM_BSS __attribute__((section(".dtcmram_bss")))

#define MAX_CYCLES 256
#define MAX_SAMPLES_PER_CYCLE 2048

using namespace daisy;
using namespace chompi;
FIFO<int16_t, kMaxFileStreamingSamps> test_read_samps;

Hardware hw;
UserInterface ui;

SdmmcHandler sdmmc;
FatFSInterface fsi;
myEngine engine;
PresetManager presets;
OptionsManager options;
wavetableLoader wtLoader;
Sequencer seq;
clockManager cManager;
MidiManager midi;

bool testLoad; // To make sure wavetables don't get loaded until later

daisysp::Reverb DSY_DTCMRAM_BSS reverb;
chompi::InterpolatedDelayLine::AudioSample DSY_SDRAM_BSS del_mem[kMaxDelayTime];

float wavetableMemory[MAX_CYCLES][MAX_SAMPLES_PER_CYCLE] __attribute__((section(".sdram_bss")));

daisysp::Oscillator osc;

// CpuLoadMeter meter;
uint32_t pret, sd_checkt;
// bool log_batt;
bool booting = true;
bool rainbow_done = false;
bool testSDLoaded;
bool loading_screen = true;
size_t loading_screen_time = 0;

/** breakdown:
 *  Inputs:
 *  Channel 1 - Microphone
 *  Channel 2 - X
 *  Channel 3 - Aux L
 *  Channel 3 - Aux R
 *
 *  Outputs:
 *  Channel 1 - Headphone L
 *  Channel 2 - Headphone R
 *  Channel 3 - Master L
 *  Channel 4 - Master R
 */

// The audio ISR. Called by the Daisy audio driver once per block
void AudioCallback(AudioHandle::InputBuffer in, AudioHandle::OutputBuffer out, size_t size)
{
    // meter.OnBlockStart();

    if((booting || loading_screen) && !ui.InTestMode())
    {
        hw.ProcessAllControls();
        ui.GenerateEvents();
        ui.DoEvents();

        for(size_t i = 0; i < size; i++)
        {
            out[0][i] = out[1][i] = out[2][i] = out[3][i] = 0.f;
        }

        return;
    }

    midi.ProcessMidiIn();

    hw.ProcessAllControls();
    ui.GenerateEvents();
    if (seq.getPlaying()) {
        seq.checkAndPop();
    }
    engine.Prepare();

    if(ui.InTestMode() && ui.GetToggleState())
    {
        for(size_t i = 0; i < size; i++)
        {
            out[0][i] = out[1][i] = out[2][i] = out[3][i] = osc.Process();
        }
    }
    else {
        engine.Process(in, out, size);
    }

    // meter.OnBlockEnd();
}

/** Clears the Daisy Seed 64MB external SDRAM bank at boot. Large audio stuff placed there
 *  (wavetableMemory, the delay line buffer). Not zero-initialized by the
 *  startup code the way internal-RAM statics are so without this it
 *  could play back stale/garbage SDRAM data */
void ZeroSDRAM()
{
    uint32_t *beg, *end;
    size_t    size_in_words = (1024 * 1024 * 64) / sizeof(uint32_t);
    beg                     = (uint32_t*)0xc0000000;
    end                     = (uint32_t*)(beg + size_in_words);
    std::fill(beg, end, 0);
}

bool no_sd_card = false;
void CheckSDCardMounted()
{
    DSTATUS res = disk_status(0);
    // lost the SD card, must reboot
    if(res != RES_OK)
    {
        no_sd_card = true;
        ui.NoSDCard();

        const uint32_t start_time = System::GetNow();
        while(System::GetNow() - start_time < 3000)
        {
            ui.NoSDCardAnimation(true);
            ui.DoEvents();
            System::Delay(1);
        }

        ui.NoSDCardAnimation(false);
    }
}

void SDCallback(void* data)
{
    const uint32_t now = System::GetNow();
    if (now - sd_checkt > 1000 && !no_sd_card && !booting)
    {
        sd_checkt = now;
        CheckSDCardMounted();
    }
    else if(no_sd_card)
        return;

    engine.ProcessFileRequests();

    if(now - pret > 50)
    {
        pret = now;
        ui.WritePresets();
    }    
}

uint32_t uit, now, pre_startt;

#if !NO_BATT
uint32_t batt;
#endif

uint8_t preset = 0;
uint8_t bank = 0; 
uint8_t mode = 0;

TimerHandle midi_clock_timer;
size_t tim_base_freq;

/** Queues a MIDI clock pulse. CHOMPI runs on 12 PPQN, not 24. 24 created too much traffic
 * on the MIDI out bus. */
void MidiClockCallback(void* ctx)
{
    if (seq.getPlaying() && options.midi_clock_out) {
        midi.QueueMidiClock();
    }
}

/** Setup for MIDI clock timer. Uses TIM_16 to not conflict with existing
 * libDaisy stuff. This was one of the modifications made to libDaisy to help
 * MIDI clock out work. libs/libDaisy/src/tim.cpp was changed from the
 * original libDaisy
 */
void InitMidiClockTimer()
{
    TimerHandle::Config tim_cfg;

    tim_cfg.periph = TimerHandle::Config::Peripheral::TIM_16;
    tim_cfg.dir    = TimerHandle::Config::CounterDir::UP;
    tim_cfg.enable_irq = true;

    midi_clock_timer.Init(tim_cfg);
    midi_clock_timer.SetPrescaler(239);
    midi_clock_timer.SetPeriod(15624);
    midi_clock_timer.SetCallback(MidiClockCallback, nullptr);
    midi_clock_timer.Start();
}

void MainLoop(void* data)
{
    if(booting)
    {
        hw.LowBatteryLockoutCheck();
        booting = false;
    }
    else if(!rainbow_done && !loading_screen)
    {
        ui.StopBootAnimation();
        ui.RainbowWave();
        rainbow_done = true;
    }

    // volatile float avg_load = meter.GetAvgCpuLoad();
    // volatile float max_load = meter.GetMaxCpuLoad();
    now = daisy::System::GetNow();

    midi.ProcessMidiOut(); // We want midi checks to happen every 10uS for more precise timing

    if (now - uit > 1)
    {
        ui.DoEvents();
        //ui.ProcessMidi();
        uit = now;
    }

    if (now - pre_startt > 1000) {
        if (!testLoad) {
            wtLoader.loadAllToMemory();
            testLoad = true;
        }
    }

    // release the loading screen 250 ms after
    // the wavetable requests drain; if they never drain (no card), it never releases
    if (!testSDLoaded) {
        //numWavetables guard: with no card the loader finds zero tables
        //and queues nothing, so an empty request queue must not count as "loaded"
        if (testLoad && wtLoader.numWavetables > 0 && engine.checkLoaded()) {
            testSDLoaded = true;
            ui.BootSelectDefaultSlot();
            loading_screen_time = System::GetNow();
        }
    }
    if (loading_screen_time) {
        if (System::GetNow() - loading_screen_time > 250) {
            loading_screen = false;
            loading_screen_time = 0;
        }
    }

    if (now - pre_startt > 5000)
    {
        ui.TestPresets();
        pre_startt = now;
    }

    // update now to actually be now
    now = daisy::System::GetNow();

    #if !NO_BATT

    if(ui.InRainbows())
    {
        batt = now;
    }
    else if(now - batt > 20)
    {
        hw.LowBatteryLockoutCheck();
        batt = now;
    }

    if(ui.InTestMode())
    {
        hw.MpReadAll();

        while (!hw.read_ready) {
            System::Delay(1);
        }
        ui.TestPowerCable(hw.mp_buff_[1] >> 5 & 1); //VIN_RDY

        // Normal NTC_MISSING, BATT_MISSING, NTC1_FAULT, and NTC2_FAULT
        ui.TestBMC(hw.mp_buff_[3] == 0); 
    }
    #endif

    System::DelayUs(10);
}

int main(void)
{
    hw.Init();

    // System::Delay(100);
    midi.Init(&cManager, &seq, &ui, &engine, &hw);

    hw.MpWrite(0x0c, 0B01010001); // set BATT_LOW to 3V, turn on 

    hw.MpReadAll();

    for(size_t i = 0; i < 10; i++)
    {
        hw.LowBatteryLockoutCheck();
        System::Delay(10);
    }

    /** SDMMC Init */
    System::Delay(100);
    SdmmcHandler::Config sd_cfg;
    sd_cfg.speed = SdmmcHandler::Speed::FAST;
    sd_cfg.width = SdmmcHandler::BusWidth::BITS_4;
    // sd_cfg.clock_powersave = true;
    sdmmc.Init(sd_cfg);
    System::Delay(100);
    fsi.Init(FatFSInterface::Config::MEDIA_SD);
    System::Delay(100);
    f_mount(&fsi.GetSDFileSystem(), fsi.GetSDPath(), 1);

    wtLoader.Init(&fsi.GetSDFileSystem(), wavetableMemory, hw.seed.AudioSampleRate());

    // delete the battery log if it exists
    char filename[32];
    sprintf(filename, ".batt_log.txt");
    f_unlink(filename);

    // macos makes a copy
    sprintf(filename, "._.batt_log.txt");
    f_unlink(filename);


    options.Init();
    midi.setMidiOptions(options.midi_ch_in, options.midi_ch_out, options.midi_cc_in);
    hw.setMidiCCOut(options.midi_cc_out);

    LedSetup();
    ui.Init(&cManager, &engine, &seq, &hw, &presets,
        options.midi_ch_out);

    tim_base_freq = System::GetPClk2Freq();
    InitMidiClockTimer();
    cManager.Init(&midi_clock_timer, tim_base_freq);
    seq.Init(&engine, &cManager, &hw);

    hw.StartLowPriorityCallback(SDCallback, 1000);
    hw.StartAudio(AudioCallback);

    ZeroSDRAM();

    testLoad = false;
    testSDLoaded = false;

    // meter.Init(hw.seed.AudioSampleRate(), hw.seed.AudioBlockSize());

    engine.Init(hw.seed.AudioSampleRate(), &del_mem[0], &reverb, &wtLoader);
    
    osc.Init(hw.seed.AudioSampleRate());
    osc.SetAmp(.2f);

    now = daisy::System::GetNow();
    uit = now;
    pret = now;
    pre_startt = now;

    #if !NO_BATT
    batt = now;
    #endif

    // get any junk out of the SRs, takes .5s
    uint32_t vol_state = 0;
    uint32_t sleep_state = 0;

    for(int i = 0; i < 5000; i++)
    {
        hw.ProcessAllControls();
        vol_state += hw.button_sr.State(int(Hardware::SwId::ENC_6_SW));
        sleep_state += hw.button_sr.State(int(Hardware::SwId::KEY_26))
                        && hw.button_sr.State(int(Hardware::SwId::KEY_27))
                        && hw.button_sr.State(int(Hardware::SwId::KEY_28));

        System::DelayUs(100);
    }

    if(sleep_state > 4000)
        hw.MpWrite(0x08, 0B10111111); // SHIPPING MODE
    else if(vol_state > 4000)
        ui.TestMode();

    hw.usb_sw.Write(false);     // give USB control
    daisy::System::Delay(1); // Wait a sec
    hw.MpWrite(0x0a, 0B00100100); // AutoDPDM
    daisy::System::Delay(1); // Wait a sec
    hw.usb_sw.Write(true);     // take USB control

    while (1)
    {
        MainLoop(nullptr);
    }
}