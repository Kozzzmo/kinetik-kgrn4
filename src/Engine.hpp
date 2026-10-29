// Kinetik KGRN-4 — engine: 4 polyphonic granular tracks played by a step sequencer (notes, chords, gates, ratchets,
// conditions, per-step locks) and by MIDI, two LFOs per track, echo / reverb sends, chorus and master section.
// 0.3: three grain engines per track (CLASSIC / CLOUD / LOOP), stereo material, cubic interpolation, grains that never
// wrap, window shapes, reverse / octave / jitter per grain, level normalized by the grains really playing, LIVE input.
// 0.4: 6 voices per track (ADSR, own filter with envelope), START / END, PITCH MIX, FINE, DRIVE, LFOs, band-limited
// reading of pitched-up grains, extended effects.
#pragma once
#include "Bank.hpp"
#include "Fx.hpp"
#include "MasterFx.hpp"
#include "Click.hpp"
#include "Sample.hpp"
#include "Materials.hpp"
#include <climits>
#include <cstring>
#include <vector>
#include <atomic>
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
// live notes (MIDI, UI keyboard): note = MIDI note (kBaseNote = the sample's own pitch)
struct NoteIn { uint32_t frame; int track; int note; float vel; bool on; };


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
    static constexpr int kMaxGrains = 40;            // per voice
    static constexpr int kGrainBudget = 128;          // all tracks, all voices: keeps the worst case affordable
    static constexpr int kSlots = kVoices + 2;        // voices plus room for the ones fading out
    static constexpr int kFine = 24;
    float trackParam[kTracks][kTrackParams];
    float global[kGlobalParams];
    Bank bank;                                   // audio-side copy (the plugin syncs it)
    const SampleData* source[kTracks] = {};      // material of each track (set by the plugin; LIVE tracks use the input)
    const float* inL = nullptr; const float* inR = nullptr;   // the audio input of the next process() (nullptr = none)
    bool fill = false;                           // FILL held (conditions FILL / !FILL)
    // outputs for the UI
    int outStep = -1; uint32_t outActive = 0; int outPattern = 0;
    float outPos[kTracks] = {-1.f, -1.f, -1.f, -1.f};   // play head of the newest voice, fraction of the material (-1 = silent)
    float inPeak = 0.f;                                  // input level (UI meter)
    int midiPattern = -1;
    Click click;
    bool recording = false;                      // REC: live notes are written into the pattern (popRec)
    bool popRec(RecEvent& e)
    {
        const int r = recR.load(std::memory_order_relaxed);
        if (r == recW.load(std::memory_order_acquire)) return false;
        e = recQ[r]; recR.store((r + 1) % kRecQ, std::memory_order_release);
        return true;
    }
    void setGlitchHold(bool on) { fx.glHold.store(on ? 1 : 0); }

    Engine()
    {
        for (int t = 0; t < kTracks; ++t) for (int k = 0; k < kTrackParams; ++k) trackParam[t][k] = trackDefault(t, k);
        for (int g = 0; g < kGlobalParams; ++g) global[g] = globalDefault(g);
        bank.initDefault();
        for (auto& a : recStepOf) for (auto& x : a) x = -1;
    }

    void init(double sampleRate)
    {
        sr = (float)sampleRate;
        fx.init(sr); click.init(sr);
        for (auto* b : {&bML, &bMR, &bDL, &bDR, &bRL, &bRR, &bCL, &bCR, &bGL, &bGR}) b->assign(kBlock, 0.f);
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
        fx.reset(); click.reset();
        for (auto* f : {&mfHp[0], &mfHp[1], &mfLp[0], &mfLp[1]}) f->reset();
        mfHpHz = 20.f; mfLpHz = 20000.f;
        wasPlaying = false; usingInternal = false; internalBeat = 0; prevPlayParam = false; lastK16 = INT64_MIN;
        grains = 0;
    }

    // a note now (UI keyboard / MIDI): no locks, ends with noteOff
    void noteOn(int t, int note, float vel) { if (t >= 0 && t < kTracks) startNote(t, nullptr, note, vel, true, -1.0); }
    void noteOff(int t, int note)
    {
        if (t < 0 || t >= kTracks) return;
        for (auto& v : tr[t].v) if (v.active && v.live && v.note == note && v.stage < 2) v.stage = 2;
    }
    void allNotesOff() { for (auto& T : tr) for (auto& v : T.v) if (v.active && v.stage < 2) v.stage = 2; }

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
    // the play heads of a track's sounding voices (fractions of the material), for the display; returns how many
    int voiceHeads(int t, float* out, int max) const
    {
        int n = 0;
        if (t < 0 || t >= kTracks) return 0;
        for (auto& v : tr[t].v) if (v.active && v.env > 0.01f && n < max) out[n++] = (float)v.headFrac;
        return n;
    }

    // a host block in pieces of at most kBlock frames (the master effects work on buffers)
    void process(float* outL, float* outR, uint32_t frames, const Transport& tp, const NoteIn* notes = nullptr, uint32_t nNotes = 0)
    {
        knDenormalsOff();
        uint32_t done = 0, ni = 0;
        const float* in0L = inL; const float* in0R = inR;
        while (done < frames) {
            const uint32_t n = std::min<uint32_t>(kBlock, frames - done);
            Transport t = tp;
            if (t.hostValid && t.hostPlaying && done > 0) t.beat += done * std::max(20.0, std::min(400.0, t.bpm > 1.0 ? t.bpm : (double)global[gTEMPO])) / (60.0 * sr);
            uint32_t a = ni; while (ni < nNotes && notes[ni].frame < done + n) ++ni;
            NoteIn sub[64]; uint32_t ns = 0;
            for (uint32_t i = a; i < ni && ns < 64; ++i) { sub[ns] = notes[i]; sub[ns].frame = notes[i].frame >= done ? notes[i].frame - done : 0; ++ns; }
            inL = in0L ? in0L + done : nullptr; inR = in0R ? in0R + done : nullptr;
            processBlock(outL + done, outR + done, n, t, sub, ns);
            done += n;
        }
        inL = in0L; inR = in0R;
    }
    void processBlock(float* outL, float* outR, uint32_t frames, const Transport& tp, const NoteIn* notes, uint32_t nNotes)
    {
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
        if (wasPlaying && !playing) for (auto& T : tr) { T.ratLeft = 0; for (auto& v : T.v) if (v.active && !v.live && v.stage < 2) v.stage = 2; }   // stop: release
        if (restarted) { for (auto& T : tr) { T.lastK = INT64_MIN; T.k0 = INT64_MIN; } barW = -1; lastBar = INT64_MIN; }
        const double bpf = bpm / (60.0 * sr);
        const double b1 = b0 + bpf * frames;
        const int want = clampi(midiPattern >= 0 ? midiPattern : (int)global[gPATTERN], 0, kPatterns - 1);
        if (!playing || restarted) {
            const int c = playing ? chainPl.start(bank.chain) : -1;
            if (!playing) chainPl.reset();
            curPattern = c >= 0 ? c : want;
        }
        spb = 60.0 * sr / bpm;
        if (!playing) { for (auto& T : tr) T.recSkipK = INT64_MIN; }
        const float sm = 1.f - std::exp(-1.f / (0.02f * sr));
        const bool freeze = global[gFREEZE] > 0.5f;
        bool anySolo = false; for (int t = 0; t < kTracks; ++t) anySolo = anySolo || bank.kit[t].solo;
        liveMips = false; for (int t = 0; t < kTracks; ++t) liveMips = liveMips || isLive(t);
        // the input: its level for the meter
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
                if (e.on) noteOn(e.track, e.note, e.vel); else noteOff(e.track, e.note);
                if (recording && playing) recordNote(e, b0 + pos * bpf);
            }
            const double beat = b0 + pos * bpf;
            if (playing) {
                {
                    const Pattern& P = bank.patterns[curPattern];
                    const int64_t k16 = (int64_t)std::floor(beat / 0.25 + 1e-9), len = std::max<int>(1, P.length);
                    if (lastK16 != INT64_MIN && k16 / len != lastK16 / len) {
                        const int c = chainPl.next(bank.chain); const int np = c >= 0 ? c : want;
                        if (np != curPattern) { curPattern = np; for (auto& T : tr) T.k0 = INT64_MIN; }
                    }
                }
                lastK16 = (int64_t)std::floor(beat / 0.25 + 1e-9);
                const Pattern& P = bank.patterns[curPattern];
                for (int t = 0; t < kTracks; ++t) {
                    TrackState& T = tr[t];
                    const double sbt = 0.25 / kSpeedMult[P.speed[t] % kSpeeds];
                    const double off = (P.swing / 100.0 * 2.0 - 1.0) * sbt;
                    int64_t k = (int64_t)std::floor(beat / sbt + 1e-9);
                    double start = k * sbt + ((k & 1) ? off : 0.0);
                    if (beat < start) { --k; start = k * sbt + ((k & 1) ? off : 0.0); }   // the swung step has not started yet
                    // ratchet repeats of the step that is playing
                    if (T.ratLeft > 0 && beat >= T.ratNext) {
                        --T.ratLeft; T.ratNext += T.ratEvery;
                        playStep(t, P, T.ratStep, T.ratGate);
                    }
                    if (k < 0 || k == T.lastK) continue;
                    T.lastK = k;
                    if (T.k0 == INT64_MIN) T.k0 = k;
                    const int step = P.trackStep(t, k);
                    T.step = step;
                    T.ratLeft = 0;
                    if (k == T.recSkipK) { T.recSkipK = INT64_MIN; continue; }   // just recorded: the held key plays it
                    const bool audible = !bank.kit[t].mute && (!anySolo || bank.kit[t].solo);
                    if (!P.on[t][step] || !audible) continue;
                    const int len = P.len[t] ? P.len[t] : 16;
                    if (!condPasses(T, P.cond[t][step], (k - T.k0) / len)) continue;
                    const double dur = sbt + ((k & 1) ? -off : off);
                    const int r = kRatchValues[P.ratch[t][step] % kRatchCount];
                    const double gateBeats = P.gate[t][step] >= kGateHold ? -1.0 : kGateSteps[P.gate[t][step] % kGateCount] * sbt / (r > 1 ? r : 1);
                    playStep(t, P, step, gateBeats);
                    if (r > 1) { T.ratLeft = r - 1; T.ratEvery = dur / r; T.ratNext = start + T.ratEvery; T.ratStep = step; T.ratGate = gateBeats; }
                }
            } else lastK16 = INT64_MIN;
            outStep = playing ? (int)((int64_t)std::floor(beat / 0.25 * kFine) % (32768LL * kFine)) : -1;

            // per track settings for this segment (LFOs first: they move the knobs)
            voiceCount = 0;
            for (int t = 0; t < kTracks; ++t) { runLfos(t, beat, playing, (float)n / sr); setupTrack(t); voiceCount += activeVoices(t); }
            for (uint32_t i = 0; i < n; ++i) {
                // the input buffer (bar starts are remembered for CAPTURE)
                if (!freeze) liveWrite(inL ? inL[pos + i] : 0.f, inR ? inR[pos + i] : 0.f);
                const double bt = playing ? beat + i * bpf : freeBeat + (pos + i) * bpf;
                if (playing) { const int64_t bar = (int64_t)std::floor(bt / 4.0 + 1e-9); if (bar != lastBar) { lastBar = bar; barW = liveW; } }
                float mixL = 0.f, mixR = 0.f, dSend = 0.f, dSendR = 0.f, rL = 0.f, rR = 0.f, cL = 0.f, cR = 0.f, gL = 0.f, gR = 0.f;
                for (int t = 0; t < kTracks; ++t) {
                    TrackState& T = tr[t];
                    if (!T.busy) continue;
                    float l = 0.f, r = 0.f;
                    for (auto& v : T.v) if (v.active) renderVoice(t, v, l, r, bt, playing);
                    if (T.drive > 0.001f) { const float g = 1.f + T.drive * 7.f, c = 1.f / std::sqrt(g); l = std::tanh(l * g) * c; r = std::tanh(r * g) * c; }
                    T.vol += (T.volT - T.vol) * sm;
                    l *= T.vol; r *= T.vol;
                    mixL += l; mixR += r;
                    dSend += l * T.dSend; dSendR += r * T.dSend; rL += l * T.rSend; rR += r * T.rSend;
                    cL += l * T.cSend; cR += r * T.cSend; gL += l * T.gSend; gR += r * T.gSend;
                }
                const uint32_t j = pos + i;
                bML[j] = mixL; bMR[j] = mixR; bDL[j] = dSend; bDR[j] = dSendR; bRL[j] = rL; bRR[j] = rR;
                bCL[j] = cL; bCR[j] = cR; bGL[j] = gL; bGR[j] = gR;
            }
            pos += n;
        }
        // the master effects of KDSP-16 (echo, reverb, chorus, CHAOS, EQ, compressor, drive, volume), the click, the filter
        fx.process(global, (float)bpm, bML.data(), bMR.data(), bDL.data(), bDR.data(), bRL.data(), bRR.data(), bCL.data(), bCR.data(),
                   bGL.data(), bGR.data(), playing ? b0 : freeBeat, playing, outL, outR, frames);
        click.process(outL, outR, frames, b0, bpf, recording && playing);
        masterFilter(outL, outR, frames);
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
        writeView();
        wasPlaying = playing; prevB1 = b1;
        if (playing && usingInternal) internalBeat = b1;
    }

    // what the display shows: every grain and every voice of a track, copied at the end of each block (seqlock)
    int grainView(int t, GrainView* out, int max, VoiceView* vout, int vmax, int& nv) const
    {
        nv = 0;
        if (t < 0 || t >= kTracks) return 0;
        const ViewSnap& V = view[t];
        for (int tries = 0; tries < 4; ++tries) {
            const uint32_t a = V.seq.load(std::memory_order_acquire);
            if (a & 1u) continue;
            const int n = std::min(max, V.ng), m = std::min(vmax, V.nv);
            std::memcpy(out, V.g, sizeof(GrainView) * (size_t)n);
            std::memcpy(vout, V.v, sizeof(VoiceView) * (size_t)m);
            std::atomic_thread_fence(std::memory_order_acquire);
            if (V.seq.load(std::memory_order_relaxed) == a) { nv = m; return n; }
        }
        return 0;
    }
    int activeGrains(int t) const { int n = 0; for (auto& v : tr[t].v) if (v.active) n += v.ng; return n; }   // tests
    int activeVoices(int t) const { int n = 0; for (auto& v : tr[t].v) if (v.active && v.stage < 3) ++n; return n; }
    void spawnStats(int t, int& all, int& rev2, int& oct) const { all = tr[t].nSpawn; rev2 = tr[t].nRev; oct = tr[t].nOct; }
    float lfoValue(int t, int i) const { return tr[t].lfoOut[i]; }

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
#if defined(__GNUC__)
        __attribute__((always_inline))
#endif
        inline float cubic(int lv, const float* d, double pos) const
        {
            int64_t i = (int64_t)pos; if ((double)i > pos) --i;          // floor without a library call
            const float f = (float)(pos - (double)i);
            const int64_t m = n[lv];
            if (ring) { i %= m; if (i < 0) i += m; }
            float y0, y1, y2, y3;
            if (i >= 1 && i + 2 < m) { const float* q = d + i; y0 = q[-1]; y1 = q[0]; y2 = q[1]; y3 = q[2]; }   // the usual case
            else { y0 = rd(lv, d, i - 1); y1 = rd(lv, d, i); y2 = rd(lv, d, i + 1); y3 = rd(lv, d, i + 2); }
            const float c1 = 0.5f * (y2 - y0), c2 = y0 - 2.5f * y1 + 2.f * y2 - 0.5f * y3, c3 = 0.5f * (y3 - y0) + 1.5f * (y1 - y2);
            return ((c3 * f + c2) * f + c1) * f + y1;
        }
        double lo() const { return ring ? (double)std::max<int64_t>(0, W - n[0] + 4096) : 1.0; }    // oldest safe position (level 0 frames)
        double hi() const { return ring ? (double)W - 256.0 : (double)n[0] - 3.0; }                 // newest safe position
    };
    struct Grain { double pos, inc; float ph, dph, gl, gr, offR; int8_t wa; float wf, xf; int8_t lvl;
                   double p0, travel; float semis; uint8_t flags; };   // display: start, travel (level-0 frames), pitch, kGv* flags
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
        bool active = false, live = false; int stage = 0;    // 0 attack, 1 decay / sustain, 2 release, 3 choke (fast fade)
        float env = 0.f, vel = 1.f; int note = kBaseNote; uint32_t serial = 0;
        double gateLeft = -1.0;                              // frames until the note ends (< 0 = until note off / next note)
        bool holdGate = false;                               // sequenced HOLD note: ends at the track's next note
        uint8_t lock[kTrackParams];
        double scan = 0.0;                                   // frames moved by SCAN since the note started
        double headFrac = 0.0, liveStart = 0.0;
        float firstSkip = 0.f;
        float timer = 0.f, ltimer[4] = {}, lDet[4] = {}, lPan[4] = {}; bool first = true; int64_t gridK = INT64_MIN;
        Grain g[kMaxGrains]; int ng = 0;
        float normInv = 1.f; int normWait = 0;
        float normA = 1.f, normP = 1.f;                      // smoothed sum of the windows playing (and of their squares)
        Svf fl, fr; bool filterOn = false, hp = false;       // the voice's own filter (its envelope moves it)
        // per segment
        int mode = gmClassic; double ra = 0, rb = 1, head = 0, scanInc = 0, grainInc = 1, origInc = 1, sprayFr = 0; float grainLen = 1000.f, overlap = 2.f;
        float incoh = 0.f;                                    // 0 = the grains add up in phase (amplitude sum), 1 = at random (power sum)
        float shape = 0.5f, jitter = 0.f, reverse = 0.f, octave = 0.f, spread = 0.f, pmix = 1.f; bool random = false, sync = false; double syncBeats = 0.25;
        float atkInc = 1.f, decCoef = 0.f, sus = 1.f, relCoef = 0.f; float panL = 0.707f, panR = 0.707f; int copies = 1;
        float fBase = 64.f, fEnv = 0.f, fq = 0.7f;
        Svf hl, hr; bool lowCutOn = false;                   // LOW CUT (a second filter, high pass)
        // GRAIN MIX: the sample played straight (no grains) from POSITION to END, looping to START, at the note's pitch
        double dryPos = 0.0, dryInc = 1.0; bool dryInit = false; float dryG = 0.f, grainG = 1.f; int8_t dryLvl = 0;
    };
    struct TrackState {
        Voice v[kSlots]; int64_t lastK = INT64_MIN, k0 = INT64_MIN; int step = -1; uint32_t serial = 0; Rng rng;
        bool busy = false; float vol = 0.f, volT = 0.f, dSend = 0.f, rSend = 0.f, cSend = 0.f, gSend = 1.f, drive = 0.f;
        int64_t recSkipK = INT64_MIN;
        Src src;
        float mod[kModTargets] = {};                          // LFO offsets in knob units, this segment
        double lfoPh[kLfos] = {}; float lfoRnd[kLfos] = {}, lfoOut[kLfos] = {}; bool lfoReset[kLfos] = {};
        int ratLeft = 0, ratStep = 0; double ratNext = 0.0, ratEvery = 0.0, ratGate = 0.0;
        int nSpawn = 0, nRev = 0, nOct = 0;            // tests
    };
    static constexpr uint32_t kSeg = 16;
    struct ViewSnap { std::atomic<uint32_t> seq{0}; int ng = 0, nv = 0; GrainView g[kViewGrains]; VoiceView v[kSlots]; };
    ViewSnap view[kTracks];
    void writeView()
    {
        for (int t = 0; t < kTracks; ++t) {
            TrackState& T = tr[t];
            ViewSnap& V = view[t];
            V.seq.fetch_add(1, std::memory_order_acq_rel);
            int ng = 0, nv = 0;
            if (T.busy && hasSource(t)) {
                const bool live = isLive(t);
                const double winLen = live ? liveWindow() : 1.0, base = live ? (double)liveW - winLen : 0.0;
                const double inv = live ? 1.0 / winLen : 1.0 / (double)std::max<int64_t>(1, T.src.n[0]);
                auto frac = [&](double x) { return (float)((x - base) * inv); };
                for (int s = 0; s < kSlots; ++s) {
                    const Voice& v = T.v[s];
                    if (!v.active) continue;
                    if (nv < kSlots) V.v[nv++] = {(float)v.headFrac, v.env * v.vel, (int16_t)v.note, (uint8_t)s, (uint8_t)(v.stage >= 2)};
                    const float a = v.env * v.vel * v.normInv;
                    for (int i = 0; i < v.ng && ng < kViewGrains; ++i) {
                        const Grain& g = v.g[i];
                        const float w = window(g);
                        const float sum = g.gl + g.gr;
                        V.g[ng++] = {frac(g.p0), frac(g.p0 + g.travel), clampf(g.ph, 0.f, 1.f), w * a,
                                     sum > 1e-6f ? (g.gr - g.gl) / sum : 0.f, g.semis, (uint8_t)s, g.flags};
                    }
                }
            }
            V.ng = ng; V.nv = nv;
            V.seq.fetch_add(1, std::memory_order_release);
        }
    }
    float sr = 48000.f; double curBpm = 120.0, spb = 24000.0, freeBeat = 0.0;
    TrackState tr[kTracks];
    GrainWindows win;
    MasterFx fx;
    static constexpr uint32_t kBlock = 1024;
    std::vector<float> bML, bMR, bDL, bDR, bRL, bRR, bCL, bCR, bGL, bGR;
    // master HP / LP filter with resonance, after the master FX (FILTER tab), as KDSP-16
    Svf mfHp[2], mfLp[2];
    float mfHpHz = 20.f, mfLpHz = 20000.f;
    void masterFilter(float* L, float* R, uint32_t n)
    {
        const bool on = global[gMF_ON] > 0.5f;
        const bool hpOn = on && global[gMF_HP] > 0.5f, lpOn = on && global[gMF_LP] < 126.5f;
        if (!hpOn && !lpOn) { mfHpHz = 20.f; mfLpHz = 20000.f; for (auto* f : {&mfHp[0], &mfHp[1], &mfLp[0], &mfLp[1]}) f->reset(); return; }
        const float q = resoQ(global[gMF_RESO], 0.707f, 9.707f);
        mfHpHz += (cutoffHz(global[gMF_HP]) - mfHpHz) * 0.35f;
        mfLpHz += (cutoffHz(global[gMF_LP]) - mfLpHz) * 0.35f;
        for (int c = 0; c < 2; ++c) { mfHp[c].setup(mfHpHz, q, sr); mfLp[c].setup(mfLpHz, q, sr); }
        float* ch[2] = {L, R};
        for (int c = 0; c < 2; ++c)
            for (uint32_t i = 0; i < n; ++i) {
                float x = ch[c][i];
                if (hpOn) { mfHp[c].tick(x); x = mfHp[c].hp; }
                if (lpOn) { mfLp[c].tick(x); x = mfLp[c].lp; }
                ch[c][i] = safety(x);
            }
    }
    // REC
    static constexpr int kRecQ = 256;
    RecEvent recQ[kRecQ]; std::atomic<int> recW{0}, recR{0};
    double recStart[kTracks][128]; int16_t recStepOf[kTracks][128]; int8_t recPatOf[kTracks][128];
    void pushRec(const RecEvent& e) { const int w = recW.load(std::memory_order_relaxed), nw = (w + 1) % kRecQ; if (nw == recR.load(std::memory_order_acquire)) return; recQ[w] = e; recW.store(nw, std::memory_order_release); }
    void recordNote(const NoteIn& e, double beat)
    {
        if (e.track < 0 || e.track >= kTracks || e.note < 0 || e.note > 127) return;
        TrackState& T = tr[e.track];
        const Pattern& P = bank.patterns[curPattern];
        const double sbt = 0.25 / kSpeedMult[P.speed[e.track] % kSpeeds];
        if (e.on) {
            const int64_t k = (int64_t)std::floor(beat / sbt + 0.5);           // the nearest step of the track's grid
            const int step = P.trackStep(e.track, k);
            if (k > T.lastK) T.recSkipK = k;                                    // not started yet: the live note stands in for it
            recStart[e.track][e.note] = beat; recStepOf[e.track][e.note] = (int16_t)step; recPatOf[e.track][e.note] = (int8_t)curPattern;
            pushRec({0, (int8_t)curPattern, (int8_t)e.track, (int16_t)step, (int16_t)e.note, 0});
        } else if (recStepOf[e.track][e.note] >= 0) {
            const double steps = (beat - recStart[e.track][e.note]) / sbt;
            int gi = 0; double best = 1e9;
            for (int i = 0; i < kGateHold; ++i) { const double d = std::fabs(std::log(std::max(0.05, steps) / kGateSteps[i])); if (d < best) { best = d; gi = i; } }
            pushRec({1, recPatOf[e.track][e.note], (int8_t)e.track, recStepOf[e.track][e.note], (int16_t)e.note, (int16_t)gi});
            recStepOf[e.track][e.note] = -1;
        }
    }
    int64_t lastK16 = INT64_MIN, lastBar = INT64_MIN;
    int curPattern = 0;
    ChainPlayer chainPl;
    bool wasPlaying = false, usingInternal = false, prevPlayParam = false, playingNow = false;
    double internalBeat = 0, prevB1 = 0;
    int grains = 0;                                         // grains alive (all tracks)
    int voiceCount = 1;
    bool liveMips = false;                                     // voices sounding (all tracks), this segment
    // input
    std::vector<float> liveL, liveR; int64_t liveCap = 1, liveW = 0, barW = -1;
    std::vector<float> liveML[SampleData::kMips], liveMR[SampleData::kMips]; int64_t liveMW[SampleData::kMips] = {};
    Dec2 decL[SampleData::kMips], decR[SampleData::kMips];
    inline void liveWrite(float l, float r)
    {
        const size_t k = (size_t)(liveW % liveCap);
        liveL[k] = l; liveR[k] = r; ++liveW;
        if (!liveMips) return;                             // the band-limited copies only while a track plays LIVE
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
    // a knob of a voice: its step's lock or the knob, plus the track's LFOs
    float kv(const TrackState& T, const Voice& v, const float* knobs, int k) const
    {
        float x = v.lock[k] != kNoLock ? (float)v.lock[k] : knobs[k];
        if (k < kModTargets) x += T.mod[k];
        return clampf(x, 0.f, 127.f);
    }
    bool condPasses(TrackState& T, int c, int64_t loop)
    {
        const CondDef& d = kConds[clampi(c, 0, kNumConds - 1)];
        switch (d.kind) {
        case ckProb: return T.rng.uni() * 100.f < (float)d.a;
        case ckFill: return fill;
        case ckNotFill: return !fill;
        case ckFirst: return loop == 0;
        case ckNotFirst: return loop != 0;
        case ckRatio: return (int)(loop % d.b) == d.a - 1;
        default: return true;
        }
    }

    // a sequenced step: its note (or chord), its locks, its length in beats (< 0 = HOLD)
    void playStep(int t, const Pattern& P, int step, double gateBeats)
    {
        const double gateFrames = gateBeats < 0.0 ? -1.0 : gateBeats * spb;
        const ChordDef& ch = kChords[P.chord[t][step] % kChordCount];
        TrackState& T = tr[t];
        // HOLD notes end when the track plays its next note
        for (auto& v : T.v) if (v.active && !v.live && v.holdGate && v.stage < 2 && !bank.kit[t].legato) v.stage = 2;
        for (int i = 0; i < ch.n; ++i) startNote(t, P.locks[t][step], P.note[t][step] + ch.notes[i], 1.f, false, gateFrames, ch.n == 1);
    }

    void startNote(int t, const uint8_t* locks, int note, float vel, bool live, double gateFrames, bool single = true)
    {
        TrackState& T = tr[t];
        if (!hasSource(t)) return;
        // LEGATO: a single sequenced note changes the playing one (its locks, its pitch, its length) instead of starting
        if (bank.kit[t].legato && !live && single) {
            Voice* cur = nullptr;
            for (auto& v : T.v) if (v.active && !v.live && v.stage < 2 && (!cur || v.serial > cur->serial)) cur = &v;
            if (cur) {
                if (locks) std::memcpy(cur->lock, locks, kTrackParams); else std::memset(cur->lock, kNoLock, kTrackParams);
                cur->note = note; cur->vel = vel; cur->gateLeft = gateFrames; cur->holdGate = gateFrames < 0.0;
                return;
            }
        }
        // a free slot; above kVoices sounding notes the oldest one fades out (stolen)
        int sounding = 0; Voice* oldest = nullptr;
        for (auto& v : T.v) if (v.active && v.stage < 3) { ++sounding; if (!oldest || v.serial < oldest->serial) oldest = &v; }
        if (sounding >= kVoices && oldest) oldest->stage = 3;
        Voice* slot = nullptr;
        for (auto& v : T.v) if (!v.active) { slot = &v; break; }
        if (!slot) {                                        // every slot busy: take the quietest fading one
            for (auto& v : T.v) if (v.stage == 3 && (!slot || v.env < slot->env)) slot = &v;
            if (!slot) slot = oldest ? oldest : &T.v[0];
            grains -= slot->ng; slot->ng = 0;
        }
        Voice& v = *slot;
        v.active = true; v.live = live; v.stage = 0; v.env = 0.f; v.vel = vel; v.note = note; v.serial = ++T.serial;
        v.gateLeft = gateFrames; v.holdGate = !live && gateFrames < 0.0;
        if (locks) std::memcpy(v.lock, locks, kTrackParams); else std::memset(v.lock, kNoLock, kTrackParams);
        v.scan = 0.0; v.timer = 0.f; v.first = true; v.gridK = INT64_MIN; v.normA = v.normP = 1.f; v.normWait = 0;
        v.fl.reset(); v.fr.reset(); v.filterOn = false; v.hl.reset(); v.hr.reset(); v.lowCutOn = false; v.dryInit = false;
        v.liveStart = (double)liveW;
        for (int j = 0; j < 4; ++j) {                      // LOOP copies: fixed detune / pan for this note
            v.ltimer[j] = -1e9f;                           // set at the first sample (copies staggered)
            v.lDet[j] = j == 0 ? 0.f : T.rng.next();
            v.lPan[j] = j == 0 ? 0.f : (j & 1 ? -1.f : 1.f) * (0.5f + 0.5f * T.rng.uni());
        }
        for (int i = 0; i < kLfos; ++i) T.lfoReset[i] = true;    // RETRIG LFOs start again
    }

    // the two LFOs of a track: their offsets for this segment (knob units, added to the destination)
    void runLfos(int t, double beat, bool playing, float dt)
    {
        TrackState& T = tr[t];
        for (float& m : T.mod) m = 0.f;
        const float* knobs = trackParam[t];
        // the newest voice's locks also lock the LFO knobs
        const Voice* nv = nullptr;
        for (auto& v : T.v) if (v.active && (!nv || v.serial > nv->serial)) nv = &v;
        auto knob = [&](int k) { return nv && nv->lock[k] != kNoLock ? (float)nv->lock[k] : knobs[k]; };
        for (int i = 0; i < kLfos; ++i) {
            const int dest = lfoDest(knob(lfoParam(i, 4)));
            const float depth = (knob(lfoParam(i, 3)) - 64.f) / 63.f;
            const bool synced = lfoSynced(knob(lfoParam(i, 2))), retrig = lfoRetrig(knob(lfoParam(i, 5)));
            double ph = T.lfoPh[i];
            if (T.lfoReset[i] && retrig) ph = 0.0;
            T.lfoReset[i] = false;
            const double prev = ph;
            if (synced && playing && !retrig) ph = beat / (double)kLfoSyncBeats[lfoSyncDiv(knob(lfoParam(i, 1)))];
            else ph += (synced ? (curBpm / 60.0) / kLfoSyncBeats[lfoSyncDiv(knob(lfoParam(i, 1)))] : lfoHz(knob(lfoParam(i, 1)))) * dt;
            if (std::floor(ph) != std::floor(prev)) T.lfoRnd[i] = T.rng.next();
            T.lfoPh[i] = ph;
            const float x = (float)(ph - std::floor(ph));
            float y;
            switch (lfoWave(knob(lfoParam(i, 0)))) {
            case 0: y = x < 0.5f ? 4.f * x - 1.f : 3.f - 4.f * x; break;
            case 1: y = std::sin(kTwoPi * x); break;
            case 2: y = x < 0.5f ? 1.f : -1.f; break;
            case 3: y = 2.f * x - 1.f; break;
            case 4: y = 1.f - 2.f * x; break;
            default: y = T.lfoRnd[i]; break;
            }
            T.lfoOut[i] = y * depth;
            if (dest > 0 && std::fabs(depth) > 0.004f) T.mod[dest - 1] += y * depth * 63.5f;
        }
    }

    void setupTrack(int t)
    {
        TrackState& T = tr[t];
        T.busy = false;
        if (!hasSource(t)) { for (auto& v : T.v) if (v.active) { grains -= v.ng; v.ng = 0; v.active = false; } return; }
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
            auto P = [&](int k) { return kv(T, v, knobs, k); };
            v.mode = kit.mode; v.sync = kit.sync != 0;
            double ra, rb;
            if (live) {                                    // the window of the input that was there when the note started
                const double winLen = liveWindow();
                ra = v.liveStart - winLen; rb = v.liveStart;
            } else {
                const int slice = sliceOf(P(tpSLICE), kit.nSlices);
                sliceRegion(kit, slice, (uint32_t)S.n[0], ra, rb);
            }
            // START / END: the part of the region that plays
            {
                double a = P(tpSTART) / 127.0, b = P(tpEND) / 127.0;
                if (b < a) std::swap(a, b);
                const double len0 = rb - ra;
                v.ra = ra + a * len0; v.rb = ra + b * len0;
                if (v.rb - v.ra < 64.0) v.rb = v.ra + 64.0;
            }
            const double rl = std::max(64.0, v.rb - v.ra);
            v.scanInc = scanSpeed(P(tpSCAN)) * srcRatio;
            double hf = P(tpPOSITION) / 127.0 + v.scan / rl;
            if (!live) hf -= std::floor(hf);
            v.head = v.ra + hf * rl;
            if (live) {                                    // stay inside what the buffer still holds, never ahead of the input
                while (v.head > S.hi() - 64.0) { v.head -= rl; v.scan -= rl; }
                while (v.head < S.lo() + 64.0 && v.head + rl < S.hi()) { v.head += rl; v.scan += rl; }
                const double winLen = liveWindow();
                v.headFrac = clampf((float)((v.head - (S.W - winLen)) / winLen), 0.f, 1.f);
            } else v.headFrac = v.head / (double)S.n[0];
            const double semis = pitchSemis(P(tpPITCH)) + (v.note - kBaseNote) + fineCents(P(tpFINE)) / 100.0;
            v.grainInc = srcRatio * std::pow(2.0, semis / 12.0);
            v.origInc = srcRatio;
            v.pmix = P(tpPMIX) / 127.f;
            const float sz = P(tpSIZE);
            v.syncBeats = kSyncBeats[syncBin(sz)];
            v.grainLen = (float)std::max(48.0, v.sync ? v.syncBeats * spb : (double)grainSec(sz) * sr);
            const float dn = P(tpDENSITY);
            v.overlap = v.mode == gmClassic ? classicOverlap(dn) : (v.mode == gmCloud ? cloudOverlap(dn) : 1.f);
            v.random = cloudRandom(dn); v.copies = loopCopies(dn);
            v.shape = P(tpSHAPE) / 127.f;
            v.sprayFr = spraySec(P(tpSPRAY)) * S.sr;
            v.jitter = P(tpJITTER) / 127.f; v.reverse = P(tpREVERSE) / 127.f;
            v.octave = P(tpOCTAVE) / 127.f; v.spread = P(tpSPREAD) / 127.f;
            {   // how far apart the grains sounding together read (source frames): a few ms apart = random phases (power sum),
                // the same place = in phase (amplitude sum); the level is normalized between the two
                const double gl = v.grainLen, ai = std::fabs(v.grainInc);
                double spreadFr = std::fabs(v.grainInc - v.scanInc) * gl * (v.mode == gmLoop ? (0.005 + v.shape * 0.5) : 1.0)
                                + 2.0 * v.sprayFr + (v.reverse + 0.5f * v.jitter) * gl * ai;
                if (v.mode == gmClassic && std::fabs(v.scanInc) < 0.05) spreadFr += (1.0 - std::fabs(v.scanInc) / 0.05) * 0.5 * gl * ai;
                if (v.mode == gmLoop && v.copies > 1) spreadFr += gl * ai / v.copies;
                if (v.pmix > 0.02f && v.pmix < 0.98f) spreadFr += 0.004 * S.sr;    // two pitches at once never add in phase
                v.incoh = clampf((float)(spreadFr / (0.004 * S.sr)), 0.f, 1.f);
            }
            v.atkInc = 1.f / std::max(1.f, attackSec(P(tpATTACK)) * sr);
            v.decCoef = decayCoef(decaySec(P(tpDECAY)), sr);
            v.sus = P(tpSUSTAIN) / 127.f;
            v.relCoef = decayCoef(releaseSec(P(tpRELEASE)), sr);
            const float pan = (P(tpPAN) - 64.f) / 63.f;
            v.panL = std::cos((pan + 1.f) * kPi * 0.25f); v.panR = std::sin((pan + 1.f) * kPi * 0.25f);
            // the voice's filter, moved by its envelope (FLT ENV): up to 5 octaves either way
            v.fBase = P(tpFILTER); v.fEnv = (P(tpFLTENV) - 64.f) / 63.f; v.fq = resoQ(P(tpRESO), 0.6f, 12.f);
            const bool on = std::fabs(v.fBase - 64.f) >= 1.f;
            if (on && !v.filterOn) { v.fl.reset(); v.fr.reset(); }
            v.filterOn = on; v.hp = v.fBase > 64.f;
            if (on) {
                const float hz = clampf(filterHz(v.fBase) * std::exp2(v.fEnv * 5.f * v.env), 20.f, sr * 0.45f);
                v.fl.setup(hz, v.fq, sr); v.fr.setup(hz, v.fq, sr);
            }
            {   // LOW CUT
                const float lc = P(tpLOWCUT);
                const bool lon = lc > 0.5f;
                if (lon && !v.lowCutOn) { v.hl.reset(); v.hr.reset(); }
                v.lowCutOn = lon;
                if (lon) { const float hz = cutHz(lc, 20.f, 2000.f); v.hl.setup(hz, 0.707f, sr); v.hr.setup(hz, 0.707f, sr); }
            }
            {   // GRAIN MIX (equal power): the straight sample against the grains
                const float m = P(tpGMIX) / 127.f;
                v.grainG = std::sin(m * kPi * 0.5f); v.dryG = m >= 0.999f ? 0.f : std::cos(m * kPi * 0.5f);
                v.dryInc = v.grainInc;
                int lvl = 0; while (lvl + 1 < S.levels && std::fabs(v.dryInc) / (double)(1 << lvl) > 1.12) ++lvl;
                v.dryLvl = (int8_t)lvl;
                if (!v.dryInit) { v.dryPos = v.head; v.dryInit = true; }
                if (v.dryPos < v.ra || v.dryPos > v.rb) v.dryPos = v.ra + std::fmod(std::max(0.0, v.dryPos - v.ra), std::max(64.0, v.rb - v.ra));
            }
        }
        if (!T.busy) return;
        // level, drive and sends follow the newest note
        const Voice& nv = *newest;
        T.volT = volumeGain(kv(T, nv, knobs, tpVOLUME));
        if (T.vol == 0.f) T.vol = T.volT;
        T.drive = kv(T, nv, knobs, tpDRIVE) / 127.f;
        const float d = kv(T, nv, knobs, tpDELAY) / 127.f, r = kv(T, nv, knobs, tpREVERB) / 127.f;
        T.dSend = d * d; T.rSend = r * r;
        const float c = kv(T, nv, knobs, tpCHORUS) / 127.f; T.cSend = c * c;
        T.gSend = kv(T, nv, knobs, tpCHAOS) / 127.f;
    }

    // a new grain from the play head; layer >= 0 = a LOOP copy
    void spawn(TrackState& T, Voice& v, int layer)
    {
        // with many voices sounding each one gets its share of the budget (the level follows the grains really playing)
        if (v.grainG < 0.001f) return;                       // GRAIN MIX all the way to the sample: no grains
        const int cap = std::min(kMaxGrains, std::max(8, kGrainBudget / std::max(1, voiceCount)));
        if (v.ng >= cap || grains >= kGrainBudget + 24) return;
        const Src& S = T.src;
        Rng& rng = T.rng;
        const bool loop = v.mode == gmLoop;
        // length (output frames) and speed (source frames per output frame)
        const double lxf = 0.005 + v.shape * 0.5;                // LOOP: each pass lasts SIZE plus its crossfade
        double len = loop ? v.grainLen * (1.0 + lxf) : (double)v.grainLen, inc = v.grainInc;
        if (!loop && v.jitter > 0.f) len *= 1.0 + 0.5 * v.jitter * rng.next();
        // PITCH MIX: some grains keep the sample's own pitch
        const bool pitched = v.pmix >= 0.999f || (v.pmix > 0.001f && rng.uni() < v.pmix);
        float semis = 0.f;
        if (!pitched) inc = v.origInc;
        if (v.jitter > 0.f) semis += (loop ? (layer > 0 ? v.lDet[layer] * 0.35f * v.jitter : 0.f) : rng.next() * v.jitter * 0.3f);   // up to 30 cents: in tune
        if (pitched && v.octave > 0.f && rng.uni() < v.octave) { semis += rng.uni() < 0.75f ? 12.f : -12.f; ++T.nOct; }
        if (semis != 0.f) inc *= std::pow(2.0, semis / 12.0);
        if (v.reverse > 0.f && rng.uni() < v.reverse) inc = -inc;
        // start: the play head, SPRAY, and in CLASSIC a little random spread when frozen (no buzz)
        double p = v.head;
        if (v.sprayFr > 0.0) p += v.sprayFr * rng.next();
        if (v.mode == gmClassic) {
            const double s = std::fabs(v.scanInc);
            if (s < 0.05) p += (1.0 - s / 0.05) * 0.5 * len * std::fabs(inc) * rng.next();
        }
        if (!loop && std::fabs(std::fabs(inc) - std::fabs(v.scanInc)) > 0.01)   // pitched against the scan: a few ms of random start
            p += 0.003 * S.sr * rng.next();                                        // breaks the comb of evenly spaced grains
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
        Grain& g = v.g[v.ng++]; ++grains;
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
        g.p0 = p; g.travel = len * inc;
        g.semis = (float)(12.0 * std::log2(std::max(1e-6, std::fabs(inc) / std::max(1e-9, v.origInc))));
        g.flags = (uint8_t)((inc < 0.0 ? kGvReverse : 0) | (!pitched ? kGvOriginal : 0) | (std::fabs(semis) >= 11.f ? kGvOctave : 0) | (loop ? kGvLoop : 0));
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
        // the note's length (sequenced gates)
        if (v.gateLeft >= 0.0 && v.stage < 2) { v.gateLeft -= 1.0; if (v.gateLeft < 0.0) v.stage = 2; }
        // envelope (ADSR)
        switch (v.stage) {
        case 0: v.env += v.atkInc; if (v.env >= 1.f) { v.env = 1.f; v.stage = 1; } break;
        case 1: v.env = v.sus + (v.env - v.sus) * v.decCoef; break;
        case 2: v.env *= v.relCoef; break;
        default: v.env -= 1.f / (0.008f * sr); break;
        }
        if ((v.env <= 1e-4f && v.stage >= 2) || (v.stage == 1 && v.sus <= 0.0005f && v.env <= 1e-4f)) { grains -= v.ng; v.ng = 0; v.active = false; return; }
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
        default: {                                          // LOOP: each copy restarts its loop every SIZE
            const float period = v.grainLen;
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
            if (g.ph >= 1.f) { g = v.g[--v.ng]; --grains; continue; }
            ++i;
        }
        // level: divided by how many grains really sound (coherent for CLASSIC / LOOP, power for CLOUD)
        const float c = 1.f / (0.03f * sr);
        v.normA += (sw - v.normA) * c; v.normP += (sw2 - v.normP) * c;
        if (--v.normWait <= 0) {                            // the level divider, every 8 samples (it moves over 30 ms)
            v.normWait = 8;
            const float A = std::max(0.f, v.normA), P = std::sqrt(std::max(0.f, v.normP));
            const float norm = v.incoh >= 0.999f ? P : (v.incoh <= 0.001f ? A : std::exp((1.f - v.incoh) * std::log(std::max(A, 1e-6f)) + v.incoh * std::log(std::max(P, 1e-6f))));
            v.normInv = 1.f / std::max(1.f, norm);
        }
        const float a = v.env * v.vel * v.normInv;
        l *= a; r *= a;
        if (v.grainG < 0.999f) {                              // GRAIN MIX: the straight sample in
            l *= v.grainG; r *= v.grainG;
            if (v.dryG > 0.f) {
                const double rl = std::max(64.0, v.rb - v.ra), xf = std::min(0.01 * S.sr, rl * 0.25);
                const int lv = v.dryLvl; const double sc = 1.0 / (double)(1 << lv);
                auto rd = [&](double p, float& a, float& b) { const double q = (p + S.off[lv]) * sc; a = S.cubic(lv, S.l[lv], q); b = S.cubic(lv, S.r[lv], q); };
                float al, ar; rd(v.dryPos, al, ar);
                const double into = v.dryPos - (v.rb - xf);
                if (into > 0.0) {                                // crossfade into the loop start (no click at the loop point)
                    float bl, br; rd(v.dryPos - rl, bl, br);
                    const float w = (float)(into / xf);
                    al += (bl - al) * w; ar += (br - ar) * w;
                }
                const float g = v.dryG * v.env * v.vel * 0.707f;
                l += al * g * v.panL; r += ar * g * v.panR;
                v.dryPos += v.dryInc;
                if (v.dryPos >= v.rb) v.dryPos -= rl;
            }
        }
        if (v.filterOn) { v.fl.tick(l); v.fr.tick(r); l = v.hp ? v.fl.hp : v.fl.lp; r = v.hp ? v.fr.hp : v.fr.lp; }
        if (v.lowCutOn) { v.hl.tick(l); v.hr.tick(r); l = v.hl.hp; r = v.hr.hp; }
        outL += l; outR += r;
    }
};

} // namespace kn
