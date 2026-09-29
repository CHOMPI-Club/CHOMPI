#ifndef DSY_BOOTLOADER
#define DSY_BOOTLOADER

#include <cstdint>
#include "daisy_seed.h"
#include "dfu.h"
#include "fatfs.h"

namespace daisy
{
/** @addtogroup serial
@{
*/

#define SRAM_SPACE 0x80000U
#define ITCMRAM_SPACE 0x10000U
#define PROGRAM_SPACE SRAM_SPACE + ITCMRAM_SPACE
#define EXEC_START 0x24000000U

// TODO --  also make this part of qspi
#define QSPI_INITIAL 0x90000000U

#define DSY_SRAM_EXEC __attribute__((section(".sram_exec")))
#define DSY_ITCMRAM_EXEC __attribute__((section(".itcmram_exec")))

/**
   @author Electrosmith
    @date 14 July, 2021

    @brief Presents a DFU device and endpoints to the USB host,
    and downloads an application if initiated within 5 seconds.
    Jumps to existing program after if present. Breathing LED indicates
    idle bootloader. An SOS pattern indicates the application is not
    correctly configured to run from the bootloader.
*/
class Bootloader
{
  public:
    enum Result
    {
      OK = 0,
      ERR
    };

    enum State
    {
      CHECK_SD = 0,
      CHECK_USB,
      CHECK_DFU,
      CHECK_TIMEOUT,
    };

    Bootloader() {}
    ~Bootloader() {}

    /** Initializes the Bootloader manager
     *
     *  \param seed Pointer to initialized seed hardware class
     */
    Result Init(DaisySeed &seed, uint32_t timeout);

    /** A separate initialization for IO that takes some time.
     *  Should be called after Init and before any LoopProcess calls.
     *  This permits an immediate interface  startup so the bootloader
     *  _feels_ fast.
     */
    Result IoInit();

    /** Handles audio and interface related processes. Currently
     *  only updates the LED and button states, but may include
     *  audio-based firmware updating in the future.
     */
    void CallbackProcess();

    /** Handles the main bootloader state machine, and jumps to
     *  the target application if the conditions are right.
     */
    void LoopProcess();

    /** Triggers an error sequence on the user LED
     *  \param error_code the number of times the LED will blink in sequence
     */
    void TriggerError(uint8_t error_code);

    Bootloader(const Bootloader &other) = default;
    Bootloader &operator=(const Bootloader &other) = default;

    /** LED Code */
    void LedsOff();
    void RandomColors();
    void BootAnimation();

  private:
    Result DeInit();

    Result InitDFU();
    Result DeinitDFU();

    // are we in loadprogramandjump? Determines if we should turn leds off
    bool load_prog = false;
    void LoadProgramAndJump(uint8_t error_code);
    void ManageLed();

    uint32_t FillTargetMemory();
    void ErrorLed();
    void HappyLed();

    void ResetTimeout() { timeout_start_ = System::GetNow(); }

    enum FatfsResult
    {
      ABSENT,
      PRESENT,
      ALREADY_LOADED,
      QUERYING,
      ERROR,
    };

    // FatFS operations
    void UpdateLog(bool success, const char *attempted_file, uint32_t address, const char *message);
    FatfsResult EnsureValidBinary(size_t file_size, System::MemoryRegion *mem);
    FatfsResult LoadFAT(FILINFO *info);
    FatfsResult SearchBin();
    FatfsResult TryLoadingSD();
    FatfsResult TryLoadingUSB();
    void DeinitFatfs();

    enum FatfsMount
    {
      NONE = 0,
      SD,
      USB,
    };

    const char *FatFS_Path_;
    FATFS *FatFS_Obj_;

    SdmmcHandler sd_;
    FatFSInterface fsi_;
    USBHostHandle msd_;

    bool sd_skip_;
    bool usb_skip_;
    FIL FatfsFile_;

    static constexpr const char *BOOTLOADER_LOG_NAME = "daisy_boot_log.txt";
    static constexpr uint32_t PAGE_SIZE = 65536;
    static constexpr uint32_t FILE_BUFF_LEN = 65536;

    static constexpr uint32_t HAPPY_BLINKS = 10;
    static constexpr uint32_t HAPPY_PERIOD_MS = 50;
    static constexpr uint32_t ERROR_PERIOD_MS = 200;

    DaisySeed *hw_;

    DFUHandle dfu;

    State state_;

    Switch boot_button_;
    bool boot_button_pressed_;
    bool dfu_initialized_;

    bool downloading_binary_;
    bool do_error_;
    bool error_led_;
    uint32_t error_tick_;
    uint32_t error_step_;
    uint8_t error_code_;

    bool do_happy_;
    bool happy_led_;
    uint32_t happy_tick_;
    uint32_t happy_step_;

    static constexpr uint32_t sine_fid_ = 10;
    static constexpr uint32_t sine_hz_ = 1.f;
    float angle_;
    uint32_t pwm_tick_;

    uint32_t timeout_ = 2000; // 2 seconds
    uint32_t timeout_start_;

    /** LED State*/
    float r = 0.f;
    float g = 0.f;
    float b = 0.f;
    float bright = 0.f;
    float bright_inc = .0002f;
};

/** @} */

/** This function handles the bootloader startup before any other initialization
 * \returns Timeout length
*/
uint32_t startup_process();

} // namespace daisy

#endif // DSY_BOOTLOADER
