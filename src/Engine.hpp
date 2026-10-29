// Kinetik KGRN-4 — engine: 4 granular tracks played by a step sequencer (per-step locks), echo and reverb sends.
// 0.3: three grain engines per track (CLASSIC / CLOUD / LOOP), stereo material, cubic interpolation, grains that never
// wrap inside a slice (they read on into the sample and fade out early at its ends), window shapes, reverse / octave /
// jitter per grain, level normalized by the grains really playing, and a LIVE input buffer (granulated live, captured).
#pragma once
#include "Bank.hpp"
#include "Fx.hpp"
#include "Sample.hpp"
#include "Materials.hpp"
#include <climits>
#include <cstring>
#include <vector>
#if defined(__SSE__) || defined(_M_X64) || defined(__x86_64__)
#include <xmmintrin.h>
#endif

namespace kn {

inline void knDenormalsOff()
{
#if defined(__SSE__) || defined(_M_X64) || defined(__x86_64__)
    _mm_setcsr(_mm_getcsr() | 0x8040);
#elif defined(__aarch64__)
    uint64_t fpcr; __asm__ __volatile__("mrs %0, fpcr" : "=r"(fpcr)); __asm__ __volatile__("msr fpcr, %0" :: "r"(fpcr | (1ull << 24)));
#endif
}

struct Transport { bool hostValid = false, hostPlaying = false; double beat = 0, bpm = 120; };
struct NoteIn { uint32_t frame; int track; int semis; float vel; bool on; };   // live notes (MIDI, UI)

// grain windows: five shapes the SHAPE knob morphs through (square, triangle, smooth, percussive, reversed percussive)
struct GrainWindows {
    static constexpr int kN = 1024;
    float t[5][kN + 2];
    GrainWindows()
    {
        for (int i = 0; i <= kN + 1; ++i) {
            const float x = std::min(1.f, i / (float)kN);
            const float edge = std::min(1.f, std::min(x, 1.f - x) / 0.012f);
            t[0][i] = edge;                                                         // square (12 ms-ish edges at 1 s)
            t[1][i] = 1.f - std::fabs(2.f * x - 1.f);                               // triangle
            t[2][i] = 0.5f - 0.5f * std::cos(kTwoPi * x);                           // Hann
            const float pe = std::min(1.f, x / 0.02f) * std::exp(-4.5f * x) * std::min(1.f, (1.f - x) / 0.03f);
            t[3][i] = pe;                                                           // percussive
        }
        for (int i = 0; i <= kN + 1; ++i) t[4][i] = t[3][std::max(0, kN - i)];       // reversed percussive
    }
    inline float at(int a, float ph) const { const float p = ph * kN; const int i = (int)p; return t[a][i] + (t[a][i + 1] - t[a][i]) * (p - i); }
};

class Engine {
public:
    static constexpr int kMaxGrains = 48;
    static constexpr int kFine = 24;
    float trackParam[kTracks][kTrackParams];
    float global[kGlobalParams];
    Bank bank;                                   // audio-side copy (the plugin syncs it)
    const SampleData* source[kTracks] = {};      // material of each track (set by the plugin; LIVE tracks use the input)
    const float* inL = nullptr; const float* inR = nullptr;   // the audio input of the next process() (nullptr = none)
    // outputs for the UI
    int outStep = -1; uint32_t outActive = 0; int outPattern = 0;
    float outPos[kTracks] = {-1.f, -1.f, -1.f, -1.f};   // play head of the newest voice, fraction of the material (-1 = silent)
    float inPeak = 0.f;                                  // input level (UI meter)
    int midiPattern = -1;

    Engine()
    {
        for (int t = 0; t < kTracks; ++t) for (int k = 0; k < kTrackParams; ++k) trackParam[t][k] = trackDefault(t, k);
        for (int g = 0; g < kGlobalParams; ++g) global[g] = globalDefault(g);
        bank.initDefault();
    }

    void init(double sampleRate)
    {
        sr = (float)sampleRate;
        dly.init(sr); rev.init(sr);
        liveCap = (int64_t)(kLiveSeconds * sr);
        liveL.assign((size_t)liveCap, 0.f); liveR.assign((size_t)liveCap, 0.f);
        for (int m = 0; m < SampleData::kMips; ++m) {
            liveML[m].assign((size_t)(liveCap >> (m + 1)) + 1, 0.f); liveMR[m].assign(liveML[m].size(), 0.f);
            liveMW[m] = 0; decL[m] = Dec2(); decR[m] = Dec2();
        }
        liveW = 0; barW = -1;
        reset();
    }
    void reset()
    {
        for (int t = 0; t < kTracks; ++t) { tr[t] = TrackState(); tr[t].rng.s = 0x9E3779B9u * (t + 1); }
        dly.clear(); rev.clear();
        wasPlaying = false; usingInternal = false; internalBeat = 0; prevPlayParam = false; lastK16 = INT64_MIN;
    }

    // a note now (UI audition / MIDI): no locks
    void noteOn(int t, int semis, float vel) { if (t >= 0 && t < kTracks) trig(t, nullptr, semis, vel, true); }
    void noteOff(int t) { if (t >= 0 && t < kTracks) for (auto& v : tr[t].v) if (v.active && v.live && v.stage < 2) v.stage = 2; }

    // ---- the input buffer (UI thread readers: they only look behind the write position)
    int64_t liveWritten() const { return liveW; }
    double barFrames() const { return 4.0 * 60.0 * sr / curBpm; }
    double liveWindow() const { return std::min((double)liveCap - sr, capBars(global[gCAPLEN]) * barFrames()); }
    // the last `bars` complete bars of the input (while playing; otherwise the last bars of time), as a stereo sample
    SamplePtr capture(int bars) const
    {
        const int64_t W = liveW;
        const int64_t len = std::min<int64_t>(liveCap - (int64_t)sr, (int64_t)(bars * barFrames()));
        int64_t end = (playingNow && barW > 0 && W - barW < (int64_t)barFrames() + 64) ? barW : W;
        int64_t start = end - len;
        start = std::max<int64_t>(start, std::max<int64_t>(0, W - liveCap + 1024));
        if (end - start < 256) return nullptr;
        auto d = std::make_shared<SampleData>();
        const size_t n = (size_t)(end - start);
        d->l.resize(n); d->r.resize(n); d->pcm.resize(n); d->sr = sr;
        float peak = 0.f;
        for (size_t i = 0; i < n; ++i) {
            const size_t k = (size_t)((start + (int64_t)i) % liveCap);
            d->l[i] = liveL[k]; d->r[i] = liveR[k];
            peak = std::max(peak, std::max(std::fabs(d->l[i]), std::fabs(d->r[i])));
        }
        if (peak < 1e-4f) return nullptr;                        // nothing came in
        const float g = std::min(8.f, 0.89f / peak);             // -1 dBFS, like a loaded file
        const size_t fade = std::min<size_t>(n / 4, (size_t)(0.002f * sr));
        for (size_t i = 0; i < n; ++i) {
            float f = g;
            if (i < fade) f *= (float)i / fade; else if (n - 1 - i < fade) f *= (float)(n - 1 - i) / fade;
            d->l[i] *= f; d->r[i] *= f; d->pcm[i] = 0.5f * (d->l[i] + d->r[i]);
        }
        return d;
    }
    // min / max of the LIVE window in `cols` columns (for the waveform)
    void liveWave(float* mn, float* mx, int cols) const
    {
        const int64_t W = liveW; const double win = liveWindow();
        const int64_t a = W - (int64_t)win;
        for (int c = 0; c < cols; ++c) {
            const int64_t s0 = a + (int64_t)(win * c / cols), s1 = a + (int64_t)(win * (c + 1) / cols);
            const int64_t step = std::max<int64_t>(1, (s1 - s0) / 48);
            float lo = 0.f, hi = 0.f;
            for (int64_t s = s0; s < s1; s += step) {
                if (s < 0) continue;
                const size_t k = (size_t)(s % liveCap); const float v = 0.5f * (liveL[k] + liveR[k]);
                lo = std::min(lo, v); hi = std::max(hi, v);
            }
            mn[c] = lo; mx[c] = hi;
        }
    }

    void process(float* outL, float* outR, uint32_t frames, const Transport& tp, const NoteIn* notes = nullptr, uint32_t nNotes = 0)
    {
        knDenormalsOff();
        if (frames == 0) return;
        const bool playParam = global[gPLAY] > 0.5f;
        double bpm = tp.hostValid && tp.bpm > 1.0 ? tp.bpm : (double)global[gTEMPO];
        bpm = std::max(20.0, std::min(400.0, bpm));
        curBpm = bpm;
        bool playing = false, restarted = false; double b0 = 0;
        if (tp.hostValid && tp.hostPlaying) {
            playing = true; b0 = tp.beat;
            if (!wasPlaying || usingInternal || std::fabs(b0 - prevB1) > 0.01) restarted = true;
            usingInternal = false;
        } else if (playParam) {
            if (!prevPlayParam || !wasPlaying || !usingInternal) { internalBeat = 0; restarted = true; }
            playing = true; b0 = internalBeat; usingInternal = true;
        }
        prevPlayParam = playParam;
        playingNow = playing;
        if (wasPlaying && !playing) for (auto& T : tr) for (auto& v : T.v) if (v.active && !v.live) v.stage = 3;   // stop: fade out
        if (restarted) { for (auto& T : tr) T.lastK = INT64_MIN; barW = -1; lastBar = INT64_MIN; }
        const double bpf = bpm / (60.0 * sr);
        const double b1 = b0 + bpf * frames;
        const int want = clampi(midiPattern >= 0 ? midiPattern : (int)global[gPATTERN], 0, kPatterns - 1);
        if (!playing || restarted) {
            const int c = playing ? chainPl.start(bank.chain) : -1;
            if (!playing) chainPl.reset();
            curPattern = c >= 0 ? c : want;
        }
        spb = 60.0 * sr / bpm;
        dly.set(global[gDLY_TIME], global[gDLY_FB], spb);
        rev.set(global[gREV_SIZE], global[gREV_TONE]);
        const float outTarget = trimGain(global[gOUTPUT]);
        if (outG < 0.f) outG = outTarget;
        const float sm = 1.f - std::exp(-1.f / (0.02f * sr));
        const bool freeze = global[gFREEZE] > 0.5f;
        // the input: into the LIVE buffer (unless FROZEN), and its level for the meter
        {
            float pk = 0.f;
            if (inL && inR) for (uint32_t i = 0; i < frames; ++i) pk = std::max(pk, std::max(std::fabs(inL[i]), std::fabs(inR[i])));
            inPeak = std::max(pk, inPeak * 0.93f);
        }
        uint32_t ni = 0;

        uint32_t pos = 0;
        while (pos < frames) {
            const uint32_t n = std::min<uint32_t>(kSeg, frames - pos);
            // live notes in this segment
            while (ni < nNotes && notes[ni].frame < pos + n) {
                const NoteIn& e = notes[ni++];
                if (e.on) noteOn(e.track, e.semis, e.vel); else noteOff(e.track);
            }
            const double beat = b0 + pos * bpf;
            if (playing) {
                {
                    const Pattern& P = bank.patterns[curPattern];
                    const int64_t k16 = (int64_t)std::floor(beat / 0.25 + 1e-9), len = std::max<int>(1, P.length);
                    if (lastK16 != INT64_MIN && k16 / len != lastK16 / len) { const int c = chainPl.next(bank.chain); curPattern = c >= 0 ? c : want; }
                }
                lastK16 = (int64_t)std::floor(beat / 0.25 + 1e-9);
                const Pattern& P = bank.patterns[curPattern];
                for (int t = 0; t < kTracks; ++t) {
                    TrackState& T = tr[t];
                    const double sbt = 0.25 / kSpeedMult[P.speed[t] % kSpeeds];
                    const double off = (P.swing / 100.0 * 2.0 - 1.0) * sbt;
                    int64_t k = (int64_t)std::floor(beat / sbt + 1e-9);
                    const double start = k * sbt + ((k & 1) ? off : 0.0);
                    if (beat < start) --k;                       // the swung step has not started yet
                    if (k < 0 || k == T.lastK) continue;
                    T.lastK = k;
                    const int step = P.trackStep(t, k);
                    T.step = step;
                    if (P.on[t][step] && !bank.kit[t].mute) trig(t, P.locks[t][step], 0, 1.f, false);
                }
            } else lastK16 = INT64_MIN;
            outStep = playing ? (int)((int64_t)std::floor(beat / 0.25 * kFine) % (32768LL * kFine)) : -1;

            // per track settings for this segment
            for (int t = 0; t < kTracks; ++t) setupTrack(t);
            for (uint32_t i = 0; i < n; ++i) {
                // the input buffer (bar starts are remembered for CAPTURE)
                if (!freeze) liveWrite(inL ? inL[pos + i] : 0.f, inR ? inR[pos + i] : 0.f);
                const double bt = playing ? beat + i * bpf : freeBeat + (pos + i) * bpf;
                if (playing) { const int64_t bar = (int64_t)std::floor(bt / 4.0 + 1e-9); if (bar != lastBar) { lastBar = bar; barW = liveW; } }
                float mixL = 0.f, mixR = 0.f, dSend = 0.f, dSendR = 0.f, rL = 0.f, rR = 0.f;
                for (int t = 0; t < kTracks; ++t) {
                    TrackState& T = tr[t];
                    if (!T.busy) continue;
                    float l = 0.f, r = 0.f;
                    for (auto& v : T.v) if (v.active) renderVoice(t, v, l, r, bt, playing);
                    if (T.filterOn) { T.fl.tick(l); T.fr.tick(r); l = T.hp ? T.fl.hp : T.fl.lp; r = T.hp ? T.fr.hp : T.fr.lp; }
                    T.vol += (T.volT - T.vol) * sm;
                    l *= T.vol; r *= T.vol;
                    mixL += l; mixR += r;
                    dSend += l * T.dSend; dSendR += r * T.dSend; rL += l * T.rSend; rR += r * T.rSend;
                }
                float el, er, vl, vr;
                dly.process(dSend, dSendR, el, er);
                rev.process(rL, rR, vl, vr);
                outG += (outTarget - outG) * sm;
                const float L = (mixL + el + vl) * outG, R = (mixR + er + vr) * outG;
                outL[pos + i] = safety(L); outR[pos + i] = safety(R);
            }
            pos += n;
        }
        if (!playing) freeBeat += bpf * frames; else freeBeat = b1;
        // outputs
        uint32_t act = 0;
        for (int t = 0; t < kTracks; ++t) {
            float best = -1.f; uint32_t newest = 0;
            for (auto& v : tr[t].v) if (v.active && v.env > 0.01f && v.serial >= newest) { newest = v.serial; best = (float)v.headFrac; }
            outPos[t] = best;
            if (best >= 0.f) act |= 1u << t;
        }
        outActive = act; outPattern = curPattern;
        wasPlaying = playing; prevB1 = b1;
        if (playing && usingInternal) internalBeat = b1;
    }

    int activeGrains(int t) const { int n = 0; for (auto& v : tr[t].v) if (v.active) n += v.ng; return n; }   // tests
    void spawnStats(int t, int& all, int& rev, int& oct) const { all = tr[t].nSpawn; rev = tr[t].nRev; oct = tr[t].nOct; }

private:
    // where a grain reads: a sample (linear) or the input ring (absolute write positions)
    // level 0 = the material itself, levels 1..3 = band-limited copies at 1/2, 1/4, 1/8 of the rate (pitched-up grains
    // read the level where they move at most ~1 frame per output frame: no aliasing)
    static constexpr int kLevels = 1 + SampleData::kMips;
    struct Src {
        const float* l[kLevels] = {}; const float* r[kLevels] = {}; int64_t n[kLevels] = {}; double off[kLevels] = {};
        int levels = 1; bool ring = false; double sr = 48000.0;
        int64_t W = 0; bool growing = false;      // ring: frames written so far (level 0), still writing
        inline float rd(int lv, const float* d, int64_t i) const
        {
            const int64_t m = n[lv];
            if (ring) { int64_t k = i % m; if (k < 0) k += m; return d[k]; }
            return d[i < 0 ? 0 : (i >= m ? m - 1 : i)];
        }
        // cubic (Catmull-Rom) read of one channel at a fractional position (in the level's frames)
        inline float cubic(int lv, const float* d, double pos) const
        {
            const double fl = std::floor(pos); const int64_t i = (int64_t)fl; const float f = (float)(pos - fl);
            const float y0 = rd(lv, d, i - 1), y1 = rd(lv, d, i), y2 = rd(lv, d, i + 1), y3 = rd(lv, d, i + 2);
            const float c1 = 0.5f * (y2 - y0), c2 = y0 - 2.5f * y1 + 2.f * y2 - 0.5f * y3, c3 = 0.5f * (y3 - y0) + 1.5f * (y1 - y2);
            return ((c3 * f + c2) * f + c1) * f + y1;
        }
        double lo() const { return ring ? (double)std::max<int64_t>(0, W - n[0] + 4096) : 1.0; }    // oldest safe position (level 0 frames)
        double hi() const { return ring ? (double)W - 256.0 : (double)n[0] - 3.0; }                 // newest safe position
    };
    struct Grain { double pos, inc; float ph, dph, gl, gr, offR; int8_t wa; float wf, xf; int8_t lvl; };
    // streaming half-band decimator (the input ring's levels): output j is centred on input 2j - 22
    struct Dec2 {
        float x[47] = {}; int w = 0, ph = 0;
        static const float* taps()
        {
            static float h[47]; static bool made = false;
            if (!made) {
                double sum = 0;
                for (int i = 0; i < 47; ++i) {
                    const double t = i - 23, win = 0.42 - 0.5 * std::cos(2.0 * kPi * i / 46.0) + 0.08 * std::cos(4.0 * kPi * i / 46.0);
                    h[i] = (float)((t == 0 ? 0.45 : std::sin(kPi * 0.45 * t) / (kPi * t)) * win); sum += h[i];
                }
                for (float& v : h) v = (float)(v / sum);
                made = true;
            }
            return h;
        }
        // push one input; true (with y) every second input
        inline bool push(float v, float& y)
        {
            x[w] = v; w = (w + 1) % 47;
            if (++ph < 2) return false;
            ph = 0;
            const float* h = taps(); float acc = 0.f;
            for (int k = 0; k < 47; ++k) acc += h[k] * x[(w + k) % 47];
            y = acc; return true;
        }
    };
    struct Voice {
        bool active = false, live = false; int stage = 0;    // 0 attack, 1 hold / decay, 2 release (note off), 3 choke
        float env = 0.f, vel = 1.f; int semis = 0; uint32_t serial = 0;
        uint8_t lock[kTrackParams];
        double scan = 0.0;                                   // frames moved by SCAN since the note started
        double headFrac = 0.0, liveStart = 0.0;
        float firstSkip = 0.f;
        float timer = 0.f, ltimer[4] = {}, lDet[4] = {}, lPan[4] = {}; bool first = true; int64_t gridK = INT64_MIN;
        Grain g[kMaxGrains]; int ng = 0;
        float normA = 1.f, normP = 1.f;                      // smoothed sum of the windows playing (and of their squares)
        // per segment
        int mode = gmClassic; double ra = 0, rb = 1, head = 0, scanInc = 0, grainInc = 1, sprayFr = 0; float grainLen = 1000.f, overlap = 2.f;
        float incoh = 0.f;                                    // 0 = the grains add up in phase (amplitude sum), 1 = at random (power sum)
        float shape = 0.5f, jitter = 0.f, reverse = 0.f, octave = 0.f, spread = 0.f; bool random = false, sync = false; double syncBeats = 0.25;
        float atkInc = 1.f, decCoef = 0.f, relCoef = 0.f; bool hold = true; float panL = 0.707f, panR = 0.707f; int copies = 1;
        float p(const float* knobs, int k) const { return lock[k] != kNoLock ? (float)lock[k] : knobs[k]; }
    };
    struct TrackState {
        Voice v[2]; int64_t lastK = INT64_MIN; int step = -1; uint32_t serial = 0; Rng rng;
        bool busy = false, filterOn = false, hp = false; Svf fl, fr; float vol = 0.f, volT = 0.f, dSend = 0.f, rSend = 0.f;
        Src src;
        int nSpawn = 0, nRev = 0, nOct = 0;            // tests
    };
    static constexpr uint32_t kSeg = 16;
    float sr = 48000.f; double curBpm = 120.0, spb = 24000.0, freeBeat = 0.0;
    TrackState tr[kTracks];
    GrainWindows win;
    PingPong dly; Reverb rev;
    int64_t lastK16 = INT64_MIN, lastBar = INT64_MIN;
    int curPattern = 0;
    ChainPlayer chainPl;
    bool wasPlaying = false, usingInternal = false, prevPlayParam = false, playingNow = false;
    double internalBeat = 0, prevB1 = 0;
    float outG = -1.f;
    // input
    std::vector<float> liveL, liveR; int64_t liveCap = 1, liveW = 0, barW = -1;
    std::vector<float> liveML[SampleData::kMips], liveMR[SampleData::kMips]; int64_t liveMW[SampleData::kMips] = {};
    Dec2 decL[SampleData::kMips], decR[SampleData::kMips];
    inline void liveWrite(float l, float r)
    {
        const size_t k = (size_t)(liveW % liveCap);
        liveL[k] = l; liveR[k] = r; ++liveW;
        for (int m = 0; m < SampleData::kMips; ++m) {
            float yl = 0.f, yr = 0.f;
            if (!decL[m].push(l, yl)) break;
            decR[m].push(r, yr);
            const int64_t cap = (int64_t)liveML[m].size();
            liveML[m][(size_t)(liveMW[m] % cap)] = yl; liveMR[m][(size_t)(liveMW[m] % cap)] = yr; ++liveMW[m];
            l = yl; r = yr;
        }
    }

    // above 1 the level bends smoothly towards 1.5 (continuous, no step)
    static inline float safety(float x)
    {
        const float a = std::fabs(x);
        if (a <= 1.f) return x;
        const float y = 1.f + 0.5f * std::tanh((a - 1.f) * 2.f);
        return x < 0.f ? -y : y;
    }
    bool isLive(int t) const { return bank.kit[t].material == kMatLive; }
    bool hasSource(int t) const { return isLive(t) ? liveCap > 1 : (source[t] && source[t]->pcm.size() >= 64); }

    void trig(int t, const uint8_t* locks, int semis, float vel, bool live)
    {
        TrackState& T = tr[t];
        if (!hasSource(t)) return;
        // LEGATO: a step changes the sound (its locks, its pitch) without starting the note again: the cloud goes on
        if (bank.kit[t].legato && !live) {
            Voice* cur = nullptr;
            for (auto& v : T.v) if (v.active && !v.live && v.stage < 2 && (!cur || v.serial > cur->serial)) cur = &v;
            if (cur) {
                if (locks) std::memcpy(cur->lock, locks, kTrackParams); else std::memset(cur->lock, kNoLock, kTrackParams);
                cur->semis = semis; cur->vel = vel;
                return;
            }
        }
        // the playing voice fades out quickly; the new one takes the other slot (or the quietest)
        int slot = -1;
        for (int i = 0; i < 2; ++i) if (!T.v[i].active) { slot = i; break; }
        if (slot < 0) slot = T.v[0].env < T.v[1].env ? 0 : 1;
        for (int i = 0; i < 2; ++i) if (i != slot && T.v[i].active) T.v[i].stage = 3;
        Voice& v = T.v[slot];
        v.active = true; v.live = live; v.stage = 0; v.env = 0.f; v.vel = vel; v.semis = semis; v.serial = ++T.serial;
        if (locks) std::memcpy(v.lock, locks, kTrackParams); else std::memset(v.lock, kNoLock, kTrackParams);
        v.scan = 0.0; v.timer = 0.f; v.ng = 0; v.first = true; v.gridK = INT64_MIN; v.normA = v.normP = 1.f;
        v.liveStart = (double)liveW;
        for (int j = 0; j < 4; ++j) {                      // LOOP copies: fixed detune / pan for this note
            v.ltimer[j] = -1e9f;                           // set at the first sample (copies staggered)
            v.lDet[j] = j == 0 ? 0.f : T.rng.next();
            v.lPan[j] = j == 0 ? 0.f : (j & 1 ? -1.f : 1.f) * (0.5f + 0.5f * T.rng.uni());
        }
    }

    void setupTrack(int t)
    {
        TrackState& T = tr[t];
        T.busy = false;
        if (!hasSource(t)) { for (auto& v : T.v) v.active = false; return; }
        const bool live = isLive(t);
        Src& S = T.src;
        if (live) {
            S.l[0] = liveL.data(); S.r[0] = liveR.data(); S.n[0] = liveCap; S.off[0] = 0.0;
            for (int m = 0; m < SampleData::kMips; ++m) {
                S.l[m + 1] = liveML[m].data(); S.r[m + 1] = liveMR[m].data(); S.n[m + 1] = (int64_t)liveML[m].size();
                S.off[m + 1] = 22.0 * ((1 << (m + 1)) - 1);
            }
            S.levels = kLevels; S.ring = true; S.sr = sr; S.W = liveW; S.growing = global[gFREEZE] < 0.5f;
        } else {
            const SampleData* src = source[t];
            const bool st = src->stereo();
            S.l[0] = st ? src->l.data() : src->pcm.data(); S.r[0] = st ? src->r.data() : src->pcm.data();
            S.n[0] = (int64_t)src->pcm.size(); S.off[0] = 0.0; S.levels = 1;
            if (src->hasMips())
                for (int m = 0; m < SampleData::kMips; ++m) {
                    S.l[m + 1] = src->mipL[m].data(); S.r[m + 1] = src->mipR[m].empty() ? src->mipL[m].data() : src->mipR[m].data();
                    S.n[m + 1] = (int64_t)src->mipL[m].size(); S.off[m + 1] = 0.0; S.levels = m + 2;
                }
            S.ring = false; S.sr = src->sr; S.W = S.n[0]; S.growing = false;
        }
        const float* knobs = trackParam[t];
        const Kit& kit = bank.kit[t];
        const double srcRatio = S.sr / sr;
        Voice* newest = nullptr;
        for (auto& v : T.v) {
            if (!v.active) continue;
            T.busy = true;
            if (!newest || v.serial > newest->serial) newest = &v;
            v.mode = kit.mode; v.sync = kit.sync != 0;
            double rl;
            if (live) {                                    // the window of the input that was there when the note started
                const double winLen = liveWindow();
                v.ra = v.liveStart - winLen; v.rb = v.liveStart; rl = winLen;
            } else {
                const int slice = sliceOf(v.p(knobs, tpSLICE), kit.nSlices);
                sliceRegion(kit, slice, (uint32_t)S.n[0], v.ra, v.rb);
                rl = v.rb - v.ra;
            }
            rl = std::max(64.0, rl);
            v.scanInc = scanSpeed(v.p(knobs, tpSCAN)) * srcRatio;
            double hf = v.p(knobs, tpPOSITION) / 127.0 + v.scan / rl;
            if (!live) hf -= std::floor(hf);
            v.head = v.ra + hf * rl;
            if (live) {                                    // stay inside what the buffer still holds, never ahead of the input
                while (v.head > S.hi() - 64.0) { v.head -= rl; v.scan -= rl; }
                while (v.head < S.lo() + 64.0 && v.head + rl < S.hi()) { v.head += rl; v.scan += rl; }
                v.headFrac = clampf((float)((v.head - (S.W - rl)) / rl), 0.f, 1.f);
            } else v.headFrac = v.head / (double)S.n[0];
            v.grainInc = srcRatio * std::pow(2.0, (pitchSemis(v.p(knobs, tpPITCH)) + v.semis) / 12.0);
            const float sz = v.p(knobs, tpSIZE);
            v.syncBeats = kSyncBeats[syncBin(sz)];
            v.grainLen = (float)std::max(48.0, v.sync ? v.syncBeats * spb : (double)grainSec(sz) * sr);
            const float dn = v.p(knobs, tpDENSITY);
            v.overlap = v.mode == gmClassic ? classicOverlap(dn) : (v.mode == gmCloud ? cloudOverlap(dn) : 1.f);
            v.random = cloudRandom(dn); v.copies = loopCopies(dn);
            v.shape = v.p(knobs, tpSHAPE) / 127.f;
            v.sprayFr = spraySec(v.p(knobs, tpSPRAY)) * S.sr;
            v.jitter = v.p(knobs, tpJITTER) / 127.f; v.reverse = v.p(knobs, tpREVERSE) / 127.f;
            v.octave = v.p(knobs, tpOCTAVE) / 127.f; v.spread = v.p(knobs, tpSPREAD) / 127.f;
            {   // how far apart the grains sounding together read (source frames): a few ms apart = random phases (power sum),
                // the same place = in phase (amplitude sum); the level is normalized between the two
                const double gl = v.grainLen, ai = std::fabs(v.grainInc);
                double spreadFr = std::fabs(v.grainInc - v.scanInc) * gl * (v.mode == gmLoop ? (0.005 + v.shape * 0.5) : 1.0)
                                + 2.0 * v.sprayFr + (v.reverse + 0.5f * v.jitter) * gl * ai;
                if (v.mode == gmClassic && std::fabs(v.scanInc) < 0.05) spreadFr += (1.0 - std::fabs(v.scanInc) / 0.05) * 0.5 * gl * ai;
                if (v.mode == gmLoop && v.copies > 1) spreadFr += gl * ai / v.copies;
                v.incoh = clampf((float)(spreadFr / (0.004 * S.sr)), 0.f, 1.f);
            }
            v.atkInc = 1.f / std::max(1.f, attackSec(v.p(knobs, tpATTACK)) * sr);
            const float dk = v.p(knobs, tpDECAY);
            v.hold = decayHold(dk);
            v.decCoef = decayCoef(decaySec(dk), sr);
            v.relCoef = decayCoef(v.hold ? 0.25f : std::min(decaySec(dk), 2.f), sr);
            const float pan = (v.p(knobs, tpPAN) - 64.f) / 63.f;
            v.panL = std::cos((pan + 1.f) * kPi * 0.25f); v.panR = std::sin((pan + 1.f) * kPi * 0.25f);
        }
        if (!T.busy) return;
        // the track's filter, level and sends follow its newest note
        const Voice& nv = *newest;
        const float f = nv.p(knobs, tpFILTER);
        const bool on = std::fabs(f - 64.f) >= 1.f;
        if (on && !T.filterOn) { T.fl.reset(); T.fr.reset(); }
        T.filterOn = on; T.hp = f > 64.f;
        if (on) { const float q = resoQ(nv.p(knobs, tpRESO), 0.6f, 12.f); T.fl.setup(filterHz(f), q, sr); T.fr.setup(filterHz(f), q, sr); }
        T.volT = volumeGain(nv.p(knobs, tpVOLUME));
        if (T.vol == 0.f) T.vol = T.volT;
        const float d = nv.p(knobs, tpDELAY) / 127.f, r = nv.p(knobs, tpREVERB) / 127.f;
        T.dSend = d * d; T.rSend = r * r;
    }

    // a new grain from the play head; layer >= 0 = a LOOP copy
    void spawn(TrackState& T, Voice& v, int layer)
    {
        if (v.ng >= kMaxGrains) return;
        const Src& S = T.src;
        Rng& rng = T.rng;
        const bool loop = v.mode == gmLoop;
        // length (output frames) and speed (source frames per output frame)
        const double lxf = 0.005 + v.shape * 0.5;                // LOOP: each pass lasts SIZE plus its crossfade
        double len = loop ? v.grainLen * (1.0 + lxf) : (double)v.grainLen, inc = v.grainInc;
        if (!loop && v.jitter > 0.f) len *= 1.0 + 0.5 * v.jitter * rng.next();
        float semis = 0.f;
        if (v.jitter > 0.f) semis += (loop ? (layer > 0 ? v.lDet[layer] * 0.35f * v.jitter : 0.f) : rng.next() * v.jitter * 0.3f);   // up to 30 cents: in tune
        if (v.octave > 0.f && rng.uni() < v.octave) { semis += rng.uni() < 0.75f ? 12.f : -12.f; ++T.nOct; }
        if (semis != 0.f) inc *= std::pow(2.0, semis / 12.0);
        if (v.reverse > 0.f && rng.uni() < v.reverse) inc = -inc;
        // start: the play head, SPRAY, and in CLASSIC a little random spread when frozen (no buzz)
        double p = v.head;
        if (v.sprayFr > 0.0) p += v.sprayFr * rng.next();
        if (v.mode == gmClassic) {
            const double s = std::fabs(v.scanInc);
            if (s < 0.05) p += (1.0 - s / 0.05) * 0.5 * len * std::fabs(inc) * rng.next();
        }
        if (!loop && std::fabs(v.grainInc - v.scanInc) > 0.01)    // pitched against the scan: a few ms of random start
            p += 0.003 * S.sr * rng.next();                        // breaks the comb of evenly spaced grains
        if (inc < 0.0 && !loop) p += 0.5 * len * -inc;            // a reversed grain is centred on the head
        if (inc < 0.0 && loop) p += len * -inc;                    // a reversed loop plays the same stretch backwards
        // the whole grain inside what can be read: near an end its start moves back (never a wrap, never a gap);
        // only a grain longer than the whole material is shortened (its window still runs to the end)
        const double lo = S.lo(), hi = S.hi();
        const double ahead = inc > 0.0 ? inc - (S.growing ? 1.0 : 0.0) : 0.0;      // a growing ring: the input moves on too
        const double back = inc < 0.0 ? -inc + (S.growing ? 1.0 : 0.0) : 0.0;
        const double span = len * (inc > 0.0 ? std::max(0.0, ahead) : back);        // how far the grain travels
        if (inc > 0.0) { if (p + span > hi) p = hi - span; if (p < lo) p = lo; }
        else { if (p - span < lo) p = lo + span; if (p > hi) p = hi; }
        double avail = 1e12;
        if (inc > 0.0 && ahead > 1e-9) avail = (hi - p) / ahead;
        if (inc < 0.0) avail = (p - lo) / back;
        len = std::min(len, avail);
        if (len < 256.0) return;
        Grain& g = v.g[v.ng++];
        ++T.nSpawn; if (inc < 0.0) ++T.nRev;
        int lvl = 0;                                              // the band-limited level this grain reads
        while (lvl + 1 < S.levels && std::fabs(inc) / (double)(1 << lvl) > 1.12) ++lvl;
        const double sc = 1.0 / (double)(1 << lvl);
        g.lvl = (int8_t)lvl;
        g.pos = (p + S.off[lvl]) * sc; g.inc = inc * sc;
        g.dph = (float)(1.0 / len);
        if (loop) { g.wa = -1; g.xf = (float)(lxf / (1.0 + lxf)); g.wf = 0.f; }
        else { const float a = v.shape * 4.f; g.wa = (int8_t)std::min(3, (int)a); g.wf = a - g.wa; g.xf = 0.f; }
        // the first grain of a note starts at its loudest point (the note's ATTACK fades it in)
        g.ph = 0.f;
        if (v.first && layer <= 0) {
            if (loop) g.ph = g.xf;
            else { static const float peak[5] = {0.02f, 0.5f, 0.5f, 0.03f, 0.5f}; g.ph = peak[g.wa] + (peak[g.wa + 1] - peak[g.wa]) * g.wf; }
            v.first = false;
            v.firstSkip = (float)(g.ph * len);                // the next grain comes that much sooner
        }
        // stereo: SPREAD pans the grain and delays its right channel a little (width from mono material too)
        float pr = v.spread * rng.next();
        if (loop && layer > 0) pr = v.spread * v.lPan[layer];
        const float pl = clampf(1.f - pr, 0.f, 2.f) * 0.5f, pR = clampf(1.f + pr, 0.f, 2.f) * 0.5f;
        float amp = 1.f;
        if (v.jitter > 0.f && !loop) amp -= 0.5f * v.jitter * rng.uni();
        g.gl = v.panL * pl * 1.414f * amp; g.gr = v.panR * pR * 1.414f * amp;
        g.offR = (float)(v.spread * 0.004 * S.sr * rng.next() * sc);
    }

    inline float window(const Grain& g) const
    {
        if (g.wa < 0) { const float x = std::min(g.ph, 1.f - g.ph); return x >= g.xf ? 1.f : x / g.xf; }
        const float a = win.at(g.wa, g.ph);
        return g.wf > 0.f ? a + (win.at(g.wa + 1, g.ph) - a) * g.wf : a;
    }

    inline void renderVoice(int t, Voice& v, float& outL, float& outR, double beat, bool playing)
    {
        TrackState& T = tr[t];
        const Src& S = T.src;
        // envelope
        switch (v.stage) {
        case 0: v.env += v.atkInc; if (v.env >= 1.f) { v.env = 1.f; v.stage = 1; } break;
        case 1: if (!v.hold) v.env *= v.decCoef; break;
        case 2: v.env *= v.relCoef; break;
        default: v.env -= 1.f / (0.015f * sr); break;
        }
        if (v.env <= 1e-4f && v.stage >= 1) { v.active = false; v.ng = 0; return; }
        // new grains
        switch (v.mode) {
        case gmClassic:
            v.timer -= 1.f;
            if (v.timer <= 0.f) {
                v.firstSkip = 0.f;
                spawn(T, v, -1);
                const float iv = v.grainLen / v.overlap;
                v.timer += std::max(1.f, iv * (1.f + 0.3f * v.jitter * T.rng.next()) - v.firstSkip);
            }
            break;
        case gmCloud:
            if (v.sync && !v.random && playing) {           // regular + SYNC: grains on the tempo grid
                const double gb = v.syncBeats / std::max(1.f, std::exp2(std::round(std::log2(v.overlap))));
                const int64_t k = (int64_t)std::floor(beat / gb + 1e-9);
                if (k != v.gridK) { v.gridK = k; spawn(T, v, -1); }
                break;
            }
            v.timer -= 1.f;
            if (v.timer <= 0.f) {
                v.firstSkip = 0.f;
                spawn(T, v, -1);
                const float iv = v.grainLen / v.overlap;
                if (v.firstSkip > 0.f) { v.timer += std::max(1.f, iv - v.firstSkip); break; }
                if (v.random) v.timer += iv * clampf(-std::log(std::max(1e-4f, T.rng.uni())), 0.05f, 4.f);
                else v.timer += iv * (1.f + 0.3f * v.jitter * T.rng.next());
            }
            break;
        default: {                                          // LOOP: each copy restarts its loop every SIZE minus the crossfade
            const float period = v.grainLen;                 // one pass every SIZE (a note value with SYNC)
            for (int j = 0; j < v.copies; ++j) {
                if (v.ltimer[j] < -1e8f) v.ltimer[j] = j == 0 ? 0.f : period * j / v.copies;   // the copies come in staggered
                v.ltimer[j] -= 1.f;
                if (v.ltimer[j] <= 0.f) {
                    spawn(T, v, j);
                    v.ltimer[j] += period * (1.f + 0.15f * v.jitter * T.rng.next() * (j > 0));
                }
            }
            break;
        }
        }
        // move the play head
        v.scan += v.scanInc;
        // grains
        float l = 0.f, r = 0.f, sw = 0.f, sw2 = 0.f;
        for (int i = 0; i < v.ng;) {
            Grain& g = v.g[i];
            const float w = window(g);
            l += S.cubic(g.lvl, S.l[g.lvl], g.pos) * w * g.gl;
            r += S.cubic(g.lvl, S.r[g.lvl], g.pos + g.offR) * w * g.gr;
            sw += w; sw2 += w * w;
            g.pos += g.inc;
            g.ph += g.dph;
            if (g.ph >= 1.f) { g = v.g[--v.ng]; continue; }
            ++i;
        }
        // level: divided by how many grains really sound (coherent for CLASSIC / LOOP, power for CLOUD)
        const float c = 1.f / (0.03f * sr);
        v.normA += (sw - v.normA) * c; v.normP += (sw2 - v.normP) * c;
        const float A = std::max(0.f, v.normA), P = std::sqrt(std::max(0.f, v.normP));
        const float norm = v.incoh >= 0.999f ? P : (v.incoh <= 0.001f ? A : std::pow(A, 1.f - v.incoh) * std::pow(std::max(P, 1e-6f), v.incoh));
        const float a = v.env * v.vel / std::max(1.f, norm);
        outL += l * a; outR += r * a;
    }
};

} // namespace kn
