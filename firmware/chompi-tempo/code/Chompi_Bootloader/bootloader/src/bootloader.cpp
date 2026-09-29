#include <cstdint>
#include "stm32h750xx.h"

#include "bootloader.h"
#include "daisy_seed.h"

#include "usbd_def.h"
#include "usbd_desc.h"
#include "usbd_dfu.h"
#include "usbd_dfu_if.h"
#include "system.h"
#include "temp_led_stuff.h"

using namespace daisy;
using namespace chompi;

uint32_t daisy::startup_process()
{
  // Enable backup SRAM
  System::InitBackupSram();

  // Write the bootloader version
  boot_info.version = System::BootInfo::Version::vC6_2;

	if (boot_info.status == System::BootInfo::Type::JUMP)
	{
		boot_info.status = System::BootInfo::Type::INVALID;
		// Do jump
		typedef void _Noreturn (*EntryPoint)(void);

    auto memory = System::GetMemoryRegion(boot_info.data);

    if (memory == System::MemoryRegion::QSPI)
    {
      QSPIHandle::Config qspi_config;

      qspi_config.device = QSPIHandle::Config::Device::IS25LP064A;
      qspi_config.mode   = QSPIHandle::Config::Mode::MEMORY_MAPPED;

      qspi_config.pin_config.io0 = Pin(PORTF, 8);
      qspi_config.pin_config.io1 = Pin(PORTF, 9);
      qspi_config.pin_config.io2 = Pin(PORTF, 7);
      qspi_config.pin_config.io3 = Pin(PORTF, 6);
      qspi_config.pin_config.clk = Pin(PORTF, 10);
      qspi_config.pin_config.ncs = Pin(PORTG, 6);

      QSPIHandle qspi;

      qspi.Init(qspi_config);
    }

		volatile uint32_t application_address = *(volatile uint32_t*) (boot_info.data + 4);
		EntryPoint application = (EntryPoint)(application_address);
		SCB->VTOR = boot_info.data;
		__set_MSP(*((volatile uint32_t*) boot_info.data));
		application();
	}

	if (boot_info.status == System::BootInfo::Type::SKIP_TIMEOUT)
	{
		boot_info.status = System::BootInfo::Type::INVALID;
		// do the regular startup, but skip the timeout
    return 100;
	}

  return 2000;
}

uint8_t DSY_QSPI_BSS qspi_buffer[PROGRAM_SPACE];
// uint8_t DSY_SRAM_EXEC sram_program[SRAM_SPACE / 2];
uint8_t DSY_SRAM_EXEC sram_program[SRAM_SPACE - 32768];
uint8_t DSY_ITCMRAM_EXEC itcmram_program[ITCMRAM_SPACE];

static constexpr uint32_t INVALID_ADDRESS = 0xFFFFFFFF;

Bootloader::Result Bootloader::Init(DaisySeed &seed, uint32_t timeout)
{
  timeout_start_ = System::GetNow();
  timeout_ = timeout;
  pwm_tick_ = timeout_start_;
  angle_ = -M_PI / 4; // LED will start at 0 like this

  hw_ = &seed;

  // Zero fill the first few addresses to avoid reinit bugs
  for (size_t i = 0; i < 32; i++)
  {
    sram_program[i] = 0;
  }

  do_error_ = false;
  error_led_ = false;
  error_step_ = 0;
  error_tick_ = 0;

  do_happy_ = false;
  happy_led_ = false;
  happy_step_ = 0;
  happy_tick_ = 0;

  dsy_gpio_pin button{dsy_gpio_port::DSY_GPIOG, 3};
  boot_button_.Init(button, 1000, Switch::TYPE_MOMENTARY, Switch::POLARITY_NORMAL, Switch::PULL_NONE);

  boot_button_pressed_ = false;
  downloading_binary_ = false;

  state_ = State::CHECK_SD;

  LedSetup();

  return Result::OK;
}

Bootloader::Result Bootloader::IoInit()
{
  InitDFU();

  // SD interface init
  SdmmcHandler::Config sd_cfg;
  sd_cfg.Defaults();
  sd_cfg.speed = SdmmcHandler::Speed::MEDIUM_SLOW;
  sd_cfg.width = SdmmcHandler::BusWidth::BITS_1;
  sd_.Init(sd_cfg);
  sd_skip_ = false;

  // USB interface init
  USBHostHandle::Config config;
  msd_.Init(config);
  usb_skip_ = false;

  fsi_.Init(FatFSInterface::Config::MEDIA_USB | FatFSInterface::Config::MEDIA_SD);

  return Result::OK;
}

Bootloader::Result Bootloader::InitDFU()
{
  if (!dfu_initialized_)
  {
    dfu_initialized_ = true;
    return (Result) dfu.Init(&hw_->qspi);
  }

  return Result::OK;
}

Bootloader::Result Bootloader::DeinitDFU()
{
  if (dfu_initialized_)
  {
    dfu_initialized_ = false;
    return (Result) dfu.DeInit();
  }

  return Result::OK;
}

Bootloader::Result Bootloader::DeInit()
{
  DeinitDFU();
  HAL_PWREx_DisableUSBVoltageDetector();

  hw_->StopAudio();
  hw_->DeInit();
  return Result::OK;
}

// NOTE -- this is very destructive to any code that might attempt to run after
// The locals here will all be in dtcmram (on the stack), so this should be fine
uint32_t Bootloader::FillTargetMemory()
{
  uint32_t entry_address = *(uint32_t *)(qspi_buffer + 4);
  auto mem = System::GetMemoryRegion(entry_address);

  switch (mem)
  {
    case System::SRAM_D1:
    {
      // sram_program = (uint8_t*) System::kSramStart;
      for (size_t i = 0; i < sizeof(sram_program); i++)
      {
        sram_program[i] = qspi_buffer[i];
      }
      hw_->qspi.DeInit();
      return (uint32_t)sram_program;
    }
    case System::QSPI:
    {
      // WARNING -- this will need to change with multi-programs
      // (should be the beginning of the program, not the memory)
      return QSPI_BASE + System::kQspiBootloaderOffset;
    }
    default:
    {
      // If we got here, then the stack address is valid, but the
      // entry point is not, meaning the user should know their
      // build isn't going to work
      TriggerError(1);
      return INVALID_ADDRESS;
    }
  }

  return INVALID_ADDRESS;
}

void Bootloader::LoadProgramAndJump(uint8_t error_code)
{
  // Deinitialize the fatfs related peripherals
  DeinitFatfs();

  // If the stack pointer isn't here, then either the
  // download failed or was invalid, or a program
  // has never been written to flash
  uint32_t *stack_ptr = (uint32_t *)qspi_buffer;
  // The stack pointer itself won't be valid without any stack frames, so we subtract one
  auto mem = System::GetMemoryRegion((*stack_ptr) - 1);
  if (mem == System::QSPI || mem == System::INTERNAL_FLASH || mem == System::INVALID_ADDRESS)
  {
    TriggerError(error_code);
    load_prog = false;
    return;
  }

  uint32_t program_start = FillTargetMemory();

  if (program_start == INVALID_ADDRESS)
  {
    load_prog = false;
    TriggerError(error_code);
    return;
  }

  load_prog = true;

  DeInit();

  // // disable interupts NOTE -- These two seem to cause
  // // errors for the target application
  // // __set_PRIMASK(1);
  // // __disable_irq();
  // RCC->CIER = 0x00000000;

  // hw_->SetLed(false);

  // typedef void _Noreturn (*EntryPoint)(void);

  // volatile uint32_t application_address = *(__IO uint32_t *)(program_start + 4);
  // EntryPoint application = (EntryPoint)(application_address);
  // SCB->VTOR = program_start;
  // __set_MSP(*(__IO uint32_t *)program_start);
  // application();

  boot_info.status = System::BootInfo::Type::JUMP;
  boot_info.data = program_start;

  HAL_NVIC_SystemReset();
  while (1);
}

void Bootloader::ManageLed()
{
  if (do_happy_)
  {
    HappyLed();
  }
  else if (do_error_)
  {
    ErrorLed();
  }
  else if (downloading_binary_ || (dfu_initialized_ && dfu.GetDfuInitiated()))
  {
    hw_->SetLed(true);
  }
  else
  {
    uint32_t time = System::GetNow();
    if (time > pwm_tick_)
    {
      pwm_tick_ = time;
      angle_ += (2 * M_PI / 1000.f) / sine_hz_;
      bool led = sin(angle_) * sine_fid_ + sine_fid_ - 1 > time % (sine_fid_ * 2);
      hw_->SetLed(led);
    }
  }
}

void Bootloader::TriggerError(uint8_t error_code)
{
  do_error_ = true;
  error_led_ = false;
  error_step_ = 0;
  error_tick_ = System::GetNow();
  hw_->SetLed(error_led_);
  error_code_ = error_code;
}

void Bootloader::ErrorLed()
{
  uint32_t now = System::GetNow();
  if (now - error_tick_ >= ERROR_PERIOD_MS)
  {
    error_step_++;
    error_tick_ = now;
    error_led_ = !error_led_;
    hw_->SetLed(error_led_);
    if (error_step_ >= (uint32_t)(error_code_ * 2) + 1)
    {
      do_error_ = false;
      ResetTimeout();
    }
  }
}

void Bootloader::HappyLed()
{
  uint32_t now = System::GetNow();
  if ((now - happy_tick_) >= HAPPY_PERIOD_MS)
  {
    happy_step_++;
    happy_tick_ = now;
    happy_led_ = !happy_led_;
    hw_->SetLed(happy_led_);
    if (happy_step_ > HAPPY_BLINKS)
    {
      do_happy_ = false;
    }
  }
}

void Bootloader::CallbackProcess()
{
  ManageLed();
  
  if(!load_prog)
    BootAnimation();
  else
    LedsOff();

  boot_button_.Debounce();

  if (boot_button_.RisingEdge())
  {
    boot_button_pressed_ = true;
    do_happy_ = true;
  }
}

void Bootloader::LoopProcess()
{
  switch (state_)
  {
    case State::CHECK_SD:
    {
      const State next_state = State::CHECK_USB;
      if (sd_skip_)
      {
        state_ = next_state;
        break;
      }

      FatFS_Path_ = fsi_.GetSDPath();
      FatFS_Obj_ = &fsi_.GetSDFileSystem();

      FatfsResult res = SearchBin();

      if (res == FatfsResult::ERROR)
      {
        // Either there was a .bin file with an invalid executable or
        // a drive was present that failed to mount / open
        TriggerError(3);
        state_ = next_state;
        sd_skip_ = true;
      }
      else if (res == FatfsResult::PRESENT)
      {
        LoadProgramAndJump(4);
        // If we got here without restarting, the loading encountered an error
        state_ = next_state;
      }
      else if (res == FatfsResult::ALREADY_LOADED)
      {
        state_ = next_state;
        sd_skip_ = true;
      }
      else if (res == FatfsResult::ABSENT)
      {
        state_ = next_state;
      }
      break;
    }
    case State::CHECK_USB:
    {
      const State next_state = State::CHECK_DFU;
      if (usb_skip_)
      {
        state_ = next_state;
        break;
      }

      FatFS_Path_ = fsi_.GetUSBPath();
      FatFS_Obj_ = &fsi_.GetUSBFileSystem();

      msd_.Process();

      if (!msd_.GetReady())
      {
        state_ = next_state;
        break;
      }

      FatfsResult res = SearchBin();

      if (res == FatfsResult::ERROR)
      {
        TriggerError(5);
        state_ = next_state;
        usb_skip_ = true;
      }
      else if (res == FatfsResult::PRESENT)
      {
        LoadProgramAndJump(6);

        state_ = next_state;
      }
      else if (res == FatfsResult::ALREADY_LOADED)
      {
        state_ = next_state;
        usb_skip_ = true;
      }
      else if (res == FatfsResult::ABSENT)
      {
        state_ = next_state;
      }
      break;
    }
    case State::CHECK_DFU:
    {
      bool dfu_done = dfu.GetDfuComplete();

      if (dfu_done)
      {
        LoadProgramAndJump(7);

        DeinitDFU();
        InitDFU();
        break;
      }
      state_ = State::CHECK_TIMEOUT;
      break;
    }
    case State::CHECK_TIMEOUT:
    {
      bool timeout_elapsed = (System::GetNow() - timeout_start_ > timeout_);
      bool dfu_ongoing = dfu.GetDfuInitiated();

      if (timeout_elapsed && !do_error_ && !boot_button_pressed_ && !dfu_ongoing)
      {
        LoadProgramAndJump(8);
      }

      state_ = State::CHECK_SD;
      break;
    }
  }
}

///////////////////////////////////////////////////
// FatFS
///////////////////////////////////////////////////

void Bootloader::UpdateLog(bool success, const char *attempted_file, uint32_t address, const char *message)
{
  address += QSPI_INITIAL;
  // create / update the log
  size_t log_number = 1;
  if (f_open(&FatfsFile_, BOOTLOADER_LOG_NAME, FA_READ | FA_OPEN_EXISTING) == FR_OK)
  {
    // The file exists, so we need to count how many entries it has.
    // Newlines will be our proxy for this.
    UINT data_read;
    do
    {
      f_read(&FatfsFile_, sram_program, FILE_BUFF_LEN, &data_read);
      for (size_t i = 0; i < data_read; i++)
        if (sram_program[i] == '\n')
          log_number++;
    } while (data_read == FILE_BUFF_LEN);

    f_close(&FatfsFile_);
  }

  if (f_open(&FatfsFile_, BOOTLOADER_LOG_NAME, FA_WRITE | FA_OPEN_APPEND) == FR_OK)
  {
    UINT data_to_write;
    UINT data_written;

    if (success)
    {
      data_to_write = sprintf((char*) sram_program,
                              "%d. Successfully flashed file \"%s\" to address 0x%08lX\n", log_number, attempted_file, address);
    }
    else
    {
      data_to_write = sprintf((char*) sram_program,
                              "%d. Encountered error when attempting to flash \"%s\" to address 0x%08lX: %s\n", log_number, attempted_file, address, message);
    }

    f_write(&FatfsFile_, sram_program, data_to_write, &data_written);
    f_close(&FatfsFile_);
  }
}

Bootloader::FatfsResult Bootloader::EnsureValidBinary(size_t file_size, System::MemoryRegion *mem)
{
  // right now, we're just expecting the raw binary
  uint32_t stack_ptr;
  uint32_t entry_point;
  UINT read;
  f_read(&FatfsFile_, &stack_ptr, sizeof(uint32_t), &read);
  f_read(&FatfsFile_, &entry_point, sizeof(uint32_t), &read);

  // The -1 places the stack_ptr within the actual memory space
  auto stack_location = System::GetMemoryRegion(stack_ptr - 1);
  if (stack_location == System::QSPI || stack_location == System::INTERNAL_FLASH || stack_location == System::INVALID_ADDRESS)
  {
    return FatfsResult::ERROR; // no need to rewind if the file isn't valid
  }

  // verifying that a valid memory space is used
  *mem = System::GetMemoryRegion(entry_point);

  FatfsResult valid = FatfsResult::PRESENT;
  if (*mem == System::INVALID_ADDRESS || *mem == System::INTERNAL_FLASH)
    valid = FatfsResult::ERROR;

  // Verifying that the sizes match up
  switch (*mem)
  {
  case System::SRAM_D1:
    // if (file_size > System::kSramEnd - System::kSramStart)
    //   valid = false;
    break;
  case System::QSPI:
    // if (file_size > System::kQspiEnd - System::kQspiStart)
    //   valid = false;
    break;
  default:
    valid = FatfsResult::ERROR;
    break;
  }

  f_rewind(&FatfsFile_);

  // Performing a binary compare
  bool identical = true;
  UINT data_read;
  uint32_t data_written = 0;
  uint8_t *qspi_ptr = (uint8_t *)(System::kQspiBootloaderOffset + QSPI_INITIAL);
  do
  {
    f_read(&FatfsFile_, sram_program, FILE_BUFF_LEN, &data_read);

    for (size_t i = 0; i < data_read; i++)
    {
      if (sram_program[i] != qspi_ptr[data_written + i])
      {
        identical = false;
        break;
      }
    }

    if (!identical)
      break;

    data_written += data_read;
  } while (data_read == FILE_BUFF_LEN);

  f_rewind(&FatfsFile_);

  if (identical)
    valid = FatfsResult::ALREADY_LOADED;

  return valid;
}

Bootloader::FatfsResult Bootloader::LoadFAT(FILINFO *info)
{
  size_t file_size = info->fsize;

  if (f_open(&FatfsFile_, info->fname, FA_OPEN_EXISTING | FA_READ) != FR_OK)
  {
    f_close(&FatfsFile_);
    UpdateLog(false, info->fname, System::kQspiBootloaderOffset, "unable to open file");
    return FatfsResult::ERROR;
  }

  System::MemoryRegion mem;

  FatfsResult binary_check = EnsureValidBinary(file_size, &mem);

  if (binary_check == FatfsResult::ALREADY_LOADED)
  {
    // No need to log this case (?)
    return ALREADY_LOADED;
  }

  if (binary_check == FatfsResult::PRESENT)
  {
    // Check if DFU is occurring
    if (dfu.GetDfuInitiated())
    {
      // Simply abort
      return ABSENT;
    }
    // Indicate that binary is being flashed
    downloading_binary_ = true;
    // Ensure DFU can't interrupt
    DeinitDFU();

    // Write file data to QSPI
    UINT data_read;
    uint32_t data_written = 0;

    do
    {
      if (data_written % PAGE_SIZE == 0)
      {
        hw_->qspi.Erase(System::kQspiBootloaderOffset + data_written, System::kQspiBootloaderOffset + data_written + PAGE_SIZE);
      }
      f_read(&FatfsFile_, sram_program, FILE_BUFF_LEN, &data_read);

      // // Ensuring the memory isn't overrun
      // switch (mem)
      // {
      //   case System::AXI_SRAM:
      //     if (data_written + data_read >= System::kSramEnd - System::kSramStart)
      //       return Result::ERR;
      //     break;
      //   case System::QSPI:
      //     if (data_written + data_read >= System::kQspiEnd - System::kQspiStart)
      //       return Result::ERR;
      //     break;
      //   default:
      //     return Result::ERR;
      // }

      hw_->qspi.Write(System::kQspiBootloaderOffset + data_written, data_read, sram_program);
      data_written += data_read;
    } while (data_read == FILE_BUFF_LEN);

    f_close(&FatfsFile_);
    UpdateLog(true, info->fname, System::kQspiBootloaderOffset, "");

    return FatfsResult::PRESENT;
  }
  else
  {
    f_close(&FatfsFile_);
    UpdateLog(false, info->fname, System::kQspiBootloaderOffset, "file does not contain executable code");
    return FatfsResult::ERROR;
  }

  return FatfsResult::PRESENT;
}

Bootloader::FatfsResult Bootloader::SearchBin()
{
  // Mount Media
  if (f_mount(FatFS_Obj_, FatFS_Path_, 1) == FR_OK)
  {
    DIR dir;
    FILINFO info;
    FRESULT result = FR_OK;

    if (f_opendir(&dir, FatFS_Path_) != FR_OK)
      return FatfsResult::ERROR;

    if (f_chdrive(FatFS_Path_) != FR_OK)
      return FatfsResult::ERROR;

    do
    {
      result = f_readdir(&dir, &info);
      // Exit if bad read or NULL fname
      if (result != FR_OK || info.fname[0] == 0)
        break;
      // Skip if its a directory or a hidden file.
      if (info.fattrib & (AM_HID | AM_DIR))
        continue;

      if (strstr(info.fname, ".bin") || strstr(info.fname, ".BIN"))
      {
        auto status = LoadFAT(&info);
        f_closedir(&dir);
        return status;
      }
    } while (result == FR_OK);

    f_closedir(&dir);
  }

  return FatfsResult::ABSENT;
}

void Bootloader::DeinitFatfs()
{
  // TODO -- add sd deinit
  msd_.Deinit();
}

/** LED stuff */
void Bootloader::LedsOff()
{
    for(size_t i = 0; i < kNumPthLeds; i++)
    {
        SetPthLed(i, 0, 0, 0);
    }

    for(size_t i = 0; i < kNumSmtLeds; i++)
    {
        SetSmtLed(i, 0, 0, 0);
    }

    // ========   send the data   =========
    fill_led_data();
}


void Bootloader::RandomColors()
{
    r = daisy::System::GetNow() % 66;
    g = daisy::System::GetNow() % 53;
    b = daisy::System::GetNow() % 36;

    r = r / 66.f;
    g = g / 53.f;
    b = b / 36.f;
}

void Bootloader::BootAnimation()
{
    bright += bright_inc;
    if(bright > 1.f)
    {
        bright_inc *= -1.f;
    }
    else if(bright < 0.f)
    {
        RandomColors();
        bright_inc *= -1.f;
    }                

    for(size_t i = 0; i < kNumPthLeds; i++)
    {
        SetPthLedFloat(i, r * bright, g * bright, b * bright);
    }

    for(size_t i = 0; i < kNumSmtLeds; i++)
    {
        SetSmtLedFloat(i, r * bright, g * bright, b * bright);
    }

    // ========   send the data   =========
    fill_led_data();
}

