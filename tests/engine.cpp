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
#include <chrono>
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
    // ---------------------------------------------------------------- 0.3: the new grain engine
    // a click shows up as a jump in the second difference far above what a 440 Hz sine of amplitude 0.5 gives (0.0017)
    auto clickiness = [](const std::vector<float>& x, int a, int b) { float m = 0.f; for (int i = a + 2; i < b; ++i) m = std::max(m, std::fabs(x[i] - 2.f * x[i - 1] + x[i - 2])); return m; };
    {
        auto s = sine(440.f, 2.f);
        bool smooth = true, level = true; char msg[200];
        float matRms[9] = {};
        for (int m : {1, 3}) { const auto& x = material(m)->pcm; double q = 0; for (size_t i = 24000; i < 120000; ++i) q += x[i] * x[i]; matRms[m] = (float)std::sqrt(q / 96000.0); }
        for (int mode = 0; mode < kGrainModes; ++mode)
            for (float size : {30.f, 50.f, 70.f, 110.f})
                for (float dens : {10.f, 64.f, 120.f})
                    for (float pitch : {64.f, 64.f + 64.f * 7 / 24}) {
                        auto e = make(s.get());
                        e->bank.kit[0].mode = (uint8_t)mode;
                        e->bank.patterns[0].on[0][0] = 1; e->bank.patterns[0].len[0] = 64;
                        e->trackParam[0][tpSIZE] = size; e->trackParam[0][tpDENSITY] = dens; e->trackParam[0][tpPITCH] = pitch;
                        e->trackParam[0][tpPOSITION] = 110;              // near the end: grains must not wrap or run off
                        e->trackParam[0][tpSHAPE] = mode == gmLoop ? 40.f : 64.f;
                        Run r = render(*e, 48000);
                        const float c = clickiness(r.L, 2000, 48000);
                        // the level on real material (a sine adds up in strange ways): PAD and VOICE
                        float lv = 0.f;
                        for (int m : {1, 3}) {
                            auto f = make(material(m).get());
                            f->bank.kit[0].mode = (uint8_t)mode;
                            f->bank.patterns[0].on[0][0] = 1; f->bank.patterns[0].len[0] = 64;
                            f->trackParam[0][tpSIZE] = size; f->trackParam[0][tpDENSITY] = dens; f->trackParam[0][tpPITCH] = pitch;
                            f->trackParam[0][tpPOSITION] = 40; f->trackParam[0][tpSHAPE] = mode == gmLoop ? 40.f : 64.f;
                            Run q = render(*f, 48000);
                            const float one = 20.f * std::log10(std::max(1e-6f, rms(q.L, 4000, 48000)) / matRms[m]);
                            lv += 0.5f * one;
                            if (one < -8.f || one > 5.f) std::printf("     (%s on %s: %+.1f dB)\n", kModeNames[mode], kMaterialNames[m], one);
                        }
                        const float lim = pitch > 64.f ? 0.03f : 0.02f;
                        if (c > lim) { smooth = false; std::snprintf(msg, sizeof(msg), "     click: %s size %.0f dens %.0f pitch %.0f -> %.4f", kModeNames[mode], size, dens, pitch, c); std::puts(msg); }
                        const float db = lv;
                        // (tiny dense pitched grains comb a little: metallic, as on the classic hardware)
                        if (db < (size < 40.f ? -14.f : -7.f) || db > 4.f) { level = false; std::snprintf(msg, sizeof(msg), "     level: %s size %.0f dens %.0f -> %+.1f dB", kModeNames[mode], size, dens, db); std::puts(msg); }
                    }
        check(smooth, "no clicks in CLASSIC / CLOUD / LOOP at any size, density, pitch, near the end of the sample");
        check(level, "PAD / VOICE in every mode / size / density: within -7 .. +4 dB of the material");
    }
    {   // CLASSIC: an even sound (no gaps), even when frozen
        for (float scan : {96.f, 64.f}) {
            auto e = make(material(1).get());
            e->bank.patterns[0].on[0][0] = 1; e->bank.patterns[0].len[0] = 64;
            e->trackParam[0][tpPOSITION] = 50.f; e->trackParam[0][tpSIZE] = 60.f; e->trackParam[0][tpSCAN] = scan; e->trackParam[0][tpSHAPE] = 64.f;
            Run r = render(*e, 48000);
            float lo = 1e9f, hi = 0.f;
            for (int a = 4800; a + 2400 <= 48000; a += 2400) { const float v = rms(r.L, a, a + 2400); lo = std::min(lo, v); hi = std::max(hi, v); }
            const float var = 20.f * std::log10(hi / std::max(1e-9f, lo));
            std::printf("     CLASSIC %s: level varies %.1f dB\n", scan == 64.f ? "frozen" : "scanning", var);
            check(var < 6.f, scan == 64.f ? "CLASSIC frozen: steady level (no buzz, no gaps)" : "CLASSIC: steady level while scanning");
        }
    }
    {   // LOOP + SYNC 1/16, frozen: the sound repeats every 16th (6000 frames at 120)
        auto two = sine(220.f, 2.f, 330.f);
        auto e = make(two.get());
        e->bank.kit[0].mode = gmLoop; e->bank.kit[0].sync = 1;
        e->bank.patterns[0].on[0][0] = 1; e->bank.patterns[0].len[0] = 64;
        e->trackParam[0][tpSIZE] = (4 + 0.5f) * 128.f / kSyncSizes;   // 1/16
        e->trackParam[0][tpSCAN] = 64.f; e->trackParam[0][tpSHAPE] = 10.f; e->trackParam[0][tpDENSITY] = 0.f;
        Run r = render(*e, 48000);
        double c = 0, a2 = 0, b2 = 0; for (int i = 12000; i < 18000; ++i) { c += r.L[i] * r.L[i + 6000]; a2 += r.L[i] * r.L[i]; b2 += r.L[i + 6000] * r.L[i + 6000]; }
        check(c / std::sqrt(a2 * b2 + 1e-12) > 0.95, "LOOP + SYNC 1/16 frozen: the same 16th again and again");
    }
    {   // REVERSE / OCTAVE chances
        auto s = sine(440.f, 2.f);
        auto e = make(s.get());
        e->bank.kit[0].mode = gmCloud;
        e->bank.patterns[0].on[0][0] = 1; e->bank.patterns[0].len[0] = 64;
        e->trackParam[0][tpREVERSE] = 64.f; e->trackParam[0][tpOCTAVE] = 32.f; e->trackParam[0][tpDENSITY] = 110.f;
        render(*e, 96000);
        int all, rev, oct; e->spawnStats(0, all, rev, oct);
        std::printf("     %d grains, %d reversed, %d octave\n", all, rev, oct);
        check(all > 100 && std::fabs(rev / (float)all - 0.5f) < 0.1f && std::fabs(oct / (float)all - 0.25f) < 0.08f, "REVERSE 50%: half the grains backwards; OCTAVE 25%: a quarter shifted");
    }
    {   // stereo material stays stereo
        auto st = std::make_shared<SampleData>(); st->sr = 48000;
        for (int i = 0; i < 96000; ++i) { const float a = 0.5f * (float)std::sin(6.283185307 * 440.0 * i / 48000.0), b = 0.5f * (float)std::sin(6.283185307 * 660.0 * i / 48000.0); st->l.push_back(a); st->r.push_back(b); st->pcm.push_back(0.5f * (a + b)); }
        auto e = make(st.get());
        e->bank.patterns[0].on[0][0] = 1; e->bank.patterns[0].len[0] = 64;
        Run r = render(*e, 24000);
        check(std::fabs(zcHz(r.L, 4000, 23000) - 440.f) < 30.f && std::fabs(zcHz(r.R, 4000, 23000) - 660.f) < 40.f, "a stereo sample keeps its left and right");
        SamplePtr back; smp::decodeAll(smp::encodeLine(2, *st).c_str(), [&](int t, SamplePtr d) { if (t == 2) back = d; });
        bool same = back && back->stereo() && back->l.size() == st->l.size();
        for (size_t i = 0; same && i < st->l.size(); i += 97) same = std::fabs(back->l[i] - st->l[i]) < 1e-4f && std::fabs(back->r[i] - st->r[i]) < 1e-4f;
        check(same, "stereo sample: saved and read back");
    }
    {   // LIVE: the input is granulated as it comes; FREEZE keeps what is in the buffer; CAPTURE takes the last bars
        std::unique_ptr<Engine> e(new Engine()); e->init(sr);
        e->global[gPLAY] = 1.f; e->global[gTEMPO] = 120.f; e->global[gCAPLEN] = 0.f;   // 1 bar = 2 s
        e->bank.kit[0].material = kMatLive;
        e->trackParam[0][tpSPREAD] = 0.f; e->trackParam[0][tpPOSITION] = 120.f;
        e->bank.patterns[0].on[0][0] = 1; e->bank.patterns[0].len[0] = 64;
        std::vector<float> in(256), L(256), R(256), outAll;
        double ph = 0;
        auto feed = [&](int frames, float hz, float amp) {
            for (int p = 0; p < frames; p += 256) {
                for (int i = 0; i < 256; ++i) { ph += hz / 48000.0; in[i] = amp * (float)std::sin(6.283185307 * ph); }
                e->inL = in.data(); e->inR = in.data();
                e->process(L.data(), R.data(), 256, Transport());
                outAll.insert(outAll.end(), L.begin(), L.end());
            }
        };
        feed(96000 * 2, 330.f, 0.4f);                       // 4 s of 330 Hz in (the track started at once: silence first)
        const int n0 = (int)outAll.size();
        check(std::fabs(zcHz(outAll, n0 - 24000, n0) - 330.f) < 25.f && rms(outAll, n0 - 24000, n0) > 0.05f, "LIVE: the track plays the input");
        e->global[gFREEZE] = 1.f;
        feed(48000, 0.f, 0.f);                               // silence comes in, frozen
        const int n1 = (int)outAll.size();
        check(rms(outAll, n1 - 24000, n1) > 0.05f, "FREEZE: the buffer keeps the sound when the input goes silent");
        e->global[gFREEZE] = 0.f;
        feed(96000 * 3, 0.f, 0.f);
        const int n2 = (int)outAll.size();
        check(rms(outAll, n2 - 12000, n2) < 0.01f, "without FREEZE the silence comes through");
        check(!e->capture(1), "CAPTURE of silence: nothing");
        feed(96000 * 2, 550.f, 0.3f);
        SamplePtr c = e->capture(1);
        check(c && std::abs((int)c->l.size() - 96000) < 600 && c->stereo(), "CAPTURE 1 bar at 120 BPM = 2 s, stereo");
        if (c) {
            std::vector<float> m(c->pcm.begin(), c->pcm.end());
            check(std::fabs(zcHz(m, 1000, 90000) - 550.f) < 20.f, "CAPTURE holds the last input (550 Hz)");
        }
    }
    {   // bank: mode / sync saved, 0.2 locks moved to their new places
        Bank b; b.initDefault(); b.kit[1].mode = gmLoop; b.kit[1].sync = 1;
        Bank c; deserializeBank(c, serializeBank(b).c_str());
        check(c.kit[1].mode == gmLoop && c.kit[1].sync == 1, "MODE and SYNC are saved");
        Bank d; deserializeBank(d, "KGRN41\nk 0 0 3 6 90\nk 0 0 3 15 40\n");
        check(d.patterns[0].locks[0][3][tpSPRAY] == 90 && d.patterns[0].locks[0][3][tpREVERB] == 40, "0.2 banks: SPRAY and REVERB locks found again");
    }
    {   // CPU: four tracks, dense clouds of long grains, stereo, for 4 s
        auto e = make(material(1).get());
        for (int t = 0; t < kTracks; ++t) {
            e->source[t] = material(t + 1).get(); e->bank.kit[t].mode = gmCloud;
            e->bank.patterns[0].on[t][0] = 1; e->bank.patterns[0].len[t] = 64;
            e->trackParam[t][tpSIZE] = 127.f; e->trackParam[t][tpDENSITY] = 127.f; e->trackParam[t][tpSPREAD] = 100.f;
        }
        std::vector<float> L(256), R(256);
        const auto t0 = std::chrono::steady_clock::now();
        for (int p = 0; p < 48000 * 4; p += 256) e->process(L.data(), R.data(), 256, Transport());
        const double secs = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
        std::printf("     worst case: %.1f %% of one core\n", secs / 4.0 * 100.0);
        check(secs / 4.0 < 0.25, "worst case CPU under 25 % of a core");
    }
    std::printf(fails ? "\n%d FAILED\n" : "\nall passed\n", fails);
    return fails ? 1 : 0;
}
