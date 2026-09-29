// Copyright 2015 Emilie Gillet.
//
// Author: Emilie Gillet (emilie.o.gillet@gmail.com)
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
// 
// The above copyright notice and this permission notice shall be included in
// all copies or substantial portions of the Software.
// 
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
// THE SOFTWARE.
// 
// See http://creativecommons.org/licenses/MIT/ for more information.
//
// -----------------------------------------------------------------------------

#pragma once
#include <stdlib.h>
// #include "daisysp.h"

#define SLOPE(out, in, positive, negative)                \
    {                                                     \
        float error = (in)-out;                           \
        out += (error > 0 ? positive : negative) * error; \
    }

namespace chompi
{
/** Simple Peak Limiter

This was extracted from pichenettes/stmlib.

Credit to pichenettes/Mutable Instruments
*/
class Limiter
{
  public:
    Limiter() {}
    ~Limiter() {}
    
    /** Initializes the Limiter instance. 
    */
    void Init()
    {
        peak_ = 0.5f;
    }


    /** Processes a block of audio through the limiter.
        \param in - pointer to a block of audio samples to be processed. The buffer is operated on directly.
        \param size - size of the buffer "in"
        \param pre_gain - amount of pre_gain applied to the signal.
    */
    float Process(float in)
    {
        float peak = fabsf(in);
        SLOPE(peak_, peak, 0.05f, 0.0004f);
        float gain = (peak_ <= 1.f ? 1.f : 1.f / peak_);
        return daisysp::SoftLimit(in * gain); // Dropped * 0.7f to match TEMPO
    }

    /** Compression like setup */
    float ProcessComp(float in, float pregain, float thresh, float ratio, float makeup)
    {
        const float pre = in * pregain;
        const float peak = fabsf(pre);
        SLOPE(peak_, peak, 0.05f, 0.0002f);
        const float gain = (peak_ <= thresh ? 1.f : 1.f / (ratio * (1.f + (peak_ - thresh))) );
        SLOPE(gain_, gain, .001f, .005f);
        return daisysp::SoftLimit(pre * gain_ * makeup);
    }

    // process, with no gain loss. Used in compressor overdub loop to avoid slow gain reduction
    float ProcessHard(float in)
    {
        float peak = fabsf(in);
        SLOPE(peak_, peak, 0.05f, 0.0004f);
        float gain = (peak_ <= 1.f ? 1.f : 1.f / peak_);
        return daisysp::fclamp(in * gain, -1.f, 1.f);
    }


  private:
    float peak_;
    float gain_;
};
} // namespace chompi