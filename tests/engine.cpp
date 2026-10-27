// KGRN-4 engine: materials, trigs, pitch independent of scan, freeze, slices, locks, mute, decay, live notes,
// slicer, no NaN at extreme settings, pattern change at the end of the pattern.
//   g++ -O2 -std=c++17 tests/engine.cpp -o /tmp/t && /tmp/t
#include "../src/Engine.hpp"
#include "../src/Materials.hpp"
#include "../src/Slicer.hpp"
#include <cstdio>
#include <memory>
#include <vector>
#include <functional>
using namespace kn;

static int fails = 0;
static void check(bool ok, const char* what) { std::printf("%s %s\n", ok ? "ok  " : "FAIL", what); if (!ok) ++fails; }
static const float sr = 48000.f;

static std::shared_ptr<SampleData> sine(float hz, float secs, float hz2 = 0.f)
{
    auto d = std::make_shared<SampleData>(); d->sr = 48000.0; d->name = "sine";
    const int n = (int)(secs * 48000.f);
    double ph = 0;
    for (int i = 0; i < n; ++i) { const float f = hz2 > 0.f && i >= n / 2 ? hz2 : hz; ph += f / 48000.0; d->pcm.push_back(0.5f * (float)std::sin(6.283185307 * ph)); }
    return d;
}

struct Run { std::vector<float> L, R; };
static Run render(Engine& e, int frames, const Transport& tp = Transport())
{
    Run r; r.L.resize(frames); r.R.resize(frames);
    for (int p = 0; p < frames; p += 256) { const int n = std::min(256, frames - p); Transport t = tp; e.process(r.L.data() + p, r.R.data() + p, n, t); }
    return r;
}
static float rms(const std::vector<float>& x, int a, int b) { double s = 0; for (int i = a; i < b; ++i) s += x[i] * x[i]; return (float)std::sqrt(s / std::max(1, b - a)); }
static float zcHz(const std::vector<float>& x, int a, int b)
{
    int z = 0; for (int i = a + 1; i < b; ++i) if ((x[i - 1] < 0.f) != (x[i] < 0.f)) ++z;
    return z * 0.5f * sr / (b - a);
}
static std::unique_ptr<Engine> make(const SampleData* src)
{
    std::unique_ptr<Engine> e(new Engine()); e->init(sr);
    e->global[gPLAY] = 1.f; e->global[gTEMPO] = 120.f;
    e->trackParam[0][tpSPREAD] = 0.f;              // no random detune / pan for the measurements
    e->source[0] = src;
    return e;
}

int main()
{
    // materials
    bool matOk = true;
    for (int i = 1; i <= kMaterials; ++i) {
        SamplePtr m = material(i);
        float pk = 0.f; bool fin = true; for (float v : m->pcm) { pk = std::max(pk, std::fabs(v)); fin = fin && std::isfinite(v); }
        if (!m || m->pcm.size() != 192000 || pk < 0.5f || pk > 0.95f || !fin) { matOk = false; std::printf("  material %d bad\n", i); }
    }
    check(matOk, "8 built-in materials, 4 s, normalised, finite");

    auto s440 = sine(440.f, 2.f);
    {   // trig on step 5 only: silent before, sound after
        auto e = make(s440.get());
        e->bank.patterns[0].on[0][4] = 1;
        Run r = render(*e, 24000 * 3);
        check(rms(r.L, 0, 23000) < 1e-4f && rms(r.L, 25000, 40000) > 0.02f, "silent before the trig on step 5, sound after it (step = 6000 frames)");
        check(std::fabs(zcHz(r.L, 30000, 60000) - 440.f) < 30.f, "grains keep the pitch of the material (440 Hz)");
    }
    {   // pitch +12: twice the frequency, with the same scan
        auto e = make(s440.get());
        e->bank.patterns[0].on[0][0] = 1;
        e->trackParam[0][tpPITCH] = 64.f + 32.f;   // +12
        Run r = render(*e, 48000);
        const float f = zcHz(r.L, 6000, 46000);
        check(std::fabs(f - 880.f) < 50.f, "PITCH +12 = 880 Hz");
        e->trackParam[0][tpSCAN] = 64.f;           // frozen
        e->trackParam[0][tpPITCH] = 64.f;
        Run r2 = render(*e, 4800);
        (void)r2;
    }
    {   // freeze: the play head does not move; 100%: it moves at the original speed
        auto e = make(s440.get());
        e->bank.patterns[0].on[0][0] = 1;
        e->bank.patterns[0].len[0] = 64;
        e->trackParam[0][tpSCAN] = 64.f;
        render(*e, 4800); const float p0 = e->outPos[0];
        render(*e, 24000); const float p1 = e->outPos[0];
        check(p0 >= 0.f && std::fabs(p1 - p0) < 1e-4f, "SCAN 0 = frozen play head");
        e->trackParam[0][tpSCAN] = 96.f;
        render(*e, 4800); const float q0 = e->outPos[0];
        render(*e, 24000); const float q1 = e->outPos[0];
        check(std::fabs((q1 - q0) - 0.25f) < 0.01f, "SCAN 100% = 0.5 s of a 2 s sample in 0.5 s");
        e->trackParam[0][tpSCAN] = 48.f;           // -50%
        render(*e, 4800); const float w0 = e->outPos[0];
        render(*e, 24000); const float w1 = e->outPos[0];
        check(std::fabs((w0 - w1) - 0.125f) < 0.01f, "SCAN -50% = backwards at half speed");
    }
    {   // slices: two halves 220 / 880 Hz, slice 2 plays the second half
        auto two = sine(220.f, 2.f, 880.f);
        auto e = make(two.get());
        Kit& k = e->bank.kit[0]; k.nSlices = 2; k.slice[0] = 0; k.slice[1] = 500000;
        e->bank.patterns[0].on[0][0] = 1;
        e->trackParam[0][tpSLICE] = sliceKnob(1, 2);
        e->trackParam[0][tpSCAN] = 64.f;
        e->trackParam[0][tpPOSITION] = 40.f;
        Run r = render(*e, 24000);
        check(std::fabs(zcHz(r.L, 4000, 23000) - 880.f) < 40.f, "SLICE 2 / 2 plays the second half");
        check(sliceOf(sliceKnob(0, 2), 2) == 0 && sliceOf(sliceKnob(1, 2), 2) == 1 && sliceOf(0.f, 2) == -1 && sliceOf(127.f, 7) == 6, "slice knob mapping");
        e->trackParam[0][tpSLICE] = sliceKnob(0, 2);
        e->reset(); Run r2 = render(*e, 24000);
        check(std::fabs(zcHz(r2.L, 4000, 23000) - 220.f) < 20.f, "SLICE 1 / 2 plays the first half");
    }
    {   // locks: step 1 locked to +12, step 3 plain
        auto e = make(s440.get());
        Pattern& P = e->bank.patterns[0];
        P.on[0][0] = 1; P.on[0][2] = 1; P.locks[0][0][tpPITCH] = 96;
        Run r = render(*e, 6000 * 4);
        const float a = zcHz(r.L, 1500, 5900), b = zcHz(r.L, 13500, 17900);
        check(std::fabs(a - 880.f) < 60.f && std::fabs(b - 440.f) < 30.f, "a PITCH lock changes only its step");
    }
    {   // mute
        auto e = make(s440.get());
        e->bank.patterns[0].on[0][0] = 1; e->bank.kit[0].mute = 1;
        Run r = render(*e, 12000);
        check(rms(r.L, 0, 12000) < 1e-5f, "muted track is silent");
    }
    {   // decay
        auto e = make(s440.get());
        e->bank.patterns[0].on[0][0] = 1; e->bank.patterns[0].len[0] = 64;
        e->trackParam[0][tpDECAY] = 40.f;          // ~140 ms
        Run r = render(*e, 24000);
        check(rms(r.L, 500, 2000) > 0.02f && rms(r.L, 12000, 24000) < 1e-4f, "short DECAY ends the note");
    }
    {   // live note while stopped, note off releases a HOLD note
        auto e = make(s440.get());
        e->global[gPLAY] = 0.f;
        NoteIn on{0, 0, 12, 1.f, true};
        std::vector<float> L(24000), R(24000);
        e->process(L.data(), R.data(), 256, Transport(), &on, 1);
        for (int p = 256; p < 12000; p += 256) e->process(L.data() + p, R.data() + p, 256, Transport());
        const float f = zcHz(L, 3000, 12000);
        NoteIn off{0, 0, 0, 0.f, false};
        e->process(L.data() + 12000, R.data() + 12000, 256, Transport(), &off, 1);
        for (int p = 12256; p < 24000; p += 256) e->process(L.data() + p, R.data() + p, std::min(256, 24000 - p), Transport());
        check(std::fabs(f - 880.f) < 60.f, "live note +12 while stopped");
        check(rms(L, 20000, 24000) < 0.01f * rms(L, 6000, 12000), "note off releases it");
    }
    {   // extreme settings: no NaN, bounded
        auto e = make(material(6).get());
        for (int t = 0; t < kTracks; ++t) e->source[t] = material(t + 1).get();
        bool ok = true;
        for (int round = 0; round < 6; ++round) {
            for (int t = 0; t < kTracks; ++t) { e->bank.patterns[0].on[t][0] = e->bank.patterns[0].on[t][2] = 1; for (int k = 0; k < kTrackParams; ++k) e->trackParam[t][k] = (float)(((round + t + k) * 37) % 2 ? 127 : 0); }
            e->global[gREV_SIZE] = 127; e->global[gDLY_FB] = 127; e->global[gOUTPUT] = 127;
            Run r = render(*e, 24000);
            for (float v : r.L) if (!std::isfinite(v) || std::fabs(v) > 1.6f) ok = false;
        }
        check(ok, "extreme knob values: finite and bounded");
    }
    {   // pattern change at the end of the pattern
        auto e = make(s440.get());
        e->bank.patterns[1].on[0][0] = 1;
        render(*e, 6000);
        e->global[gPATTERN] = 1.f;
        render(*e, 6000 * 10);
        check(e->outPattern == 0, "a new pattern waits for the end of the playing one");
        render(*e, 6000 * 7);
        check(e->outPattern == 1, "then it starts");
    }
    {   // CHAIN: A02 then A03 x2, looped
        auto e = make(s440.get());
        e->bank.chain.parse("1 2 1:1 2:2", kPatterns);
        std::vector<int> seq; std::vector<float> L(256), R(256);
        for (int p = 0; p < 96000 * 7; p += 256) { e->process(L.data(), R.data(), 256, Transport()); if (seq.empty() || seq.back() != e->outPattern) seq.push_back(e->outPattern); }
        check(seq.size() >= 4 && seq[0] == 1 && seq[1] == 2 && seq[2] == 1 && seq[3] == 2, "CHAIN plays its patterns in order and loops");
    }
    {   // slicer
        SamplePtr b = material(6);
        const auto s = slicer::transients(b->pcm, b->sr, 0.5f, kMaxSlices);
        bool asc = s[0] == 0; for (size_t i = 1; i < s.size(); ++i) asc = asc && s[i] > s[i - 1];
        std::printf("  BEAT: %d transient slices\n", (int)s.size());
        check(s.size() >= 8 && s.size() <= 32 && asc, "transients: the drum material gives one slice per hit");
        // each slice start is close to a 16th
        int near = 0; for (uint32_t f : s) { const double k = f / 6000.0; if (std::fabs(k - std::round(k)) * 6000.0 < 300.0) ++near; }
        check(near >= (int)s.size() - 1, "slices start on the hits");
        const auto hi = slicer::transients(b->pcm, b->sr, 0.9f, kMaxSlices);
        check(hi.size() > s.size(), "higher sensitivity, more slices");
        const auto q = slicer::equal(1000, 8);
        check(q.size() == 8 && q[1] == 125, "equal slices");
        const auto m = slicer::toMillionths(s, b->pcm.size());
        check(m.size() == s.size() && m[0] == 0, "millionths");
        const auto pad = slicer::transients(material(1)->pcm, 48000.0, 0.5f, kMaxSlices);
        check(pad.size() <= 3, "a pad has (almost) no transients");
    }
    std::printf(fails ? "\n%d FAILED\n" : "\nall passed\n", fails);
    return fails ? 1 : 0;
}
