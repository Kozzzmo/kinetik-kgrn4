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
    uint8_t locks[kTracks][kSteps][kTrackParams]; // per-step knob values (kNoLock = the knob)

    Pattern() { clear(); }
    void clear()
    {
        length = 16; swing = 50;
        for (int t = 0; t < kTracks; ++t) { len[t] = 16; speed[t] = 0; }
        std::memset(on, 0, sizeof(on));
        std::memset(locks, kNoLock, sizeof(locks));
    }
    void clearTrack(int t) { std::memset(on[t], 0, sizeof(on[t])); std::memset(locks[t], kNoLock, sizeof(locks[t])); }
    int trackStep(int t, int64_t k) const { const int n = len[t] ? len[t] : 16; return (int)(((k % n) + n) % n); }
    bool hasLocks(int t, int s) const { for (int k = 0; k < kTrackParams; ++k) if (locks[t][s][k] != kNoLock) return true; return false; }
    bool trackEmpty(int t) const { for (int s = 0; s < kSteps; ++s) if (on[t][s] || hasLocks(t, s)) return false; return true; }
    bool isDefault() const
    {
        if (length != 16 || swing != 50) return false;
        for (int t = 0; t < kTracks; ++t) { if (len[t] != 16 || speed[t] != 0 || !trackEmpty(t)) return false; }
        return true;
    }
    void rotateTrack(int t, int n)
    {
        const int L = len[t];
        uint8_t o[kSteps]; uint8_t lk[kSteps][kTrackParams];
        std::memcpy(o, on[t], sizeof(o)); std::memcpy(lk, locks[t], sizeof(lk));
        for (int s = 0; s < L; ++s) {
            const int d = ((s + n) % L + L) % L;
            on[t][d] = o[s]; std::memcpy(locks[t][d], lk[s], kTrackParams);
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
    uint32_t slice[kMaxSlices] = {};      // slice starts, millionths of the sample length (ascending, slice[0] usually 0)
};

struct Bank {
    Pattern patterns[kPatterns];
    Kit kit[kTracks];
    Chain chain;
    void initDefault() { for (auto& p : patterns) p.clear(); for (auto& k : kit) k = Kit(); chain = Chain(); }
};

static constexpr int kKitTouched = 1000;   // applyCommand: a kit changed

inline std::string serializeBank(const Bank& b)
{
    std::string s = "KGRN43\n";
    char buf[200];
    for (int t = 0; t < kTracks; ++t) {
        const Kit& k = b.kit[t];
        std::snprintf(buf, sizeof(buf), "M %d %d %d %d %d %d %d %d\n", t, k.material, k.mute, k.sliceMode, k.sliceAmount, k.mode, k.sync, k.legato); s += buf;
        if (k.nSlices) {
            std::snprintf(buf, sizeof(buf), "X %d %d", t, k.nSlices); s += buf;
            for (int i = 0; i < k.nSlices; ++i) { std::snprintf(buf, sizeof(buf), " %u", k.slice[i]); s += buf; }
            s += "\n";
        }
    }
    if (!b.chain.isDefault()) s += b.chain.line() + "\n";
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
                for (int k = 0; k < kTrackParams; ++k)
                    if (P.locks[t][st][k] != kNoLock) { std::snprintf(buf, sizeof(buf), "k %d %d %d %d %d\n", p, t, st, k, P.locks[t][st][k]); s += buf; }
        }
    }
    return s;
}

// one edit line; returns the pattern touched, kKitTouched, or -1
//   P p length swing      L p t len speed      S p t <64 x 0/1>      s p t step on      k p t step knob value (255 = clear)
//   kc p t step (clear the step's locks)      M t material mute sliceMode sliceAmount      X t n s1 s2 ...      U t mute
//   cl p      clt p t      cp src dst      rot p t n
inline int applyCommand(Bank& b, const char* line)
{
    char op[8] = {0};
    if (std::sscanf(line, "%7s", op) != 1) return -1;
    const char* a = line + std::strlen(op);
    int v[8] = {0};
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
    if (!std::strcmp(op, "M") && std::sscanf(a, "%d %d %d %d %d %d %d %d", &v[0], &v[1], &v[2], &v[3], &v[4], &v[5], &v[6], &v[7]) >= 5 && okT(v[0])) {
        Kit& k = b.kit[v[0]];
        k.material = (uint8_t)clampi(v[1], 0, 64); k.mute = v[2] ? 1 : 0; k.sliceMode = v[3] ? 1 : 0; k.sliceAmount = (uint8_t)clampi(v[4], 0, 100);
        k.mode = (uint8_t)clampi(v[5], 0, kGrainModes - 1); k.sync = v[6] ? 1 : 0; k.legato = v[7] ? 1 : 0;
        return kKitTouched;
    }
    if (!std::strcmp(op, "U") && std::sscanf(a, "%d %d", &v[0], &v[1]) == 2 && okT(v[0])) { b.kit[v[0]].mute = v[1] ? 1 : 0; return kKitTouched; }
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

// 0.2 banks (header KGRN41) had 16 knobs per track: their locks move to the new places
inline void convertLock41(std::string& line)
{
    static const int to[16] = {tpPOSITION, tpSLICE, tpSCAN, tpPITCH, tpSIZE, tpDENSITY, tpSPRAY, tpSPREAD,
                               tpATTACK, tpDECAY, tpFILTER, tpRESO, tpVOLUME, tpPAN, tpDELAY, tpREVERB};
    int p, t, st, k, val;
    if (line.rfind("k ", 0) != 0 || std::sscanf(line.c_str() + 2, "%d %d %d %d %d", &p, &t, &st, &k, &val) != 5 || k < 0 || k >= 16) return;
    char b[64]; std::snprintf(b, sizeof(b), "k %d %d %d %d %d", p, t, st, to[k], val); line = b;
}
inline bool deserializeBank(Bank& b, const char* text)
{
    if (!text || std::strncmp(text, "KGRN4", 5) != 0) return false;
    const bool v41 = std::strncmp(text, "KGRN41", 6) == 0;
    b.initDefault();
    const char* s = text; std::string line;
    while (*s) {
        const char* e = std::strchr(s, '\n');
        const size_t n = e ? (size_t)(e - s) : std::strlen(s);
        line.assign(s, n);
        if (v41) convertLock41(line);
        if (!line.empty() && line.rfind("KGRN", 0) != 0) applyCommand(b, line.c_str());
        s += n; if (*s == '\n') ++s;
    }
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
