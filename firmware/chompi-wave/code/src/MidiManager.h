/** @file MidiManager.h
 *  @brief MIDI in/out over both 5-pin DIN (UART, DMA-transmitted) and USB
 *  port. Incoming note/CC messages are translated into the same
 *  KeyRequest/UI-event paths a front-panel key press or knob turn. libDaisy's uart class
 *  had to be modified to allow DMA in and out. Without using DMA, MIDI clock would not run
 *  fast/often enough to be accurate. This system is fragile but it works.
 */
#pragma once
#include "ui.h"

struct MidiFifoEntry {
    uint8_t data[3];   // max 3 bytes for MIDI messages
    size_t length;     // actual number of bytes
};

class MidiManager {
    public:
    MidiManager() {};
    ~MidiManager() {};

    void Init(clockManager *cManager, Sequencer *seq, UserInterface *ui, myEngine *engine, Hardware *hw) {

        clock_manager_ = cManager;
        seq_ = seq;
        ui_ = ui;
        engine_ = engine;
        hw_ = hw;
        menu_page_ = &ui_->menu_page_;
        normal_page_ = &ui_->normal_page_;

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

    void setMidiOptions(uint8_t in_ch, uint8_t out_ch, bool enable_cc_in) {
        in_channel = in_ch;
        out_channel = out_ch;
        midi_cc_in = enable_cc_in;
        seq_->setMidiChannel(out_ch);
    }

    void ProcessMidiIn() {
        daisy::MidiEvent event;
        while(GetMidi(event))
        {

            if (event.channel != in_channel) {
                continue;
            }

            switch(event.type)
            {
                // Incoming MIDI note numbers are remapped into CHOMPI's own small
                // key range. key -= 36 shifts MIDI note 36 (C1) down to 0, and
                // notes outside the span are ignored
                case NoteOn:
                {
                    int key = event.data[0];
                    key -= 36;
                    if (key > 48 || key < 0)
                        break;
                    KeyRequest req(KeyRequest::Type::START, key - 24, midi2key[key],
                                   static_cast<float>(event.data[1] + 1));
                    engine_->request_fifo.PushBack(req);
                }
                break;
                case NoteOff:
                {
                    int key = event.data[0];
                    key -= 36;
                    if (key > 48 || key < 0)
                        break;
                    KeyRequest req(KeyRequest::Type::STOP, key - 24, midi2key[key], 127.f);
                    engine_->request_fifo.PushBack(req);
                }
                break;
                // CC-in is disabled while the menu is open so CC traffic can't fight
                // with menu navigation. CC 20-25 map to the 6 knobs
                // CC 14/15 are treated as two extra keys
                case ControlChange:
                {
                    if (!midi_cc_in) {
                        break;
                    }
                    if (menu_page_->IsActive())
                        break;

                    uint8_t cc = event.data[0];
                    uint8_t val = event.data[1];

                    if (cc >= 20 && cc < 26)
                    {
                        uint8_t knob = cc - 20;

                        if (knob == 4)
                            break;

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

    // When the MIDI timer interrupt fires, it queues the clock byte. If it's not already transmitting
    // a UART message, it can send it out right away, otherwise it has to wait. MIDI UART uses a very slow
    // baud rate and messages sometimes do have to wait for each other, especially when clock out is active.
    void QueueMidiClock() {
        uint8_t tx_buf_ = 0xF8;
        QueueDmaMessage(&tx_buf_, 1);
        if (usb_midi_active) {
            usb_midi.SendMessage(&tx_buf_, 1);
        }
    }

    // Without cleaning cache, it will transmit old data.
    void StartDmaTransfer() {
        uint8_t* p = fifo[fifo_tail].data;
        size_t len = fifo[fifo_tail].length;

        uintptr_t start = (uintptr_t)p & ~(32 - 1);
        uintptr_t end = ((uintptr_t)p + len + 32 - 1) & ~(32 - 1);
        SCB_CleanDCache_by_Addr((uint32_t*)start, end - start);

        uart_handler_.DmaTransmit(p, len, nullptr, TxEndCallback, this);
    }

    // Interrupts need to be managed carefully here, otherwise race conditions will happen
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

    // If a message finishes and there is another one waiting, send it now.
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

    private:
    UartHandler uart_handler_;
    MidiFifoEntry fifo[64];
    size_t fifo_head, fifo_tail;
    bool uart_tx_busy;
    bool midi_cc_in = true;

    uint8_t in_channel, out_channel;
    clockManager *clock_manager_;
    Sequencer *seq_;
    UserInterface *ui_;
    MenuPage *menu_page_;
    NormalPage *normal_page_;
    myEngine *engine_;
    Hardware *hw_;

    MidiUartHandler uart_midi;
    MidiUsbHandler usb_midi;
    bool usb_midi_active = true;
    uint32_t last_reset;

    bool key_cc[2];
};