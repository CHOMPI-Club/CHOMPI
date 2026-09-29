#include "FileStreamingManager.h"
#include "util/scopedirqblocker.h"

using namespace daisy;

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
        case FileRequest::Type::OPEN_DIR:
        {
            
        }
        break;
        case FileRequest::Type::OPEN:
            if (req.fil_ && req.fname_)
            {
                fres = f_open(req.fil_,
                                req.fname_,
                                (FA_OPEN_ALWAYS | FA_WRITE | FA_READ));
                retval = fres == FR_OK ? Status::OK : Status::ERR_READ;
                if (retval == Status::OK) { }
                    //static_cast<FileSampleReader*>(req.id_)->FileOpened();
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
    
                // if (retval == Status::OK)
                {
                    //static_cast<FileSampleReader*>(req.id_)->DecrementSeekRequests();
                }
            }
        }
        break;
        case FileRequest::Type::PARSE_HEADER:
        {
            UINT br = 0;
            uint8_t temp_buffer[116];
            fres = f_lseek(req.fil_, 0);
            if (fres == FR_OK)
            {
                fres = f_read(req.fil_, temp_buffer, sizeof(temp_buffer), &br);
                if (fres == FR_OK && br == sizeof(temp_buffer))
                {
                    // Search for "data" in temp_buffer
                    for (size_t i = 0; i + 8 <= sizeof(temp_buffer); ++i)
                    {
                        if (temp_buffer[i] == 'd' && temp_buffer[i+1] == 'a' &&
                            temp_buffer[i+2] == 't' && temp_buffer[i+3] == 'a')
                        {
                            // Read the 4 bytes after "data" as little-endian uint32_t
                            uint32_t data_chunk_size = temp_buffer[i+4] |
                                                       (temp_buffer[i+5] << 8) |
                                                       (temp_buffer[i+6] << 16) |
                                                       (temp_buffer[i+7] << 24);
                            // Optionally store or log data_chunk_size
                            req.sample_info_->numSamples = fmin(data_chunk_size, 1920000);
                            // After finding "data", seek to its offset in the file
                            size_t data_offset = i + 8;
                            f_lseek(req.fil_, data_offset);
                            break;
                        }
                    }
                    if (req.sample_info_->numSamples == 0) {
                        req.sample_info_->status = SampleStatus::FAILED;
                    }
                }
                else {
                    req.sample_info_->status = SampleStatus::FAILED;
                }
            }
            else {
                req.sample_info_->status = SampleStatus::FAILED;
            }
        }
        break;
        case FileRequest::Type::MASS_READ_INT16:
        {
            UINT br = 0;
            if (req.sample_info_->status != SampleStatus::FAILED) {
                FRESULT fresmr = f_read(req.fil_, req.pcmMemory, req.sample_info_->numSamples, &br);

                if (fresmr != FR_OK || br != req.sample_info_->numSamples) {
                    retval = Status::ERR_READ;
                    req.sample_info_->status = SampleStatus::FAILED;
                }
                else {
                    req.sample_info_->status = SampleStatus::LOADED;
                }
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
        case FileRequest::Type::MASS_WRITE:
        {
            if (req.fil_) {
                uint8_t* src_ptr = (uint8_t*)req.pcmMemory;
                size_t remaining = req.size_in_bytes_;

                while (remaining > 0)
                {
                    size_t chunk = remaining > 10000 ? 10000 : remaining;
                    memcpy(workspace_buffer, src_ptr, chunk);

                    UINT bw = 0;
                    FRESULT fres = f_write(req.fil_, workspace_buffer, chunk, &bw);
                    if (fres != FR_OK || bw != chunk)
                        break;

                    src_ptr += chunk;
                    remaining -= chunk;
                }
                f_sync(req.fil_);
            }
        }
        break;;
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
