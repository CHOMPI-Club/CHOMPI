#pragma once
#include "hardware.h"

/** Needed for the Flush/Clear LEDs functions */
extern chompi::Hardware hw;

void FlushLeds(const daisy::UiCanvasDescriptor& canvasDescriptor)
{
    // hw.UpdateLeds();
}
void ClearLeds(const daisy::UiCanvasDescriptor& canvasDescriptor)
{
    // hw.ClearLeds();
}