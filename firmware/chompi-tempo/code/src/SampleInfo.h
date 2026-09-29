#pragma once
#include "daisy.h"
#include "daisysp.h"

enum SampleStatus {
    EMPTY,
    LOADING,
    LOADED,
    FAILED
};

struct SampleInfo {
    void* start;          // Pointer into SDRAM
    size_t numSamples;     // Length of the sample in floats
    std::string name;      // Optional: filename or identifier
    std::string fullPath;
    uint32_t sampleRate;   // Optional: useful for playback rate conversion
    SampleStatus status;
};