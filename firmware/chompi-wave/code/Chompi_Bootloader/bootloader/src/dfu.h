#ifndef DSY_DFU
#define DSY_DFU

#include <cstdint>
#include "per/qspi.h"

namespace daisy
{
/** @addtogroup serial
@{
*/

/**
    @author Electrosmith
    @date 14 July, 2021

    @brief Presents a DFU device and endpoints to the USB host,
    and downloads an application if initiated within 5 seconds.
    Jumps to existing program after if present. Breathing LED indicates
    idle bootloader. An SOS pattern indicates the application is not
    correctly configured to run from the bootloader.
*/
class DFUHandle
{
  public:
    enum Result
    {
      OK = 0,
      ERR
    };

    /** Configuration structure for interfacing with DFU Driver */
    struct Config
    {
    };

    /** Initializes the USB drivers. */
    Result Init(QSPIHandle* qspi);

    /** Deinitializes DFU-related peripherals */
    Result DeInit();

    /** Returns true if a DFU transaction has completed */
    bool GetDfuComplete();

    /** Returns true if a DFU transaction has been initiated */
    bool GetDfuInitiated();

    DFUHandle() : pimpl_(nullptr) {}
    DFUHandle(const DFUHandle &other) = default;
    DFUHandle &operator=(const DFUHandle &other) = default;

    class Impl; /**< & */

  private:
    Impl *pimpl_;

    static constexpr uint32_t sine_fid_ = 10;
    static constexpr uint32_t sine_hz_ = 1.f;
    float angle_;
    uint32_t pwm_tick_;
};

/** @} */

} // namespace daisy

#endif // DSY_DFU
