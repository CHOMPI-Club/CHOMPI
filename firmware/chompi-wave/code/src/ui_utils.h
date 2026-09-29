/** @file ui_utils.h
 *  @brief The LED canvas's clear/flush functions required by libDaisy's UI framework
 *  Actual LED output in temp_led_stuff.h
 */
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