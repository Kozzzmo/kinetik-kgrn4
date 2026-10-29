// Kinetik KGRN-4 — what the UI reaches directly in the plug-in (same process): the input buffer, the voices, the LFOs
#pragma once
#include "Sample.hpp"

namespace kn {

struct GrainDirect {
    virtual ~GrainDirect() {}
    // the last `bars` complete bars of the input as a stereo sample (nullptr = nothing came in)
    virtual SamplePtr captureInput(int bars) = 0;
    // min / max of the LIVE window, `cols` columns
    virtual void liveWave(float* mn, float* mx, int cols) = 0;
    // 0.4: the play heads of a track's sounding voices (fractions of the material), how many
    virtual int voiceHeads(int track, float* out, int max) = 0;
    // 0.4: the current output of LFO i of a track (-1 .. 1, times its depth)
    virtual float lfoValue(int track, int i) = 0;
};

} // namespace kn
