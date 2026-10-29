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
    std::string s = "M " + std::to_string(t) + " " + std::to_string(k.material) + " " + std::to_string(k.mute) + " " + std::to_string(k.sliceMode) + " " + std::to_string(k.sliceAmount)
                    + " " + std::to_string(k.mode) + " " + std::to_string(k.sync) + " " + std::to_string(k.legato) + " " + std::to_string(k.solo) + "\n";
    s += "X " + std::to_string(t) + " " + std::to_string(k.nSlices);
    for (int i = 0; i < k.nSlices; ++i) s += " " + std::to_string(k.slice[i]);
    return s;
}

// the kit of a new instance (and of Init): four built-in materials, sliced at their transients
inline void defaultKits(Bank& b)
{
    // PAD as a cloud, KEYS as a grain synth (CLASSIC, LEGATO), BEAT in loops on the 16ths, BELLS as a grain rain
    static const int defMat[kTracks] = {1, 8, 6, 2}, defMode[kTracks] = {gmCloud, gmClassic, gmLoop, gmCloud};
    static const uint8_t defSync[kTracks] = {0, 0, 1, 0}, defLegato[kTracks] = {1, 1, 0, 0};
    for (int t = 0; t < kTracks; ++t) {
        Kit& k = b.kit[t]; k = Kit(); k.material = (uint8_t)defMat[t]; k.mode = (uint8_t)defMode[t]; k.sync = defSync[t]; k.legato = defLegato[t];
        sliceKit(k, material(defMat[t]).get());
    }
}

struct PB {
    float* v; Bank& b;
    PB(float* values, Bank& bank) : v(values), b(bank)
    {
        for (int t = 0; t < kTracks; ++t) for (int k = 0; k < kTrackParams; ++k) v[tpIndex(t, k)] = trackParamDefault(k);
        for (int g = 0; g < gOUT_STEP; ++g) v[kGlobalBase + g] = globalDefault(g);
        b.initDefault();
        defaultKits(b);
        for (auto& k : b.kit) { k.mode = gmClassic; k.sync = 0; k.legato = 0; }   // presets say what they want
    }
    void mat(int t, int m, int mode = 0, int amount = 50)
    {
        Kit& k = b.kit[t]; k.material = (uint8_t)m; k.sliceMode = (uint8_t)mode; k.sliceAmount = (uint8_t)amount;
        sliceKit(k, material(m).get());
    }
    static float sliceKnobOf(PB& b, int t, int s) { return sliceKnob(s, b.slices(t)); }
    int slices(int t) const { return b.kit[t].nSlices; }
    void tp(int t, std::initializer_list<std::pair<int, float>> kv) { for (auto& p : kv) v[tpIndex(t, p.first)] = p.second; }
    void glob(std::initializer_list<std::pair<int, float>> kv) { for (auto& p : kv) v[kGlobalBase + p.first] = p.second; }
    // steps of pattern 0: 'x' = trig, anything else = rest; the string length sets the track length
    void row(int t, const char* s, int speed = 0)
    {
        Pattern& P = b.patterns[0];
        const int n = std::min<int>(kSteps, (int)std::strlen(s));
        for (int i = 0; i < n; ++i) { P.on[t][i] = s[i] == 'x'; P.gate[t][i] = (uint8_t)kGateHold; }   // a note lasts until the next one
        P.len[t] = (uint8_t)std::max(1, n); P.speed[t] = (uint8_t)speed;
    }
    void lock(int t, int s, int k, float val) { b.patterns[0].locks[t][s][k] = (uint8_t)clampi((int)std::lround(val), 0, 127); }
    void sliceLock(int t, int s, int slice) { lock(t, s, tpSLICE, sliceKnob(slice, slices(t))); }
    void pitchLock(int t, int s, int semis) { b.patterns[0].note[t][s] = (uint8_t)(kBaseNote + semis); }
    void chord(int t, int s, int c, int semis = 0) { b.patterns[0].chord[t][s] = (uint8_t)c; pitchLock(t, s, semis); }
    void gate(int t, int s, int g) { b.patterns[0].gate[t][s] = (uint8_t)g; }
    void ratch(int t, int s, int r) { b.patterns[0].ratch[t][s] = (uint8_t)r; }
    void cond(int t, int s, int c) { b.patterns[0].cond[t][s] = (uint8_t)c; }
    // an LFO: wave 0..5, rate knob, synced (rate = a kLfoSyncNames index), depth -63..63, destination knob
    void lfo(int t, int i, int wave, float rate, bool synced, int depth, int dest, bool retrig = false)
    {
        v[tpIndex(t, lfoParam(i, 0))] = (wave + 0.5f) * 128.f / kLfoWaves;
        v[tpIndex(t, lfoParam(i, 1))] = synced ? (rate + 0.5f) * 128.f / kLfoSyncDivs : rate;
        v[tpIndex(t, lfoParam(i, 2))] = synced ? 100.f : 0.f;
        v[tpIndex(t, lfoParam(i, 3))] = 64.f + depth;
        v[tpIndex(t, lfoParam(i, 4))] = lfoDestKnob(dest + 1);
        v[tpIndex(t, lfoParam(i, 5))] = retrig ? 100.f : 0.f;
    }
    void length(int n) { b.patterns[0].length = (uint8_t)n; }
    void mode(int t, int m, bool sync = false) { b.kit[t].mode = (uint8_t)m; b.kit[t].sync = sync ? 1 : 0; }
    void legato(int t) { b.kit[t].legato = 1; }
    static float syncSize(int bin) { return (bin + 0.5f) * 128.f / kSyncSizes; }      // SIZE knob for a kSyncNames entry
    void mute(int t) { b.kit[t].mute = 1; }
};

// Every preset is in C minor (the materials are), at a tempo that suits it. Pitch locks use the scale's steps:
// 0 2 3 5 7 8 10 12 (C D Eb F G Ab Bb C). Sync size bins: 0 1/64, 1 1/32T, 2 1/32, 3 1/16T, 4 1/16, 5 1/8T, 6 1/8,
// 7 1/4T, 8 1/4, 9 1/2, 10 1 BAR, 11 2 BARS.
static void pInit(PB& b)
{
    for (int t = 0; t < kTracks; ++t) for (int k = 0; k < kTrackParams; ++k) b.v[tpIndex(t, k)] = trackDefault(t, k);
    defaultKits(b.b);
}

// ---- washes: the sound dissolves into a wide, moving texture
static void pDissolvingStrings(PB& b)
{
    b.mat(0, 1); b.mode(0, gmCloud); b.legato(0);
    b.tp(0, {{tpPOSITION, 20}, {tpSCAN, 68}, {tpSPRAY, 50}, {tpSIZE, 100}, {tpDENSITY, 114}, {tpJITTER, 30}, {tpREVERSE, 25}, {tpSPREAD, 115},
             {tpATTACK, 95}, {tpVOLUME, 96}, {tpREVERB, 80}});
    b.row(0, "x...............................");
    b.mat(1, 3); b.mode(1, gmClassic); b.legato(1);
    b.tp(1, {{tpPOSITION, 40}, {tpSCAN, 64}, {tpSPRAY, 40}, {tpSIZE, 104}, {tpDENSITY, 110}, {tpJITTER, 20}, {tpOCTAVE, 18}, {tpSPREAD, 105},
             {tpATTACK, 110}, {tpVOLUME, 70}, {tpREVERB, 95}});
    b.row(1, "x...............................");
    b.lock(1, 16, tpPOSITION, 100);                               // the choir moves to its second chord
    b.mute(2); b.mute(3);
    b.length(32);
    b.glob({{gREV_SIZE, 110}, {gREV_TONE, 80}, {gTEMPO, 76}});
    b.lfo(0, 0, 1, 18, false, 18, tpPOSITION);                   // the pad drifts slowly through its chords
    b.lfo(1, 0, 0, 1, true, -20, tpFILTER);
}

static void pChoirFog(PB& b)
{
    b.mat(0, 3); b.mode(0, gmCloud); b.legato(0);
    b.tp(0, {{tpPOSITION, 30}, {tpSCAN, 70}, {tpSPRAY, 70}, {tpSIZE, 96}, {tpDENSITY, 116}, {tpJITTER, 25}, {tpREVERSE, 45}, {tpSPREAD, 120},
             {tpATTACK, 100}, {tpFILTER, 60}, {tpVOLUME, 100}, {tpREVERB, 90}});
    b.row(0, "x...............................................................");
    b.lock(0, 32, tpPOSITION, 90);
    b.mat(1, 7); b.mode(1, gmClassic);
    b.tp(1, {{tpPOSITION, 50}, {tpSCAN, 66}, {tpSIZE, 108}, {tpDENSITY, 90}, {tpSPRAY, 30}, {tpSPREAD, 90}, {tpPITCH, 64 - 32},
             {tpATTACK, 110}, {tpVOLUME, 72}, {tpREVERB, 60}});
    b.row(1, "x...............................................................");
    b.mute(2); b.mute(3);
    b.length(64);
    b.glob({{gREV_SIZE, 118}, {gREV_TONE, 66}, {gTEMPO, 70}});
    b.lfo(0, 0, 0, 1, true, -22, tpFILTER); b.lfo(0, 1, 1, 30, false, 25, tpSPRAY);
}

static void pFrozenDrone(PB& b)
{
    b.mat(0, 7); b.mode(0, gmClassic); b.legato(0);
    b.tp(0, {{tpPOSITION, 60}, {tpSCAN, 64}, {tpSPRAY, 55}, {tpSIZE, 110}, {tpDENSITY, 127}, {tpJITTER, 22}, {tpSPREAD, 120},
             {tpATTACK, 105}, {tpVOLUME, 96}, {tpREVERB, 70}});
    b.row(0, "x...............................................................");
    b.lock(0, 32, tpPOSITION, 20);
    b.mat(1, 4); b.mode(1, gmCloud);
    b.tp(1, {{tpPOSITION, 40}, {tpSCAN, 72}, {tpSPRAY, 100}, {tpSIZE, 60}, {tpDENSITY, 104}, {tpSPREAD, 127}, {tpFILTER, 84},
             {tpATTACK, 90}, {tpVOLUME, 64}, {tpREVERB, 70}});
    b.row(1, "x...............................................................");
    b.mute(2); b.mute(3);
    b.length(64);
    b.glob({{gREV_SIZE, 115}, {gTEMPO, 72}});
    b.lfo(0, 0, 1, 12, false, 14, tpPOSITION); b.lfo(1, 0, 5, 9, true, 35, tpPITCH);
}

static void pShimmerFreeze(PB& b)
{
    b.mat(0, 1); b.mode(0, gmClassic); b.legato(0);
    b.tp(0, {{tpSCAN, 64}, {tpPOSITION, 70}, {tpSIZE, 108}, {tpDENSITY, 127}, {tpSPRAY, 60}, {tpOCTAVE, 45}, {tpJITTER, 18}, {tpSPREAD, 120},
             {tpATTACK, 100}, {tpFILTER, 74}, {tpREVERB, 110}, {tpVOLUME, 84}});
    b.row(0, "x...............................");
    b.mat(1, 3); b.mode(1, gmCloud);
    b.tp(1, {{tpSCAN, 64}, {tpPOSITION, 50}, {tpSIZE, 90}, {tpDENSITY, 116}, {tpSPRAY, 80}, {tpOCTAVE, 55}, {tpSPREAD, 125}, {tpPITCH, 64 + 32},
             {tpATTACK, 110}, {tpVOLUME, 60}, {tpREVERB, 120}});
    b.row(1, "................x...............");
    b.mute(2); b.mute(3);
    b.length(32);
    b.glob({{gREV_SIZE, 122}, {gREV_TONE, 92}, {gTEMPO, 76}});
}

// ---- grain rain: you hear the grains themselves, scattered and sparkling
static void pBellRain(PB& b)
{
    b.mat(0, 2); b.mode(0, gmCloud); b.legato(0);
    b.tp(0, {{tpSCAN, 70}, {tpSIZE, 38}, {tpDENSITY, 116}, {tpSPRAY, 95}, {tpSHAPE, 90}, {tpJITTER, 30}, {tpREVERSE, 30}, {tpOCTAVE, 28},
             {tpSPREAD, 127}, {tpVOLUME, 92}, {tpDELAY, 50}, {tpREVERB, 70}});
    b.row(0, "x.......x.......x.......x.......");
    b.pitchLock(0, 8, 3); b.pitchLock(0, 16, -2); b.pitchLock(0, 24, 5);
    b.mat(1, 1); b.mode(1, gmCloud); b.legato(1);
    b.tp(1, {{tpSCAN, 68}, {tpSIZE, 100}, {tpDENSITY, 110}, {tpSPRAY, 60}, {tpJITTER, 20}, {tpSPREAD, 110}, {tpFILTER, 56},
             {tpATTACK, 100}, {tpVOLUME, 72}, {tpREVERB, 80}});
    b.row(1, "x...............................");
    b.mute(2); b.mute(3);
    b.length(32);
    b.glob({{gREV_SIZE, 100}, {gDLY_TIME, 104}, {gDLY_FB, 70}, {gTEMPO, 92}});
    b.lfo(0, 0, 5, 9, true, 40, tpPAN); b.lfo(0, 1, 1, 3, true, 30, tpSIZE);
}

static void pRainWindow(PB& b)
{
    b.mat(0, 4); b.mode(0, gmCloud); b.legato(0);
    b.tp(0, {{tpSCAN, 66}, {tpSIZE, 30}, {tpDENSITY, 120}, {tpSPRAY, 110}, {tpSHAPE, 96}, {tpJITTER, 40}, {tpSPREAD, 127},
             {tpATTACK, 60}, {tpVOLUME, 104}, {tpREVERB, 60}});
    b.row(0, "x...............................");
    b.mat(1, 5); b.mode(1, gmCloud);
    b.tp(1, {{tpSCAN, 64}, {tpPOSITION, 30}, {tpSIZE, 44}, {tpDENSITY, 76}, {tpSPRAY, 120}, {tpSHAPE, 96}, {tpJITTER, 25}, {tpREVERSE, 35}, {tpOCTAVE, 20},
             {tpSPREAD, 127}, {tpDECAY, 110}, {tpSUSTAIN, 0}, {tpVOLUME, 100}, {tpDELAY, 55}, {tpREVERB, 70}});
    b.row(1, "x...x...x...x...x...x...x...x...");
    b.lock(1, 8, tpPOSITION, 90); b.lock(1, 20, tpPOSITION, 60);
    b.mat(2, 1); b.mode(2, gmClassic); b.legato(2);
    b.tp(2, {{tpSCAN, 68}, {tpSIZE, 100}, {tpDENSITY, 100}, {tpSPRAY, 40}, {tpSPREAD, 110}, {tpFILTER, 50}, {tpATTACK, 100}, {tpVOLUME, 62}, {tpREVERB, 80}});
    b.row(2, "x...............................");
    b.mute(3);
    b.length(32);
    b.glob({{gREV_SIZE, 104}, {gDLY_TIME, 104}, {gTEMPO, 84}});
}

static void pMusicBoxDust(PB& b)
{
    b.mat(0, 5); b.mode(0, gmCloud); b.legato(0);
    b.tp(0, {{tpSCAN, 72}, {tpSIZE, 52}, {tpDENSITY, 84}, {tpSPRAY, 80}, {tpSHAPE, 88}, {tpJITTER, 30}, {tpREVERSE, 25}, {tpOCTAVE, 35},
             {tpSPREAD, 127}, {tpVOLUME, 98}, {tpDELAY, 60}, {tpREVERB, 75}});
    b.row(0, "x...............x...............");
    b.lock(0, 16, tpPOSITION, 64);
    b.mat(1, 8); b.mode(1, gmClassic); b.legato(1);
    b.tp(1, {{tpSCAN, 66}, {tpSIZE, 96}, {tpDENSITY, 90}, {tpSPRAY, 30}, {tpSPREAD, 90}, {tpFILTER, 54}, {tpATTACK, 90}, {tpVOLUME, 74}, {tpREVERB, 70}});
    b.row(1, "x.......x.......x.......x.......");
    for (int s = 0; s < 4; ++s) b.lock(1, s * 8, tpPOSITION, (float)(8 + s * 32));
    b.mute(2); b.mute(3);
    b.length(32);
    b.glob({{gREV_SIZE, 100}, {gDLY_TIME, 104}, {gDLY_FB, 64}, {gTEMPO, 88}});
}

static void pParticleKeys(PB& b)
{
    b.mat(0, 8); b.mode(0, gmCloud); b.legato(0);
    b.tp(0, {{tpSCAN, 70}, {tpSIZE, 34}, {tpDENSITY, 122}, {tpSPRAY, 70}, {tpSHAPE, 80}, {tpJITTER, 25}, {tpREVERSE, 40}, {tpOCTAVE, 25},
             {tpSPREAD, 127}, {tpVOLUME, 92}, {tpDELAY, 40}, {tpREVERB, 70}});
    b.row(0, "x.......x.......x.......x.......");
    for (int s = 0; s < 4; ++s) b.lock(0, s * 8, tpPOSITION, (float)(8 + s * 32));
    b.mat(1, 7); b.mode(1, gmClassic); b.legato(1);
    b.tp(1, {{tpSCAN, 66}, {tpSIZE, 104}, {tpDENSITY, 80}, {tpSPREAD, 80}, {tpPITCH, 64 - 32}, {tpFILTER, 50}, {tpATTACK, 100}, {tpVOLUME, 70}, {tpREVERB, 50}});
    b.row(1, "x.......x.......x.......x.......");
    b.pitchLock(1, 8, 5); b.pitchLock(1, 16, -4); b.pitchLock(1, 24, -5);
    b.mute(2); b.mute(3);
    b.length(32);
    b.glob({{gREV_SIZE, 96}, {gDLY_TIME, 104}, {gTEMPO, 96}});
    b.lfo(0, 0, 1, 3, true, 30, tpSPRAY); b.lfo(0, 1, 5, 11, true, 25, tpPMIX);
}

// ---- rhythmic glitch: pieces repeated, reversed, transposed, on the tempo
static void pBeatShuffle(PB& b)
{
    b.mat(0, 6); b.mode(0, gmLoop, true);
    const int n = b.slices(0);
    b.tp(0, {{tpSCAN, 96}, {tpSIZE, PB::syncSize(4)}, {tpSHAPE, 6}, {tpSPREAD, 20}, {tpVOLUME, 76}, {tpREVERB, 12}});
    b.row(0, "xxxxxxxxxxxxxxxx");
    static const int order[16] = {0, 1, 2, 1, 4, 3, 0, 5, 6, 1, 2, 7, 8, 9, 4, 3};
    for (int s = 0; s < 16; ++s) b.sliceLock(0, s, order[s] % std::max(1, n));
    b.lock(0, 7, tpSIZE, PB::syncSize(2)); b.lock(0, 7, tpSCAN, 64);            // a 1/32 stutter
    b.lock(0, 11, tpREVERSE, 127);
    b.pitchLock(0, 13, 7); b.pitchLock(0, 15, -5);
    b.mat(1, 5); b.mode(1, gmLoop, true);
    b.tp(1, {{tpSCAN, 96}, {tpSIZE, PB::syncSize(6)}, {tpSHAPE, 20}, {tpSPREAD, 70}, {tpDECAY, 90}, {tpSUSTAIN, 0}, {tpVOLUME, 78}, {tpDELAY, 40}, {tpREVERB, 30}});
    b.row(1, "x.....x.....x...");
    b.lock(1, 6, tpPOSITION, 64); b.lock(1, 12, tpREVERSE, 127);
    b.mute(2); b.mute(3);
    b.glob({{gTEMPO, 120}, {gDLY_TIME, 88}});
}

static void pStutterSlices(PB& b)
{
    b.mat(0, 6, 1, 16); b.mode(0, gmLoop, true);
    b.tp(0, {{tpSCAN, 64}, {tpSIZE, PB::syncSize(2)}, {tpSHAPE, 8}, {tpSPREAD, 15}, {tpDECAY, 66}, {tpSUSTAIN, 0}, {tpVOLUME, 98}});
    b.row(0, "xxx.xx.xxx.x.xxx");
    static const int sl[16] = {0, 0, 0, 0, 4, 4, 0, 8, 9, 10, 0, 12, 0, 14, 14, 15};
    for (int s = 0; s < 16; ++s) b.sliceLock(0, s, sl[s]);
    for (int s = 8; s < 11; ++s) b.lock(0, s, tpSIZE, PB::syncSize(s - 8));
    b.lock(0, 13, tpDENSITY, 127); b.lock(0, 13, tpJITTER, 60);
    b.pitchLock(0, 15, 12);
    b.mat(1, 8); b.mode(1, gmLoop, true);
    b.tp(1, {{tpSCAN, 64}, {tpSIZE, PB::syncSize(4)}, {tpSHAPE, 14}, {tpSPREAD, 80}, {tpDECAY, 50}, {tpSUSTAIN, 0}, {tpFILTER, 80}, {tpVOLUME, 76}, {tpDELAY, 50}, {tpREVERB, 30}});
    b.row(1, "..x...x...x..x.x");
    b.lock(1, 6, tpPOSITION, 32); b.lock(1, 10, tpPOSITION, 64); b.lock(1, 13, tpPOSITION, 96); b.lock(1, 15, tpREVERSE, 127);
    b.mute(2); b.mute(3);
    b.glob({{gTEMPO, 128}, {gDLY_TIME, 72}});
}

static void pGridCloud(PB& b)
{
    b.mat(0, 5); b.mode(0, gmCloud, true);
    b.tp(0, {{tpSCAN, 80}, {tpSIZE, PB::syncSize(4)}, {tpDENSITY, 24}, {tpSHAPE, 92}, {tpSPRAY, 30}, {tpOCTAVE, 20}, {tpSPREAD, 100},  
             {tpDELAY, 40}, {tpREVERB, 40}, {tpVOLUME, 80}});
    b.row(0, "x.......x.......");
    b.pitchLock(0, 8, 5);
    b.mat(1, 2); b.mode(1, gmCloud, true);
    b.tp(1, {{tpSCAN, 64}, {tpSIZE, PB::syncSize(6)}, {tpDENSITY, 40}, {tpSHAPE, 100}, {tpOCTAVE, 30}, {tpSPREAD, 110}, {tpVOLUME, 70}, {tpDELAY, 60}, {tpREVERB, 60}});
    b.row(1, "x...............");
    b.mat(2, 6); b.mode(2, gmLoop, true);
    b.tp(2, {{tpSCAN, 96}, {tpSIZE, PB::syncSize(4)}, {tpSHAPE, 6}, {tpSPREAD, 20}, {tpVOLUME, 70}, {tpREVERB, 12}});
    b.row(2, "x...x...x...x..x");
    b.mute(3);
    b.glob({{gTEMPO, 120}, {gDLY_TIME, 104}});
}

static void pBrokenArp(PB& b)
{
    b.mat(0, 5); b.mode(0, gmLoop, true);
    b.tp(0, {{tpSCAN, 96}, {tpSIZE, PB::syncSize(4)}, {tpSHAPE, 12}, {tpSPREAD, 90}, {tpVOLUME, 80}, {tpDELAY, 45}, {tpREVERB, 40}});
    b.row(0, "xxxxxxxxxxxxxxxx");
    static const int rev[16] = {0, 0, 0, 1, 0, 0, 1, 0, 0, 0, 0, 1, 0, 1, 0, 0};
    for (int s = 0; s < 16; ++s) {
        if (rev[s]) b.lock(0, s, tpREVERSE, 127);
        if (s % 4 == 3) b.lock(0, s, tpSCAN, 64);
    }
    b.pitchLock(0, 7, 12); b.pitchLock(0, 14, 12); b.lock(0, 10, tpSIZE, PB::syncSize(2));
    b.mat(1, 6); b.mode(1, gmLoop, true);
    b.tp(1, {{tpSCAN, 96}, {tpSIZE, PB::syncSize(6)}, {tpSHAPE, 6}, {tpSPREAD, 15}, {tpVOLUME, 76}, {tpREVERB, 10}});
    b.row(1, "x.......x.......");
    b.mat(2, 7); b.mode(2, gmClassic); b.legato(2);
    b.tp(2, {{tpSCAN, 66}, {tpSIZE, 104}, {tpDENSITY, 90}, {tpSPREAD, 90}, {tpPITCH, 64 - 32}, {tpFILTER, 54}, {tpATTACK, 90}, {tpVOLUME, 66}, {tpREVERB, 50}});
    b.row(2, "x...............................");
    b.pitchLock(2, 16, -4);
    b.length(32);
    b.mute(3);
    b.glob({{gTEMPO, 116}, {gDLY_TIME, 88}, {gDLY_FB, 50}});
}

// ---- grain synth: chords and lines played from the material, as an instrument
static void pGrainKeys(PB& b)
{
    // KEYS frozen on one chord at a time (POSITION picks Cm9, Fm9, Abmaj7, Gm7), the drone follows the roots
    b.mat(0, 8); b.mode(0, gmClassic); b.legato(0);
    b.tp(0, {{tpSCAN, 66}, {tpSIZE, 92}, {tpDENSITY, 100}, {tpSPRAY, 25}, {tpJITTER, 15}, {tpSPREAD, 100}, {tpATTACK, 60},
             {tpVOLUME, 96}, {tpDELAY, 25}, {tpREVERB, 60}});
    b.row(0, "x...............x...............x...............x...............");
    for (int s = 0; s < 4; ++s) b.lock(0, s * 16, tpPOSITION, (float)(6 + s * 32));
    b.mat(1, 7); b.mode(1, gmClassic); b.legato(1);
    b.tp(1, {{tpSCAN, 66}, {tpSIZE, 100}, {tpDENSITY, 90}, {tpSPREAD, 80}, {tpPITCH, 64 - 32}, {tpFILTER, 52}, {tpATTACK, 90}, {tpVOLUME, 70}, {tpREVERB, 40}});
    b.row(1, "x...............x...............x...............x...............");
    b.pitchLock(1, 16, 5); b.pitchLock(1, 32, -4); b.pitchLock(1, 48, -5);
    b.mat(2, 2); b.mode(2, gmCloud);
    b.tp(2, {{tpSCAN, 70}, {tpSIZE, 36}, {tpDENSITY, 80}, {tpSPRAY, 100}, {tpSHAPE, 90}, {tpOCTAVE, 30}, {tpSPREAD, 127}, {tpVOLUME, 60}, {tpDELAY, 60}, {tpREVERB, 70}});
    b.row(2, "x...............................................................");
    b.mute(3);
    b.length(64);
    b.glob({{gREV_SIZE, 100}, {gDLY_TIME, 104}, {gTEMPO, 90}});
}

static void pChoirChords(PB& b)
{
    // Cm | Ab | Fm | Bb from the choir's two chords (POSITION) and PITCH locks
    b.mat(0, 3); b.mode(0, gmClassic); b.legato(0);
    b.tp(0, {{tpSCAN, 66}, {tpSIZE, 98}, {tpDENSITY, 110}, {tpSPRAY, 35}, {tpJITTER, 20}, {tpSPREAD, 110}, {tpATTACK, 80}, {tpVOLUME, 98}, {tpREVERB, 85}});
    b.row(0, "x...............x...............x...............x...............");
    b.lock(0, 0, tpPOSITION, 20);
    b.lock(0, 16, tpPOSITION, 90); b.pitchLock(0, 16, -2);
    b.lock(0, 32, tpPOSITION, 20); b.pitchLock(0, 32, 5);
    b.lock(0, 48, tpPOSITION, 90);
    b.mat(1, 1); b.mode(1, gmCloud); b.legato(1);
    b.tp(1, {{tpSCAN, 68}, {tpSIZE, 100}, {tpDENSITY, 110}, {tpSPRAY, 60}, {tpSPREAD, 120}, {tpFILTER, 56}, {tpATTACK, 100}, {tpVOLUME, 64}, {tpREVERB, 80}});
    b.row(1, "x...............................................................");
    b.mute(2); b.mute(3);
    b.length(64);
    b.glob({{gREV_SIZE, 112}, {gTEMPO, 80}});
}

static void pPluckLines(PB& b)
{
    // a melody played on single harp grains: PITCH locks in the scale, short LOOP passes of one note
    b.mat(0, 5); b.mode(0, gmClassic);
    b.tp(0, {{tpPOSITION, 0}, {tpSCAN, 64}, {tpSIZE, 70}, {tpDENSITY, 60}, {tpSPRAY, 10}, {tpSPREAD, 70}, {tpATTACK, 0}, {tpDECAY, 80}, {tpSUSTAIN, 0},
             {tpVOLUME, 120}, {tpDELAY, 55}, {tpREVERB, 60}});
    b.row(0, "x..x..x.x..x.x..x..x..x.x.xx.x..");
    static const int mel[32] = {0, 0, 0, 3, 0, 0, 7, 0, 5, 0, 0, 3, 0, 2, 0, 0, 0, 0, 0, 3, 0, 0, 10, 0, 8, 0, 7, 5, 0, 3, 0, 0};
    for (int s = 0; s < 32; ++s) if (mel[s]) b.pitchLock(0, s, mel[s]);
    b.mat(1, 1); b.mode(1, gmCloud); b.legato(1);
    b.tp(1, {{tpSCAN, 68}, {tpSIZE, 100}, {tpDENSITY, 110}, {tpSPRAY, 60}, {tpSPREAD, 120}, {tpFILTER, 58}, {tpATTACK, 100}, {tpVOLUME, 66}, {tpREVERB, 80}});
    b.row(1, "x...............x...............");
    b.lock(1, 16, tpPOSITION, 100);
    b.mat(2, 6); b.mode(2, gmLoop, true);
    b.tp(2, {{tpSCAN, 96}, {tpSIZE, PB::syncSize(4)}, {tpSHAPE, 6}, {tpFILTER, 86}, {tpSPREAD, 30}, {tpVOLUME, 70}, {tpREVERB, 20}});
    b.row(2, "x...x...x...x...");
    b.mute(3);
    b.length(32);
    b.glob({{gREV_SIZE, 98}, {gDLY_TIME, 104}, {gDLY_FB, 56}, {gTEMPO, 100}});
}

static void pAmbientFour(PB& b)
{
    static const int mats[4] = {1, 2, 3, 7};
    static const int modes[4] = {gmCloud, gmCloud, gmClassic, gmClassic};
    static const char* rows[4] = {"x...............", "....x.........", "........x.......................", "x....................."};
    for (int t = 0; t < 4; ++t) {
        b.mat(t, mats[t]); b.mode(t, modes[t]); b.legato(t);
        b.tp(t, {{tpSCAN, (float)(66 + t * 2)}, {tpPOSITION, (float)(t * 25)}, {tpSIZE, t == 1 ? 46.f : 96.f}, {tpDENSITY, modes[t] == gmCloud ? 112.f : 96.f},
                 {tpSPRAY, 60}, {tpJITTER, 20}, {tpSPREAD, 115}, {tpATTACK, 95}, {tpDECAY, 100}, {tpSUSTAIN, 70}, {tpVOLUME, 94}, {tpPAN, (float)(30 + t * 22)},
                 {tpREVERB, 90}, {tpDELAY, t == 1 ? 55.f : 0.f}});
        b.row(t, rows[t]);
    }
    b.tp(1, {{tpOCTAVE, 30}, {tpREVERSE, 30}});
    b.tp(3, {{tpPITCH, 64 - 32}});
    b.length(32);
    b.glob({{gREV_SIZE, 116}, {gDLY_TIME, 104}, {gTEMPO, 80}});
}

// ---- polyphony: chords played by the sequencer on one frozen harp note
static void pGrainChords(PB& b)
{
    b.mat(0, 5); b.mode(0, gmClassic);
    b.tp(0, {{tpSLICE, PB::sliceKnobOf(b, 0, 0)}, {tpEND, 70}, {tpPOSITION, 10}, {tpSCAN, 64}, {tpSIZE, 88}, {tpDENSITY, 110}, {tpSPRAY, 18}, {tpJITTER, 22},
             {tpSPREAD, 115}, {tpATTACK, 70}, {tpDECAY, 90}, {tpSUSTAIN, 100}, {tpRELEASE, 85}, {tpFILTER, 50}, {tpFLTENV, 96}, {tpVOLUME, 90},
             {tpDELAY, 25}, {tpREVERB, 75}});
    b.row(0, "x...............x...............x...............x...............");
    b.chord(0, 0, 5, 0); b.chord(0, 16, 4, -4); b.chord(0, 32, 5, 5); b.chord(0, 48, 5, -5);   // Cm7 Abmaj7 Fm7 Gm7
    b.lfo(0, 0, 1, 2, true, 12, tpPOSITION); b.lfo(0, 1, 0, 30, false, 20, tpPMIX);
    b.mat(1, 2); b.mode(1, gmCloud);
    b.tp(1, {{tpSCAN, 70}, {tpSIZE, 36}, {tpDENSITY, 110}, {tpSPRAY, 100}, {tpSHAPE, 90}, {tpOCTAVE, 30}, {tpPMIX, 80}, {tpSPREAD, 127},
             {tpVOLUME, 58}, {tpDELAY, 60}, {tpREVERB, 70}});
    b.row(1, "x...............................................................");
    b.mute(2); b.mute(3);
    b.length(64);
    b.glob({{gREV_SIZE, 106}, {gDLY_TIME, 104}, {gCHO_MIX, 50}, {gTEMPO, 84}});
}

static void pLiveGranulator(PB& b)
{
    // play something into the input (side-chain): track 1 clouds it, track 2 loops it on the beat, track 3 freezes a shimmer
    b.b.kit[0].material = kMatLive; b.b.kit[0].nSlices = 0; b.mode(0, gmCloud); b.legato(0);
    b.tp(0, {{tpSCAN, 96}, {tpPOSITION, 90}, {tpSIZE, 80}, {tpDENSITY, 108}, {tpSPRAY, 50}, {tpJITTER, 20}, {tpSPREAD, 115}, {tpREVERB, 60}, {tpVOLUME, 90}});
    b.row(0, "x...............");
    b.b.kit[1].material = kMatLive; b.b.kit[1].nSlices = 0; b.mode(1, gmLoop, true);
    b.tp(1, {{tpSCAN, 64}, {tpPOSITION, 110}, {tpSIZE, PB::syncSize(4)}, {tpSHAPE, 10}, {tpDECAY, 70}, {tpSUSTAIN, 0}, {tpSPREAD, 60}, {tpVOLUME, 100}, {tpDELAY, 40}});
    b.row(1, "....x.......x.x.");
    b.b.kit[2].material = kMatLive; b.b.kit[2].nSlices = 0; b.mode(2, gmClassic);
    b.tp(2, {{tpSCAN, 64}, {tpPOSITION, 60}, {tpSIZE, 105}, {tpDENSITY, 127}, {tpOCTAVE, 50}, {tpSPREAD, 120}, {tpATTACK, 100}, {tpVOLUME, 70}, {tpREVERB, 110}});
    b.row(2, "x...............................");
    b.mute(2); b.mute(3);
    b.length(32);
    b.glob({{gCAPLEN, 1}, {gTEMPO, 120}});
}

struct PresetDef { const char* name; const char* desc; void (*build)(PB&); };
static const PresetDef kPresets[] = {
    {"Init",               "Four kinds of granular sound, empty sequencer",    pInit},
    {"Dissolving Strings", "A pad melting into a cloud, a frozen choir",       pDissolvingStrings},
    {"Choir Fog",          "A choir scattered backwards over a drone",         pChoirFog},
    {"Frozen Drone",       "A frozen drone under a rain of grains",            pFrozenDrone},
    {"Shimmer Freeze",     "A frozen pad with octave grains, a choir above",   pShimmerFreeze},
    {"Bell Rain",          "Tiny bell grains everywhere, a soft pad below",    pBellRain},
    {"Rain Window",        "Rain, harp grains and a distant pad",              pRainWindow},
    {"Music Box Dust",     "Harp grains in the echo over sequenced chords",    pMusicBoxDust},
    {"Particle Keys",      "Piano chords broken into particles",               pParticleKeys},
    {"Beat Shuffle",       "The beat re-ordered slice by slice, harp loops",   pBeatShuffle},
    {"Stutter Slices",     "Stutters and repeats on the beat and the keys",    pStutterSlices},
    {"Grid Cloud",         "Clouds whose grains fall on the 16ths",            pGridCloud},
    {"Broken Arp",         "The harp arpeggio chopped, reversed, transposed",  pBrokenArp},
    {"Grain Keys",         "Frozen piano chords and a drone following them",   pGrainKeys},
    {"Choir Chords",       "Cm, Ab, Fm, Bb made from the choir",               pChoirChords},
    {"Pluck Lines",        "A melody on harp grains, pad and beat",            pPluckLines},
    {"Ambient Four",       "Four tracks of different lengths drifting",        pAmbientFour},
    {"Grain Chords",       "Chords on a frozen harp note, a bell cloud",       pGrainChords},
    {"Live Granulator",    "Three tracks on the input: cloud, loop, freeze",   pLiveGranulator},
};
static constexpr int kNumPresets = (int)(sizeof(kPresets) / sizeof(kPresets[0]));

inline void buildPreset(int i, float* values, Bank& bank)
{
    PB b(values, bank);
    if (i >= 0 && i < kNumPresets) kPresets[i].build(b);
}

} // namespace kn
