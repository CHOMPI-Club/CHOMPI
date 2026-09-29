#pragma once
#include "daisy_seed.h"
#include "daisysp.h"

namespace daisy
{

    static const size_t kMaxRamBuffSize = 31694848 / 2;
    // total size = 31694848, which goes evenly into 8K, which means SDRAM page alignment

    // just a buffer with its length
    struct RamBufferMemory
    {
        // pointer to array of size kMaxRamBuffSize
        void Init(int16_t* m)
        {
            mem = m;
        }

        int16_t* mem;
        size_t length;

        void Clear()
        {
            std::fill(&mem[0], &mem[kMaxRamBuffSize], 0);
        }
    };

    // keeps track of its own read and write heads
    class RamBuffer
    {
    public:
        void Init(RamBufferMemory* b)
        {
            buff = b;
            buff->Clear();
        }

        void Reset()
        {
            buff->length = read_head = write_head = 0;
        }

        void BlockRead(int16_t* copy_buff, size_t size)
        {
            if(read_head + size > kMaxRamBuffSize)
                size = kMaxRamBuffSize - read_head;
            if(read_head + size > buff->length)
                size = buff->length - read_head;

            std::copy(&buff->mem[read_head], &buff->mem[read_head + size], copy_buff);
            read_head += size;
        }

        void StereoRead(int16_t* l, int16_t* r, bool rev)
        {
            const size_t new_read_head = read_head + 2;
            if(!rev && new_read_head <= buff->length && new_read_head <= kMaxRamBuffSize)
            {
                *l = buff->mem[read_head];
                *r = buff->mem[read_head + 1];
                read_head = new_read_head;
            }
            else if(rev && read_head >= 2)
            {
                *l = buff->mem[read_head - 2];
                *r = buff->mem[read_head - 1];
                read_head -= 2;
            }
            else
            {
                *l = *r = 0;
            }
        }

        void BlockWrite(int16_t* copy_buff, size_t size)
        {
            if(write_head + size > kMaxRamBuffSize)
                size = kMaxRamBuffSize - write_head;

            std::copy(copy_buff, &copy_buff[size], &buff->mem[write_head]);
            write_head += size;
            buff->length = write_head;
            buff->length -= buff->length % 2;
        }

        void StereoWrite(int16_t l, int16_t r, bool rev, bool set_len)
        {
            if(!rev)
            {
                const size_t new_write_head = write_head + 2;
                if(new_write_head <= kMaxRamBuffSize)
                {
                    buff->mem[write_head] = l;
                    buff->mem[write_head + 1] = r;
                    write_head = new_write_head;
                    if(set_len)
                        buff->length = write_head;
                }
            }
            else if(write_head >= 2)
            {
                buff->mem[write_head - 2] = l;
                buff->mem[write_head - 1] = r;
                write_head -= 2;
            }
        }

        inline size_t GetSize() { return buff->length; }

        inline size_t GetReadHead() { return read_head; }
        inline bool ReadEOF() { return read_head >= buff->length || read_head >= kMaxRamBuffSize; }
        inline bool ReadLoop(bool rev) { return (rev && read_head == 0) || (!rev && ReadEOF()); }

        void SetReadHead(size_t pos) 
        { 
            if(pos <= kMaxRamBuffSize && pos <= buff->length)
                read_head = pos;
        }
        
        size_t GetRemainingRead(bool rev)
        {
            if(!rev && !ReadEOF())
                return buff->length - read_head;
            else if(rev)
                return read_head;

            return 0;
        }

        inline size_t GetWriteHead() { return write_head; }
        inline bool WriteEOF() { return write_head >= buff->length || write_head >= kMaxRamBuffSize; }
        inline bool WriteFullLength() { return write_head >= kMaxRamBuffSize; }
        inline bool WriteLoop(bool rev) { return (rev && write_head == 0) || (!rev && WriteEOF()); }

        void SetWriteHead(size_t pos) 
        {
            if(pos <= kMaxRamBuffSize && pos <= buff->length)
                write_head = pos;
        }

        size_t GetRemainingWrite(bool rev)
        {
            if(!rev && !WriteEOF())
                return buff->length - write_head;
            else if(rev)
                return write_head;

            return 0;            
        }

        void AdvanceWrite(bool rev)
        {
            if(rev && write_head >= 2)
                write_head -= 2;
            else if(!rev && write_head + 2 <= buff->length && write_head + 2 < kMaxRamBuffSize)
                write_head += 2;
        }

    private:
        RamBufferMemory* buff;
        size_t read_head, write_head;
    };
} // namespace daisy