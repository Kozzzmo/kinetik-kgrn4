// Kinetik KGRN-4 — what the UI reaches directly in the plug-in (same process): the input buffer, the voices, the LFOs
#pragma once
#include "Sample.hpp"
#include "Params.hpp"

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
    // 0.4.1: every grain and voice of a track now (for the grain display); returns the grain count, nv = voices
    virtual int grainView(int track, GrainView* out, int max, VoiceView* vout, int vmax, int& nv) = 0;
};

} // namespace kn
