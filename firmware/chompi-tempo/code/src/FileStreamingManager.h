#pragma once
#include "daisy.h"
#include "fatfs.h"
#include "SampleInfo.h"

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
            OPEN_DIR,
            OPEN,
            OPEN_NEW, // CREATE_ALWAYS
            SEEK,
            PARSE_HEADER,
            READ,
            REV_READ,
            MASS_READ_INT16,
            WRITE,
            MASS_WRITE,
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
        SampleFifo *fifo_;
        void *pcmMemory;
        SampleInfo *sample_info_;

        /** constructor for full request data */
        FileRequest(Type type,
                    FIL *fileptr,
                    const char *filename,
                    size_t bytes,
                    SampleFifo *fifo,
                    void *pcm_mem,
                    SampleInfo *info
                    )
            : type_(type),
              fil_(fileptr),
              fname_(filename),
              size_in_bytes_(bytes),
              fifo_(fifo),
              pcmMemory(pcm_mem),
              sample_info_(info)
        {
        }

        /** Empty, invalid request */
        FileRequest()
            : type_(Type::DUMMY),
              fil_(nullptr),
              fname_(nullptr),
              size_in_bytes_(0),
              fifo_(nullptr),
              pcmMemory(nullptr),
              sample_info_(nullptr)
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

        /** FIFO of requests to fileIO that can be handled in order of reception
         *  Requests generated and pushed to the back from inside the AudioCallback,
         *  or from UI/MIDI interactions outside of the callback.
         */
        FIFO<FileRequest, 96> request_fifo;
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


        /** I don't see an easy way of sequential access directly from the fifo pointer
         *  So the copy-to/from-the-workspace is a disappointing extra step.
         */
        uint8_t workspace_buffer[kMaxFileStreamingSamps * sizeof(int16_t)];

        Status ProcessRequests();

        WAV_FormatTypeDef file_header;

    };
} // namespace daisy