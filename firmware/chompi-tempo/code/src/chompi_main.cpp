#include "hardware.h"
#include "temp_led_stuff.h"
#include "ui.h"
#include "daisysp.h"
#include "fatfs.h"
#include "diskio.h"
#include "OptionsManager.h"
#include "EngineBase.h"
#include "SampleEngine.h"
#include "SliceEngine.h"
#include "SampleManager.h"
#include "ArpeggiatorSequencer.h"
#include "clockManager.h"
#include "MidiManager.h"
#include "FxEngine.h"
#include "granularDelay.h"
#include "StateSaver.h"
#include "reverb.h"

#define MAX_CYCLES 256
#define MAX_SAMPLES_PER_CYCLE 2048
constexpr size_t kBufferSize = 480000; // 10 sec @ 48kHz mono

using namespace daisy;
using namespace chompi;

Hardware hw;
UserInterface ui;

float __attribute__((section(".sdram_bss"))) granularBuffer[kBufferSize * 2]; // *2 for stereo so 10 sec stereo
float __attribute__((section(".sdram_bss"))) frozenBuffer[kBufferSize * 2]; // *2 for stereo so 10 sec stereo

SdmmcHandler sdmmc;
FatFSInterface fsi;
PresetManager presets;
OptionsManager options;
sampleEngine sEngine;
sliceEngine slice;
sampleManager sManager;
fxEngine fx;
FileStreamingManager file_manager;
ArpeggiatorSequencer arpSeq;
clockManager cManager;
MidiManager midi;
granularDelay delay;
StateSaver stateSaver;
BaseEngine* engines[2] = { &sEngine, &slice };
Reverb __attribute__((section(".dtcmram_bss"))) reverb;
TimerHandle midi_clock_timer;

size_t tim_base_freq;

size_t testThingy;

float chromaBuffer[2][48];
float sliceBuffer[2][48];

float *chromaPtr[2] = { chromaBuffer[0], chromaBuffer[1] };
float *slicePtr[2] = { sliceBuffer[0], sliceBuffer[1] };

volatile uint32_t testTimer;

bool testLoad; //To make sure wavetables don't get loaded until later
bool testSDLoaded;

void *sample_ram_start = (void *)0xC0000000;

daisysp::Oscillator osc;

// CpuLoadMeter meter;
uint32_t pret, sd_checkt;
// bool log_batt;
bool booting = true;
bool loading_screen = true;
size_t loading_screen_time = 0;
bool rainbow_done = false;

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
bool line_in_state;

void AudioCallback(AudioHandle::InputBuffer in, AudioHandle::OutputBuffer out, size_t size)
{
    // meter.OnBlockStart();
    uint32_t point1 = System::GetUs();

    for(size_t i = 0; i < size; i++) {
        out[0][i] = out[1][i] = out[2][i] = out[3][i] = 0.f;
    }

    if((booting || loading_screen) && !ui.InTestMode())
    {
        hw.ProcessAllControls();
        ui.GenerateEvents();
        ui.DoEvents();

        return;
    }

    midi.ProcessMidiIn();
    cManager.averageMidiClock();

    hw.ProcessAllControls();
    ui.GenerateEvents();
    if (cManager.checkIntervalExpired(0)) {
        arpSeq.setClockEdge(0);
        delay.setClockEdge();
    }
    if (cManager.checkIntervalExpired(1)) {
        arpSeq.setClockEdge(1);
    }
    cManager.checkIntervalExpired(2);
    arpSeq.Prepare();
    for (size_t i = 0; i < kNumEngines; ++i) {
        engines[i]->Prepare();
    }

    if(hw.jack_detect.Read() != line_in_state)
    {
        if(hw.jack_detect.Read())
            fx.SetInputSource(InputSource::LINE_IN);
        else
            fx.SetInputSource(InputSource::MIC);
    }
    line_in_state = hw.jack_detect.Read();

    if(ui.InTestMode() && ui.GetToggleState())
    {
        for(size_t i = 0; i < size; i++)
        {
            out[0][i] = out[1][i] = out[2][i] = out[3][i] = osc.Process();
        }
    }
    else {
    }

    if (testSDLoaded) {

        engines[0]->Process(in, chromaPtr, size);
        engines[1]->Process(in, slicePtr, size);

        for (size_t i = 0; i < size; ++i) {
            out[0][i] = chromaBuffer[0][i] + sliceBuffer[0][i];
            out[1][i] = chromaBuffer[1][i] + sliceBuffer[1][i];
        }

        fx.Process(in, out, size, chromaPtr, slicePtr);
        fx.ApplyOutputFX(in, out, size);
    }
    else {
        for(size_t i = 0; i < size; i++)
        {
            out[0][i] = out[1][i] = out[2][i] = out[3][i] = 0.f;
        }
    }

    testTimer = System::GetUs() - point1;

    // meter.OnBlockEnd();
}

void MidiClockCallback(void* ctx)
{
    // Called at the timer frequency
    if (arpSeq.getPlay(0) && options.midi_clock_out && cManager.getClockMode() == FREE) {
        midi.QueueMidiClock();
    }
    cManager.incrementCounters();
    delay.setClockPulse();
}

void InitMidiClockTimer()
{
    TimerHandle::Config tim_cfg;

    // Pick a timer not used by libDaisy or audio
    tim_cfg.periph = TimerHandle::Config::Peripheral::TIM_16; // better choice than TIM_2
    tim_cfg.dir    = TimerHandle::Config::CounterDir::UP;
    tim_cfg.enable_irq = true;

    float bpm = 160.f;
    float midi_clock_hz = bpm * 24.f / 60.f; // 24 PPQN
    uint32_t timer_tick_rate = 1000000;     // 1 MHz
    uint32_t prescaler = ((tim_base_freq * 2) / timer_tick_rate) - 1;
    uint32_t arr = static_cast<uint32_t>(timer_tick_rate / midi_clock_hz) - 1;

    midi_clock_timer.Init(tim_cfg);
    midi_clock_timer.SetPrescaler(239);
    midi_clock_timer.SetPeriod(15624);
    midi_clock_timer.SetCallback(MidiClockCallback, nullptr);
    midi_clock_timer.Start();
}

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

    file_manager.ProcessRequests();

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

    midi.ProcessMidiOut(); // We want midi checks to happen every 10uS for more precise timing

    // volatile float avg_load = meter.GetAvgCpuLoad();
    // volatile float max_load = meter.GetMaxCpuLoad();
    now = daisy::System::GetNow();
    testThingy = now;

    if (now - uit > 1)
    {
        ui.DoEvents();
        //ui.ProcessMidi();
        uit = now;
    }

    if (now - pre_startt > 1000) {
        if (!testLoad) {
            sManager.loadFileData();
            testLoad = true;
        }
    }

    if (!testSDLoaded) {
        if (sManager.checkLoaded()) {
            testSDLoaded = true;
            if (!sManager.isValidSample(15, 0)) {
                //sManager.fillDefaultSample(hw.seed.AudioSampleRate());
            }
            for (size_t i = 0; i < kNumEngines; ++i) {
                engines[i]->setSample(14);
            }
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
    midi.Init(&cManager, &ui, &hw, &fx, &arpSeq, engines);

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
    sd_cfg.speed = SdmmcHandler::Speed::VERY_FAST;
    sd_cfg.width = SdmmcHandler::BusWidth::BITS_4;
    // sd_cfg.clock_powersave = true;
    sdmmc.Init(sd_cfg);
    System::Delay(100);
    fsi.Init(FatFSInterface::Config::MEDIA_SD);
    System::Delay(100);
    f_mount(&fsi.GetSDFileSystem(), fsi.GetSDPath(), 1);

    options.Init();
    tim_base_freq = System::GetPClk2Freq();

    reverb.Init(hw.seed.AudioSampleRate());
    fx.Init(&sManager, hw.seed.AudioSampleRate(), &delay, &reverb, options.monitor_position);
    delay.Init(granularBuffer, frozenBuffer, kBufferSize, &cManager, options.delay_mute);
    sManager.Init(sample_ram_start, &file_manager, hw.seed.AudioSampleRate());
    for (size_t i = 0; i < kNumEngines; ++i) {
        engines[i]->Init(&sManager, hw.seed.AudioSampleRate());
    }

    // delete the battery log if it exists
    char filename[32];
    sprintf(filename, ".batt_log.txt");
    f_unlink(filename);

    // macos makes a copy
    sprintf(filename, "._.batt_log.txt");
    f_unlink(filename);

    InitMidiClockTimer();
    cManager.Init(&midi_clock_timer, tim_base_freq);
    LedSetup();
    ui.Init(&fx, &cManager, &arpSeq, &sManager, &hw, &presets, options.pitch_shift_quantization, 
        engines, &stateSaver, &options);

    arpSeq.Init(engines, &cManager, &hw, &options);
    hw.setMidiCCOut(options.midi_cc_out);
    midi.setMidiOptions(options.midi_ch_in, options.midi_cc_in, options.transport_type);

    stateSaver.Init();

    hw.StartLowPriorityCallback(SDCallback, 1000);
    hw.StartAudio(AudioCallback);

    ZeroSDRAM();

    testLoad = false;
    testSDLoaded = false;

    // meter.Init(hw.seed.AudioSampleRate(), hw.seed.AudioBlockSize());
    
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