#include <cstdint>
#include <stm32h750xx.h>

#include "dfu.h"

#include "usbd_def.h"
#include "usbd_desc.h"
#include "usbd_dfu.h"
#include "usbd_dfu_if.h"
#include "system.h"

#include "dfu_log.h"

using namespace daisy;

static constexpr const size_t kMaxDfuProgramSize = 8192;

static DfuLogger __attribute__((section(".dtcmram_bss"))) dfu_log;
static uint8_t __attribute__((section(".dtcmram_bss"))) gDfuWriteBuffer[kMaxDfuProgramSize];

#define DSY_DTCMRAM_BSS __attribute__((section(".dtcmram_bss")))

extern "C"
{
    USBD_HandleTypeDef hUsbDeviceFS;
    USBD_HandleTypeDef hUsbDeviceHS;
    extern PCD_HandleTypeDef hpcd_USB_OTG_FS;
    extern PCD_HandleTypeDef hpcd_USB_OTG_HS;
    void enable_jump();
}

class DFUHandle::Impl
{
public:
    Impl() {}
    ~Impl() {}

    Result Init(QSPIHandle* qspi);

    Result MemoryInit();
    Result MemoryDeinit();
    Result MemoryErase(uint32_t Add);
    Result MemoryWrite(uint8_t *src, uint8_t *dest, uint32_t Len);
    Result MemoryRead(uint8_t *src, uint8_t *dest, uint32_t Len);
    Result MemoryStatus(uint32_t Add, uint8_t Cmd, uint8_t *buffer);
    Result USBInit_FS();
    Result USBInit_HS();
    Result DeInit();

    /** Handles the I/O  */
    Result ProcessIoRequests();

    bool dfu_complete;
    bool dfu_initiated;

private:
    static constexpr uint32_t addr_offset_ = 0x90000000U;
    static constexpr uint32_t sector_size_ = 0x10000U;


    // New stuff for managing blocking I/O in a safer location
    struct IoStatus {
        enum class Type {
            Write,
            Erase,
            Idle,
        };

        bool busy;
        Type type;
        uint32_t start_address, length;
        uint32_t start_time;

        IoStatus()
            : busy(false),
              type(Type::Idle),
              start_address(0),
              length(0),
              start_time(0) {}
    };

    IoStatus io_state;
    uint8_t* io_buffer;
    size_t io_buffer_size;


    QSPIHandle* qspi_;
    size_t data_written_;
};

// Global dfu handle
DFUHandle::Impl dfu_impl;


DFUHandle::Result DFUHandle::Impl::Init(QSPIHandle* qspi)
{

#if DSY_DFU_USE_EXT_USB
    uint8_t *clear_ptr = (uint8_t *)&hUsbDeviceHS;
    for (size_t i = 0; i < sizeof(USBD_HandleTypeDef); i++)
        *clear_ptr++ = 0;
    if (USBInit_HS() != Result::OK)
        return Result::ERR;
#else
    uint8_t *clear_ptr = (uint8_t *)&hUsbDeviceFS;
    for (size_t i = 0; i < sizeof(USBD_HandleTypeDef); i++)
        *clear_ptr++ = 0;
    if (USBInit_FS() != Result::OK)
        return Result::ERR;
#endif

    HAL_PWREx_EnableUSBVoltageDetector();

    data_written_ = 0;
    dfu_complete = false;
    dfu_initiated = false;

    dfu_log.Clear();

    io_buffer = gDfuWriteBuffer;
    io_buffer_size = kMaxDfuProgramSize;

    qspi_ = qspi;

    return Result::OK;
}

DFUHandle::Result DFUHandle::Impl::DeInit()
{
#if DSY_DFU_USE_EXT_USB
    if (USBD_DeInit(&hUsbDeviceHS) != USBD_OK)
        return Result::ERR;
#else
    if (USBD_DeInit(&hUsbDeviceFS) != USBD_OK)
        return Result::ERR;
#endif
    // HAL_PWREx_DisableUSBVoltageDetector();

    // TODO -- create mechanism to ensure the
    // deinit happens after USB disconnect so
    // the disconnect can happen without hanging
    System::Delay(100);

    return Result::OK;
}

DFUHandle::Result DFUHandle::Impl::MemoryInit()
{
    return Result::OK;
}

DFUHandle::Result DFUHandle::Impl::MemoryDeinit()
{
    return Result::OK;
}

DFUHandle::Result DFUHandle::Impl::MemoryErase(uint32_t Add)
{
    // DFU download has begun, so we shouldn't allow a jump
    // to happen before it completes

    if (io_state.busy) {
        // Presumably ok to still busy here..?
        dfu_log.pushEvent(DfuLogger::Event::Type::EraseBusy, System::GetNow(), Add, sector_size_, 0);
        return Result::ERR;
    }

    if (System::GetMemoryRegion(Add) == System::MemoryRegion::QSPI)
    {
        dfu_initiated = true;
        io_state.busy = true;
        io_state.type = IoStatus::Type::Erase;
        io_state.start_time = System::GetNow();
        io_state.start_address = Add;
        io_state.length = sector_size_;
        return Result::OK;
    }


    // uint32_t tstart = System::GetNow();

    // if (System::GetMemoryRegion(Add) == System::MemoryRegion::QSPI)
    // {
    //     dfu_initiated = true;
    //     Add -= addr_offset_;
    //     qspi_->Erase(Add, Add + sector_size_);
    //     dfu_log.pushEvent(DfuLogger::Event::Type::Erase, tstart, Add, sector_size_, System::GetNow() - tstart);
    //     return Result::OK;
    // }

    // uint32_t tend = System::GetNow();
    // auto dur = tend - tstart;
    // dfu_log.pushEvent(DfuLogger::Event::Type::Erase, tstart, Add, sector_size_, dur);

    return Result::ERR;
}

DFUHandle::Result DFUHandle::Impl::MemoryWrite(uint8_t *src, uint8_t *dest, uint32_t Len)
{

    if (io_state.busy) {
        dfu_log.pushEvent(DfuLogger::Event::Type::WriteBusy, System::GetNow(), (uint32_t)dest, Len, 0);
        return Result::ERR;
    }

    if (System::GetMemoryRegion((uint32_t)dest) == System::System::MemoryRegion::QSPI) {
        // Copy data to scratch buffer
        std::copy(src, src+ Len, io_buffer);

        io_state.busy = true;
        io_state.type = IoStatus::Type::Write;
        io_state.start_time = System::GetNow();
        io_state.start_address = (uint32_t)dest;
        io_state.length = Len;
        return Result::OK;
    }

    // uint32_t tstart = System::GetNow();
    // if (System::GetMemoryRegion((uint32_t)dest) == System::MemoryRegion::QSPI)
    // {
    //     uint32_t write_addr = (uint32_t)dest - addr_offset_;
    //     qspi_->Write(write_addr, Len, src);
    //     data_written_ += Len;
    //     // Log
    //     dfu_log.pushEvent(DfuLogger::Event::Type::Write, tstart, (uint32_t)dest, Len, System::GetNow() - tstart);
    //     return Result::OK;
    // }
    // uint32_t tend = System::GetNow();
    // auto dur = tend - tstart;
    // dfu_log.pushEvent(DfuLogger::Event::Type::Write, tstart, (uint32_t)dest, Len, dur);

    return Result::ERR;
}

DFUHandle::Result DFUHandle::Impl::MemoryRead(uint8_t *src, uint8_t *dest, uint32_t Len)
{
    uint32_t tstart = System::GetNow();
    if (System::GetMemoryRegion((uint32_t)src) == System::MemoryRegion::QSPI)
    {
        // TODO -- this will need to change for multi-programs
        for (size_t i = 0; i < Len; i++)
            dest[i] = *((__IO uint8_t *)QSPI_BASE + *src + i);
        // dest[i] = qspi_buffer[*src + i];
        // Log
        uint32_t tend = System::GetNow();
        auto dur = tend - tstart;
        dfu_log.pushEvent(DfuLogger::Event::Type::Read, tstart, (uint32_t)dest, Len, dur);
        return Result::OK;
    }
    uint32_t tend = System::GetNow();
    auto dur = tend - tstart;
    dfu_log.pushEvent(DfuLogger::Event::Type::Read, tstart, (uint32_t)dest, Len, dur);

    return Result::ERR;
}

DFUHandle::Result DFUHandle::Impl::MemoryStatus(uint32_t Add, uint8_t Cmd, uint8_t *buffer)
{
    uint32_t tstart = System::GetNow();
    switch (Cmd)
    {
    case DFU_MEDIA_PROGRAM:
        buffer[0] = 0; // bStatus (0 = OK) TODO -- make this actually check the status
        // I'm assuming this is little-endian
        // (originally) 3 -> 3 milliseconds (0.2 typ page program * 16 (= 4096 bytes))
        // updated for "Max" time (0.8 page program * 16) = 12.8 with some wiggle room (20ms)
        // buffer[1] = 20; // bwPollTimeout 0
        // buffer[1] = 6; // bwPollTimeout 0
        buffer[1] = 3; // bwPollTimeout 0
        buffer[2] = 0; // bwPollTimeout 1
        buffer[3] = 0; // bwPollTimeout 2
        buffer[4] = 4; // bState (4 = dfuDNBUSY)
        buffer[5] = 0; // no state string
        break;

    default:
    case DFU_MEDIA_ERASE:
        // Datasheet values:
        // 64k erase (typ: 0.15s, max:1.0s)
        // 4k erase (used in libDaisy) (typ: 0.07s, max: 0.3s)
        // (0.3 * 16) = 4.8s, with some wiggle room for command latency, etc.
        // we'll put this at 8s timeout with a plan to switch to 64kB erasures later
        // to reduce the timeout..
        // uint16_t timeout = 8000;
        // uint16_t timeout = 1000;
        uint16_t timeout = 200;
        buffer[0] = 0;   // bStatus (0 = OK) TODO -- make this actually check the status
        buffer[1] = (uint8_t)(timeout & 0x00ff);
        buffer[2] = (uint8_t)((timeout >> 8) & 0x00ff);
        // buffer[1] = 150; // bwPollTimeout 0
        // buffer[2] = 0;   // bwPollTimeout 1
        buffer[3] = 0;   // bwPollTimeout 2
        buffer[4] = 4;   // bState (4 = dfuDNBUSY)
        buffer[5] = 0;   // no state string
        break;
    }
    uint32_t tend = System::GetNow();
    auto dur = tend - tstart;
    dfu_log.pushEvent(DfuLogger::Event::Type::Status, tstart, (uint32_t)0, 0, dur);
    return Result::OK;
}

DFUHandle::Result DFUHandle::Impl::USBInit_FS()
{
    if (USBD_Init(&hUsbDeviceFS, &FS_Desc, DEVICE_FS) != USBD_OK)
    {
        return Result::ERR;
    }
    if (USBD_RegisterClass(&hUsbDeviceFS, &USBD_DFU) != USBD_OK)
    {
        return Result::ERR;
    }
    // hUsbDeviceFS.pClass = &USBD_DFU;
    if (USBD_DFU_RegisterMedia(&hUsbDeviceFS, &USBD_DFU_fops_FS) != USBD_OK)
    {
        return Result::ERR;
    }
    if (USBD_Start(&hUsbDeviceFS) != USBD_OK)
    {
        return Result::ERR;
    }
    return Result::OK;
}

DFUHandle::Result DFUHandle::Impl::USBInit_HS()
{
    if(USBD_Init(&hUsbDeviceHS, &HS_Desc, DEVICE_HS) != USBD_OK)
    {
        return Result::ERR;
    }
    if(USBD_RegisterClass(&hUsbDeviceHS, &USBD_DFU) != USBD_OK)
    {
        return Result::ERR;
    }
    // the flash media operations callbacks are the same for HS/FS
    if(USBD_DFU_RegisterMedia(&hUsbDeviceHS, &USBD_DFU_fops_FS) != USBD_OK)
    {
        return Result::ERR;
    }
    if(USBD_Start(&hUsbDeviceHS) != USBD_OK)
    {
        return Result::ERR;
    }
    return Result::OK;
}

DFUHandle::Result DFUHandle::Impl::ProcessIoRequests()
{
        // io_state.busy = true;
        // io_state.type = IoStatus::Type::Erase;
        // io_state.start_time = System::GetNow();
        // io_state.start_address = Add;
        // io_state.length = sector_size_;

    // Only one request can be handled at a time so we have one state
    // instead of list of requests, etc.
    if (io_state.busy) {
        // take care of business
        switch(io_state.type) {
            case IoStatus::Type::Erase:
            {
                uint32_t normalized_addr = io_state.start_address - addr_offset_;
                qspi_->Erase(normalized_addr, normalized_addr + io_state.length);
                dfu_log.pushEvent(
                    DfuLogger::Event::Type::Erase,
                    io_state.start_time,
                    io_state.start_address,
                    io_state.length,
                    System::GetNow() - io_state.start_time
                );
                // Clear busy flag
                io_state.busy = false;
            }
            break;
            case IoStatus::Type::Write:
            {
                uint32_t normalized_addr = io_state.start_address - addr_offset_;
                qspi_->Write(normalized_addr, io_state.length, io_buffer);
                dfu_log.pushEvent(
                    DfuLogger::Event::Type::Write,
                    io_state.start_time,
                    io_state.start_address,
                    io_state.length,
                    System::GetNow() - io_state.start_time
                );
                data_written_ += io_state.length;
                // Clear busy flag
                io_state.busy = false;
            }
            break;
            case IoStatus::Type::Idle:
            io_state.busy = false;
            break;
        }
    }


    return Result::OK;
}

extern "C"
{
// The chip is split into 3 regions -- the first 256k is broken into 64 4K
// segments so smaller portions can be rewritten if necessary (say we need
// a lookup table or something). The rest of the chip is split in two because
// this memory layout syntax is very limited, and the number of segments
// can only be two digits (so 124*64Kg isn't possible).
#define FLASH_INT_STR "@Flash /0x90000000/64*4Kg/0x90040000/60*64Kg/0x90400000/60*64Kg"

    uint16_t MEM_If_Init_FS(void);
    uint16_t MEM_If_Erase_FS(uint32_t Add);
    uint16_t MEM_If_Write_FS(uint8_t *src, uint8_t *dest, uint32_t Len);
    uint8_t *MEM_If_Read_FS(uint8_t *src, uint8_t *dest, uint32_t Len);
    uint16_t MEM_If_DeInit_FS(void);
    uint16_t MEM_If_GetStatus_FS(uint32_t Add, uint8_t Cmd, uint8_t *buffer);

    __ALIGN_BEGIN USBD_DFU_MediaTypeDef USBD_DFU_fops_FS __ALIGN_END =
        {
            (uint8_t *)FLASH_INT_STR,
            MEM_If_Init_FS,
            MEM_If_DeInit_FS,
            MEM_If_Erase_FS,
            MEM_If_Write_FS,
            MEM_If_Read_FS,
            MEM_If_GetStatus_FS};

    /**
     * @brief  Memory initialization routine.
     * @retval USBD_OK if operation is successful, MAL_FAIL else.
     */
    uint16_t MEM_If_Init_FS(void)
    {
        return dfu_impl.MemoryInit();
    }

    /**
     * @brief  De-Initializes Memory
     * @retval USBD_OK if operation is successful, MAL_FAIL else
     */
    uint16_t MEM_If_DeInit_FS(void)
    {
        return dfu_impl.MemoryDeinit();
    }

    /**
     * @brief  Erase sector.
     * @param  Add: Address of sector to be erased.
     * @retval 0 if operation is successful, MAL_FAIL else.
     */
    uint16_t MEM_If_Erase_FS(uint32_t Add)
    {
        return dfu_impl.MemoryErase(Add);
    }

    /**
     * @brief  Memory write routine.
     * @param  src: Pointer to the source buffer. Address to be written to.
     * @param  dest: Address to write to.
     * @param  Len: Number of data to be written (in bytes).
     * @retval USBD_OK if operation is successful, MAL_FAIL else.
     */
    uint16_t MEM_If_Write_FS(uint8_t *src, uint8_t *dest, uint32_t Len)
    {
        return dfu_impl.MemoryWrite(src, dest, Len);
    }

    /**
     * @brief  Memory read routine.
     * @param  src: Address to read from.
     * @param  dest: Pointer to the destination buffer.
     * @param  Len: Number of data to be read (in bytes).
     * @retval Pointer to the physical address where data should be read.
     */
    uint8_t *MEM_If_Read_FS(uint8_t *src, uint8_t *dest, uint32_t Len)
    {
        /* Return a valid address to avoid HardFault */
        return (uint8_t *)dfu_impl.MemoryRead(src, dest, Len);
    }

    /**
     * @brief  Get status routine
     * @param  Add: Address to be read from
     * @param  Cmd: Number of data to be read (in bytes)
     * @param  buffer: used for returning the time necessary for a program or an erase operation
     * @retval USBD_OK if operation is successful
     */
    uint16_t MEM_If_GetStatus_FS(uint32_t Add, uint8_t Cmd, uint8_t *buffer)
    {
        return dfu_impl.MemoryStatus(Add, Cmd, buffer);
    }

    void enable_jump()
    {
        dfu_impl.dfu_complete = true;
    }
}

/////////////////////////////////////////////////
// DFUHandle::Impl -> DFUHandle
/////////////////////////////////////////////////

DFUHandle::Result DFUHandle::Init(QSPIHandle* qspi)
{
    pimpl_ = &dfu_impl;
    return pimpl_->Init(qspi);
}

DFUHandle::Result DFUHandle::DeInit()
{
    return pimpl_->DeInit();
}

bool DFUHandle::GetDfuComplete()
{
    return pimpl_->dfu_complete;
}

bool DFUHandle::GetDfuInitiated()
{
    return pimpl_->dfu_initiated;
}

bool DFUHandle::ProcessIoRequests()
{
    return (bool)pimpl_->ProcessIoRequests();
}

// IRQ Handler
extern "C"
{
    void OTG_FS_EP1_OUT_IRQHandler(void)
    {
        HAL_PCD_IRQHandler(&hpcd_USB_OTG_FS);
    }

    void OTG_FS_EP1_IN_IRQHandler(void)
    {
        HAL_PCD_IRQHandler(&hpcd_USB_OTG_FS);
    }

    void OTG_FS_IRQHandler(void)
    {
        HAL_PCD_IRQHandler(&hpcd_USB_OTG_FS);
    }
}
