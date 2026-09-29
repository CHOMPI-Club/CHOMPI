#pragma once
#include <cstdlib>
#include <cstdint>

// Derived from Electrosmith DSP source.
// interpolated delay line helps keep SDRAM reads sequential

namespace chompi
{

class InterpolatedDelayLine
{
  public:
    InterpolatedDelayLine() {}
    ~InterpolatedDelayLine() {}

    struct AudioSample
    {
        int16_t l, r;
    };

    void Init(AudioSample *mem, size_t size)
    {
        line_      = mem;
        max_size_  = size;
        cur_size_  = size;
        write_ptr_ = 0;
        Reset();
    }

    void SwapMem(AudioSample *mem) { line_ = mem; }


    /** Immediately writes entire delay line to 0 */
    void FullClear()
    {
        for(size_t i = 0; i < max_size_; i++)
        {
            line_[i].l = 0;
            line_[i].r = 0;
        }
    }

    void CurrentClear()
    {
        for(size_t i = 0; i < cur_size_; i++)
        {
            line_[i].l = 0;
            line_[i].r = 0;
        }
    }

    /** Initiates the start of a clearing operation that will run during the Read() operaiton
     *  This avoids the large time it may take to clear massive delay lines
     *  during an audio callback
    */
    inline void Clear() { FullClear(); }

    void Reset()
    {
        FullClear();
        write_ptr_ = 0;
        delay_     = 1;
        frac_      = 0.f;
    }

    /** sets the delay time in samples
        If a float is passed in, a fractional component will be calculated for interpolating the delay line.
    */
    inline void SetDelay(float delay)
    {
        int32_t int_delay = static_cast<int32_t>(delay);
        frac_             = delay - static_cast<float>(int_delay);
        delay_ = static_cast<size_t>(int_delay) < cur_size_ ? int_delay
                                                            : cur_size_ - 1;
    }

    inline void Write(const AudioSample sample)
    {
        line_[write_ptr_] = sample;
        // write_ptr_        = (write_ptr_ - 1 + cur_size_) % cur_size_;
        write_ptr_        = (write_ptr_ - 1 + max_size_) % max_size_;
    }

    inline void TickWritePointer()
    {
        write_ptr_ = (write_ptr_ - 1 + cur_size_) % cur_size_;
    }

    /** Read from a set location */
    inline const AudioSample Read(float delay)
    {
        int32_t delay_integral   = static_cast<int32_t>(delay);
        float   delay_fractional = delay - static_cast<float>(delay_integral);
        const AudioSample a = line_[(write_ptr_ + delay_integral) % cur_size_];
        const AudioSample b
            = line_[(write_ptr_ + delay_integral + 1) % cur_size_];
        AudioSample out;
        out.l = a.l + (b.l - a.l) * delay_fractional;
        out.r = a.r + (b.r - a.r) * delay_fractional;
        return out;
    }

    inline const AudioSample Read() const
    {
        AudioSample out;
        AudioSample a = line_[(write_ptr_ + delay_) % cur_size_];
        AudioSample b = line_[(write_ptr_ + delay_ + 1) % cur_size_];
        out.l         = a.l + (b.l - a.l) * frac_;
        out.r         = a.r + (b.r - a.r) * frac_;
        return out;
    }

    inline size_t GetWritePosition() const { return write_ptr_; }

    inline void ClearValueAtPosition(uint32_t pos)
    {
        line_[pos].l = 0;
        line_[pos].r = 0;
    }

    inline void SetValueAtPosition(uint32_t pos, AudioSample value)
    {
        line_[pos] = value;
    }

    inline size_t GetSize() const { return max_size_; }

    inline size_t GetCurrentSize() const { return cur_size_; }
    inline void   SetCurrentSize(size_t size)
    {
        cur_size_ = size <= max_size_ ? size : max_size_;
    }

  private:
    AudioSample *line_;
    size_t       max_size_;
    size_t       cur_size_;
    size_t       write_ptr_, delay_;
    float        frac_;
};

} // namespace chompi