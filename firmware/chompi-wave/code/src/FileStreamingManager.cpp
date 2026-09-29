#include "FileStreamingManager.h"
#include "util/scopedirqblocker.h"

using namespace daisy;

#include "WavetableManager.h"

float newTestArray[10];

/** Handles exactly one queued FileRequest per call. This is called from the SD card callback, never
 *  the audio ISR. In TAPE, sample file reads are done frequently and in audio-downtime. File reading
 *  and writing is broken up so long files don't cause the UI to lag. Not really as important here and
 *  in TEMPO because all audio data is read on bootup, not during normal operation. Also, this class was
 *  ported from TAPE and many of these functions are not used. */
FileStreamingManager::Status FileStreamingManager::ProcessRequests()
{
    FRESULT fres;
    Status retval = Status::ERR_UNKNOWN;
    if (!request_fifo.IsEmpty())
    {
        // auto req = request_fifo.PopFront();
        auto req = request_fifo[0];
        switch (req.type_)
        {
        case FileRequest::Type::OPEN:
            if (req.fil_ && req.fname_)
            {
                fres = f_open(req.fil_,
                                req.fname_,
                                (FA_OPEN_ALWAYS | FA_WRITE | FA_READ));
                retval = fres == FR_OK ? Status::OK : Status::ERR_READ;
                if (req.id_) {
                    static_cast<wavetableLoader*>(req.id_)->markSetupResult(retval == Status::OK);
                }
            }

            break;
        case FileRequest::Type::OPEN_NEW:
            if (req.fil_ && req.fname_)
            {
                fres = f_open(req.fil_,
                                req.fname_,
                                (FA_CREATE_ALWAYS | FA_WRITE | FA_READ));

                UINT bw = 0;
                fres = f_write(req.fil_, &file_header, sizeof(file_header), &bw);
                fres = f_sync(req.fil_);

                retval = fres == FR_OK ? Status::OK : Status::ERR_READ;
            }
            break;
            case FileRequest::Type::OPEN_NEW_TWO:
            if (req.fil_ && req.fname_)
            {
                fres = f_open(req.fil_,
                                req.fname_,
                                (FA_OPEN_ALWAYS | FA_WRITE | FA_READ));

                UINT bw = 0;
                fres = f_write(req.fil_, &file_header, sizeof(file_header), &bw);
                fres = f_sync(req.fil_);

                retval = fres == FR_OK ? Status::OK : Status::ERR_READ;
            }
            break;
        case FileRequest::Type::SEEK:
        {
            if (req.fil_)
            {
                if(req.size_in_bytes_ > f_size(req.fil_))
                {
                    retval =  Status::ERR_SEEK;
                }
                else
                {
                    fres = f_lseek(req.fil_, req.size_in_bytes_);
                    retval = fres == FR_OK ? Status::OK : Status::ERR_SEEK;
                }

                if (req.id_ && retval != Status::OK) {
                    static_cast<wavetableLoader*>(req.id_)->markSetupResult(false);
                }
            }
        }
        break;
        case FileRequest::Type::READ:
        {
            if (req.fil_)
            {
                /** Read from file to workspace */
                UINT br = 0;
                if(req.size_in_bytes_ + f_tell(req.fil_) >= f_size(req.fil_))
                {
                    req.size_in_bytes_ = f_size(req.fil_) - f_tell(req.fil_);
                }

                if(f_size(req.fil_) < f_tell(req.fil_))
                {
                    retval = Status::ERR_READ;
                }
                else
                {
                    fres = f_read(req.fil_, workspace_buffer, req.size_in_bytes_, &br);
                    retval = fres == FR_OK ? Status::READ_SUCCESS : Status::ERR_READ;
                }
                
                if (fres == FR_OK && br == req.size_in_bytes_ && retval != Status::ERR_READ)
                {
                    /** Everything's fine copy buffer -- I imagine there's a less loopy way
                        *  but we can/should probably also add some overflow handling to the FIFO
                        */
                    int16_t *sampbuff = (int16_t *)workspace_buffer; /**< C style casting just because */
                    // req.fifo_->MassPushBack(sampbuff, req.size_in_bytes_ / 2);
                    for (size_t i = 0; i < req.size_in_bytes_ / sizeof(int16_t); i++)
                    {
                        req.fifo_->PushBack(sampbuff[i]);
                    }
                }
                else if (fres == FR_OK)
                {
                    /** End of File --
                        *  TODO: loop, or pad with zeroes, and then copy over to FIFO */
                }

                // if (retval == Status::READ_SUCCESS)
                {
                    //static_cast<FileSampleReader*>(req.id_)->DecrementReadRequests();
                }
            }
        }
        break;
        case FileRequest::Type::REV_READ:
        {
            if (req.fil_)
            {
                /** Read from file to workspace */
                UINT br = 0;

                if(f_size(req.fil_) < f_tell(req.fil_))
                {
                    retval = Status::ERR_READ;
                }
                else
                {
                    fres = f_read(req.fil_, workspace_buffer, req.size_in_bytes_, &br);
                    retval = fres == FR_OK ? Status::READ_SUCCESS : Status::ERR_READ;
                }
                
                if (fres == FR_OK && br == req.size_in_bytes_ && retval != Status::ERR_READ)
                {
                    /** Everything's fine copy buffer -- I imagine there's a less loopy way
                        *  but we can/should probably also add some overflow handling to the FIFO
                        */
                    int16_t *sampbuff = (int16_t *)workspace_buffer; /**< C style casting just because */

                    /** write the samples in reverse order */
                    size_t size = req.size_in_bytes_ / sizeof(int16_t);
                    for (size_t i = 0; i < size; i += 2)
                    {
                        req.fifo_->PushBack(sampbuff[size - i - 2]);
                        req.fifo_->PushBack(sampbuff[size - i - 1]);
                    }
                }
                else if (fres == FR_OK)
                {
                    /** End of File --
                        *  TODO: loop, or pad with zeroes, and then copy over to FIFO */
                }

                // if (retval == Status::READ_SUCCESS)
                {
                    //static_cast<FileSampleReader*>(req.id_)->DecrementReadRequests();
                }
            }
        }
        break;
        case FileRequest::Type::MASS_READ:
        {
            wavetableLoader *ldr = static_cast<wavetableLoader*>(req.id_);
            if (ldr && ldr->currentLoadFailed()) {
                ldr->markReadResult(false);
                retval = Status::ERR_READ;
            }
            else {
                UINT br = 0;
                FRESULT fresmr = f_read(req.fil_, req.wavetableMemory, req.size_in_bytes_, &br);
                bool ok = (fresmr == FR_OK) && (br == req.size_in_bytes_);
                if (ldr) {
                    ldr->markReadResult(ok);
                }
                retval = ok ? Status::OK : Status::ERR_READ;
            }
        }
        break;
        case FileRequest::Type::WRITE:
        {
            if (req.fil_)
            {
                /** Copy data from fifo to workspace */
                int16_t *sampbuff = (int16_t *)workspace_buffer;
                for (size_t i = 0; i < req.size_in_bytes_ / sizeof(int16_t); i++)
                {
                    sampbuff[i] = req.fifo_->PopFront();
                }
                /** Then write workspace to file */
                UINT bw = 0;
                // HAL_NVIC_DisableIRQ(DMA1_Stream1_IRQn);
                FRESULT fresw = f_write(req.fil_, workspace_buffer, req.size_in_bytes_, &bw);
                // HAL_NVIC_EnableIRQ(DMA1_Stream1_IRQn);
                fres = f_sync(req.fil_);
                if (fresw == FR_OK && fres == FR_OK && bw == req.size_in_bytes_)
                {
                    /** we're good */
                    retval = Status::WRITE_SUCCESS;
                }
                else
                {
                    /** we're not so good */
                    retval = Status::ERR_WRITE;
                }
            }
        }
        break;
        case FileRequest::Type::CLOSE:
            if (req.fil_)
            {
                if(f_size(req.fil_) == 0)
                {
                    retval = Status::OK;
                    break;
                }

                fres = f_close(req.fil_);

                if(fres == FR_OK)
                {
                    req.fil_->obj.objsize = 0;
                }
                retval = fres == FR_OK ? Status::OK : Status::ERR_UNKNOWN;
            }
            break;
        case FileRequest::Type::HEADER: // update the WAV header after a write
            if (req.fil_)
            {
                UINT bw = 0;
                file_header.SubCHunk2Size = f_size(req.fil_) - sizeof(file_header);
                file_header.FileSize = f_size(req.fil_);

                fres = f_lseek(req.fil_, 0);
                fres = f_write(req.fil_, &file_header, sizeof(file_header), &bw);
                fres = f_sync(req.fil_);

                retval = fres == FR_OK ? Status::OK : Status::ERR_WRITE;
            }
            break;
        case FileRequest::Type::UNLINK:
            fres = f_unlink(req.fname_);

            retval = fres == FR_OK ? Status::OK : Status::ERR_UNKNOWN;
            break;
        case FileRequest::Type::TRUNCATE:
        {
            FRESULT frest = f_truncate(req.fil_);
            fres = f_sync(req.fil_);

            if(frest != FR_OK || fres != FR_OK)
                retval = Status::ERR_UNKNOWN;
            else
                retval = Status::OK; 
        break;
        }
        default:
            retval = Status::ERR_UNKNOWN;
            break;
        }
    
        if (retval == Status::ERR_READ
            || retval == Status::ERR_SEEK
            || retval == Status::ERR_WRITE
            || retval == Status::ERR_UNKNOWN
        )
        {
            // On failure, move the request into debug_fifo instead of discarding it -
            // a fixed-size ring (oldest dropped once full) of recently-failed requests
            // that can be inspected later (e.g. over USB serial) without needing a
            // debugger attached at the moment of failure.
            auto db_req = request_fifo.PopFront();

            if(debug_fifo.IsFull())
                debug_fifo.PopFront();

            debug_fifo.PushBack(db_req);
        }
        else
        {
            request_fifo.PopFront();
        }
    }
    else
    {
        retval = Status::EMPTY;
    }
    
    return retval;
}
