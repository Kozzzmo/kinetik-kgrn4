// Kinetik KGRN-4 — factory presets (built in code on the built-in materials) and the kit helpers
#pragma once
#include "Bank.hpp"
#include "Materials.hpp"
#include "Slicer.hpp"
#include <vector>
#include <cstring>
#include <string>

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
                    + " " + std::to_string(k.mode) + " " + std::to_string(k.sync) + " " + std::to_string(k.legato) + " " + std::to_string(k.solo) + " " + std::to_string(k.beats) + "\n";
    s += "X " + std::to_string(t) + " " + std::to_string(k.nSlices);
    for (int i = 0; i < k.nSlices; ++i) s += " " + std::to_string(k.slice[i]);
    return s;
}

// the kit of a new instance (and of Init): four built-in materials, sliced at their transients
inline void defaultKits(Bank& b)
{
    // PAD as a cloud, KEYS as a grain synth (CLASSIC), BEAT in loops on the 16ths, BELLS as a grain rain
    static const int defMat[kTracks] = {1, 8, 6, 2}, defMode[kTracks] = {gmCloud, gmClassic, gmLoop, gmCloud};
    static const uint8_t defSync[kTracks] = {0, 0, 1, 0}, defLegato[kTracks] = {0, 0, 0, 0};
    for (int t = 0; t < kTracks; ++t) {
        Kit& k = b.kit[t]; k = Kit(); k.material = (uint8_t)defMat[t]; k.mode = (uint8_t)defMode[t]; k.sync = defSync[t]; k.legato = defLegato[t];
        k.beats = kMaterialBeats[k.material];
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
        k.beats = kMaterialBeats[m];
        sliceKit(k, material(m).get());
    }
    static float sliceKnobOf(PB& b, int t, int s) { return sliceKnob(s, b.slices(t)); }
    int slices(int t) const { return b.kit[t].nSlices; }
    // kOldFilter: the bipolar FILTER the presets were voiced with (left = low pass, right = high pass) -> TYPE + CUTOFF
    static constexpr int kOldFilter = 1000;
    void tp(int t, std::initializer_list<std::pair<int, float>> kv)
    {
        for (auto& p : kv) {
            if (p.first == kOldFilter) { int ty, cu; convertFilter05((int)std::lround(p.second), ty, cu); v[tpIndex(t, tpFTYPE)] = (float)ty; v[tpIndex(t, tpCUTOFF)] = (float)cu; }
            // DELAY / REVERB were voiced on squared sends: the same send level on the linear ones of 0.7.1
            else if (p.first == tpDELAY || p.first == tpREVERB) v[tpIndex(t, p.first)] = std::round(p.second * p.second / (127.f * 1.4f));
            else v[tpIndex(t, p.first)] = p.second;
        }
    }
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

// ---- 0.8 factory presets: 4-bar pieces (64 steps) on the built-in materials, all in C minor. They use the step notes,
// chords, gates, ratchets and conditions, the NOTES page (grains forced into a scale), the FILTER page, the ARP, the three
// LFOs, the echo / reverb / chorus / CHAOS sends and, where it helps, a second pattern in a CHAIN. Track levels are
// balanced with the mix checker (`mix`), every preset plays at about the same loudness (`PresetDef::trimDb`).
struct PB2 {
    PB& p;
    explicit PB2(PB& b) : p(b) {}
    Pattern& P(int i) { return p.b.patterns[i]; }
    void set(int t, std::initializer_list<std::pair<int, float>> kv) { for (auto& x : kv) p.v[tpIndex(t, x.first)] = x.second; }
    // steps: 'x' = note (gate HOLD = until the next one, unless `gates` says otherwise), '.' = rest; spaces / '|' ignored
    void row(int i, int t, const std::string& s, int gate = kGateHold, int speed = 0)
    {
        int st = 0;
        for (char c : s) {
            if (c == ' ' || c == '|') continue;
            if (st >= kSteps) break;
            P(i).on[t][st] = c == 'x' || c == 'X'; P(i).gate[t][st] = (uint8_t)gate; ++st;
        }
        P(i).len[t] = (uint8_t)std::max(1, st); P(i).speed[t] = (uint8_t)speed;
    }
    // the notes of the lit steps, in order (semitones from the sample's pitch), and chords (kChords index, 0 = one note)
    void notes(int i, int t, std::initializer_list<int> semis, std::initializer_list<int> chords = {})
    {
        int k = 0, c = 0; const int n = (int)semis.size(), nc = (int)chords.size();
        for (int s = 0; s < P(i).len[t]; ++s) if (P(i).on[t][s]) {
            P(i).note[t][s] = (uint8_t)(kBaseNote + semis.begin()[k++ % n]);
            if (nc) P(i).chord[t][s] = (uint8_t)chords.begin()[c++ % nc];
        }
    }
    void cyc(int i, int t, int k, std::initializer_list<int> vals)
    {
        int c = 0; const int n = (int)vals.size();
        for (int s = 0; s < P(i).len[t]; ++s) if (P(i).on[t][s]) P(i).locks[t][s][k] = (uint8_t)clampi(vals.begin()[c++ % n], 0, 127);
    }
    void lock(int i, int t, int s, int k, float v) { P(i).locks[t][s][k] = (uint8_t)clampi((int)std::lround(v), 0, 127); }
    void cond(int i, int t, std::initializer_list<int> steps, int c) { for (int s : steps) P(i).cond[t][s] = (uint8_t)c; }
    void ratch(int i, int t, int s, int r) { P(i).ratch[t][s] = (uint8_t)r; }
    void gate(int i, int t, int s, int g) { P(i).gate[t][s] = (uint8_t)g; }
    void length(int i, int n, int swing = 50) { P(i).length = (uint8_t)n; P(i).swing = (uint8_t)swing; }
    void copy(int src, int dst) { p.b.patterns[dst] = p.b.patterns[src]; }
    void chain(const char* spec) { p.b.chain.parse(spec, kPatterns); }
    void flt(int t, int type, int cut, int reso, int env = 64, int fatk = 0, int fdec = 60)
    {
        set(t, {{tpFTYPE, binKnob(type, kFilterTypes)}, {tpCUTOFF, (float)cut}, {tpRESO, (float)reso}, {tpFLTENV, (float)env}, {tpFATK, (float)fatk}, {tpFDEC, (float)fdec}});
    }
    void sends(int t, int del, int rev, int cho = 0, int chaos = 0) { set(t, {{tpDELAY, (float)del}, {tpREVERB, (float)rev}, {tpCHORUS, (float)cho}, {tpCHAOS, (float)chaos}}); }
    // NOTES: grains forced into a scale (kScaleNames index), chance, range in octaves, order (kNOrderNames)
    void scaleNotes(int t, int scale, int amount, int octaves, int order, int step = 1)
    {
        set(t, {{tpNSCALE, binKnob(scale + 1, kScales + 1)}, {tpNROOT, binKnob(0, 13)}, {tpNAMOUNT, (float)amount},
                {tpNRANGE, binKnob(octaves - 1, 4)}, {tpNORDER, binKnob(order, kNOrders)}, {tpNSTEP, binKnob(step - 1, 8)}});
    }
    void arp(int t, int mode, int rate, int oct, int gate = 60, int swing = 0)
    {
        set(t, {{tpAMODE, binKnob(mode, kArpModes)}, {tpARATE, binKnob(rate, kArpRates)}, {tpAOCT, binKnob(oct - 1, 4)}, {tpAGATE, (float)gate}, {tpASWING, (float)swing}});
    }
    void lfo(int t, int i, int wave, int syncDiv, int depth, int dest, bool retrig = false) { p.lfo(t, i, wave, (float)syncDiv, true, depth, dest, retrig); }
    void lfoFree(int t, int i, int wave, int rate, int depth, int dest) { p.lfo(t, i, wave, (float)rate, false, depth, dest); }
    void mix(std::initializer_list<float> db)
    {
        int t = 0;
        for (float d : db) {
            const float k = std::pow(10.f, d / 40.f);
            float& v = p.v[tpIndex(t, tpVOLUME)]; v = std::min(127.f, std::round(v * k));
            for (auto& Pt : p.b.patterns) for (int s = 0; s < kSteps; ++s) { uint8_t& l = Pt.locks[t][s][tpVOLUME]; if (l != kNoLock) l = (uint8_t)std::min(127.f, std::round(l * k)); }
            if (++t >= kTracks) break;
        }
    }
};
// sync size bins: 0 1/64, 1 1/32T, 2 1/32, 3 1/16T, 4 1/16, 5 1/8T, 6 1/8, 7 1/4T, 8 1/4, 9 1/2, 10 1 BAR, 11 2 BARS
// LFO sync: 0 8 BARS, 1 4 BARS, 2 2 BARS, 3 1 BAR, 4 1/2, 5 1/4D, 6 1/4, 7 1/4T, 8 1/8D, 9 1/8, 10 1/8T, 11 1/16 ...
// scales (NOTES): 0 MAJOR 1 MINOR 2 DORIAN 3 PHRYGIAN 4 HARM MINOR 5 MAJOR PENTA 6 MINOR PENTA 7 BLUES
inline std::string gBars(std::initializer_list<const char*> bs) { std::string s; for (auto x : bs) s += x; return s; }
inline std::string gRep(const char* bar, int n) { std::string s; for (int i = 0; i < n; ++i) s += bar; return s; }
enum { chNote, chMaj, chMin, chSus4, chMaj7, chMin7, chDom7, ch5th, chOct, chDim };
enum { cAlways, c90, c75, c50, c33, c25, c10, cFill, cNotFill, cFirst, cNotFirst, c1of2, c2of2, c1of3, c2of3, c3of3, c1of4, c2of4, c3of4, c4of4 };
static float sz(int bin) { return PB::syncSize(bin); }

static void pInit(PB& b)
{
    for (int t = 0; t < kTracks; ++t) for (int k = 0; k < kTrackParams; ++k) b.v[tpIndex(t, k)] = trackDefault(t, k);
    defaultKits(b.b);
}

// Dissolving Strings 76: the pad melts into a cloud and follows Cm - Ab - Fm - G, the choir holds the top notes,
// the drone doubles the roots; LFO 3 slowly closes the pad filter.
static void pDissolvingStrings(PB& b)
{
    PB2 q(b);
    b.mat(0, 1); b.mode(0, gmCloud);
    q.set(0, {{tpPOSITION, 20}, {tpSCAN, 68}, {tpSPRAY, 50}, {tpSIZE, 100}, {tpDENSITY, 114}, {tpJITTER, 30}, {tpREVERSE, 25}, {tpSPREAD, 115},
              {tpATTACK, 95}, {tpRELEASE, 90}, {tpVOLUME, 92}});
    q.flt(0, ftLP24, 100, 30); q.sends(0, 0, 50, 40);
    q.row(0, 0, gRep("x...............", 4)); q.notes(0, 0, {0, -4, 5, 7}, {chMin, chMaj, chMin, chMaj});
    b.mat(1, 3); b.mode(1, gmClassic);
    q.set(1, {{tpPOSITION, 40}, {tpSCAN, 64}, {tpSPRAY, 40}, {tpSIZE, 104}, {tpDENSITY, 110}, {tpJITTER, 20}, {tpOCTAVE, 18}, {tpSPREAD, 105},
              {tpATTACK, 110}, {tpRELEASE, 100}, {tpVOLUME, 70}});
    q.sends(1, 0, 70); q.flt(1, ftHP12, 30, 20);
    q.row(0, 1, gBars({"........x.......", "........x.......", "........x.......", "........x...x..."})); q.notes(0, 1, {7, 8, 8, 7, 11});
    b.mat(2, 7); b.mode(2, gmClassic);
    q.set(2, {{tpPOSITION, 50}, {tpSCAN, 66}, {tpSIZE, 108}, {tpDENSITY, 90}, {tpSPREAD, 60}, {tpPITCH, 64 - 32}, {tpATTACK, 100}, {tpVOLUME, 70}});
    q.flt(2, ftLP24, 70, 20); q.sends(2, 0, 30);
    q.row(0, 2, gRep("x...............", 4)); q.notes(0, 2, {0, -4, 5, 7});
    q.length(0, 64);
    b.glob({{gREV_SIZE, 110}, {gREV_DAMP, 47}, {gCHO_LEV, 70}, {gTEMPO, 76}});
    q.lfoFree(0, 0, 0, 18, 18, tpPOSITION); q.lfo(0, 2, 1, 1, -20, tpCUTOFF); q.lfo(1, 0, 1, 2, 20, tpPAN);
    b.mute(3);
    q.mix({-3.1f, 3.4f, 8.0f, 0.0f});
}

// Bell Rain 92: bell grains forced into C minor pentatonic (random order, 2 octaves), a soft pad, rain in the back,
// harp drops by chance; B = the bells play the scale upwards.
static void pBellRain(PB& b)
{
    PB2 q(b);
    b.mat(0, 2); b.mode(0, gmCloud);
    q.set(0, {{tpSCAN, 70}, {tpSIZE, 38}, {tpDENSITY, 116}, {tpSPRAY, 95}, {tpSHAPE, 90}, {tpJITTER, 30}, {tpREVERSE, 30}, {tpOCTAVE, 20},
              {tpSPREAD, 127}, {tpVOLUME, 88}});
    q.scaleNotes(0, 6, 110, 2, 0); q.sends(0, 50, 60, 0, 60);
    q.row(0, 0, gRep("x.......x.......", 4)); q.notes(0, 0, {0, 3, -2, 5, 0, 7, 3, -5});
    b.mat(1, 1); b.mode(1, gmCloud);
    q.set(1, {{tpSCAN, 68}, {tpSIZE, 100}, {tpDENSITY, 110}, {tpSPRAY, 60}, {tpJITTER, 20}, {tpSPREAD, 110}, {tpATTACK, 100}, {tpRELEASE, 100}, {tpVOLUME, 72}});
    q.flt(1, ftLP12, 80, 20); q.sends(1, 0, 70);
    q.row(0, 1, gRep("x...............", 4)); q.notes(0, 1, {0, -4, -7, -5}, {chMin7, chMaj7, chMin7, chMin});
    b.mat(2, 4); b.mode(2, gmCloud);
    q.set(2, {{tpSCAN, 66}, {tpSIZE, 30}, {tpDENSITY, 120}, {tpSPRAY, 110}, {tpSHAPE, 96}, {tpJITTER, 40}, {tpSPREAD, 127}, {tpATTACK, 60}, {tpVOLUME, 80}});
    q.flt(2, ftHP12, 60, 10); q.sends(2, 0, 40);
    q.row(0, 2, "x" + std::string(63, '.'));
    b.mat(3, 5); b.mode(3, gmCloud);
    q.set(3, {{tpSCAN, 64}, {tpPOSITION, 30}, {tpSIZE, 44}, {tpDENSITY, 76}, {tpSPRAY, 120}, {tpSHAPE, 96}, {tpSPREAD, 127}, {tpDECAY, 90}, {tpSUSTAIN, 0}, {tpVOLUME, 90}});
    q.sends(3, 60, 50);
    q.row(0, 3, gRep("..x.....x...x...", 4), 4); q.cond(0, 3, {2, 12, 18, 28, 34, 44, 50, 60}, c50);
    q.cyc(0, 3, tpPOSITION, {30, 90, 60, 110});
    q.length(0, 64);
    q.copy(0, 1); q.set(0, {}); 
    for (int s = 0; s < 64; ++s) q.lock(1, 0, s, tpNORDER, binKnob(2, kNOrders));
    q.chain("1 2 0:3 1:1");
    b.glob({{gREV_SIZE, 100}, {gECHO_TIME, 86}, {gECHO_FB, 62}, {gGL_CHANCE, 30}, {gGL_MIX, 70}, {gTEMPO, 92}});
    q.lfo(0, 0, 5, 9, 40, tpPAN); q.lfo(0, 1, 1, 3, 30, tpSIZE); q.lfo(1, 0, 1, 1, 15, tpCUTOFF);
    q.mix({-4.1f, -4.9f, -5.7f, 8.0f});
}

// Frozen Drone 70: the drone frozen (SCAN stopped) moving between two positions, a backwards choir cloud,
// keys particles once a bar by chance.
static void pFrozenDrone(PB& b)
{
    PB2 q(b);
    b.mat(0, 7); b.mode(0, gmClassic);
    q.set(0, {{tpPOSITION, 60}, {tpSCAN, 64}, {tpSPRAY, 55}, {tpSIZE, 110}, {tpDENSITY, 127}, {tpJITTER, 22}, {tpSPREAD, 120},
              {tpATTACK, 105}, {tpRELEASE, 110}, {tpVOLUME, 94}});
    q.flt(0, ftLP24, 84, 40); q.sends(0, 0, 60, 30);
    q.row(0, 0, gBars({"x...............", "................", "x...............", "................"})); q.notes(0, 0, {0, -4});
    q.lock(0, 0, 32, tpPOSITION, 20);
    b.mat(1, 3); b.mode(1, gmCloud);
    q.set(1, {{tpPOSITION, 30}, {tpSCAN, 70}, {tpSPRAY, 70}, {tpSIZE, 96}, {tpDENSITY, 116}, {tpJITTER, 25}, {tpREVERSE, 90}, {tpSPREAD, 120},
              {tpATTACK, 100}, {tpRELEASE, 100}, {tpVOLUME, 80}});
    q.flt(1, ftBP, 70, 30); q.sends(1, 20, 80);
    q.row(0, 1, gBars({"........x.......", "................", "........x.......", "................"})); q.notes(0, 1, {7, 3});
    b.mat(2, 8); b.mode(2, gmCloud);
    q.set(2, {{tpSCAN, 70}, {tpSIZE, 34}, {tpDENSITY, 110}, {tpSPRAY, 70}, {tpSHAPE, 80}, {tpJITTER, 25}, {tpREVERSE, 40}, {tpOCTAVE, 25},
              {tpSPREAD, 127}, {tpDECAY, 100}, {tpSUSTAIN, 30}, {tpVOLUME, 84}});
    q.sends(2, 50, 70);
    q.row(0, 2, gRep("....x...........", 4), 8); q.cond(0, 2, {4, 20, 36, 52}, c50); q.cyc(0, 2, tpPOSITION, {8, 40, 72, 104});
    q.length(0, 64);
    b.glob({{gREV_SIZE, 118}, {gREV_DAMP, 61}, {gTEMPO, 70}});
    q.lfoFree(0, 0, 0, 12, 14, tpPOSITION); q.lfo(0, 1, 1, 1, -22, tpCUTOFF); q.lfoFree(1, 0, 1, 30, 25, tpSPRAY);
    b.mute(3);
    q.mix({-3.6f, -4.2f, 4.4f, 0.0f});
}

// Particle Keys 96: piano chords broken into particles (POSITION picks Cm9, Fm9, Abmaj7, Gm7 of the KEYS material),
// the drone on the roots, the beat loop at half speed, bell sparks on FILL.
static void pParticleKeys(PB& b)
{
    PB2 q(b);
    b.mat(0, 8); b.mode(0, gmCloud);
    q.set(0, {{tpSCAN, 70}, {tpSIZE, 34}, {tpDENSITY, 122}, {tpSPRAY, 70}, {tpSHAPE, 80}, {tpJITTER, 25}, {tpREVERSE, 40}, {tpOCTAVE, 25},
              {tpSPREAD, 127}, {tpVOLUME, 92}});
    q.sends(0, 40, 60);
    q.row(0, 0, gRep("x.......x.......", 4)); q.cyc(0, 0, tpPOSITION, {8, 8, 40, 40, 72, 72, 104, 104});
    q.cyc(0, 0, tpDENSITY, {122, 100, 122, 90});
    b.mat(1, 7); b.mode(1, gmClassic);
    q.set(1, {{tpSCAN, 66}, {tpSIZE, 104}, {tpDENSITY, 80}, {tpSPREAD, 80}, {tpPITCH, 64 - 32}, {tpATTACK, 100}, {tpRELEASE, 90}, {tpVOLUME, 76}});
    q.flt(1, ftLP24, 66, 30); q.sends(1, 0, 30);
    q.row(0, 1, gRep("x...............", 4)); q.notes(0, 1, {0, 5, -4, -5});
    b.mat(2, 6); b.mode(2, gmLoop, true);
    q.set(2, {{tpSCAN, 96}, {tpSIZE, sz(6)}, {tpSHAPE, 6}, {tpSPREAD, 20}, {tpVOLUME, 80}});
    q.flt(2, ftLP12, 80, 20); q.sends(2, 10, 15);
    q.row(0, 2, gRep("x.......x.......", 4), kGateHold, 3);
    b.mat(3, 2); b.mode(3, gmCloud);
    q.set(3, {{tpSCAN, 70}, {tpSIZE, 30}, {tpDENSITY, 100}, {tpSPRAY, 100}, {tpSHAPE, 90}, {tpOCTAVE, 40}, {tpSPREAD, 127}, {tpDECAY, 80}, {tpSUSTAIN, 0}, {tpVOLUME, 80}});
    q.sends(3, 60, 60);
    q.row(0, 3, gBars({"................", "................", "................", "........x...x..."}), 6); q.cond(0, 3, {56, 60}, cFill);
    q.length(0, 64, 54);
    b.glob({{gREV_SIZE, 96}, {gECHO_TIME, 86}, {gTEMPO, 96}});
    q.lfo(0, 0, 1, 3, 30, tpSPRAY); q.lfo(0, 1, 5, 11, 25, tpPMIX); q.lfo(2, 0, 0, 2, 20, tpCUTOFF);
    q.mix({1.8f, 3.2f, 1.8f, 8.0f});
}

// Beat Shuffle 120: the beat re-ordered slice by slice over 4 bars, stutters, ratchets, reverses and transpositions,
// harp loops answering; B shuffles harder and CHAOS takes the beat.
static void pBeatShuffle(PB& b)
{
    PB2 q(b);
    b.mat(0, 6); b.mode(0, gmLoop, true);
    const int n = std::max(1, b.slices(0));
    q.set(0, {{tpSCAN, 96}, {tpSIZE, sz(4)}, {tpSHAPE, 6}, {tpSPREAD, 20}, {tpVOLUME, 84}});
    q.flt(0, ftLP24, 120, 20); q.sends(0, 0, 12, 0, 127);
    q.row(0, 0, gRep("xxxxxxxxxxxxxxxx", 4), 4);
    static const int order[64] = {0, 1, 2, 1, 4, 3, 0, 5, 6, 1, 2, 7, 8, 9, 4, 3,  0, 1, 2, 1, 4, 3, 0, 5, 6, 1, 2, 7, 8, 9, 10, 11,
                                  0, 1, 2, 1, 4, 3, 0, 5, 6, 1, 2, 7, 8, 9, 4, 3,  0, 0, 2, 2, 4, 4, 6, 6, 8, 9, 10, 11, 12, 13, 14, 15};
    for (int s = 0; s < 64; ++s) q.lock(0, 0, s, tpSLICE, sliceKnob(order[s] % n, n));
    for (int s : {7, 23, 39}) { q.lock(0, 0, s, tpSIZE, sz(2)); q.lock(0, 0, s, tpSCAN, 64); }
    for (int s : {11, 43}) q.lock(0, 0, s, tpREVERSE, 127);
    q.ratch(0, 0, 15, 1); q.ratch(0, 0, 47, 2); q.ratch(0, 0, 62, 3); q.ratch(0, 0, 63, 3);
    q.P(0).note[0][13] = kBaseNote + 7; q.P(0).note[0][29] = kBaseNote - 5; q.P(0).note[0][61] = kBaseNote + 12;
    b.mat(1, 5); b.mode(1, gmLoop, true);
    q.set(1, {{tpSCAN, 96}, {tpSIZE, sz(6)}, {tpSHAPE, 20}, {tpSPREAD, 70}, {tpDECAY, 90}, {tpSUSTAIN, 0}, {tpVOLUME, 80}});
    q.sends(1, 50, 30);
    q.row(0, 1, gRep("x.....x.....x...", 4), 6); q.cyc(0, 1, tpPOSITION, {0, 64, 32, 96}); q.cond(0, 1, {12, 44}, c50);
    for (int s : {12, 28}) q.lock(0, 1, s, tpREVERSE, 127);
    b.mat(2, 7); b.mode(2, gmClassic);
    q.set(2, {{tpSCAN, 66}, {tpSIZE, 100}, {tpDENSITY, 90}, {tpSPREAD, 60}, {tpPITCH, 64 - 32}, {tpATTACK, 60}, {tpVOLUME, 70}});
    q.flt(2, ftLP24, 60, 40); q.sends(2, 0, 20);
    q.row(0, 2, gRep("x...............", 4)); q.notes(0, 2, {0, 0, -4, -5});
    q.length(0, 64, 56);
    q.copy(0, 1);
    for (int s = 0; s < 64; s += 3) q.ratch(1, 0, s, 1);
    q.chain("1 2 0:3 1:1");
    b.glob({{gTEMPO, 120}, {gECHO_TIME, 64}, {gGL_CHANCE, 40}, {gGL_MIX, 100}});
    q.lfo(1, 0, 0, 2, 20, tpCUTOFF);
    b.mute(3);
    q.mix({-3.2f, 8.0f, 4.9f, 0.0f});
}

// Stutter Slices 128: one-slice stutters and repeats on the beat with sizes shrinking, keys loops jumping position,
// the drone pulsing in eighths through an LFO on its volume.
static void pStutterSlices(PB& b)
{
    PB2 q(b);
    b.mat(0, 6, 1, 16); b.mode(0, gmLoop, true);
    q.set(0, {{tpSCAN, 64}, {tpSIZE, sz(2)}, {tpSHAPE, 8}, {tpSPREAD, 15}, {tpDECAY, 66}, {tpSUSTAIN, 0}, {tpVOLUME, 96}});
    q.sends(0, 0, 10, 0, 127);
    q.row(0, 0, gBars({"xxx.xx.xxx.x.xxx", "xxx.xx.xxx.x.xxx", "xxx.xx.xxx.x.xxx", "xxxxxxxxxxxxxxxx"}), 4);
    static const int sl[16] = {0, 0, 0, 0, 4, 4, 0, 8, 9, 10, 0, 12, 0, 14, 14, 15};
    for (int s = 0; s < 64; ++s) q.lock(0, 0, s, tpSLICE, sliceKnob(sl[s % 16], b.slices(0)));
    for (int s = 48; s < 64; ++s) q.lock(0, 0, s, tpSIZE, sz(std::max(0, 6 - (s - 48) / 3)));
    q.ratch(0, 0, 13, 2); q.ratch(0, 0, 45, 3);
    b.mat(1, 8); b.mode(1, gmLoop, true);
    q.set(1, {{tpSCAN, 64}, {tpSIZE, sz(4)}, {tpSHAPE, 14}, {tpSPREAD, 80}, {tpDECAY, 50}, {tpSUSTAIN, 0}, {tpVOLUME, 76}});
    q.flt(1, ftHP12, 40, 20); q.sends(1, 50, 30);
    q.row(0, 1, gRep("..x...x...x..x.x", 4), 4); q.cyc(0, 1, tpPOSITION, {8, 32, 64, 96, 110});
    q.cond(0, 1, {13, 29, 45, 61}, c75);
    b.mat(2, 7); b.mode(2, gmClassic);
    q.set(2, {{tpSCAN, 66}, {tpSIZE, 100}, {tpDENSITY, 90}, {tpPITCH, 64 - 32}, {tpATTACK, 10}, {tpVOLUME, 80}});
    q.flt(2, ftLP24, 64, 50); q.sends(2, 0, 20);
    q.row(0, 2, gRep("x...............", 4)); q.notes(0, 2, {0, 0, -4, -2});
    q.length(0, 64);
    b.glob({{gTEMPO, 128}, {gECHO_TIME, 54}});
    q.lfo(2, 0, 2, 9, -40, tpVOLUME);
    b.mute(3);
    q.mix({-1.3f, 8.0f, -4.3f, 0.0f});
}

// Grain Arp 110: the keys as a grain synth playing chords through the ARP (UP-DOWN, 1/16, 2 octaves), pad cloud,
// beat loop, harp drops; the arp turns RANDOM in the last bar.
static void pGrainArp(PB& b)
{
    PB2 q(b);
    b.mat(0, 8); b.mode(0, gmClassic);
    q.set(0, {{tpPOSITION, 6}, {tpSCAN, 64}, {tpSIZE, 70}, {tpDENSITY, 100}, {tpSPREAD, 90}, {tpATTACK, 0}, {tpDECAY, 70}, {tpSUSTAIN, 40}, {tpRELEASE, 50}, {tpVOLUME, 84}});
    q.flt(0, ftLP12, 84, 40, 90, 0, 50); q.sends(0, 40, 40);
    q.arp(0, amUpDown, 8, 2, 50);
    q.row(0, 0, gRep("x...............", 4)); q.notes(0, 0, {0, -4, 5, -5}, {chMin7, chMaj7, chMin7, chMin});
    q.lock(0, 0, 48, tpAMODE, binKnob(amRandom, kArpModes));
    b.mat(1, 1); b.mode(1, gmCloud);
    q.set(1, {{tpSCAN, 68}, {tpSIZE, 100}, {tpDENSITY, 110}, {tpSPRAY, 60}, {tpSPREAD, 110}, {tpATTACK, 100}, {tpRELEASE, 100}, {tpVOLUME, 66}});
    q.flt(1, ftLP24, 76, 20); q.sends(1, 0, 70, 50);
    q.row(0, 1, gRep("x...............", 4)); q.notes(0, 1, {0, -4, 5, -5}, {chMin, chMaj, chMin, chMin});
    b.mat(2, 6); b.mode(2, gmLoop, true);
    q.set(2, {{tpSCAN, 96}, {tpSIZE, sz(4)}, {tpSHAPE, 6}, {tpSPREAD, 20}, {tpVOLUME, 84}});
    q.sends(2, 0, 10, 0, 127);
    q.row(0, 2, gRep("xxxxxxxxxxxxxxxx", 4), 4);
    b.mat(3, 5); b.mode(3, gmCloud);
    q.set(3, {{tpSCAN, 64}, {tpPOSITION, 40}, {tpSIZE, 44}, {tpDENSITY, 70}, {tpSPRAY, 110}, {tpSHAPE, 96}, {tpSPREAD, 127}, {tpDECAY, 90}, {tpSUSTAIN, 0}, {tpVOLUME, 84}});
    q.sends(3, 60, 50);
    q.row(0, 3, gRep("......x.......x.", 4), 4); q.cond(0, 3, {14, 30, 46, 62}, c50);
    q.length(0, 64, 54);
    b.glob({{gTEMPO, 110}, {gECHO_TIME, 86}, {gECHO_FB, 50}, {gREV_SIZE, 90}});
    q.lfo(0, 0, 1, 2, 20, tpCUTOFF); q.lfo(1, 0, 1, 1, 20, tpPOSITION);
    q.mix({8.0f, 0.7f, -1.5f, 8.0f});
}

// Choir Chords 80: Cm, Ab, Fm, Bb made from the choir (step chords), bells forced into the chord tones (NOTES CHORD
// order), a drone, a reverse swell into bar 1 on the first loop only.
static void pChoirChords(PB& b)
{
    PB2 q(b);
    b.mat(0, 3); b.mode(0, gmClassic);
    q.set(0, {{tpPOSITION, 40}, {tpSCAN, 64}, {tpSPRAY, 40}, {tpSIZE, 104}, {tpDENSITY, 110}, {tpJITTER, 20}, {tpSPREAD, 110},
              {tpATTACK, 90}, {tpRELEASE, 100}, {tpVOLUME, 84}});
    q.flt(0, ftLP24, 96, 30); q.sends(0, 0, 70, 40);
    q.row(0, 0, gRep("x...............", 4)); q.notes(0, 0, {0, -4, 5, -2}, {chMin, chMaj, chMin, chMaj});
    b.mat(1, 2); b.mode(1, gmCloud);
    q.set(1, {{tpSCAN, 70}, {tpSIZE, 36}, {tpDENSITY, 100}, {tpSPRAY, 100}, {tpSHAPE, 90}, {tpOCTAVE, 30}, {tpSPREAD, 127}, {tpVOLUME, 80}});
    q.scaleNotes(1, 1, 120, 2, 5); q.sends(1, 50, 70);
    q.row(0, 1, gRep("....x.......x...", 4)); q.notes(0, 1, {0, 0, -4, -4, 5, 5, -2, -2}, {chMin, chMin, chMaj, chMaj, chMin, chMin, chMaj, chMaj});
    b.mat(2, 7); b.mode(2, gmClassic);
    q.set(2, {{tpPOSITION, 50}, {tpSCAN, 66}, {tpSIZE, 108}, {tpDENSITY, 90}, {tpPITCH, 64 - 32}, {tpATTACK, 100}, {tpVOLUME, 76}});
    q.flt(2, ftLP24, 60, 20); q.sends(2, 0, 30);
    q.row(0, 2, gRep("x...............", 4)); q.notes(0, 2, {0, -4, 5, -2});
    b.mat(3, 1); b.mode(3, gmCloud);
    q.set(3, {{tpSCAN, 60}, {tpSIZE, 90}, {tpDENSITY, 110}, {tpREVERSE, 127}, {tpSPREAD, 120}, {tpATTACK, 120}, {tpRELEASE, 40}, {tpVOLUME, 70}});
    q.sends(3, 0, 90);
    q.row(0, 3, gBars({"................", "................", "................", "........x......."})); q.notes(0, 3, {0}, {chMin});
    q.cond(0, 3, {56}, cFirst);
    q.length(0, 64);
    b.glob({{gREV_SIZE, 112}, {gCHO_LEV, 70}, {gTEMPO, 80}});
    q.lfo(0, 0, 1, 2, 16, tpPAN); q.lfo(1, 0, 5, 9, 40, tpPAN);
    q.mix({-1.9f, -3.8f, 8.0f, 1.5f});
}

// Pluck Lines 104: a melody played on harp grains over 4 bars (gates, a ratchet, a fill), the pad, the beat in loops,
// the drone on the roots.
static void pPluckLines(PB& b)
{
    PB2 q(b);
    b.mat(0, 5); b.mode(0, gmClassic);
    q.set(0, {{tpPOSITION, 20}, {tpSCAN, 64}, {tpSIZE, 60}, {tpDENSITY, 90}, {tpSPREAD, 70}, {tpATTACK, 0}, {tpDECAY, 70}, {tpSUSTAIN, 20}, {tpRELEASE, 60}, {tpVOLUME, 90}});
    q.flt(0, ftLP12, 90, 30, 80, 0, 40); q.sends(0, 50, 40);
    q.row(0, 0, gBars({"x..x..x...x.x...", "x..x..x...x.....", "x..x..x...x.x...", "x.x.x.x.x...x.x."}), 2);
    q.notes(0, 0, {12, 10, 7, 8, 7, 3, 7, 5, 3, 2, 12, 10, 7, 8, 7, 3, 15, 14, 12, 10, 7, 8, 3, 7});
    q.ratch(0, 0, 60, 2); q.cond(0, 0, {62}, cFill);
    b.mat(1, 1); b.mode(1, gmCloud);
    q.set(1, {{tpSCAN, 68}, {tpSIZE, 100}, {tpDENSITY, 100}, {tpSPRAY, 40}, {tpSPREAD, 110}, {tpATTACK, 100}, {tpRELEASE, 100}, {tpVOLUME, 66}});
    q.flt(1, ftLP24, 70, 20); q.sends(1, 0, 70, 40);
    q.row(0, 1, gRep("x...............", 4)); q.notes(0, 1, {0, -4, 5, -5}, {chMin, chMaj, chMin, chMin});
    b.mat(2, 6); b.mode(2, gmLoop, true);
    q.set(2, {{tpSCAN, 96}, {tpSIZE, sz(6)}, {tpSHAPE, 6}, {tpSPREAD, 20}, {tpVOLUME, 84}});
    q.sends(2, 0, 12, 0, 127);
    q.row(0, 2, gRep("x.......x.......", 4));
    b.mat(3, 7); b.mode(3, gmClassic);
    q.set(3, {{tpPOSITION, 50}, {tpSCAN, 66}, {tpSIZE, 100}, {tpDENSITY, 90}, {tpPITCH, 64 - 32}, {tpATTACK, 40}, {tpVOLUME, 76}});
    q.flt(3, ftLP24, 60, 30); q.sends(3, 0, 20);
    q.row(0, 3, gRep("x...............", 4)); q.notes(0, 3, {0, -4, 5, -5});
    q.length(0, 64, 55);
    b.glob({{gTEMPO, 104}, {gECHO_TIME, 86}, {gECHO_FB, 50}, {gREV_SIZE, 90}});
    q.lfo(0, 0, 1, 3, 15, tpCUTOFF);
    q.mix({8.0f, -0.4f, -0.1f, 3.4f});
}

// Ambient Four 72: four tracks of 64, 48, 40 and 28 steps drifting against each other, an LFO on each, slow chords.
static void pAmbientFour(PB& b)
{
    PB2 q(b);
    b.mat(0, 1); b.mode(0, gmCloud);
    q.set(0, {{tpSCAN, 68}, {tpSIZE, 100}, {tpDENSITY, 110}, {tpSPRAY, 50}, {tpSPREAD, 110}, {tpATTACK, 110}, {tpRELEASE, 110}, {tpVOLUME, 84}});
    q.flt(0, ftLP24, 90, 20); q.sends(0, 0, 70, 40);
    q.row(0, 0, gRep("x...............", 4)); q.notes(0, 0, {0, -4, -7, -5}, {chMin7, chMaj7, chMin7, chMin});
    b.mat(1, 3); b.mode(1, gmCloud);
    q.set(1, {{tpPOSITION, 40}, {tpSCAN, 70}, {tpSIZE, 96}, {tpDENSITY, 100}, {tpSPRAY, 70}, {tpREVERSE, 50}, {tpSPREAD, 120}, {tpATTACK, 110}, {tpRELEASE, 100}, {tpVOLUME, 74}});
    q.flt(1, ftBP, 80, 20); q.sends(1, 20, 80);
    q.row(0, 1, gRep("x...........", 4)); q.notes(0, 1, {7, 3, 8, 10});
    b.mat(2, 2); b.mode(2, gmCloud);
    q.set(2, {{tpSCAN, 70}, {tpSIZE, 36}, {tpDENSITY, 90}, {tpSPRAY, 100}, {tpSHAPE, 90}, {tpOCTAVE, 30}, {tpSPREAD, 127}, {tpVOLUME, 76}});
    q.scaleNotes(2, 6, 100, 2, 0); q.sends(2, 60, 70);
    q.row(0, 2, gRep("x.........", 4)); q.cond(0, 2, {10, 30}, c50);
    b.mat(3, 7); b.mode(3, gmClassic);
    q.set(3, {{tpPOSITION, 50}, {tpSCAN, 66}, {tpSIZE, 108}, {tpDENSITY, 90}, {tpPITCH, 64 - 32}, {tpATTACK, 110}, {tpVOLUME, 76}});
    q.flt(3, ftLP24, 66, 20); q.sends(3, 0, 40);
    q.row(0, 3, gRep("x......", 4)); q.notes(0, 3, {0, -4, -7, -5});
    q.length(0, 64);
    b.glob({{gREV_SIZE, 116}, {gREV_DAMP, 50}, {gECHO_TIME, 99}, {gECHO_FB, 70}, {gCHO_LEV, 70}, {gTEMPO, 72}});
    q.lfo(0, 0, 1, 1, 20, tpCUTOFF); q.lfo(1, 0, 1, 2, 24, tpPAN); q.lfo(2, 0, 5, 3, 30, tpSIZE); q.lfoFree(3, 0, 0, 20, 12, tpPOSITION);
    q.mix({-0.4f, 5.4f, 1.1f, 7.8f});
}

// Scale Cloud 90: plucks and keys scattered into C minor pentatonic (UP and PING-PONG orders), so any position plays a
// melody; the pad holds Cm - Ab; the CHAOS repeats the plucks now and then.
static void pScaleCloud(PB& b)
{
    PB2 q(b);
    b.mat(0, 5); b.mode(0, gmCloud, true);
    q.set(0, {{tpSCAN, 72}, {tpSIZE, sz(4)}, {tpDENSITY, 60}, {tpSPRAY, 80}, {tpSHAPE, 90}, {tpSPREAD, 127}, {tpVOLUME, 88}});
    q.scaleNotes(0, 6, 127, 2, 2, 2); q.sends(0, 60, 50, 0, 127);
    q.row(0, 0, gRep("x...............", 4));
    q.cyc(0, 0, tpNORDER, {(int)binKnob(2, kNOrders), (int)binKnob(4, kNOrders), (int)binKnob(3, kNOrders), (int)binKnob(0, kNOrders)});
    b.mat(1, 8); b.mode(1, gmCloud);
    q.set(1, {{tpSCAN, 66}, {tpSIZE, 50}, {tpDENSITY, 70}, {tpSPRAY, 90}, {tpSHAPE, 80}, {tpSPREAD, 127}, {tpDECAY, 90}, {tpSUSTAIN, 20}, {tpVOLUME, 80}});
    q.scaleNotes(1, 6, 100, 1, 4); q.sends(1, 40, 60);
    q.row(0, 1, gRep("....x.......x...", 4)); q.cond(0, 1, {12, 44}, c50);
    b.mat(2, 1); b.mode(2, gmCloud);
    q.set(2, {{tpSCAN, 68}, {tpSIZE, 100}, {tpDENSITY, 110}, {tpSPRAY, 50}, {tpSPREAD, 110}, {tpATTACK, 100}, {tpRELEASE, 100}, {tpVOLUME, 70}});
    q.flt(2, ftLP24, 80, 20); q.sends(2, 0, 70, 40);
    q.row(0, 2, gBars({"x...............", "................", "x...............", "................"})); q.notes(0, 2, {0, -4}, {chMin, chMaj});
    q.length(0, 64);
    b.glob({{gTEMPO, 90}, {gECHO_TIME, 86}, {gECHO_FB, 55}, {gGL_CHANCE, 40}, {gGL_MIX, 90}, {gREV_SIZE, 96}});
    q.lfo(0, 0, 5, 3, 30, tpPOSITION); q.lfo(1, 0, 1, 2, 30, tpPOSITION);
    b.mute(3);
    q.mix({3.1f, 8.0f, -6.9f, 0.0f});
}

// Live Granulator 120: play something into the input (side-chain): track 1 clouds it, track 2 loops it on the beat with
// slices of the last bar, track 3 freezes a shimmer every other bar.
static void pLiveGranulator(PB& b)
{
    PB2 q(b);
    for (int t = 0; t < 3; ++t) { b.b.kit[t].material = kMatLive; b.b.kit[t].nSlices = 0; }
    b.mode(0, gmCloud);
    q.set(0, {{tpSCAN, 96}, {tpPOSITION, 90}, {tpSIZE, 80}, {tpDENSITY, 108}, {tpSPRAY, 50}, {tpJITTER, 20}, {tpSPREAD, 115}, {tpVOLUME, 90}});
    q.sends(0, 0, 50);
    q.row(0, 0, gRep("x...............", 4));
    b.mode(1, gmLoop, true);
    q.set(1, {{tpSCAN, 64}, {tpPOSITION, 110}, {tpSIZE, sz(4)}, {tpSHAPE, 10}, {tpDECAY, 70}, {tpSUSTAIN, 0}, {tpSPREAD, 60}, {tpVOLUME, 100}});
    q.sends(1, 40, 10, 0, 127);
    q.row(0, 1, gRep("....x.......x.x.", 4), 4); q.cyc(0, 1, tpPOSITION, {110, 90, 70, 120}); q.ratch(0, 1, 62, 2);
    b.mode(2, gmClassic);
    q.set(2, {{tpSCAN, 64}, {tpPOSITION, 60}, {tpSIZE, 105}, {tpDENSITY, 127}, {tpOCTAVE, 50}, {tpSPREAD, 120}, {tpATTACK, 100}, {tpVOLUME, 70}});
    q.sends(2, 0, 90);
    q.row(0, 2, gBars({"x...............", "................", "x...............", "................"}));
    b.mute(2); b.mute(3);
    q.length(0, 64);
    b.glob({{gCAPLEN, 1}, {gTEMPO, 120}, {gGL_CHANCE, 30}, {gGL_MIX, 80}});
}

// trimDb: level so every preset plays at about the same loudness (-16 LUFS)
struct PresetDef { const char* name; const char* desc; void (*build)(PB&); float trimDb = 0.f; };
static const PresetDef kPresets[] = {
    {"Init",               "Four kinds of granular sound, empty sequencer",                 pInit},
    {"Dissolving Strings", "A pad melting into a cloud on Cm - Ab - Fm - G, choir, drone",  pDissolvingStrings, -4.7f},
    {"Bell Rain",          "Bells forced into the pentatonic, pad, rain, harp drops",       pBellRain, 2.7f},
    {"Frozen Drone",       "A frozen drone, a backwards choir, key particles by chance",    pFrozenDrone, 2.5f},
    {"Particle Keys",      "Piano chords broken into particles, drone, half-speed beat",    pParticleKeys, -2.8f},
    {"Beat Shuffle",       "The beat re-ordered over 4 bars, stutters, ratchets; B harder", pBeatShuffle, -2.0f},
    {"Stutter Slices",     "Slice stutters shrinking, keys loops, a pulsing drone",         pStutterSlices, -0.1f},
    {"Grain Arp",          "Keys grain synth through the ARP on 7th chords, pad, beat",     pGrainArp, -4.5f},
    {"Choir Chords",       "Cm, Ab, Fm, Bb from the choir, bells on the chord tones",       pChoirChords, -4.6f},
    {"Pluck Lines",        "A harp-grain melody over 4 bars, pad, beat, drone",             pPluckLines, -0.3f},
    {"Ambient Four",       "Tracks of 64, 48, 40 and 28 steps drifting, an LFO on each",    pAmbientFour, -3.6f},
    {"Scale Cloud",        "Plucks and keys scattered into the pentatonic, CHAOS repeats",  pScaleCloud, 2.8f},
    {"Live Granulator",    "Three tracks on the input: cloud, sliced loop, freeze",         pLiveGranulator, 1.2f},
};
static constexpr int kNumPresets = (int)(sizeof(kPresets) / sizeof(kPresets[0]));

inline void buildPreset(int i, float* values, Bank& bank)
{
    PB b(values, bank);
    if (i >= 0 && i < kNumPresets) {
        kPresets[i].build(b);
        const float db = kPresets[i].trimDb;
        if (std::fabs(db) > 0.05f) {   // master VOLUME first (gain 1.2 (v / 127)^2), then every track by the same factor
            float& mv = values[kGlobalBase + gVOLUME];
            const float gain = 1.2f * (mv / 127.f) * (mv / 127.f) * std::pow(10.f, db / 20.f);
            const float v = 127.f * std::sqrt(gain / 1.2f);
            if (v <= 127.f) mv = std::round(v);
            else {
                mv = 127.f;
                float top = 1.f;
                for (int t = 0; t < kTracks; ++t) top = std::max(top, values[tpIndex(t, tpVOLUME)]);
                const float f = std::min(std::sqrt(gain / 1.2f), 127.f / top);
                for (int t = 0; t < kTracks; ++t) values[tpIndex(t, tpVOLUME)] = std::round(values[tpIndex(t, tpVOLUME)] * f);
            }
        }
    }
}

} // namespace kn
