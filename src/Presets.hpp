// Kinetik KGRN-4 — factory presets (built in code on the built-in materials) and the kit helpers
#pragma once
#include "Bank.hpp"
#include "Materials.hpp"
#include "Slicer.hpp"
#include <vector>
#include <cstring>

namespace kn {

// slices of a sample with the kit's settings, written into the kit
inline void sliceKit(Kit& k, const SampleData* s)
{
    k.nSlices = 0;
    if (!s || s->pcm.empty()) return;
    std::vector<uint32_t> f = k.sliceMode ? slicer::equal(s->pcm.size(), clampi(k.sliceAmount, 1, kMaxSlices))
                                          : slicer::transients(s->pcm, s->sr, k.sliceAmount / 100.f, kMaxSlices);
    const std::vector<uint32_t> m = slicer::toMillionths(f, s->pcm.size());
    for (size_t i = 0; i < m.size() && i < (size_t)kMaxSlices; ++i) k.slice[k.nSlices++] = m[i];
}
inline std::string kitLines(const Bank& b, int t)
{
    const Kit& k = b.kit[t];
    std::string s = "M " + std::to_string(t) + " " + std::to_string(k.material) + " " + std::to_string(k.mute) + " " + std::to_string(k.sliceMode) + " " + std::to_string(k.sliceAmount) + "\n";
    s += "X " + std::to_string(t) + " " + std::to_string(k.nSlices);
    for (int i = 0; i < k.nSlices; ++i) s += " " + std::to_string(k.slice[i]);
    return s;
}

// the kit of a new instance (and of Init): four built-in materials, sliced at their transients
inline void defaultKits(Bank& b)
{
    static const int defMat[kTracks] = {1, 2, 6, 3};
    for (int t = 0; t < kTracks; ++t) { Kit& k = b.kit[t]; k = Kit(); k.material = (uint8_t)defMat[t]; sliceKit(k, material(defMat[t]).get()); }
}

struct PB {
    float* v; Bank& b;
    PB(float* values, Bank& bank) : v(values), b(bank)
    {
        for (int t = 0; t < kTracks; ++t) for (int k = 0; k < kTrackParams; ++k) v[tpIndex(t, k)] = trackParamDefault(k);
        for (int g = 0; g < gOUT_STEP; ++g) v[kGlobalBase + g] = globalDefault(g);
        b.initDefault();
        defaultKits(b);
    }
    void mat(int t, int m, int mode = 0, int amount = 50)
    {
        Kit& k = b.kit[t]; k.material = (uint8_t)m; k.sliceMode = (uint8_t)mode; k.sliceAmount = (uint8_t)amount;
        sliceKit(k, material(m).get());
    }
    int slices(int t) const { return b.kit[t].nSlices; }
    void tp(int t, std::initializer_list<std::pair<int, float>> kv) { for (auto& p : kv) v[tpIndex(t, p.first)] = p.second; }
    void glob(std::initializer_list<std::pair<int, float>> kv) { for (auto& p : kv) v[kGlobalBase + p.first] = p.second; }
    // steps of pattern 0: 'x' = trig, anything else = rest; the string length sets the track length
    void row(int t, const char* s, int speed = 0)
    {
        Pattern& P = b.patterns[0];
        const int n = std::min<int>(kSteps, (int)std::strlen(s));
        for (int i = 0; i < n; ++i) P.on[t][i] = s[i] == 'x';
        P.len[t] = (uint8_t)std::max(1, n); P.speed[t] = (uint8_t)speed;
    }
    void lock(int t, int s, int k, float val) { b.patterns[0].locks[t][s][k] = (uint8_t)clampi((int)std::lround(val), 0, 127); }
    void sliceLock(int t, int s, int slice) { lock(t, s, tpSLICE, sliceKnob(slice, slices(t))); }
    void pitchLock(int t, int s, int semis) { lock(t, s, tpPITCH, 64.f + semis * 64.f / 24.f); }
    void length(int n) { b.patterns[0].length = (uint8_t)n; }
    void mute(int t) { b.kit[t].mute = 1; }
};

static void pInit(PB&) {}

static void pFrozenPad(PB& b)
{
    b.tp(0, {{tpSCAN, 64}, {tpPOSITION, 40}, {tpSIZE, 100}, {tpDENSITY, 70}, {tpSPRAY, 50}, {tpSPREAD, 70}, {tpATTACK, 90}, {tpREVERB, 80}});
    b.row(0, "x...............");
    b.tp(1, {{tpSCAN, 70}, {tpSIZE, 80}, {tpDENSITY, 40}, {tpSPRAY, 80}, {tpSPREAD, 90}, {tpPITCH, 64 + 32}, {tpVOLUME, 70}, {tpREVERB, 100}, {tpDELAY, 50}});
    b.row(1, "........x.......");
    b.mute(2); b.mute(3);
    b.glob({{gREV_SIZE, 105}, {gTEMPO, 90}});
}

static void pBeatShuffle(PB& b)
{
    b.mat(0, 6);
    const int n = b.slices(0);
    b.tp(0, {{tpSCAN, 96}, {tpSIZE, 50}, {tpDENSITY, 90}, {tpSPREAD, 0}, {tpDECAY, 70}});
    b.row(0, "xxxxxxxxxxxxxxxx");
    static const int order[16] = {0, 1, 2, 1, 4, 3, 0, 5, 6, 1, 2, 7, 8, 9, 4, 3};
    for (int s = 0; s < 16; ++s) b.sliceLock(0, s, order[s] % std::max(1, n));
    b.pitchLock(0, 13, 7); b.pitchLock(0, 15, -5);
    b.mute(1); b.mute(2); b.mute(3);
    b.glob({{gTEMPO, 128}});
}

static void pGlassRain(PB& b)
{
    b.mat(0, 8);
    b.tp(0, {{tpSCAN, 80}, {tpSIZE, 40}, {tpDENSITY, 100}, {tpSPRAY, 60}, {tpSPREAD, 80}, {tpDECAY, 72}, {tpREVERB, 70}, {tpDELAY, 40}});
    b.row(0, "x.x.x..xx.x.x..x");
    static const int pt[16] = {0, 0, 7, 0, 12, 0, 0, 5, 3, 0, 10, 0, 7, 0, 0, 15};
    for (int s = 0; s < 16; ++s) if (pt[s]) b.pitchLock(0, s, pt[s]);
    b.mat(1, 1);
    b.tp(1, {{tpSCAN, 70}, {tpSIZE, 95}, {tpDENSITY, 60}, {tpSPREAD, 60}, {tpATTACK, 80}, {tpVOLUME, 70}, {tpREVERB, 90}});
    b.row(1, "x...............................");
    b.mute(2); b.mute(3);
    b.glob({{gREV_SIZE, 95}, {gDLY_TIME, 104}, {gTEMPO, 100}});
}

static void pVoiceChoir(PB& b)
{
    for (int t = 0; t < 3; ++t) {
        b.mat(t, 3);
        b.tp(t, {{tpSCAN, 72}, {tpPOSITION, (float)(t * 30)}, {tpSIZE, 90}, {tpDENSITY, 70}, {tpSPRAY, 40}, {tpSPREAD, 60}, {tpATTACK, 85},
                 {tpPAN, (float)(64 + (t - 1) * 40)}, {tpVOLUME, 85}, {tpREVERB, 85}});
    }
    b.tp(1, {{tpPITCH, 64 + 64.f * 7 / 24}}); b.tp(2, {{tpPITCH, 64 + 64.f * 12 / 24}});
    b.row(0, "x..............................."); b.row(1, "x..............................."); b.row(2, "x...............................");
    b.pitchLock(0, 0, 0); b.pitchLock(1, 0, 7); b.pitchLock(2, 0, 12);
    b.mute(3);
    b.length(32);
    b.glob({{gREV_SIZE, 110}, {gTEMPO, 80}});
}

static void pBellScatter(PB& b)
{
    b.mat(0, 2);
    b.tp(0, {{tpSCAN, 64}, {tpSIZE, 55}, {tpDENSITY, 110}, {tpSPRAY, 100}, {tpSPREAD, 100}, {tpDECAY, 90}, {tpDELAY, 60}, {tpREVERB, 60}});
    b.row(0, "x...x...x...x.x.");
    b.lock(0, 4, tpPOSITION, 60); b.lock(0, 8, tpPOSITION, 100); b.lock(0, 14, tpPOSITION, 20);
    b.pitchLock(0, 12, -12);
    b.mute(1); b.mute(2); b.mute(3);
    b.glob({{gDLY_FB, 80}, {gDLY_TIME, 104}, {gTEMPO, 110}});
}

static void pWindTunnel(PB& b)
{
    b.mat(0, 4);
    b.tp(0, {{tpSCAN, 80}, {tpSIZE, 85}, {tpDENSITY, 80}, {tpSPRAY, 70}, {tpSPREAD, 100}, {tpFILTER, 40}, {tpRESO, 90}, {tpATTACK, 60}, {tpREVERB, 60}, {tpVOLUME, 82}});
    b.row(0, "x...x...x...x...");
    static const int f[4] = {30, 48, 22, 56};
    for (int i = 0; i < 4; ++i) b.lock(0, i * 4, tpFILTER, (float)f[i]);
    b.mat(1, 7);
    b.tp(1, {{tpSCAN, 66}, {tpSIZE, 100}, {tpDENSITY, 50}, {tpPITCH, 64 - 32}, {tpVOLUME, 80}, {tpATTACK, 100}, {tpREVERB, 50}});
    b.row(1, "x...............");
    b.mute(2); b.mute(3);
    b.glob({{gREV_SIZE, 100}, {gTEMPO, 96}});
}

static void pPluckStretch(PB& b)
{
    b.mat(0, 5);
    b.tp(0, {{tpSCAN, 72}, {tpSIZE, 75}, {tpDENSITY, 80}, {tpSPREAD, 40}, {tpPITCH, 64 - 32}, {tpREVERB, 50}, {tpDELAY, 40}});
    b.row(0, "x.......x.......");
    b.lock(0, 8, tpPOSITION, 64);
    b.mat(1, 5, 1, 32);
    b.tp(1, {{tpSCAN, 96}, {tpSIZE, 45}, {tpDENSITY, 90}, {tpSPREAD, 30}, {tpDECAY, 60}, {tpVOLUME, 80}, {tpPAN, 90}});
    b.row(1, "..x...x...x.x.x.");
    b.sliceLock(1, 2, 3); b.sliceLock(1, 6, 9); b.sliceLock(1, 10, 14); b.sliceLock(1, 12, 20); b.sliceLock(1, 14, 27);
    b.mute(2); b.mute(3);
    b.glob({{gTEMPO, 104}});
}

static void pDroneField(PB& b)
{
    b.mat(0, 7);
    b.tp(0, {{tpSCAN, 52}, {tpSIZE, 110}, {tpDENSITY, 70}, {tpSPRAY, 60}, {tpSPREAD, 50}, {tpATTACK, 110}, {tpFILTER, 50}, {tpREVERB, 90}, {tpVOLUME, 80}});
    b.row(0, "x...............................................................");
    b.mat(1, 4);
    b.tp(1, {{tpSCAN, 76}, {tpSIZE, 90}, {tpDENSITY, 60}, {tpSPRAY, 90}, {tpSPREAD, 110}, {tpFILTER, 90}, {tpVOLUME, 60}, {tpREVERB, 100}, {tpATTACK, 100}});
    b.row(1, "................................x...............................");
    b.mute(2); b.mute(3);
    b.length(64);
    b.glob({{gREV_SIZE, 120}, {gREV_TONE, 50}, {gTEMPO, 70}});
}

static void pGranularKit(PB& b)
{
    // three tracks on the same beat, each playing one kind of hit
    for (int t = 0; t < 3; ++t) b.mat(t, 6);
    const int n = b.slices(0);
    b.tp(0, {{tpSCAN, 96}, {tpSIZE, 60}, {tpDENSITY, 80}, {tpSPREAD, 0}, {tpDECAY, 62}, {tpVOLUME, 122}});
    b.tp(0, {{tpSLICE, sliceKnob(0, n)}});
    b.row(0, "x.....x...x.....");
    b.tp(1, {{tpSCAN, 96}, {tpSIZE, 50}, {tpDENSITY, 90}, {tpSPREAD, 20}, {tpDECAY, 58}, {tpVOLUME, 108}});
    b.tp(1, {{tpSLICE, sliceKnob(std::min(1, n - 1), n)}});
    b.row(1, "....x.......x..x");
    b.pitchLock(1, 15, 5);
    b.tp(2, {{tpSCAN, 96}, {tpSIZE, 30}, {tpDENSITY, 110}, {tpSPREAD, 50}, {tpDECAY, 40}, {tpFILTER, 90}, {tpVOLUME, 80}, {tpPAN, 80}});
    b.tp(2, {{tpSLICE, sliceKnob(std::min(2, n - 1), n)}, {tpPOSITION, 50}});
    b.row(2, "xxxxxxxxxxxxxxxx", 0);
    for (int s = 0; s < 16; ++s) if (s % 2) b.lock(2, s, tpVOLUME, 55);
    b.mat(3, 2);
    b.tp(3, {{tpSCAN, 64}, {tpSIZE, 60}, {tpDENSITY, 90}, {tpSPRAY, 60}, {tpSPREAD, 80}, {tpDECAY, 80}, {tpVOLUME, 70}, {tpDELAY, 70}, {tpREVERB, 40}});
    b.row(3, "..x.......x.....");
    b.glob({{gTEMPO, 124}, {gDLY_TIME, 88}});
}

static void pReverseSwells(PB& b)
{
    b.mat(0, 1);
    b.tp(0, {{tpSCAN, 32}, {tpPOSITION, 120}, {tpSIZE, 90}, {tpDENSITY, 80}, {tpSPREAD, 60}, {tpATTACK, 118}, {tpDECAY, 80}, {tpREVERB, 70}, {tpVOLUME, 118}});
    b.row(0, "x...............");
    b.mat(1, 8);
    b.tp(1, {{tpSCAN, 40}, {tpPOSITION, 100}, {tpSIZE, 80}, {tpDENSITY, 70}, {tpSPREAD, 80}, {tpATTACK, 110}, {tpDECAY, 70}, {tpPITCH, 64 + 32}, {tpVOLUME, 75}, {tpREVERB, 90}});
    b.row(1, "........x.......");
    b.mute(2); b.mute(3);
    b.glob({{gTEMPO, 90}});
}

static void pStutterSlices(PB& b)
{
    b.mat(0, 6, 1, 16);
    b.tp(0, {{tpSCAN, 64}, {tpSIZE, 18}, {tpDENSITY, 70}, {tpSPREAD, 10}, {tpDECAY, 66}, {tpVOLUME, 124}});
    b.row(0, "xxx.xx.xxx.x.xxx");
    static const int sl[16] = {0, 0, 0, 0, 4, 4, 0, 8, 9, 10, 0, 12, 0, 14, 14, 15};
    for (int s = 0; s < 16; ++s) b.sliceLock(0, s, sl[s]);
    for (int s = 8; s < 11; ++s) b.lock(0, s, tpSIZE, 8.f + s * 2);
    b.pitchLock(0, 15, 12);
    b.mute(1); b.mute(2); b.mute(3);
    b.glob({{gTEMPO, 132}});
}

static void pAmbientFour(PB& b)
{
    static const int mats[4] = {1, 8, 3, 7};
    static const char* rows[4] = {"x...............", "....x.........", "........x.......................", "x....................."};
    for (int t = 0; t < 4; ++t) {
        b.mat(t, mats[t]);
        b.tp(t, {{tpSCAN, (float)(66 + t * 4)}, {tpPOSITION, (float)(t * 25)}, {tpSIZE, 95}, {tpDENSITY, 65}, {tpSPRAY, 50}, {tpSPREAD, 80},
                 {tpATTACK, 95}, {tpDECAY, 100}, {tpVOLUME, 80}, {tpPAN, (float)(24 + t * 26)}, {tpREVERB, 90}, {tpDELAY, t == 1 ? 50.f : 0.f}});
        b.row(t, rows[t]);
    }
    b.tp(1, {{tpPITCH, 64 + 32}});
    b.tp(3, {{tpPITCH, 64 - 32}});
    b.length(32);
    b.glob({{gREV_SIZE, 115}, {gTEMPO, 84}});
}

struct PresetDef { const char* name; const char* desc; void (*build)(PB&); };
static const PresetDef kPresets[] = {
    {"Init",            "Four materials, empty sequencer",                  pInit},
    {"Frozen Pad",      "A frozen cloud of the pad, a bright layer on top", pFrozenPad},
    {"Beat Shuffle",    "The beat re-ordered slice by slice",               pBeatShuffle},
    {"Glass Rain",      "Glass grains with pitch locks over a pad",         pGlassRain},
    {"Voice Choir",     "Three voice tracks as a chord",                    pVoiceChoir},
    {"Bell Scatter",    "Sprayed bells into the echo",                      pBellScatter},
    {"Wind Tunnel",     "Filtered wind with a low drone",                   pWindTunnel},
    {"Pluck Stretch",   "Plucks slowed down, slices on top",                pPluckStretch},
    {"Drone Field",     "Slow backwards drone, long reverb",                pDroneField},
    {"Granular Kit",    "Kick, snare and hats from one beat",               pGranularKit},
    {"Reverse Swells",  "Pads played backwards with long fades",            pReverseSwells},
    {"Stutter Slices",  "Tiny grains on equal slices",                      pStutterSlices},
    {"Ambient Four",    "Four tracks of different lengths drifting",        pAmbientFour},
};
static constexpr int kNumPresets = (int)(sizeof(kPresets) / sizeof(kPresets[0]));

inline void buildPreset(int i, float* values, Bank& bank)
{
    PB b(values, bank);
    if (i >= 0 && i < kNumPresets) kPresets[i].build(b);
}

} // namespace kn
