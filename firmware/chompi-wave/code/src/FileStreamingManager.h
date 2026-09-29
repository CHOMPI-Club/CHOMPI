/** @file FileStreamingManager.h
 *  @brief SD card access (FatFs) is far too slow and unpredictable to call directly 
 *  from the audio ISR, so every file operation is handled outside of it. A lot of stuff
 *  in here isn't used.
 */
#pragma once
#include "daisy.h"
#include "fatfs.h"

#define MAX_SAMPLES_PER_CYCLE 2048

namespace daisy
{
    //class FileSampleReader; // forward declare gets around circular import
    class Engine; // forward declare gets around circular import
    // class VUTarget;
    // struct KeyRequest;

    //static constexpr size_t kMaxFileStreamingSamps = 32768;
    static constexpr size_t kMaxFileStreamingSamps = 8192;
    using SampleFifo = FIFO<int16_t, kMaxFileStreamingSamps>;

    /** @brief structure containing info necessary for deferred fileIO operations */
    struct FileRequest
    {
        enum class Type
        {
            OPEN,
            OPEN_NEW,
            OPEN_NEW_TWO,
            SEEK,
            READ,
            REV_READ,
            MASS_READ,
            WRITE,
            CLOSE,
            HEADER,
            UNLINK,
            TRUNCATE,
            DUMMY,
        };

        Type type_;
        FIL *fil_;
        const char *fname_;
        size_t size_in_bytes_;
        SampleFifo *fifo_;       // sample source/destination for READ/REV_READ/WRITE
        void *id_;
        float *wavetableMemory;  // destination for MASS_READ (bypasses fifo_ for bulk wavetable loads)

        /** constructor for full request data */
        FileRequest(Type type,
                    FIL *fileptr,
                    const char *filename,
                    size_t bytes,
                    SampleFifo *fifo,
                    void *id,
                    float *memory
                    )
            : type_(type),
              fil_(fileptr),
              fname_(filename),
              size_in_bytes_(bytes),
              fifo_(fifo),
              id_(id),
              wavetableMemory(memory)
        {
        }

        /** Empty, invalid request */
        FileRequest()
            : type_(Type::DUMMY),
              fil_(nullptr),
              fname_(nullptr),
              size_in_bytes_(0),
              fifo_(nullptr),
              id_(nullptr),
              wavetableMemory(nullptr)
        {
        }
    };

    class FileStreamingManager
    {
    public:
        enum class Status
        {
            OK,
            EMPTY,
            READ_SUCCESS,
            WRITE_SUCCESS,
            ERR_READ,
            ERR_WRITE,
            ERR_SEEK,
            ERR_UNKNOWN
        };

        FIFO<FileRequest, 64> request_fifo;
        FIFO<FileRequest, 32> debug_fifo;

        void Init(float samplerate)
        {
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
            /** Also calcs SubChunk2Size */
            // file_header.FileSize = CalcFileSize(); // do this on write complete
        }

        /** Get the number of requests in the FIFO for a given ID */
        size_t GetNumReqs(void* id)
        {
            size_t ret = 0;
            
            for(size_t i = 0; i < request_fifo.GetNumElements(); i++)
            {
                if(request_fifo[i].id_ == id)
                    ret++;
            }

            return ret;
        }

        /** Get the number of requests in the FIFO for a given ID and Type */
        size_t GetNumReqs(void* id, FileRequest::Type type)
        {
            size_t ret = 0;
            
            for(size_t i = 0; i < request_fifo.GetNumElements(); i++)
            {
                if(request_fifo[i].id_ == id && request_fifo[i].type_ == type)
                    ret++;
            }

            return ret;
        }

        void ClearRequests(void* id)
        {
            for(int i = request_fifo.GetNumElements() - 1; i >= 0; i--)
            {
                if(request_fifo[i].id_ == id)
                    request_fifo.Remove(i);
            }
        }


        /** I don't see an easy way of sequential access directly from the fifo pointer
         *  So the copy-to/from-the-workspace is a disappointing extra step.
         */
        uint8_t workspace_buffer[kMaxFileStreamingSamps * sizeof(int16_t)];

        Status ProcessRequests();

        WAV_FormatTypeDef file_header;

    };
} // namespace daisy