// Kinetik KGRN-4 — constants, parameters and value texts (four polyphonic granular sample tracks)
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
static constexpr int kVoices = 6;          // notes at once per track
inline int clampi(int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); }
static constexpr int kMaxSlices = 32;
static constexpr uint8_t kNoLock = 255;

// ---- grain engine of a track (Kit::mode)
enum GrainMode { gmClassic = 0, gmCloud, gmLoop, kGrainModes };
static const char* const kModeNames[kGrainModes] = {"CLASSIC", "CLOUD", "LOOP"};
static const char* const kModeHelp[kGrainModes] = {
    "CLASSIC: grains chained without gaps (2 to 6 overlapping); PITCH and SCAN are independent, for clean time stretch and freeze",
    "CLOUD: a cloud of free grains; DENSITY left of centre = regular, right = random, further from the centre = more grains at once",
    "LOOP: a loop of SIZE taken at the play head, faded by SHAPE; DENSITY stacks 1 to 4 copies, JITTER detunes and spreads them"};

// ---- per-track knobs (every one can be locked per step), shown in pages of up to eight
enum TrackParam {
    tpSTART = 0, tpEND, tpPOSITION, tpSLICE, tpSCAN, tpSPRAY, tpPITCH, tpPMIX,          // SOURCE
    tpSIZE, tpDENSITY, tpSHAPE, tpSPREAD, tpJITTER, tpREVERSE, tpOCTAVE, tpFINE,        // GRAIN
    tpATTACK, tpDECAY, tpSUSTAIN, tpRELEASE, tpVOLUME, tpPAN,                           // AMP
    tpFILTER, tpRESO, tpFLTENV, tpDRIVE, tpDELAY, tpREVERB,                             // FX (track)
    tpL1WAVE, tpL1RATE, tpL1SYNC, tpL1DEPTH, tpL1DEST, tpL1TRIG,                        // LFO 1
    tpL2WAVE, tpL2RATE, tpL2SYNC, tpL2DEPTH, tpL2DEST, tpL2TRIG,                        // LFO 2
    kTrackParams
};
static constexpr int kLfos = 2, kLfoParams = 6;
inline int lfoParam(int i, int k) { return tpL1WAVE + i * kLfoParams + k; }
static constexpr int kModTargets = tpL1WAVE;         // LFOs can move every knob before the LFOs
static constexpr int kPages = 6;
static const char* const kPageNames[kPages] = {"SOURCE", "GRAIN", "AMP", "FX", "LFO 1", "LFO 2"};
static const int kPageStart[kPages + 1] = {0, 8, 16, 22, 28, 34, 40};
struct TrackParamDef { const char* name; uint8_t def; const char* help; };
static const TrackParamDef kTP[kTrackParams] = {
    {"START",    0,   "Start of the part of the sample (or slice) that plays"},
    {"END",      127, "End of the part of the sample (or slice) that plays"},
    {"POSITION", 0,   "Where the play head starts between START and END; LIVE: 0 = the oldest sound of the window"},
    {"SLICE",    0,   "ALL = the whole sample, or one of the slices found when the sample was loaded"},
    {"SCAN",     96,  "How fast the play head moves through the material: 100% = original speed, centre = FREEZE, left = backwards"},
    {"SPRAY",    0,   "Random offset of each grain's start around the play head"},
    {"PITCH",    64,  "Pitch of the grains in semitones (the scan speed does not change)"},
    {"PITCH MIX",127, "Mix of grains at the sample's own pitch (left) and transposed grains (right: PITCH, notes, OCTAVE)"},
    {"SIZE",     70,  "Length of each grain (with SYNC: a note value)"},
    {"DENSITY",  64,  "CLASSIC: overlap 2 to 6; CLOUD: centre = few grains, left = regular, right = random, further = more; LOOP: 1 to 4 copies"},
    {"SHAPE",    64,  "Grain window: square, triangle, smooth (centre right), percussive, reversed percussive; LOOP: the crossfade"},
    {"SPREAD",   30,  "Stereo width of the grains, and a small left / right delay"},
    {"JITTER",   0,   "Random detune (a few cents, like a chorus), length, level and timing of each grain"},
    {"REVERSE",  0,   "Chance that a grain plays backwards"},
    {"OCTAVE",   0,   "Chance that a grain plays an octave up (mostly) or down: shimmer"},
    {"FINE",     64,  "Fine tuning, -50 to +50 cents"},
    {"ATTACK",   0,   "Fade-in of each note"},
    {"DECAY",    64,  "Time to fall from full level to SUSTAIN"},
    {"SUSTAIN",  127, "Level held while the note lasts (its step length, or the key)"},
    {"RELEASE",  50,  "Fade-out after the note ends"},
    {"VOLUME",   100, "Track level"},
    {"PAN",      64,  "Track position in the stereo field"},
    {"FILTER",   64,  "Left = low pass, right = high pass, centre = off"},
    {"RESO",     20,  "Filter resonance"},
    {"FLT ENV",  64,  "How much each note's envelope moves the filter (left = closes, right = opens)"},
    {"DRIVE",    0,   "Warm saturation of the track"},
    {"DELAY",    0,   "Send to the echo"},
    {"REVERB",   0,   "Send to the reverb"},
    {"WAVE",     0,   "LFO shape: triangle, sine, square, saw up, saw down, random"},
    {"RATE",     50,  "LFO speed (with SYNC: a note value)"},
    {"SYNC",     0,   "Right half: the LFO follows the tempo"},
    {"DEPTH",    64,  "How far the LFO moves its destination (centre = nothing, left = the other way)"},
    {"DEST",     0,   "The knob the LFO moves"},
    {"TRIG",     0,   "Right half: the LFO starts again with every note"},
    {"WAVE",     0,   "LFO shape: triangle, sine, square, saw up, saw down, random"},
    {"RATE",     50,  "LFO speed (with SYNC: a note value)"},
    {"SYNC",     0,   "Right half: the LFO follows the tempo"},
    {"DEPTH",    64,  "How far the LFO moves its destination (centre = nothing, left = the other way)"},
    {"DEST",     0,   "The knob the LFO moves"},
    {"TRIG",     0,   "Right half: the LFO starts again with every note"},
};

// ---- host parameters: tracks first (track t = indices t*40 .. t*40+39), then the globals.
// Saved host parameters never move: new ones go just before gOUT_STEP. (0.4.0 redid them all.)
static constexpr int kGlobalBase = kTracks * kTrackParams;
enum Global {
    gREV_SIZE = 0, gREV_TONE, gREV_PRE, gREV_WIDTH, gREV_LEVEL,
    gDLY_TIME, gDLY_FB, gDLY_HICUT, gDLY_LOCUT, gDLY_WIDTH, gDLY_LEVEL,
    gCHO_RATE, gCHO_DEPTH, gCHO_MIX,
    gOUTPUT, gDRIVE, gLOWCUT, gHIGHCUT,
    gTEMPO, gPLAY, gPATTERN, gFREEZE, gCAPLEN,
    gOUT_STEP, gOUT_ACTIVE, gOUT_PATTERN, gOUT_POS1, gOUT_POS2, gOUT_POS3, gOUT_POS4, gOUT_IN, kGlobalParams
};
static constexpr int kTotalParams = kGlobalBase + kGlobalParams;
static constexpr int kSavedParams = kGlobalBase + gOUT_STEP;
inline int tpIndex(int t, int k) { return t * kTrackParams + k; }
inline float trackParamDefault(int k) { return kTP[k].def; }    // neutral values (tests, a knob without a track)
// a new instance: each track starts as a different kind of granular sound (PAD cloud, KEYS grain synth,
// BEAT loops on the 16ths, BELLS grain rain), lively and wide rather than a plain playback
inline float trackDefault(int t, int k)
{
    if (k >= tpL1WAVE) return trackParamDefault(k);
    static const int8_t d[kTracks][kModTargets] = {
        // STA END POS SLC SCAN SPRY PIT PMIX  SIZE DENS SHAP SPRD JIT REV OCT FINE  ATK DEC SUS REL VOL PAN  FILT RES FENV DRV DLY REV
        {   0, 127, 30,  0,  72,  30, 64, 127,   80, 100,  64,  85,  20, 15,  0, 64,   70, 64, 127, 70, 96, 64,   64, 20, 64,  0,  0, 45 },
        {   0, 127, 10,  0,  80,  12, 64, 127,   76,  80,  64,  65,  12,  0,  0, 64,   12, 64, 127, 60, 92, 64,   64, 20, 64,  0, 20, 35 },
        {   0, 127,  0,  0,  96,   0, 64, 127,   48,   0,  10,  25,   0,  0,  0, 64,    0, 64, 127, 30,100, 64,   64, 20, 64,  0,  0, 12 },
        {   0, 127, 20,  0,  70,  70, 64, 127,   45, 110,  70, 110,  25, 30, 25, 64,    0, 90,  80, 70, 84, 64,   64, 20, 64,  0, 40, 50 },
    };
    return t >= 0 && t < kTracks && k >= 0 ? (float)d[t][k] : trackParamDefault(k);
}
inline float globalDefault(int g)
{
    switch (g) {
    case gREV_SIZE: return 80.f; case gREV_TONE: return 90.f; case gREV_PRE: return 20.f; case gREV_WIDTH: return 110.f; case gREV_LEVEL: return 100.f;
    case gDLY_TIME: return 88.f; case gDLY_FB: return 60.f; case gDLY_HICUT: return 96.f; case gDLY_LOCUT: return 20.f; case gDLY_WIDTH: return 110.f; case gDLY_LEVEL: return 100.f;
    case gCHO_RATE: return 40.f; case gCHO_DEPTH: return 60.f; case gCHO_MIX: return 0.f;
    case gOUTPUT: return 96.f; case gDRIVE: return 0.f; case gLOWCUT: return 0.f; case gHIGHCUT: return 127.f;
    case gTEMPO: return 120.f; case gOUT_STEP: return -1.f; case gCAPLEN: return 1.f;
    default: return 0.f;
    }
}
// CAPTURE / LIVE length in bars (index = gCAPLEN)
static constexpr int kCapLens = 4;
static const int kCapBars[kCapLens] = {1, 2, 4, 8};
inline int capBars(float v) { return kCapBars[clampi((int)std::lround(v), 0, kCapLens - 1)]; }
static constexpr float kLiveSeconds = 34.f;          // the input buffer (8 bars at 60 BPM + a margin)

// track speed (steps per 16th): index stored in the pattern
static constexpr int kSpeeds = 8;
static const double kSpeedMult[kSpeeds] = {1.0, 2.0, 4.0, 0.5, 0.25, 0.75, 1.5, 3.0};
static const char* const kSpeedNames[kSpeeds] = {"1x", "2x", "4x", "1/2x", "1/4x", "3/4x", "3/2x", "3x"};

// ---- sequencer step data (as KMSP-6)
static constexpr int kGateCount = 13;
static const float kGateSteps[kGateCount] = {0.125f, 0.25f, 0.5f, 0.75f, 1.f, 1.5f, 2.f, 3.f, 4.f, 6.f, 8.f, 16.f, 1e6f};
static const char* const kGateNames[kGateCount] = {"1/8", "1/4", "1/2", "3/4", "1", "1.5", "2", "3", "4", "6", "8", "16", "HOLD"};
static constexpr int kDefaultGate = 3;               // 3/4 of a step
static constexpr int kGateHold = kGateCount - 1;     // until the next note of the track
struct ChordDef { const char* name; int n; int notes[4]; };
static constexpr int kChordCount = 10;
static const ChordDef kChords[kChordCount] = {
    {"NOTE", 1, {0}}, {"MAJ", 3, {0, 4, 7}}, {"MIN", 3, {0, 3, 7}}, {"SUS4", 3, {0, 5, 7}},
    {"MAJ7", 4, {0, 4, 7, 11}}, {"MIN7", 4, {0, 3, 7, 10}}, {"DOM7", 4, {0, 4, 7, 10}},
    {"5TH", 2, {0, 7}}, {"OCTV", 2, {0, 12}}, {"DIM", 3, {0, 3, 6}},
};
static constexpr int kRatchCount = 6;
static const uint8_t kRatchValues[kRatchCount] = {1, 2, 3, 4, 6, 8};
enum CondKind { ckAlways, ckProb, ckFill, ckNotFill, ckFirst, ckNotFirst, ckRatio };
struct CondDef { const char* name; CondKind kind; int a, b; };
static const CondDef kConds[] = {
    {"", ckAlways, 0, 0},
    {"90%", ckProb, 90, 0}, {"75%", ckProb, 75, 0}, {"50%", ckProb, 50, 0}, {"33%", ckProb, 33, 0},
    {"25%", ckProb, 25, 0}, {"10%", ckProb, 10, 0},
    {"FILL", ckFill, 0, 0}, {"!FILL", ckNotFill, 0, 0}, {"1ST", ckFirst, 0, 0}, {"!1ST", ckNotFirst, 0, 0},
    {"1:2", ckRatio, 1, 2}, {"2:2", ckRatio, 2, 2}, {"1:3", ckRatio, 1, 3}, {"2:3", ckRatio, 2, 3},
    {"3:3", ckRatio, 3, 3}, {"1:4", ckRatio, 1, 4}, {"2:4", ckRatio, 2, 4}, {"3:4", ckRatio, 3, 4}, {"4:4", ckRatio, 4, 4},
};
static constexpr int kNumConds = (int)(sizeof(kConds) / sizeof(kConds[0]));
static constexpr int kBaseNote = 60;                 // a step's note: 60 (C3) = the sample's own pitch
inline const char* noteName(int n, char* buf, int len)
{
    static const char* names[12] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
    n = clampi(n, 0, 127);
    std::snprintf(buf, len, "%s%d", names[n % 12], n / 12 - 2);
    return buf;
}

// ---- LFO
static constexpr int kLfoWaves = 6;
static const char* const kLfoWaveNames[kLfoWaves] = {"TRI", "SINE", "SQUARE", "SAW UP", "SAW DN", "RANDOM"};
inline int lfoWave(float v) { return clampi((int)(v * kLfoWaves / 128.f), 0, kLfoWaves - 1); }
static constexpr int kLfoSyncDivs = 14;
static const float kLfoSyncBeats[kLfoSyncDivs] = {32.f, 16.f, 8.f, 4.f, 2.f, 1.5f, 1.f, 2.f / 3.f, 0.75f, 0.5f, 1.f / 3.f, 0.25f, 1.f / 6.f, 0.125f};
static const char* const kLfoSyncNames[kLfoSyncDivs] = {"8 BARS", "4 BARS", "2 BARS", "1 BAR", "1/2", "1/4D", "1/4", "1/4T", "1/8D", "1/8", "1/8T", "1/16", "1/16T", "1/32"};
inline int lfoSyncDiv(float v) { return clampi((int)(v * kLfoSyncDivs / 128.f), 0, kLfoSyncDivs - 1); }
inline bool lfoSynced(float v) { return v >= 64.f; }
inline bool lfoRetrig(float v) { return v >= 64.f; }
inline float lfoHz(float v) { return 0.03f * std::pow(1000.f, clampf(v, 0.f, 127.f) / 127.f); }   // 0.03 .. 30 Hz
inline int lfoDest(float v) { return clampi((int)(v * (kModTargets + 1) / 128.f), 0, kModTargets); }   // 0 = none, 1 + knob
inline float lfoDestKnob(int d) { return (d + 0.5f) * 128.f / (kModTargets + 1); }

// ---- effects
static const double kDelTime[8] = {0.125, 1.0 / 6.0, 0.25, 1.0 / 3.0, 0.375, 0.5, 0.75, 1.0};
static const char* const kDelTimeNames[8] = {"1/32", "1/16T", "1/16", "1/8T", "1/16D", "1/8", "1/8D", "1/4"};
inline int bin8(float v) { int b = (int)(v / 16.f); return b < 0 ? 0 : (b > 7 ? 7 : b); }

// ---- mappings (shared by the engine and the UI)
inline float scanSpeed(float v) { const float s = (v - 64.f) / 32.f; return std::fabs(s) < 0.02f ? 0.f : s; }   // -2 .. +2
inline int pitchSemis(float v) { return (int)std::lround((v - 64.f) / 64.f * 24.f); }
inline float fineCents(float v) { return (v - 64.f) / 64.f * 50.f; }
inline float grainSec(float v) { return 0.005f * std::pow(400.f, v / 127.f); }            // 5 ms .. 2 s
// SYNC sizes, in beats (quarter notes)
static constexpr int kSyncSizes = 12;
static const double kSyncBeats[kSyncSizes] = {1.0 / 16, 1.0 / 12, 1.0 / 8, 1.0 / 6, 0.25, 1.0 / 3, 0.5, 2.0 / 3, 1.0, 2.0, 4.0, 8.0};
static const char* const kSyncNames[kSyncSizes] = {"1/64", "1/32T", "1/32", "1/16T", "1/16", "1/8T", "1/8", "1/4T", "1/4", "1/2", "1 BAR", "2 BARS"};
inline int syncBin(float v) { return clampi((int)(v / 128.f * kSyncSizes), 0, kSyncSizes - 1); }
// DENSITY: CLASSIC overlap, CLOUD grains at once (centre = 1.5) and regular / random, LOOP copies
inline float classicOverlap(float v) { return 2.f + 4.f * clampf(v / 127.f, 0.f, 1.f); }
inline bool cloudRandom(float v) { return v > 64.f; }
inline float cloudOverlap(float v) { const float d = std::fabs(v - 64.f) / 63.f; return 1.5f * std::pow(16.f / 1.5f, clampf(d, 0.f, 1.f)); }   // 1.5 .. 16
inline int loopCopies(float v) { return clampi(1 + (int)(v / 128.f * 4.f), 1, 4); }
inline float spraySec(float v) { const float x = v / 127.f; return x * x * 1.0f; }        // 0 .. 1 s
inline float attackSec(float v) { return v < 0.5f ? 0.001f : 0.001f * std::pow(3000.f, v / 127.f); }   // 1 ms .. 3 s
inline float decaySec(float v) { return 0.02f * std::pow(500.f, v / 127.f); }             // 20 ms .. 10 s
inline float releaseSec(float v) { return 0.008f * std::pow(1000.f, v / 127.f); }         // 8 ms .. 8 s
inline float volumeGain(float v) { return 2.2f * (v / 127.f) * (v / 127.f); }
// OUTPUT knob -> gain (96 = 0 dB, 0 = -inf, 127 = +8 dB)
inline float trimDb(float v) { return v <= 0.5f ? -120.f : (v - 96.f) * (v < 96.f ? 0.5f : 0.26f); }
inline float trimGain(float v) { return v <= 0.5f ? 0.f : std::pow(10.f, trimDb(v) / 20.f); }
inline float levelGain(float v) { const float x = v / 100.f; return x * x; }               // send returns: 100 = 0 dB
inline float cutHz(float v, float lo, float hi) { return lo * std::pow(hi / lo, clampf(v / 127.f, 0.f, 1.f)); }
// SLICE knob: 0 = whole sample, otherwise one of n slices
inline int sliceOf(float v, int n) { if (n <= 0 || v < 0.5f) return -1; return clampi((int)((v - 0.5f) / 126.5f * n), 0, n - 1); }
inline float sliceKnob(int s, int n) { return s < 0 || n <= 0 ? 0.f : 0.5f + (s + 0.5f) * 126.5f / n; }   // centre of slice s
inline const char* shapeName(float v)
{
    static const char* n[5] = {"SQUARE", "TRIANGLE", "SMOOTH", "PERC", "REV PERC"};
    return n[clampi((int)std::lround(v / 127.f * 4.f), 0, 4)];
}
inline std::string pctText(float v) { char b[16]; if (v < 0.5f) return "OFF"; std::snprintf(b, sizeof(b), "%d%%", (int)std::lround(v / 127.f * 100.f)); return b; }
inline std::string bipText(float v) { char b[16]; const int p = (int)std::lround((v - 64.f) / 63.f * 100.f); if (p == 0) return "0"; std::snprintf(b, sizeof(b), "%+d%%", p); return b; }
inline std::string secText(float s) { char b[16]; if (s >= 1.f) std::snprintf(b, sizeof(b), "%.1f S", s); else std::snprintf(b, sizeof(b), "%d MS", (int)std::lround(s * 1000.f)); return b; }
inline float filterHz(float v) { return v < 64.f ? 20000.f * std::pow(80.f / 20000.f, (64.f - v) / 64.f) : 20.f * std::pow(400.f, (v - 64.f) / 63.f); }

inline std::string trackValueText(int k, float v, int slices = 0, int mode = gmClassic, bool sync = false)
{
    char b[32];
    switch (k) {
    case tpSTART: case tpEND: case tpPOSITION: std::snprintf(b, sizeof(b), "%d%%", (int)std::lround(v / 127.f * 100.f)); return b;
    case tpSLICE: { const int s = sliceOf(v, slices); if (s < 0) return "ALL"; std::snprintf(b, sizeof(b), "%d / %d", s + 1, slices); return b; }
    case tpSCAN: { const float s = scanSpeed(v); if (s == 0.f) return "FREEZE"; std::snprintf(b, sizeof(b), "%+d%%", (int)std::lround(s * 100.f)); return b; }
    case tpPITCH: { const int st = pitchSemis(v); std::snprintf(b, sizeof(b), st ? "%+d ST" : "0 ST", st); return b; }
    case tpPMIX: if (v < 0.5f) return "ORIGINAL"; if (v > 126.5f) return "PITCHED"; std::snprintf(b, sizeof(b), "%d%%", (int)std::lround(v / 127.f * 100.f)); return b;
    case tpFINE: { const int c = (int)std::lround(fineCents(v)); std::snprintf(b, sizeof(b), c ? "%+d CT" : "0 CT", c); return b; }
    case tpSIZE: {
        if (sync) return kSyncNames[syncBin(v)];
        const float s = grainSec(v); if (s >= 1.f) { std::snprintf(b, sizeof(b), "%.2f S", s); return b; }
        std::snprintf(b, sizeof(b), "%d MS", (int)std::lround(s * 1000.f)); return b;
    }
    case tpDENSITY:
        if (mode == gmClassic) { std::snprintf(b, sizeof(b), "X%.1f", classicOverlap(v)); return b; }
        if (mode == gmLoop) { const int c = loopCopies(v); std::snprintf(b, sizeof(b), c == 1 ? "1 COPY" : "%d COPIES", c); return b; }
        std::snprintf(b, sizeof(b), "%s X%.1f", cloudRandom(v) ? "RND" : "REG", cloudOverlap(v)); return b;
    case tpSHAPE: if (mode == gmLoop) { std::snprintf(b, sizeof(b), "XF %d%%", (int)std::lround(v / 127.f * 50.f)); return b; } return shapeName(v);
    case tpSPRAY: if (v < 0.5f) return "OFF"; std::snprintf(b, sizeof(b), "%d MS", (int)std::lround(spraySec(v) * 1000.f)); return b;
    case tpJITTER: case tpREVERSE: case tpOCTAVE: case tpSPREAD: case tpDRIVE: case tpDELAY: case tpREVERB: return pctText(v);
    case tpATTACK: return secText(attackSec(v));
    case tpDECAY: return secText(decaySec(v));
    case tpSUSTAIN: return pctText(v);
    case tpRELEASE: return secText(releaseSec(v));
    case tpFILTER: {
        if (std::fabs(v - 64.f) < 1.f) return "OFF";
        const float hz = filterHz(v);
        if (hz >= 1000.f) std::snprintf(b, sizeof(b), "%s %.1fK", v < 64.f ? "LP" : "HP", hz / 1000.f);
        else std::snprintf(b, sizeof(b), "%s %d", v < 64.f ? "LP" : "HP", (int)hz);
        return b;
    }
    case tpFLTENV: return bipText(v);
    case tpVOLUME: { const float g = volumeGain(v); if (g < 0.001f) return "-INF"; std::snprintf(b, sizeof(b), "%+.1fdB", 20.f * std::log10(g)); return b; }
    case tpPAN: { const int p = (int)std::lround((v - 64.f) / 63.f * 100.f); if (p == 0) return "C"; std::snprintf(b, sizeof(b), "%s%d", p < 0 ? "L" : "R", std::abs(p)); return b; }
    default: break;
    }
    if (k >= tpL1WAVE) {
        const int j = (k - tpL1WAVE) % kLfoParams;
        switch (j) {
        case 0: return kLfoWaveNames[lfoWave(v)];
        case 1: std::snprintf(b, sizeof(b), "%.2f HZ", lfoHz(v)); return b;           // the UI shows the synced value itself
        case 2: return lfoSynced(v) ? "SYNC" : "FREE";
        case 3: return bipText(v);
        case 4: { const int d = lfoDest(v); return d == 0 ? "NONE" : kTP[d - 1].name; }
        default: return lfoRetrig(v) ? "RETRIG" : "FREE";
        }
    }
    std::snprintf(b, sizeof(b), "%d", (int)std::lround(v)); return b;
}

} // namespace kn
