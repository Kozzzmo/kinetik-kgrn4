// Kinetik KGRN-4 — what the UI reaches directly in the plug-in (same process): the input buffer
#pragma once
#include "Sample.hpp"

namespace kn {

struct GrainDirect {
    virtual ~GrainDirect() {}
    // the last `bars` complete bars of the input as a stereo sample (nullptr = nothing came in)
    virtual SamplePtr captureInput(int bars) = 0;
    // min / max of the LIVE window, `cols` columns
    virtual void liveWave(float* mn, float* mx, int cols) = 0;
};

} // namespace kn
