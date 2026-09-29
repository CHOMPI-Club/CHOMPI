#pragma once
#include "daisy.h"
#include "fatfs.h"
#include "FileStreamingManager.h"

namespace daisy {

class FileCopier
{
    public:
        FileCopier() {}
        ~FileCopier() {}

        void Init(float samplerate, Engine* fx,
                    RamBufferMemory* chompi_buff, RamBufferMemory* looper_buff)
        {
            fx_ = fx;
            copying_ = false;

            // WAV file header template
            file_header.ChunkId       = kWavFileChunkId;     /** "RIFF" */
            file_header.FileFormat    = kWavFileWaveId;      /** "WAVE" */
            file_header.SubChunk1ID   = kWavFileSubChunk1Id; /** "fmt " */
            file_header.SubChunk1Size = 16;                  // for PCM
            file_header.AudioFormat   = WAVE_FORMAT_PCM;
            file_header.NbrChannels   = 2;
            file_header.SampleRate    = static_cast<int>(samplerate);
            file_header.ByteRate      = samplerate * 2 * 16 / 8; // sr * chan * bitspersample / 8
            file_header.BlockAlign    = 2 * 16 / 8; //channels * bitspersample / 8;
            file_header.BitPerSample  = 16;
            file_header.SubChunk2ID   = kWavFileSubChunk2Id; /** "data" */

            chompi_ram.Init(chompi_buff);
            looper_ram.Init(looper_buff);
        }

        bool CopyProcess()
        {
            if(!copying_ && !req_fifo.IsEmpty())
            {
                copying_ = true;
                req = req_fifo.PopFront();

                is_chompi_ram = req.is_chompi;
                is_looper_ram = req.is_looper;

                chompi_ram.SetReadHead(0);
                chompi_ram.SetWriteHead(0);

                looper_ram.SetReadHead(0);
                looper_ram.SetWriteHead(0);

                CopyStart(req.src, req.src_bank, req.src_mode, req.dest, req.dest_bank, req.dest_mode);
            }

            if(!copying_)
                return false;


            if(!CopySizeFull() && !IsEOF())
            {
                UINT read_num = 0;

                uint32_t read_size = buff_size;
                if(copyread + read_size >= copysize && copysize != 0)
                    read_size = copysize - copyread;

                // read
                if(is_chompi_ram == CopyRequest::RamDir::FROM)
                {
                    chompi_ram.BlockRead((int16_t*)buff, read_size / 2);
                    read_num = read_size;
                }
                else if(is_looper_ram == CopyRequest::RamDir::FROM)
                {
                    looper_ram.BlockRead((int16_t*)buff, read_size / 2);
                    read_num = read_size;
                }
                else
                {
                    f_read(&fptr_read, buff, read_size, &read_num);
                }


                // write
                if(is_chompi_ram == CopyRequest::RamDir::TO)
                {
                    chompi_ram.BlockWrite((int16_t*)buff, read_num / 2);
                }
                else if(is_looper_ram == CopyRequest::RamDir::TO)
                {
                    looper_ram.BlockWrite((int16_t*)buff, read_num / 2);
                }
                else
                {
                    if(misalign != 0)
                    {
                        for(size_t i = 0; i < misalign; i++)
                        {
                            buff[i] = 0;
                        }
                        misalign = 0;
                    }
                    f_write(&fptr_write, buff, read_size, nullptr);
                    f_sync(&fptr_write);
                }

                copyread += read_num;

                // double speed write
                if(is_chompi_ram != CopyRequest::RamDir::TO && is_looper_ram != CopyRequest::RamDir::TO)
                {
                    size_t write = 0;
                    size_t read = 0;
                    while(read < read_num)
                    {
                        buff[write] = buff[read];
                        buff[write + 1] = buff[read + 1];
                        buff[write + 2] = buff[read + 2];
                        buff[write + 3] = buff[read + 3];

                        write += 4;
                        read += 8;
                    }

                    f_write(&fptr_write_double, buff, read_num / 2, nullptr);
                    f_sync(&fptr_write_double);
                }
            }

            else
            {
                CopyDone();
            }

            return true;
        }

        inline bool IsCopying()  { return copying_ || !req_fifo.IsEmpty(); }

        bool FileExists(size_t idx, VoiceMode mode, size_t bank, bool dbl = false)
        {
            char fname[32];
            Engine::GetFileNameForSlot(idx + 1, bank, mode, fname, dbl);
            return f_stat(fname, nullptr) == FR_OK;
        }

        bool NeedsOverwrite(size_t idx, VoiceMode mode, size_t bank)
        {
            char fname[32];
            Engine::GetFileNameForSlot(idx + 1, bank, mode, fname, false);

            // no file, no overwrite
            if(f_stat(fname, nullptr) != FR_OK)
                return false;

            f_open(&fptr_read, fname, (FA_OPEN_ALWAYS | FA_WRITE | FA_READ));

            // large header || has footer?
            uint32_t size = 0;
            if(JumpToData(fname, &size))
            {
                CloseFile(&fptr_read);
                return true;
            }

            CloseFile(&fptr_read);

            // missing 2x file?
            Engine::GetFileNameForSlot(idx + 1, bank, mode, fname, true);
            if(f_stat(fname, nullptr) != FR_OK)
                return true;

            f_open(&fptr_read, fname, (FA_OPEN_ALWAYS | FA_WRITE | FA_READ));

            // 2x file large header || has footer?
            uint32_t dbl_size = 0;
            if(JumpToData(fname, &dbl_size))
            {
                CloseFile(&fptr_read);
                return true;
            }

            CloseFile(&fptr_read);

            // wrong 2x size
            return ((size - sizeof(WAV_FormatTypeDef)) / 2) != (dbl_size - sizeof(WAV_FormatTypeDef));
        }

        struct CopyRequest {

            enum RamDir {
                NONE,
                FROM,
                TO
            };

            CopyRequest(size_t s, size_t b, VoiceMode m, size_t d, size_t db, VoiceMode dm, bool st, RamDir ic, RamDir il)
                : src(s),
                src_bank (b),
                src_mode(m),
                dest(d),
                dest_bank(db),
                dest_mode(dm),
                set(st),
                is_chompi(ic),
                is_looper(il)
            {
            }

            // default
            CopyRequest()
                : src(0),
                src_bank (0),
                src_mode(VoiceMode::JAMMI),
                dest(0),
                dest_bank(0),
                dest_mode(VoiceMode::JAMMI),
                set(false),
                is_chompi(RamDir::NONE),
                is_looper(RamDir::NONE)
            {
            }

            ~CopyRequest() {}

            size_t src;
            size_t src_bank;
            VoiceMode src_mode;
            size_t dest;
            size_t dest_bank;
            VoiceMode dest_mode;
            bool set;
            RamDir is_chompi;
            RamDir is_looper;
        };

        FIFO<CopyRequest, 16> req_fifo;

    private:
        void CopyStart(size_t src, size_t src_bank, VoiceMode src_mode, size_t dest, size_t dest_bank, VoiceMode dest_mode)
        {

            if(dest < 16)
                fx_->SetFileExists(dest - 1, dest_bank, dest_mode, true);

            char fname[32];

            copyread = 0;
            copysize = 0;

            if(is_chompi_ram != CopyRequest::RamDir::FROM && is_looper_ram != CopyRequest::RamDir::FROM)
            {
                // close then open read FIL
                CloseFile(&fptr_read);

                Engine::GetFileNameForSlot(src, src_bank, src_mode, fname, false);
                f_open(&fptr_read, fname, (FA_OPEN_ALWAYS | FA_WRITE | FA_READ));

                // jump read FIL past the header, set size variables
                JumpToData(fname, &copysize);
            }
            else if(is_chompi_ram == CopyRequest::RamDir::FROM)
            {
                copysize = chompi_ram.GetSize() * 2;
                src_bank = 0;
                src_mode = VoiceMode::CUBBI;
            }
            else if(is_looper_ram == CopyRequest::RamDir::FROM)
            {
                copysize = looper_ram.GetSize() * 2;
                src_bank = 0;
                src_mode = VoiceMode::CUBBI;
            }

            if(is_chompi_ram != CopyRequest::RamDir::TO && is_looper_ram != CopyRequest::RamDir::TO)
            {
                // close then open write FILs
                CloseFile(&fptr_write);
                CloseFile(&fptr_write_double);


                Engine::GetFileNameForSlot(dest, dest_bank, dest_mode, fname, false);
                f_open(&fptr_write, fname, (FA_OPEN_ALWAYS | FA_WRITE | FA_READ));
                WriteHeader(&fptr_write);

                Engine::GetFileNameForSlot(dest, dest_bank, dest_mode, fname, true);
                f_open(&fptr_write_double, fname, (FA_OPEN_ALWAYS | FA_WRITE | FA_READ));
                WriteHeader(&fptr_write_double);
            }

            copying_ = true;
        }

        void WriteHeader(FIL* fil)
        {
            // write header
            UINT bw = 0;
            file_header.SubCHunk2Size = f_size(fil) - sizeof(file_header);
            file_header.FileSize = f_size(fil);

            f_lseek(fil, 0);
            f_write(fil, &file_header, sizeof(file_header), &bw);
            f_sync(fil);
        }

        void CloseFile(FIL* fil)
        {
            if(f_size(fil) != 0)
            {
                f_close(fil);
                fil->obj.objsize = 0;
            }
        }

        void CopyDone()
        {
            if(is_chompi_ram != CopyRequest::RamDir::TO && is_looper_ram != CopyRequest::RamDir::TO)
            {
                f_truncate(&fptr_write);
                f_truncate(&fptr_write_double);

                WriteHeader(&fptr_write);
                WriteHeader(&fptr_write_double);

                CloseFile(&fptr_write);
                CloseFile(&fptr_write_double);
            }

            if(is_chompi_ram != CopyRequest::RamDir::FROM && is_looper_ram != CopyRequest::RamDir::FROM)
                CloseFile(&fptr_read);

            fptr_read.obj.objsize = 0;
            fptr_write.obj.objsize = 0;
            fptr_write_double.obj.objsize = 0;

            copying_ = false;

            if(req.set)
            {
                fx_->SetBank(req.dest_bank);
                fx_->SetVoiceMode(req.dest_mode);
                fx_->SetVoiceSlot(req.dest, false);
            }

            fx_->SetAllCopyOccurred();
        }

        inline bool CopySizeFull() { return (copyread >= copysize && copysize != 0); }

        bool IsData(uint8_t* data)
        {
            return (data[0] == 'd') && (data[1] == 'a') && (data[2] == 't') && (data[3] == 'a');
        }

        bool JumpToData(char* fname, uint32_t* size)
        {
            if(size != nullptr)
                *size = f_size(&fptr_read);

            uint8_t data[4];
            copysize = 0;
            misalign = 0;

            uint32_t ctr = 0;
            UINT br;
            while(ctr < 2048 && !IsEOF())
            {
                f_lseek(&fptr_read, ctr);
                f_read(&fptr_read, data, 4, &br);
                ctr++;

                if(br != 4)
                {
                    break;
                }
                else if(IsData(data))
                {
                    f_read(&fptr_read, data, 4, &br); // read the data size

                    for(size_t i = 0; i < 4; i++)
                    {
                        copysize += uint32_t(data[i]) << (i * 8);
                    }

                    bool header = f_tell(&fptr_read) != sizeof(WAV_FormatTypeDef);
                    bool footer = f_tell(&fptr_read) + copysize != f_size(&fptr_read);

                    // fix 4 byte grid misalignment due to odd sized headers
                    misalign = f_tell(&fptr_read) % 4;
                    if(misalign == 2)
                        f_lseek(&fptr_read, f_tell(&fptr_read) - misalign);

                    return header || footer;
                }
            }

            // couldn't find "data", jump back to start
            f_lseek(&fptr_read, 0);
            return false;
        }

        bool IsEOF() 
        {
            if(is_chompi_ram == CopyRequest::RamDir::FROM)
                return chompi_ram.ReadEOF();
            else if(is_looper_ram == CopyRequest::RamDir::FROM)
                return looper_ram.ReadEOF();

            return f_eof(&fptr_read);
        }

        FIL fptr_read;
        FIL fptr_write;
        FIL fptr_write_double;
        bool copying_;
        uint32_t copysize = 0;
        uint32_t copyread = 0;
        WAV_FormatTypeDef file_header;

        // RAM IO
        CopyRequest::RamDir is_chompi_ram;
        CopyRequest::RamDir is_looper_ram;        
        RamBuffer chompi_ram;
        RamBuffer looper_ram;        

        CopyRequest req;

        static const size_t buff_size = 4096;
        uint8_t buff[buff_size];
        uint8_t misalign;

        Engine* fx_;
};
} // namespace daisy