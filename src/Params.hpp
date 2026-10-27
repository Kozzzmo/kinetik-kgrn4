// Kinetik KGRN-4 — constants, parameters and value texts (four granular sample tracks)
#pragma once
#include <cstdint>
#include <cstdio>
#include <cmath>
#include <string>
#include "Dsp.hpp"

namespace kn {

static constexpr int kTracks = 4;
static constexpr int kSteps = 64;          // steps per track
static constexpr int kPatterns = 16;
static constexpr int kTrackParams = 16;    // every one can be locked per step
static constexpr int kMaxSlices = 32;
static constexpr uint8_t kNoLock = 255;

// ---- per-track knobs, in four groups of four (the UI shows them in this order)
enum TrackParam {
    tpPOSITION = 0, tpSLICE, tpSCAN, tpPITCH,          // MATERIAL
    tpSIZE, tpDENSITY, tpSPRAY, tpSPREAD,              // GRAIN
    tpATTACK, tpDECAY, tpFILTER, tpRESO,               // SHAPE
    tpVOLUME, tpPAN, tpDELAY, tpREVERB                 // MIX
};
static const char* const kGroupNames[4] = {"MATERIAL", "GRAIN", "SHAPE", "MIX"};
struct TrackParamDef { const char* name; uint8_t def; const char* help; };
static const TrackParamDef kTP[kTrackParams] = {
    {"POSITION", 0,   "Where the play head starts, inside the slice (or the whole sample)"},
    {"SLICE",    0,   "ALL = the whole sample, or one of the slices found when the sample was loaded"},
    {"SCAN",     96,  "How fast the play head moves through the material: 100% = original speed, 0 = frozen, negative = backwards"},
    {"PITCH",    64,  "Pitch of the grains in semitones (the scan speed does not change)"},
    {"SIZE",     70,  "Length of each grain"},
    {"DENSITY",  64,  "Grains per second"},
    {"SPRAY",    0,   "Random offset of each grain around the play head"},
    {"SPREAD",   20,  "Random stereo position and detune of each grain"},
    {"ATTACK",   0,   "Fade-in of each note"},
    {"DECAY",    127, "Fade-out of each note (HOLD = until the next note on the track)"},
    {"FILTER",   64,  "Left = low pass, right = high pass, centre = off"},
    {"RESO",     20,  "Filter resonance"},
    {"VOLUME",   100, "Track level"},
    {"PAN",      64,  "Track position in the stereo field"},
    {"DELAY",    0,   "Send to the echo"},
    {"REVERB",   0,   "Send to the reverb"},
};

// ---- host parameters: tracks first (track t = indices t*16 .. t*16+15), then the globals.
// Saved host parameters never move: new ones go just before gOUT_STEP.
static constexpr int kGlobalBase = kTracks * kTrackParams;
enum Global {
    gREV_SIZE = 0, gREV_TONE, gDLY_TIME, gDLY_FB, gOUTPUT, gTEMPO, gPLAY, gPATTERN,
    gOUT_STEP, gOUT_ACTIVE, gOUT_PATTERN, gOUT_POS1, gOUT_POS2, gOUT_POS3, gOUT_POS4, kGlobalParams
};
static constexpr int kTotalParams = kGlobalBase + kGlobalParams;
static constexpr int kSavedParams = kGlobalBase + gOUT_STEP;
inline int tpIndex(int t, int k) { return t * kTrackParams + k; }
inline float trackParamDefault(int k) { return kTP[k].def; }
inline float globalDefault(int g)
{
    switch (g) {
    case gREV_SIZE: return 80.f; case gREV_TONE: return 70.f; case gDLY_TIME: return 88.f; case gDLY_FB: return 60.f;
    case gOUTPUT: return 96.f; case gTEMPO: return 120.f; case gOUT_STEP: return -1.f;
    default: return 0.f;
    }
}

// track speed (steps per 16th): index stored in the pattern
static constexpr int kSpeeds = 8;
static const double kSpeedMult[kSpeeds] = {1.0, 2.0, 4.0, 0.5, 0.25, 0.75, 1.5, 3.0};
static const char* const kSpeedNames[kSpeeds] = {"1x", "2x", "4x", "1/2x", "1/4x", "3/4x", "3/2x", "3x"};

static const double kDelTime[8] = {0.125, 1.0 / 6.0, 0.25, 1.0 / 3.0, 0.375, 0.5, 0.75, 1.0};
static const char* const kDelTimeNames[8] = {"1/32", "1/16T", "1/16", "1/8T", "1/16D", "1/8", "1/8D", "1/4"};
inline int bin8(float v) { int b = (int)(v / 16.f); return b < 0 ? 0 : (b > 7 ? 7 : b); }
inline int clampi(int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); }

// ---- mappings (shared by the engine and the UI)
inline float scanSpeed(float v) { const float s = (v - 64.f) / 32.f; return std::fabs(s) < 0.02f ? 0.f : s; }   // -2 .. +2
inline int pitchSemis(float v) { return (int)std::lround((v - 64.f) / 64.f * 24.f); }
inline float grainSec(float v) { return 0.005f * std::pow(200.f, v / 127.f); }            // 5 ms .. 1 s
inline float grainRate(float v) { return 2.f * std::pow(100.f, v / 127.f); }              // 2 .. 200 per second
inline float spraySec(float v) { const float x = v / 127.f; return x * x * 1.0f; }        // 0 .. 1 s
inline float attackSec(float v) { return v < 0.5f ? 0.001f : 0.001f * std::pow(3000.f, v / 127.f); }   // 1 ms .. 3 s
inline bool decayHold(float v) { return v >= 126.5f; }
inline float decaySec(float v) { return 0.02f * std::pow(500.f, v / 127.f); }             // 20 ms .. 10 s
inline float volumeGain(float v) { return 2.2f * (v / 127.f) * (v / 127.f); }
// OUTPUT knob -> gain (96 = 0 dB, 0 = -inf, 127 = +8 dB)
inline float trimDb(float v) { return v <= 0.5f ? -120.f : (v - 96.f) * (v < 96.f ? 0.5f : 0.26f); }
inline float trimGain(float v) { return v <= 0.5f ? 0.f : std::pow(10.f, trimDb(v) / 20.f); }
// SLICE knob: 0 = whole sample, otherwise one of n slices
inline int sliceOf(float v, int n) { if (n <= 0 || v < 0.5f) return -1; return clampi((int)((v - 0.5f) / 126.5f * n), 0, n - 1); }
inline float sliceKnob(int s, int n) { return s < 0 || n <= 0 ? 0.f : 0.5f + (s + 0.5f) * 126.5f / n; }   // centre of slice s

inline std::string trackValueText(int k, float v, int slices = 0)
{
    char b[32];
    switch (k) {
    case tpPOSITION: std::snprintf(b, sizeof(b), "%d%%", (int)std::lround(v / 127.f * 100.f)); return b;
    case tpSLICE: { const int s = sliceOf(v, slices); if (s < 0) return "ALL"; std::snprintf(b, sizeof(b), "%d / %d", s + 1, slices); return b; }
    case tpSCAN: { const float s = scanSpeed(v); if (s == 0.f) return "FREEZE"; std::snprintf(b, sizeof(b), "%+d%%", (int)std::lround(s * 100.f)); return b; }
    case tpPITCH: { const int st = pitchSemis(v); std::snprintf(b, sizeof(b), st ? "%+d ST" : "0 ST", st); return b; }
    case tpSIZE: { const float s = grainSec(v); if (s >= 1.f) return "1.0 S"; std::snprintf(b, sizeof(b), "%d MS", (int)std::lround(s * 1000.f)); return b; }
    case tpDENSITY: std::snprintf(b, sizeof(b), "%d /S", (int)std::lround(grainRate(v))); return b;
    case tpSPRAY: if (v < 0.5f) return "OFF"; std::snprintf(b, sizeof(b), "%d MS", (int)std::lround(spraySec(v) * 1000.f)); return b;
    case tpATTACK: { const float s = attackSec(v); if (s >= 1.f) std::snprintf(b, sizeof(b), "%.1f S", s); else std::snprintf(b, sizeof(b), "%d MS", (int)std::lround(s * 1000.f)); return b; }
    case tpDECAY: { if (decayHold(v)) return "HOLD"; const float s = decaySec(v); if (s >= 1.f) std::snprintf(b, sizeof(b), "%.1f S", s); else std::snprintf(b, sizeof(b), "%d MS", (int)std::lround(s * 1000.f)); return b; }
    case tpFILTER: {
        if (std::fabs(v - 64.f) < 1.f) return "OFF";
        const float hz = v < 64.f ? 20000.f * std::pow(80.f / 20000.f, (64.f - v) / 64.f) : 20.f * std::pow(400.f, (v - 64.f) / 63.f);
        if (hz >= 1000.f) std::snprintf(b, sizeof(b), "%s %.1fK", v < 64.f ? "LP" : "HP", hz / 1000.f);
        else std::snprintf(b, sizeof(b), "%s %d", v < 64.f ? "LP" : "HP", (int)hz);
        return b;
    }
    case tpVOLUME: { const float g = volumeGain(v); if (g < 0.001f) return "-INF"; std::snprintf(b, sizeof(b), "%+.1fdB", 20.f * std::log10(g)); return b; }
    case tpPAN: { const int p = (int)std::lround((v - 64.f) / 63.f * 100.f); if (p == 0) return "C"; std::snprintf(b, sizeof(b), "%s%d", p < 0 ? "L" : "R", std::abs(p)); return b; }
    default: std::snprintf(b, sizeof(b), "%d", (int)std::lround(v)); return b;
    }
}
inline float filterHz(float v) { return v < 64.f ? 20000.f * std::pow(80.f / 20000.f, (64.f - v) / 64.f) : 20.f * std::pow(400.f, (v - 64.f) / 63.f); }

} // namespace kn
