// Kinetik KGRN-4 — engine: 4 granular tracks played by a step sequencer (per-step locks), echo and reverb sends
#pragma once
#include "Bank.hpp"
#include "Fx.hpp"
#include "Sample.hpp"
#include <climits>
#include <cstring>
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

// raised-cosine grain window, 1024 points + guard
struct GrainWindow {
    float t[1026];
    GrainWindow() { for (int i = 0; i < 1026; ++i) { const float x = std::min(1.f, i / 1024.f); t[i] = 0.5f - 0.5f * std::cos(kTwoPi * x); } }
    inline float at(float ph) const { const float p = ph * 1024.f; const int i = (int)p; return t[i] + (t[i + 1] - t[i]) * (p - i); }
};

class Engine {
public:
    static constexpr int kMaxGrains = 40;
    static constexpr int kFine = 24;
    float trackParam[kTracks][kTrackParams];
    float global[kGlobalParams];
    Bank bank;                                   // audio-side copy (the plugin syncs it)
    const SampleData* source[kTracks] = {};      // material of each track (set by the plugin)
    // outputs for the UI
    int outStep = -1; uint32_t outActive = 0; int outPattern = 0;
    float outPos[kTracks] = {-1.f, -1.f, -1.f, -1.f};   // play head of the newest voice, fraction of the sample (-1 = silent)
    int midiPattern = -1;

    Engine()
    {
        for (int t = 0; t < kTracks; ++t) for (int k = 0; k < kTrackParams; ++k) trackParam[t][k] = trackParamDefault(k);
        for (int g = 0; g < kGlobalParams; ++g) global[g] = globalDefault(g);
        bank.initDefault();
    }

    void init(double sampleRate)
    {
        sr = (float)sampleRate;
        dly.init(sr); rev.init(sr);
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

    void process(float* outL, float* outR, uint32_t frames, const Transport& tp, const NoteIn* notes = nullptr, uint32_t nNotes = 0)
    {
        knDenormalsOff();
        if (frames == 0) return;
        const bool playParam = global[gPLAY] > 0.5f;
        double bpm = tp.hostValid && tp.bpm > 1.0 ? tp.bpm : (double)global[gTEMPO];
        bpm = std::max(20.0, std::min(400.0, bpm));
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
        if (wasPlaying && !playing) for (auto& T : tr) for (auto& v : T.v) if (v.active && !v.live) v.stage = 3;   // stop: fade out
        if (restarted) for (auto& T : tr) T.lastK = INT64_MIN;
        const double bpf = bpm / (60.0 * sr);
        const double b1 = b0 + bpf * frames;
        const int want = clampi(midiPattern >= 0 ? midiPattern : (int)global[gPATTERN], 0, kPatterns - 1);
        if (!playing || restarted) curPattern = want;
        const double spb = 60.0 * sr / bpm;
        dly.set(global[gDLY_TIME], global[gDLY_FB], spb);
        rev.set(global[gREV_SIZE], global[gREV_TONE]);
        const float outTarget = trimGain(global[gOUTPUT]);
        if (outG < 0.f) outG = outTarget;
        const float sm = 1.f - std::exp(-1.f / (0.02f * sr));
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
                if (want != curPattern) {
                    const Pattern& P = bank.patterns[curPattern];
                    const int64_t k16 = (int64_t)std::floor(beat / 0.25 + 1e-9), len = std::max<int>(1, P.length);
                    if (lastK16 != INT64_MIN && k16 / len != lastK16 / len) curPattern = want;
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
                float mixL = 0.f, mixR = 0.f, dSend = 0.f, dSendR = 0.f, rL = 0.f, rR = 0.f;
                for (int t = 0; t < kTracks; ++t) {
                    TrackState& T = tr[t];
                    if (!T.busy) continue;
                    float l = 0.f, r = 0.f;
                    for (auto& v : T.v) if (v.active) renderVoice(t, v, l, r);
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
                outL[pos + i] = L > 1.5f || L < -1.5f ? std::tanh(L / 1.5f) * 1.5f : L;
                outR[pos + i] = R > 1.5f || R < -1.5f ? std::tanh(R / 1.5f) * 1.5f : R;
            }
            pos += n;
        }
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

private:
    struct Grain { double pos; double inc; float ph, dph, gl, gr; };
    struct Voice {
        bool active = false, live = false; int stage = 0;    // 0 attack, 1 hold / decay, 2 release (note off), 3 choke
        float env = 0.f, vel = 1.f; int semis = 0; uint32_t serial = 0;
        uint8_t lock[kTrackParams];
        double scan = 0.0;                                   // frames moved by SCAN since the note started
        double headFrac = 0.0;
        float timer = 0.f; bool first = true;
        Grain g[kMaxGrains]; int ng = 0;
        // per segment
        double ra = 0, rb = 1, head = 0, scanInc = 0, grainInc = 0, sprayFr = 0; float grainLen = 1000.f, interval = 1000.f, spread = 0.f, gain = 1.f;
        float atkInc = 1.f, decCoef = 0.f, relCoef = 0.f; bool hold = true; float panL = 0.707f, panR = 0.707f;
        float p(const float* knobs, int k) const { return lock[k] != kNoLock ? (float)lock[k] : knobs[k]; }
    };
    struct TrackState {
        Voice v[2]; int64_t lastK = INT64_MIN; int step = -1; uint32_t serial = 0; Rng rng;
        bool busy = false, filterOn = false, hp = false; Svf fl, fr; float vol = 0.f, volT = 0.f, dSend = 0.f, rSend = 0.f;
    };
    static constexpr uint32_t kSeg = 16;
    float sr = 48000.f;
    TrackState tr[kTracks];
    GrainWindow win;
    PingPong dly; Reverb rev;
    int64_t lastK16 = INT64_MIN;
    int curPattern = 0;
    bool wasPlaying = false, usingInternal = false, prevPlayParam = false;
    double internalBeat = 0, prevB1 = 0;
    float outG = -1.f;

    void trig(int t, const uint8_t* locks, int semis, float vel, bool live)
    {
        TrackState& T = tr[t];
        const SampleData* src = source[t];
        if (!src || src->pcm.size() < 64) return;
        // the playing voice fades out quickly; the new one takes the other slot (or the quietest)
        int slot = -1;
        for (int i = 0; i < 2; ++i) if (!T.v[i].active) { slot = i; break; }
        if (slot < 0) slot = T.v[0].env < T.v[1].env ? 0 : 1;
        for (int i = 0; i < 2; ++i) if (i != slot && T.v[i].active) T.v[i].stage = 3;
        Voice& v = T.v[slot];
        v.active = true; v.live = live; v.stage = 0; v.env = 0.f; v.vel = vel; v.semis = semis; v.serial = ++T.serial;
        if (locks) std::memcpy(v.lock, locks, kTrackParams); else std::memset(v.lock, kNoLock, kTrackParams);
        v.scan = 0.0; v.timer = 0.f; v.ng = 0; v.first = true;
        if (T.v[slot ^ 1].active && T.v[slot ^ 1].env > 0.05f) v.env = 0.f;   // starts from silence, the old one crossfades out
    }

    void setupTrack(int t)
    {
        TrackState& T = tr[t];
        const SampleData* src = source[t];
        T.busy = false;
        if (!src || src->pcm.empty()) { for (auto& v : T.v) v.active = false; return; }
        const float* knobs = trackParam[t];
        const Kit& kit = bank.kit[t];
        const uint32_t frames = (uint32_t)src->pcm.size();
        const double srcRatio = src->sr / sr;
        Voice* newest = nullptr;
        for (auto& v : T.v) {
            if (!v.active) continue;
            T.busy = true;
            if (!newest || v.serial > newest->serial) newest = &v;
            const int slice = sliceOf(v.p(knobs, tpSLICE), kit.nSlices);
            sliceRegion(kit, slice, frames, v.ra, v.rb);
            const double rl = v.rb - v.ra;
            v.scanInc = scanSpeed(v.p(knobs, tpSCAN)) * srcRatio;
            double hf = v.p(knobs, tpPOSITION) / 127.0 + v.scan / rl;
            hf -= std::floor(hf);
            v.head = v.ra + hf * rl;
            v.headFrac = v.head / frames;
            v.grainInc = srcRatio * std::pow(2.0, (pitchSemis(v.p(knobs, tpPITCH)) + v.semis) / 12.0);
            const float size = grainSec(v.p(knobs, tpSIZE)), rate = grainRate(v.p(knobs, tpDENSITY));
            v.grainLen = std::max(16.f, size * sr);
            v.interval = std::max(8.f, sr / rate);
            v.sprayFr = spraySec(v.p(knobs, tpSPRAY)) * src->sr;
            v.spread = v.p(knobs, tpSPREAD) / 127.f;
            const float overlap = size * rate;
            v.gain = std::min(1.2f, 1.f / std::sqrt(std::max(0.35f, overlap * 0.5f)));
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

    inline void renderVoice(int t, Voice& v, float& outL, float& outR)
    {
        TrackState& T = tr[t];
        const SampleData* src = source[t];
        const float* pcm = src->pcm.data();
        const double rl = v.rb - v.ra;
        // envelope
        switch (v.stage) {
        case 0: v.env += v.atkInc; if (v.env >= 1.f) { v.env = 1.f; v.stage = 1; } break;
        case 1: if (!v.hold) v.env *= v.decCoef; break;
        case 2: v.env *= v.relCoef; break;
        default: v.env -= 1.f / (0.015f * sr); break;
        }
        if (v.env <= 1e-4f && v.stage >= 1) { v.active = false; v.ng = 0; return; }
        // spawn grains
        v.timer -= 1.f;
        if (v.timer <= 0.f) {
            v.timer += v.interval * (1.f + 0.25f * v.spread * T.rng.next());
            if (v.ng < kMaxGrains) {
                Grain& g = v.g[v.ng++];
                double p = v.head + v.sprayFr * T.rng.next();
                p = v.ra + std::fmod(std::fmod(p - v.ra, rl) + rl, rl);
                const float det = v.spread * v.spread * v.spread * 7.f * T.rng.next();
                g.pos = p; g.inc = v.grainInc * (det != 0.f ? std::pow(2.0, det / 12.0) : 1.0);
                g.ph = v.first ? 0.5f : 0.f; g.dph = 1.f / v.grainLen;   // the first grain of a note starts at full level (the note's attack fades it in)
                v.first = false;
                const float pr = v.spread * T.rng.next();
                const float pl = clampf(1.f - pr, 0.f, 2.f) * 0.5f, pR = clampf(1.f + pr, 0.f, 2.f) * 0.5f;   // 0.5 each when centred
                g.gl = v.panL * pl * 1.414f; g.gr = v.panR * pR * 1.414f;
            }
        }
        // move the play head
        v.scan += v.scanInc;
        if (v.scan > rl || v.scan < -rl) v.scan = std::fmod(v.scan, rl);
        // grains
        float l = 0.f, r = 0.f;
        for (int i = 0; i < v.ng;) {
            Grain& g = v.g[i];
            const size_t i0 = (size_t)g.pos;
            size_t i1 = i0 + 1; if ((double)i1 >= v.rb) i1 = (size_t)v.ra;
            const float fr = (float)(g.pos - (double)i0);
            const float s = (pcm[i0] + (pcm[std::min(i1, src->pcm.size() - 1)] - pcm[i0]) * fr) * win.at(g.ph);
            l += s * g.gl; r += s * g.gr;
            g.pos += g.inc;
            if (g.pos >= v.rb) g.pos -= rl;
            g.ph += g.dph;
            if (g.ph >= 1.f) { g = v.g[--v.ng]; continue; }
            ++i;
        }
        const float a = v.env * v.vel * v.gain;
        outL += l * a; outR += r * a;
    }
};

} // namespace kn
