// Kinetik KGRN-4 — patterns (4 tracks of steps with per-step locks), the kit (material, slices, mutes),
// text serialization and edit commands
#pragma once
#include "Params.hpp"
#include "Chain.hpp"
#include <cstring>
#include <cstdlib>
#include <string>
#include <algorithm>

namespace kn {

struct Pattern {
    uint8_t length = 16;                          // master length: a queued pattern starts when it ends
    uint8_t swing = 50;                           // 50..80
    uint8_t len[kTracks];                         // track lengths 1..64 (each track loops on its own)
    uint8_t speed[kTracks];                       // index into kSpeedMult
    uint8_t on[kTracks][kSteps];                  // trig on this step
    uint8_t note[kTracks][kSteps];                // MIDI note (kBaseNote = the sample's own pitch)
    uint8_t gate[kTracks][kSteps];                // index into kGateSteps
    uint8_t chord[kTracks][kSteps];               // index into kChords (0 = one note)
    uint8_t ratch[kTracks][kSteps];               // index into kRatchValues
    uint8_t cond[kTracks][kSteps];                // index into kConds (0 = always)
    uint8_t locks[kTracks][kSteps][kTrackParams]; // per-step knob values (kNoLock = the knob)

    Pattern() { clear(); }
    void clear()
    {
        length = 16; swing = 50;
        for (int t = 0; t < kTracks; ++t) { len[t] = 16; speed[t] = 0; }
        std::memset(on, 0, sizeof(on));
        std::memset(locks, kNoLock, sizeof(locks));
        for (int t = 0; t < kTracks; ++t) clearSteps(t);
    }
    void clearSteps(int t)
    {
        std::memset(note[t], kBaseNote, sizeof(note[t])); std::memset(gate[t], kDefaultGate, sizeof(gate[t]));
        std::memset(chord[t], 0, sizeof(chord[t])); std::memset(ratch[t], 0, sizeof(ratch[t])); std::memset(cond[t], 0, sizeof(cond[t]));
    }
    bool stepDefault(int t, int s) const { return note[t][s] == kBaseNote && gate[t][s] == kDefaultGate && !chord[t][s] && !ratch[t][s] && !cond[t][s]; }
    void clearTrack(int t) { std::memset(on[t], 0, sizeof(on[t])); std::memset(locks[t], kNoLock, sizeof(locks[t])); clearSteps(t); }
    int trackStep(int t, int64_t k) const { const int n = len[t] ? len[t] : 16; return (int)(((k % n) + n) % n); }
    bool hasLocks(int t, int s) const { for (int k = 0; k < kTrackParams; ++k) if (locks[t][s][k] != kNoLock) return true; return false; }
    bool trackEmpty(int t) const { for (int s = 0; s < kSteps; ++s) if (on[t][s] || hasLocks(t, s) || !stepDefault(t, s)) return false; return true; }
    bool isDefault() const
    {
        if (length != 16 || swing != 50) return false;
        for (int t = 0; t < kTracks; ++t) { if (len[t] != 16 || speed[t] != 0 || !trackEmpty(t)) return false; }
        return true;
    }
    void rotateTrack(int t, int n)
    {
        const int L = len[t];
        uint8_t o[kSteps], nt[kSteps], gt[kSteps], ch[kSteps], ra[kSteps], co[kSteps]; uint8_t lk[kSteps][kTrackParams];
        std::memcpy(o, on[t], sizeof(o)); std::memcpy(lk, locks[t], sizeof(lk));
        std::memcpy(nt, note[t], sizeof(nt)); std::memcpy(gt, gate[t], sizeof(gt)); std::memcpy(ch, chord[t], sizeof(ch)); std::memcpy(ra, ratch[t], sizeof(ra)); std::memcpy(co, cond[t], sizeof(co));
        for (int s = 0; s < L; ++s) {
            const int d = ((s + n) % L + L) % L;
            on[t][d] = o[s]; std::memcpy(locks[t][d], lk[s], kTrackParams);
            note[t][d] = nt[s]; gate[t][d] = gt[s]; chord[t][d] = ch[s]; ratch[t][d] = ra[s]; cond[t][d] = co[s];
        }
    }
};

// the sound source of each track (shared by all patterns)
struct Kit {
    uint8_t material = 0;                 // 0 = the track's own sample, 1.. = built-in material (Materials.hpp), kMatLive = the input
    uint8_t mute = 0;
    uint8_t sliceMode = 0;                // 0 = transients, 1 = equal slices
    uint8_t sliceAmount = 50;             // transients: sensitivity 0..100; equal: number of slices 1..32
    uint8_t nSlices = 0;
    uint8_t mode = gmClassic;             // GrainMode
    uint8_t sync = 0;                     // SIZE in note values
    uint8_t legato = 0;                   // steps change the locks of the playing note instead of starting a new one
    uint8_t solo = 0;
    uint8_t beats = 0;                    // LOOP SYNC: the sample is a loop of this many beats (0 = off)
    uint32_t slice[kMaxSlices] = {};      // slice starts, millionths of the sample length (ascending, slice[0] usually 0)
};

struct Bank {
    Pattern patterns[kPatterns];
    Kit kit[kTracks];
    Chain chain;
    uint8_t scale = 0, root = 0;          // SCALE for every track's grains (0 = off), root pitch class (0 = C)
    void initDefault() { for (auto& p : patterns) p.clear(); for (auto& k : kit) k = Kit(); chain = Chain(); scale = 0; root = 0; }
};

static constexpr int kKitTouched = 1000;   // applyCommand: a kit changed

inline std::string serializeBank(const Bank& b)
{
    std::string s = "KGRN46\n";
    char buf[200];
    for (int t = 0; t < kTracks; ++t) {
        const Kit& k = b.kit[t];
        std::snprintf(buf, sizeof(buf), "M %d %d %d %d %d %d %d %d %d %d\n", t, k.material, k.mute, k.sliceMode, k.sliceAmount, k.mode, k.sync, k.legato, k.solo, k.beats); s += buf;
        if (k.nSlices) {
            std::snprintf(buf, sizeof(buf), "X %d %d", t, k.nSlices); s += buf;
            for (int i = 0; i < k.nSlices; ++i) { std::snprintf(buf, sizeof(buf), " %u", k.slice[i]); s += buf; }
            s += "\n";
        }
    }
    if (!b.chain.isDefault()) s += b.chain.line() + "\n";
    if (b.scale || b.root) { std::snprintf(buf, sizeof(buf), "G %d %d\n", b.scale, b.root); s += buf; }
    for (int p = 0; p < kPatterns; ++p) {
        const Pattern& P = b.patterns[p];
        if (P.isDefault()) continue;
        std::snprintf(buf, sizeof(buf), "P %d %d %d\n", p, P.length, P.swing); s += buf;
        for (int t = 0; t < kTracks; ++t) {
            if (P.len[t] != 16 || P.speed[t] != 0) { std::snprintf(buf, sizeof(buf), "L %d %d %d %d\n", p, t, P.len[t], P.speed[t]); s += buf; }
            bool any = false; for (int st = 0; st < kSteps; ++st) any = any || P.on[t][st];
            if (any) {
                std::string row(kSteps, '0');
                for (int st = 0; st < kSteps; ++st) if (P.on[t][st]) row[st] = '1';
                std::snprintf(buf, sizeof(buf), "S %d %d ", p, t); s += buf; s += row; s += "\n";
            }
            for (int st = 0; st < kSteps; ++st)
                if (!P.stepDefault(t, st)) { std::snprintf(buf, sizeof(buf), "N %d %d %d %d %d %d %d %d\n", p, t, st, P.note[t][st], P.gate[t][st], P.chord[t][st], P.ratch[t][st], P.cond[t][st]); s += buf; }
            for (int st = 0; st < kSteps; ++st)
                for (int k = 0; k < kTrackParams; ++k)
                    if (P.locks[t][st][k] != kNoLock) { std::snprintf(buf, sizeof(buf), "k %d %d %d %d %d\n", p, t, st, k, P.locks[t][st][k]); s += buf; }
        }
    }
    return s;
}

// one edit line; returns the pattern touched, kKitTouched, or -1
//   P p length swing      L p t len speed      S p t <64 x 0/1>      s p t step on      k p t step knob value (255 = clear)
//   N p t step note gate chord ratchet condition      O t solo
//   kc p t step (clear the step's locks)      M t material mute sliceMode sliceAmount      X t n s1 s2 ...      U t mute
//   cl p      clt p t      cp src dst      rot p t n
inline int applyCommand(Bank& b, const char* line)
{
    char op[8] = {0};
    if (std::sscanf(line, "%7s", op) != 1) return -1;
    const char* a = line + std::strlen(op);
    int v[10] = {0};
    auto okP = [](int p) { return p >= 0 && p < kPatterns; };
    auto okT = [](int t) { return t >= 0 && t < kTracks; };
    auto okS = [](int s) { return s >= 0 && s < kSteps; };
    if (!std::strcmp(op, "P") && std::sscanf(a, "%d %d %d", &v[0], &v[1], &v[2]) == 3 && okP(v[0])) {
        b.patterns[v[0]].length = (uint8_t)clampi(v[1], 1, kSteps); b.patterns[v[0]].swing = (uint8_t)clampi(v[2], 50, 80); return v[0];
    }
    if (!std::strcmp(op, "L") && std::sscanf(a, "%d %d %d %d", &v[0], &v[1], &v[2], &v[3]) == 4 && okP(v[0]) && okT(v[1])) {
        b.patterns[v[0]].len[v[1]] = (uint8_t)clampi(v[2], 1, kSteps); b.patterns[v[0]].speed[v[1]] = (uint8_t)clampi(v[3], 0, kSpeeds - 1); return v[0];
    }
    if (!std::strcmp(op, "S") && std::sscanf(a, "%d %d", &v[0], &v[1]) == 2 && okP(v[0]) && okT(v[1])) {
        const char* c = a; for (int i = 0; i < 2; ++i) { while (*c == ' ') ++c; while (*c && *c != ' ') ++c; }
        while (*c == ' ') ++c;
        for (int st = 0; st < kSteps && c[st]; ++st) b.patterns[v[0]].on[v[1]][st] = c[st] == '1';
        return v[0];
    }
    if (!std::strcmp(op, "s") && std::sscanf(a, "%d %d %d %d", &v[0], &v[1], &v[2], &v[3]) == 4 && okP(v[0]) && okT(v[1]) && okS(v[2])) {
        b.patterns[v[0]].on[v[1]][v[2]] = v[3] ? 1 : 0; return v[0];
    }
    if (!std::strcmp(op, "k") && std::sscanf(a, "%d %d %d %d %d", &v[0], &v[1], &v[2], &v[3], &v[4]) == 5 && okP(v[0]) && okT(v[1]) && okS(v[2])
        && v[3] >= 0 && v[3] < kTrackParams) {
        b.patterns[v[0]].locks[v[1]][v[2]][v[3]] = v[4] >= 255 ? kNoLock : (uint8_t)clampi(v[4], 0, 127); return v[0];
    }
    if (!std::strcmp(op, "kc") && std::sscanf(a, "%d %d %d", &v[0], &v[1], &v[2]) == 3 && okP(v[0]) && okT(v[1]) && okS(v[2])) {
        std::memset(b.patterns[v[0]].locks[v[1]][v[2]], kNoLock, kTrackParams); return v[0];
    }
    if (!std::strcmp(op, "N") && std::sscanf(a, "%d %d %d %d %d %d %d %d", &v[0], &v[1], &v[2], &v[3], &v[4], &v[5], &v[6], &v[7]) == 8 && okP(v[0]) && okT(v[1]) && okS(v[2])) {
        Pattern& P = b.patterns[v[0]]; const int t = v[1], st = v[2];
        P.note[t][st] = (uint8_t)clampi(v[3], 0, 127); P.gate[t][st] = (uint8_t)clampi(v[4], 0, kGateCount - 1);
        P.chord[t][st] = (uint8_t)clampi(v[5], 0, kChordCount - 1); P.ratch[t][st] = (uint8_t)clampi(v[6], 0, kRatchCount - 1);
        P.cond[t][st] = (uint8_t)clampi(v[7], 0, kNumConds - 1);
        return v[0];
    }
    if (!std::strcmp(op, "G") && std::sscanf(a, "%d %d", &v[0], &v[1]) == 2) { b.scale = (uint8_t)clampi(v[0], 0, kScales - 1); b.root = (uint8_t)clampi(v[1], 0, 11); return kKitTouched; }
    if (!std::strcmp(op, "M") && std::sscanf(a, "%d %d %d %d %d %d %d %d %d %d", &v[0], &v[1], &v[2], &v[3], &v[4], &v[5], &v[6], &v[7], &v[8], &v[9]) >= 5 && okT(v[0])) {
        Kit& k = b.kit[v[0]];
        k.material = (uint8_t)clampi(v[1], 0, 64); k.mute = v[2] ? 1 : 0; k.sliceMode = v[3] ? 1 : 0; k.sliceAmount = (uint8_t)clampi(v[4], 0, 100);
        k.mode = (uint8_t)clampi(v[5], 0, kGrainModes - 1); k.sync = v[6] ? 1 : 0; k.legato = v[7] ? 1 : 0; k.solo = v[8] ? 1 : 0; k.beats = (uint8_t)clampi(v[9], 0, 64);
        return kKitTouched;
    }
    if (!std::strcmp(op, "U") && std::sscanf(a, "%d %d", &v[0], &v[1]) == 2 && okT(v[0])) { b.kit[v[0]].mute = v[1] ? 1 : 0; return kKitTouched; }
    if (!std::strcmp(op, "O") && std::sscanf(a, "%d %d", &v[0], &v[1]) == 2 && okT(v[0])) { b.kit[v[0]].solo = v[1] ? 1 : 0; return kKitTouched; }
    if (!std::strcmp(op, "X") && std::sscanf(a, "%d %d", &v[0], &v[1]) == 2 && okT(v[0])) {
        Kit& k = b.kit[v[0]];
        const int n = clampi(v[1], 0, kMaxSlices);
        const char* c = a; for (int i = 0; i < 2; ++i) { while (*c == ' ') ++c; while (*c && *c != ' ') ++c; }
        int got = 0; uint32_t prev = 0;
        for (int i = 0; i < n; ++i) {
            char* end = nullptr; const unsigned long x = std::strtoul(c, &end, 10);
            if (end == c) break;
            c = end;
            const uint32_t y = (uint32_t)std::min<unsigned long>(x, 999999ul);
            if (got > 0 && y <= prev) continue;            // keep them ascending
            k.slice[got++] = y; prev = y;
        }
        k.nSlices = (uint8_t)got;
        return kKitTouched;
    }
    if (!std::strcmp(op, "C")) return b.chain.parse(a, kPatterns) ? kKitTouched : -1;
    if (!std::strcmp(op, "cl") && std::sscanf(a, "%d", &v[0]) == 1 && okP(v[0])) { b.patterns[v[0]].clear(); return v[0]; }
    if (!std::strcmp(op, "clt") && std::sscanf(a, "%d %d", &v[0], &v[1]) == 2 && okP(v[0]) && okT(v[1])) { b.patterns[v[0]].clearTrack(v[1]); return v[0]; }
    if (!std::strcmp(op, "cp") && std::sscanf(a, "%d %d", &v[0], &v[1]) == 2 && okP(v[0]) && okP(v[1])) { b.patterns[v[1]] = b.patterns[v[0]]; return v[1]; }
    if (!std::strcmp(op, "rot") && std::sscanf(a, "%d %d %d", &v[0], &v[1], &v[2]) == 3 && okP(v[0]) && okT(v[1])) { b.patterns[v[0]].rotateTrack(v[1], v[2]); return v[0]; }
    return -1;
}

// ---- older banks. Every old lock is first moved to the 0.5 layout (50 knobs), then to the current one (0.7: 69 knobs,
// the bipolar FILTER of 0.4 / 0.5 becomes a filter TYPE + CUTOFF)
enum Knob05 { o5START = 0, o5END, o5POSITION, o5SLICE, o5SCAN, o5SPRAY, o5PITCH, o5PMIX, o5SIZE, o5DENSITY, o5SHAPE, o5SPREAD,
              o5JITTER, o5REVERSE, o5OCTAVE, o5FINE, o5ATTACK, o5DECAY, o5SUSTAIN, o5RELEASE, o5GMIX, o5DRIVE, o5VOLUME, o5PAN,
              o5FILTER, o5RESO, o5FLTENV, o5LOWCUT, o5DELAY, o5REVERB, o5CHORUS, o5CHAOS, o5L1WAVE, o5Count = 50 };
static const int kKnobs03[20] = {o5POSITION, o5SLICE, o5SCAN, o5SPRAY, o5PITCH, o5SIZE, o5DENSITY, o5SHAPE, o5JITTER, o5REVERSE,
                                 o5OCTAVE, o5SPREAD, o5FILTER, o5RESO, o5ATTACK, o5DECAY, o5VOLUME, o5PAN, o5DELAY, o5REVERB};
static const int kKnobs02[16] = {o5POSITION, o5SLICE, o5SCAN, o5PITCH, o5SIZE, o5DENSITY, o5SPRAY, o5SPREAD,
                                 o5ATTACK, o5DECAY, o5FILTER, o5RESO, o5VOLUME, o5PAN, o5DELAY, o5REVERB};
// 0.4 (40 knobs, 2 LFOs) -> 0.5
static const int kKnobs04[40] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, o5ATTACK, o5DECAY, o5SUSTAIN, o5RELEASE,
                                 o5VOLUME, o5PAN, o5FILTER, o5RESO, o5FLTENV, o5DRIVE, o5DELAY, o5REVERB,
                                 32, 33, 34, 35, 36, 37, 38, 39, 40, 41, 42, 43};
inline int convertDest04(int v)          // an LFO DEST value of 0.4 (29 bins: none + 28 knobs) -> 0.5 (33 bins)
{
    const int d = clampi((int)(v * 29 / 128), 0, 28);
    if (d == 0) return 0;
    return clampi((int)std::lround((kKnobs04[d - 1] + 1 + 0.5f) * 128.f / 33.f), 0, 127);
}
inline int convertKnob04(int k, int v) { const int to = kKnobs04[k]; return (to == 36 || to == 42) ? convertDest04(v) : v; }
// 0.5 -> 0.7
static const int kKnobs05[o5Count] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15,
                                      tpATTACK, tpDECAY, tpSUSTAIN, tpRELEASE, tpGMIX, tpDRIVE, tpVOLUME, tpPAN,
                                      tpCUTOFF, tpRESO, tpFLTENV, tpLOWCUT, tpDELAY, tpREVERB, tpCHORUS, tpCHAOS,
                                      tpL1WAVE, tpL1RATE, tpL1SYNC, tpL1DEPTH, tpL1DEST, tpL1TRIG, tpL2WAVE, tpL2RATE, tpL2SYNC, tpL2DEPTH, tpL2DEST, tpL2TRIG,
                                      tpL3WAVE, tpL3RATE, tpL3SYNC, tpL3DEPTH, tpL3DEST, tpL3TRIG};
inline int convertDest05(int v)          // an LFO DEST value of 0.5 (33 bins) -> now
{
    const int d = clampi((int)(v * 33 / 128), 0, 32);
    if (d == 0) return 0;
    return clampi((int)std::lround(lfoDestKnob(kKnobs05[d - 1] + 1)), 0, 127);
}
// the bipolar FILTER of 0.4 / 0.5 (left = low pass, right = high pass, centre = off) as TYPE + CUTOFF
inline void convertFilter05(int v, int& type, int& cutoff)
{
    if (std::abs(v - 64) < 1) { type = (int)binKnob(ftLP24, kFilterTypes); cutoff = 127; return; }
    const float hz = v < 64 ? 20000.f * std::pow(80.f / 20000.f, (64.f - v) / 64.f) : 20.f * std::pow(400.f, (v - 64.f) / 63.f);
    type = (int)binKnob(v < 64 ? ftLP12 : ftHP12, kFilterTypes);
    cutoff = clampi((int)std::lround(127.f * std::log(std::max(20.f, hz) / 20.f) / std::log(1000.f)), 0, 127);
}
// a knob of the 0.5 layout with its value -> the knobs now (one, or TYPE + CUTOFF for the old FILTER); returns how many
inline int convertKnob05(int k, int v, int* outK, int* outV)
{
    if (k < 0 || k >= o5Count) return 0;
    if (k == o5FILTER) { int ty, cu; convertFilter05(v, ty, cu); outK[0] = tpFTYPE; outV[0] = ty; outK[1] = tpCUTOFF; outV[1] = cu; return 2; }
    outK[0] = kKnobs05[k];
    outV[0] = (k == 36 || k == 42 || k == 48) && v != 255 ? convertDest05(v) : v;
    return 1;
}
inline std::string lockLines05(int p, int t, int st, int k5, int val)
{
    if (val == 255) {                                      // a cleared lock (old commands only)
        char b[64]; std::snprintf(b, sizeof(b), "k %d %d %d %d 255", p, t, st, k5 >= 0 && k5 < o5Count ? kKnobs05[k5] : 0); return b;
    }
    int ks[2], vs[2]; const int n = convertKnob05(k5, val, ks, vs);
    std::string r; char b[64];
    for (int i = 0; i < n; ++i) { std::snprintf(b, sizeof(b), "%sk %d %d %d %d %d", i ? "\n" : "", p, t, st, ks[i], vs[i]); r += b; }
    return r;
}
// one line of an old bank -> line(s) of the current format (version 1 = 0.2, 2 = 0.3, 3 = 0.4, 4 = 0.5 / 0.6)
inline void convertOldLine(std::string& line, int version)
{
    int p, t, st, k, val;
    if (line.rfind("k ", 0) != 0 || std::sscanf(line.c_str() + 2, "%d %d %d %d %d", &p, &t, &st, &k, &val) != 5) return;
    char b[80];
    int k5 = -1;
    if (version <= 2) {
        const int n = version == 1 ? 16 : 20;
        if (k < 0 || k >= n) { line.clear(); return; }
        k5 = version == 1 ? kKnobs02[k] : kKnobs03[k];
        // a PITCH lock of 0.2 / 0.3 becomes the step's note (notes are what the sequencer plays now), DECAY -> RELEASE
        if (k5 == o5PITCH && val != 255) { std::snprintf(b, sizeof(b), "N %d %d %d %d %d 0 0 0", p, t, st, kBaseNote + pitchSemis((float)val), kGateHold); line = b; return; }
        if (k5 == o5DECAY && val != 255) { k5 = o5RELEASE; if (val >= 127) val = 60; }
    } else if (version == 3) {
        if (k < 0 || k >= 40) { line.clear(); return; }
        k5 = kKnobs04[k]; if (val != 255) val = convertKnob04(k, val);
    } else {
        if (k < 0 || k >= o5Count) { line.clear(); return; }
        k5 = k;
    }
    line = lockLines05(p, t, st, k5, val);
}
inline bool deserializeBank(Bank& b, const char* text)
{
    if (!text || std::strncmp(text, "KGRN4", 5) != 0) return false;
    int version = 5;                                       // KGRN46 = now
    if (!std::strncmp(text, "KGRN41", 6)) version = 1;
    else if (!std::strncmp(text, "KGRN42", 6) || !std::strncmp(text, "KGRN43", 6)) version = 2;
    else if (!std::strncmp(text, "KGRN44", 6)) version = 3;
    else if (!std::strncmp(text, "KGRN45", 6)) version = 4;
    b.initDefault();
    const char* s = text; std::string line;
    while (*s) {
        const char* e = std::strchr(s, '\n');
        const size_t n = e ? (size_t)(e - s) : std::strlen(s);
        line.assign(s, n);
        if (version < 5) convertOldLine(line, version);
        size_t a = 0;                                      // a converted line can hold several
        while (a < line.size()) {
            size_t z = line.find('\n', a); if (z == std::string::npos) z = line.size();
            const std::string one = line.substr(a, z - a);
            if (!one.empty() && one.rfind("KGRN", 0) != 0) applyCommand(b, one.c_str());
            a = z + 1;
        }
        s += n; if (*s == '\n') ++s;
    }
    if (version < 3)                                       // before 0.4 a note lasted until the next one
        for (auto& P : b.patterns) for (int t = 0; t < kTracks; ++t) for (int st = 0; st < kSteps; ++st)
            if (P.gate[t][st] == kDefaultGate) P.gate[t][st] = kGateHold;
    return true;
}

// slice region of a kit for a sample of `frames` frames: [a, b) in frames; slice < 0 = everything
inline void sliceRegion(const Kit& k, int slice, uint32_t frames, double& a, double& b)
{
    if (slice < 0 || slice >= k.nSlices || frames == 0) { a = 0; b = frames; return; }
    a = (double)k.slice[slice] * 1e-6 * frames;
    b = slice + 1 < k.nSlices ? (double)k.slice[slice + 1] * 1e-6 * frames : (double)frames;
    if (b - a < 64.0) b = std::min<double>(frames, a + 64.0);
}

} // namespace kn
