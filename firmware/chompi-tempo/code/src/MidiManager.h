#pragma once
#include "ui.h"
#include "EngineBase.h"

class MidiManager {
    public:
    MidiManager() {};
    ~MidiManager() {};

    struct MidiFifoEntry {
        uint8_t data[3];   // max 3 bytes for MIDI messages
        size_t length;     // actual number of bytes
    };

    void Init(clockManager *cManager, UserInterface *ui, Hardware *hw,
                fxEngine *fx, ArpeggiatorSequencer *arpSeq, BaseEngine **engines) {

        clock_manager_ = cManager;
        ui_ = ui;
        hw_ = hw;
        menu_page_ = &ui_->menu_page_;
        normal_page_ = &ui_->normal_page_;
        fx_ = fx;
        arpSeq_ = arpSeq;
        engines_ = engines;

        fifo_head = fifo_tail = 0;
        uart_tx_busy = false;

        MidiUartHandler::Config uart_midi_cfg;
        uart_midi.Init(uart_midi_cfg);
        uart_midi.StartReceive(); // shoukld we do this repeatedly to catch crashes?

        MidiUartTransport transport = uart_midi.GetMutableTransport();
        uart_handler_ = transport.GetUartHandle();

        MidiUsbHandler::Config usb_midi_cfg;
        usb_midi_cfg.transport_config.periph = MidiUsbTransport::Config::EXTERNAL;
        usb_midi.Init(usb_midi_cfg);
        usb_midi.Listen();

    }

    void ProcessMidiIn() {
        daisy::MidiEvent event;
        while(GetMidi(event))
        {
            // only accept input on channel 1, but transport is global
            if (event.type == daisy::MidiMessageType::SystemRealTime)
            {
                if (event.srt_type == daisy::SystemRealTimeType::TimingClock) {
                    if (clock_manager_->getClockMode() == SYNC) {
                        clock_manager_->processMidiClock();
                    }
                }
                else if (event.srt_type == daisy::SystemRealTimeType::Start) {
                    if (clock_manager_->getClockMode() == SYNC && transport_in) {
                        arpSeq_->setPlayTransport(true);
                        clock_manager_->setNow(0);
                        clock_manager_->setNow(1);
                        clock_manager_->setNow(2);
                    }
                }
                else if (event.srt_type == daisy::SystemRealTimeType::Stop) {
                    if (clock_manager_->getClockMode() == SYNC && transport_in) {
                        arpSeq_->setPlayTransport(false);
                    }
                }
                continue;
            }

            if (event.channel == in_channel) {
                switch(event.type)
                {
                    case NoteOn:
                    {
                        int key = event.data[0];
                        key -= 24;
                        if (key > 48|| key < 0)
                            break;

                        float transpose_nn = fx_->getEngine() == CHROMATIC ? static_cast<float>(key - 36) : 0.f;

                        if (arpSeq_->getPlay(fx_->getEngine())) {
                            arpSeq_->request_fifo.PushBack(KeyRequest(KeyRequest::Type::START, 
                                key - 36, midi2key[key], event.data[1] + 1));
                        }
                        else if (arpSeq_->getLatch()){
                            arpSeq_->request_fifo.PushBack(KeyRequest(KeyRequest::Type::START, 
                                key - 36, midi2key[key], event.data[1] + 1));

                            engines_[fx_->getEngine()]->request_fifo.PushBack(KeyRequest(KeyRequest::Type::START, 
                                transpose_nn, midi2key[key], event.data[1] + 1));
                        }
                        else {
                            engines_[fx_->getEngine()]->request_fifo.PushBack(KeyRequest(KeyRequest::Type::START, 
                                transpose_nn, midi2key[key], event.data[1] + 1));
                        }
                    }
                    break;
                    case NoteOff:
                    {
                        int key = event.data[0];
                        key -= 24;
                        if (key > 48|| key < 0)
                            break;

                        float transpose_nn = fx_->getEngine() == CHROMATIC ? static_cast<float>(key - 36) : 0.f;

                        if (!arpSeq_->getSustain() && !arpSeq_->checkNotePlaying(transpose_nn)) {
                            engines_[fx_->getEngine()]->request_fifo.PushBack(KeyRequest(KeyRequest::Type::STOP, 
                                transpose_nn, midi2key[key], 127.f));
                        }
                        if (arpSeq_->getPlay(fx_->getEngine()) || (arpSeq_->getLatch() && !arpSeq_->getSustain())) {
                            arpSeq_->request_fifo.PushBack(KeyRequest(KeyRequest::Type::STOP, 
                                transpose_nn, midi2key[key], 127.f));
                        }
                    }
                    break;
                    case ControlChange:
                    {
                        if (menu_page_->IsActive()) {
                            break;
                        }
                        if (!midi_cc_in) {
                            break;
                        }

                        uint8_t cc = event.data[0];
                        uint8_t val = event.data[1];

                        if (cc >= 20 && cc < 26)
                        {
                            uint8_t knob = cc - 20;

                            ui_->event_queue.AddEncoderTurned(knob, val, 1);
                        }
                        else if (cc == 14 || cc == 15)
                        {
                            const uint8_t idx = cc - 14;

                            if (cc == 14 && normal_page_->getSwitchState()) {
                                break;
                            }

                            const bool last = key_cc[idx];

                            size_t ui_key = cc == 14 ? 5 : 34;

                            // top 1/3 is high, bottom 1/3 is low, middle 1/3 is dead zone
                            if (val > 84)
                                key_cc[idx] = true;
                            else if (val < 42)
                                key_cc[idx] = false;

                            if (!last && key_cc[idx]) // rising edge
                            {
                                ui_->event_queue.AddButtonPressed(ui_key, 1, true);
                            }
                            else if (last && !key_cc[idx]) // falling edge
                            {
                                ui_->event_queue.AddButtonReleased(ui_key);
                            }
                        }
                    }
                    default:
                    break;
                }
            }
        }
    }

    void ProcessMidiOut() {
        if (fifo_head != fifo_tail) {
            if (!uart_tx_busy) {
                ScopedIrqBlocker block;
                DequeueDmaMessage();
            }
        }
        if (!hw_->midi_out_queue.IsEmpty()) {
            MidiEvent event = hw_->midi_out_queue.PopFront();
            if (event.type == NoteOn) {
                uint8_t tx_buf_[3];
                tx_buf_[0] = 0x90 | (event.channel & 0x0F); // Note On for channel
                tx_buf_[1] = event.data[0];
                tx_buf_[2] = 127;
                QueueDmaMessage(tx_buf_, 3);
                if(usb_midi_active)
                {
                    usb_midi.SendNoteOn(event.channel, event.data[0], 127);
                }
            }
            else if (event.type == NoteOff) {
                uint8_t tx_buf_[3];
                tx_buf_[0] = 0x80 | (event.channel & 0x0F); // Note Off for channel
                tx_buf_[1] = event.data[0];
                tx_buf_[2] = 0;
                QueueDmaMessage(tx_buf_, 3);
                if(usb_midi_active)
                {
                    usb_midi.SendNoteOff(event.channel, event.data[0], 127);
                }
            }
            else if (event.type == ControlChange) {
                uint8_t tx_buf_[3];
                tx_buf_[0] = 0xB0 | (event.channel & 0x0F); // Control Change on given channel
                tx_buf_[1] = event.data[0];
                tx_buf_[2] = event.data[1];
                QueueDmaMessage(tx_buf_, 3);
                if (usb_midi_active) {
                    usb_midi.SendCC(event.channel, event.data[0], event.data[1]);
                }
            }
            else {
                if (event.srt_type == daisy::SystemRealTimeType::Start) {
                    uint8_t tx_buf_ = 0xFA; // MIDI Start
                    QueueDmaMessage(&tx_buf_, 1);
                    if (usb_midi_active)
                    {
                        usb_midi.SendMessage(&tx_buf_, 1);
                    }
                }
                else if (event.srt_type == daisy::SystemRealTimeType::Stop) {
                    uint8_t tx_buf_ = 0xFC; // MIDI Stop
                    QueueDmaMessage(&tx_buf_, 1);
                    if (usb_midi_active)
                    {
                        usb_midi.SendMessage(&tx_buf_, 1);
                    }
                }
                else {
                    uint8_t tx_buf_ = 0xF8;
                    QueueDmaMessage(&tx_buf_, 1);
                    if (usb_midi_active) {
                        usb_midi.SendMessage(&tx_buf_, 1);
                    }
                }
            }
        }
    }

    void setMidiOptions(uint8_t in_ch, bool enable_cc_in, size_t enable_transport_in) {
        in_channel = in_ch;
        midi_cc_in = enable_cc_in;
        transport_in = enable_transport_in == 0 || enable_transport_in == 2;
    }

    void QueueMidiClock() {
        uint8_t tx_buf_ = 0xF8;
        QueueDmaMessage(&tx_buf_, 1);
        if (usb_midi_active) {
            usb_midi.SendMessage(&tx_buf_, 1);
        }
    }

    void USBMidiActive(bool a)
    {
        // if(a)
            // ResetUSBMidi();

        usb_midi_active = a;
    }

    void ResetUSBMidi()
    {
        last_reset = System::GetNow();

        usb_midi.ResetTransport();
        /// are we still listening?
    }

    bool GetMidi(MidiEvent &event)
    {
        uart_midi.Listen();

        if (uart_midi.HasEvents())
        {
            event = uart_midi.PopEvent();
            return true;
        }
        else if(usb_midi.HasEvents())
        {
            event = usb_midi.PopEvent();
            return true;
        }

        return false;
    }

    void StartDmaTransfer() {
        uint8_t* p = fifo[fifo_tail].data;
        size_t len = fifo[fifo_tail].length;

        uintptr_t start = (uintptr_t)p & ~(32 - 1);
        uintptr_t end = ((uintptr_t)p + len + 32 - 1) & ~(32 - 1);
        SCB_CleanDCache_by_Addr((uint32_t*)start, end - start);

        uart_handler_.DmaTransmit(p, len, nullptr, TxEndCallback, this);
    }

    void QueueDmaMessage(uint8_t* msg, size_t length) {
        fifo[fifo_head].length = length;
        memcpy(fifo[fifo_head].data, msg, length);
        fifo_head = (fifo_head + 1) % 64;

        if(!uart_tx_busy) {
            __disable_irq();
            DequeueDmaMessage();
            __enable_irq();
        }
    }

    void DequeueDmaMessage() {
        if (fifo_head == fifo_tail) {
            fifo_head = (fifo_head + 1) % 64;
        }
        if (!uart_tx_busy) {
            uart_tx_busy = true;
            StartDmaTransfer();
        }
    }

    static void TxEndCallback(void *context, daisy::UartHandler::Result) {
        MidiManager* self = static_cast<MidiManager*>(context);

        self->fifo_tail++; // advance to next message
        if (self->fifo_tail > 63) {
            self->fifo_tail = 0;
        };
        self->uart_tx_busy = false;
        if(self->fifo_head != self->fifo_tail) {
            self->DequeueDmaMessage();
        }
    }

    private:
    uint8_t in_channel;
    clockManager *clock_manager_;
    UserInterface *ui_;
    MenuPage *menu_page_;
    NormalPage *normal_page_;
    Hardware *hw_;
    fxEngine *fx_;
    ArpeggiatorSequencer *arpSeq_;
    BaseEngine **engines_;
    bool midi_cc_in;
    bool transport_in;

    MidiUartHandler uart_midi;
    MidiUsbHandler usb_midi;
    bool usb_midi_active = true;
    uint32_t last_reset;

    UartHandler uart_handler_;
    MidiFifoEntry fifo[64];   // circular buffer of messages
    size_t fifo_head, fifo_tail;
    bool uart_tx_busy;

    bool key_cc[2];
};