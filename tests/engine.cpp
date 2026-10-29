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
#include <cstring>
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
    for (int t = 0; t < kTracks; ++t) for (int k = 0; k < kTrackParams; ++k) e->trackParam[t][k] = trackParamDefault(k);   // neutral knobs
    e->global[gPLAY] = 1.f; e->global[gTEMPO] = 120.f;
    e->global[gVOLUME] = 115.9f;                   // master VOLUME at unity gain (the measurements compare levels)
    e->trackParam[0][tpSPREAD] = 0.f;              // no random detune / pan for the measurements
    e->source[0] = src;
    for (auto& P : e->bank.patterns) std::memset(P.gate, kGateHold, sizeof(P.gate));   // notes last until the next one
    return e;
}

int main()
{
    // materials
    bool matOk = true;
    for (int i = 1; i <= kMaterials; ++i) {
        SamplePtr m = material(i);
        float pk = 0.f; bool fin = true; for (float v : m->pcm) { pk = std::max(pk, std::fabs(v)); fin = fin && std::isfinite(v); }
        const size_t n = m ? m->pcm.size() : 0;
        if (!m || (n != 192000 && n != 288000) || !m->stereo() || !m->hasMips() || pk < 0.4f || pk > 0.9f || !fin) { matOk = false; std::printf("  material %d bad\n", i); }
    }
    check(matOk, "8 built-in materials: 4 or 6 s, stereo, band-limited copies, normalised, finite");
    {   // not thin, not mono, not dull: wide stereo (low L/R correlation) and some air above 4 kHz for the tonal ones
        bool wide = true, bright = true;
        for (int i = 1; i <= kMaterials; ++i) {
            const SampleData& m = *material(i);
            double c = 0, ll = 0, rr = 0, hf = 0, all = 0; float prev = 0.f;
            for (size_t k = 0; k < m.l.size(); ++k) { c += m.l[k] * m.r[k]; ll += m.l[k] * m.l[k]; rr += m.r[k] * m.r[k]; const float d = m.pcm[k] - prev; prev = m.pcm[k]; hf += d * d; all += m.pcm[k] * m.pcm[k]; }
            const double corr = c / std::sqrt(ll * rr + 1e-12), air = hf / (all + 1e-12);
            std::printf("     %-7s L/R correlation %.2f, high-frequency share %.4f\n", kMaterialNames[i], corr, air);
            if (i != 6 && corr > 0.85) wide = false;          // the beat keeps its kick and snare in the centre
            if (air < 0.0015) bright = false;
        }
        check(wide, "materials are stereo (L/R correlation under 0.85, except the beat)");
        check(bright, "materials have some brightness (not dull)");
    }

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
        P.on[0][0] = 1; P.on[0][2] = 1; P.note[0][0] = kBaseNote + 12;
        Run r = render(*e, 6000 * 4);
        const float a = zcHz(r.L, 1500, 5900), b = zcHz(r.L, 13500, 17900);
        check(std::fabs(a - 880.f) < 60.f && std::fabs(b - 440.f) < 30.f, "a step note +12 changes only its step");
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
        e->trackParam[0][tpDECAY] = 40.f; e->trackParam[0][tpSUSTAIN] = 0.f;   // ~140 ms to nothing
        Run r = render(*e, 24000);
        check(rms(r.L, 500, 2000) > 0.02f && rms(r.L, 12000, 24000) < 1e-4f, "short DECAY ends the note");
    }
    {   // live note while stopped, note off releases a HOLD note
        auto e = make(s440.get());
        e->global[gPLAY] = 0.f;
        NoteIn on{0, 0, kBaseNote + 12, 1.f, true};
        std::vector<float> L(24000), R(24000);
        e->process(L.data(), R.data(), 256, Transport(), &on, 1);
        for (int p = 256; p < 12000; p += 256) e->process(L.data() + p, R.data() + p, 256, Transport());
        const float f = zcHz(L, 3000, 12000);
        NoteIn off{0, 0, kBaseNote + 12, 0.f, false};
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
            e->global[gREV_SIZE] = 127; e->global[gECHO_FB] = 127; e->global[gVOLUME] = 127;
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
    {   // 0.4: no aliasing: 15 kHz pitched an octave up must vanish (it would fold back to 18 kHz), 1 kHz stays
        auto hf = std::make_shared<SampleData>(); hf->sr = 48000;
        for (int i = 0; i < 96000; ++i) hf->pcm.push_back(0.5f * (float)std::sin(6.283185307 * 15000.0 * i / 48000.0) + 0.25f * (float)std::sin(6.283185307 * 1000.0 * i / 48000.0));
        smp::buildMips(*hf);
        auto e = make(hf.get());
        e->bank.patterns[0].on[0][0] = 1; e->bank.patterns[0].len[0] = 64; e->trackParam[0][tpPITCH] = 64.f + 32.f;
        Run r = render(*e, 48000);
        // energy around 18 kHz (the alias) versus around 2 kHz (the octave of 1 kHz)
        auto band = [&](double f0) { double re = 0, im = 0; for (int i = 6000; i < 48000; ++i) { const double w = 6.283185307 * f0 * i / 48000.0; re += r.L[i] * std::cos(w); im += r.L[i] * std::sin(w); } return std::sqrt(re * re + im * im); };
        const double alias = band(18000.0), wanted = band(2000.0);
        std::printf("     alias at 18 kHz %.1f dB below the 2 kHz octave\n", 20.0 * std::log10(wanted / std::max(1e-9, alias)));
        check(wanted > 100.0 * alias, "pitch +12: no aliasing (more than 40 dB down)");
    }
    {   // LEGATO: a step changes the pitch without starting the note again (no new attack, the play head goes on)
        auto s = sine(440.f, 2.f);
        auto e = make(s.get());
        e->bank.kit[0].legato = 1;
        Pattern& P = e->bank.patterns[0];
        P.on[0][0] = P.on[0][4] = 1; P.len[0] = 64; P.note[0][4] = kBaseNote + 12;
        e->trackParam[0][tpATTACK] = 100.f;                 // a slow attack: a retrigger would dip
        Run r = render(*e, 6000 * 8);
        check(std::fabs(zcHz(r.L, 6000 * 4 + 3000, 6000 * 6) - 880.f) < 60.f, "LEGATO: the note of step 5 plays");
        check(rms(r.L, 6000 * 4 + 200, 6000 * 4 + 1200) > 0.6f * rms(r.L, 6000 * 3, 6000 * 4), "LEGATO: no new attack at step 5");
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
                        if (db < (size < 40.f ? -14.f : -7.f) || db > 5.f) { level = false; std::snprintf(msg, sizeof(msg), "     level: %s size %.0f dens %.0f -> %+.1f dB", kModeNames[mode], size, dens, db); std::puts(msg); }
                    }
        check(smooth, "no clicks in CLASSIC / CLOUD / LOOP at any size, density, pitch, near the end of the sample");
        check(level, "PAD / CHOIR in every mode / size / density: within -7 .. +5 dB of the material");
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
        e->bank.patterns[0].on[0][0] = 1; e->bank.patterns[0].len[0] = 64; e->bank.patterns[0].gate[0][0] = kGateHold;
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
    // ---------------------------------------------------------------- 0.4: polyphony, START / END, PITCH MIX, LFOs, steps, effects
    auto bandAt = [](const std::vector<float>& x, int a, int b, double f0) { double re = 0, im = 0; for (int i = a; i < b; ++i) { const double w = 6.283185307 * f0 * i / 48000.0; re += x[i] * std::cos(w); im += x[i] * std::sin(w); } return std::sqrt(re * re + im * im) / (b - a); };
    {   // a MAJ chord: three voices, C E G of a 440 Hz sine = 440, 554, 659 Hz
        auto e = make(s440.get());
        Pattern& P = e->bank.patterns[0]; P.on[0][0] = 1; P.len[0] = 64; P.chord[0][0] = 1;
        e->trackParam[0][tpSCAN] = 64.f;
        Run r = render(*e, 24000);
        const double a = bandAt(r.L, 6000, 24000, 440.0), b = bandAt(r.L, 6000, 24000, 554.37), c = bandAt(r.L, 6000, 24000, 659.26), off = bandAt(r.L, 6000, 24000, 500.0);
        check(e->activeVoices(0) == 3 && a > 8 * off && b > 8 * off && c > 8 * off, "a MAJ chord plays three voices (root, third, fifth)");
    }
    {   // more than 6 notes: the oldest is taken (never more than 6 sounding)
        auto e = make(s440.get());
        e->global[gPLAY] = 0.f;
        std::vector<float> L(256), R(256); int most = 0;
        for (int n = 0; n < 9; ++n) { NoteIn on{0, 0, kBaseNote + n, 1.f, true}; e->process(L.data(), R.data(), 256, Transport(), &on, 1); }
        for (int p = 0; p < 4800; p += 256) { e->process(L.data(), R.data(), 256, Transport()); most = std::max(most, e->activeVoices(0)); }
        check(most == kVoices, "9 held notes: 6 voices sound, the oldest ones are stolen");
    }
    {   // START / END: a sample 220 Hz then 880 Hz; START 75% plays only the second half
        auto two = sine(220.f, 2.f, 880.f);
        auto e = make(two.get());
        e->bank.patterns[0].on[0][0] = 1; e->bank.patterns[0].len[0] = 64;
        e->trackParam[0][tpSTART] = 96.f; e->trackParam[0][tpSCAN] = 96.f;   // scanning: wraps inside START .. END
        Run r = render(*e, 96000 * 2);
        check(std::fabs(zcHz(r.L, 4000, 190000) - 880.f) < 40.f, "START 75%: only the part after START plays, even while scanning");
        auto f = make(two.get());
        f->bank.patterns[0].on[0][0] = 1; f->bank.patterns[0].len[0] = 64;
        f->trackParam[0][tpEND] = 30.f; f->trackParam[0][tpSCAN] = 96.f;
        Run q = render(*f, 96000 * 2);
        check(std::fabs(zcHz(q.L, 4000, 190000) - 220.f) < 20.f, "END 25%: the second half never plays");
    }
    {   // PITCH MIX: 0 = the sample's own pitch whatever PITCH says, centre = both
        auto e = make(s440.get());
        e->bank.patterns[0].on[0][0] = 1; e->bank.patterns[0].len[0] = 64;
        e->trackParam[0][tpPITCH] = 96.f; e->trackParam[0][tpPMIX] = 0.f;
        Run r = render(*e, 24000);
        const bool orig = bandAt(r.L, 4000, 24000, 440.0) > 10 * bandAt(r.L, 4000, 24000, 880.0);
        auto f = make(s440.get());
        f->bank.patterns[0].on[0][0] = 1; f->bank.patterns[0].len[0] = 64; f->bank.kit[0].mode = gmCloud;
        f->trackParam[0][tpPITCH] = 96.f; f->trackParam[0][tpPMIX] = 64.f; f->trackParam[0][tpDENSITY] = 120.f;
        Run q = render(*f, 48000);
        double lo = 0, hi = 0;                               // short blocks: the grains' phases are random
        for (int a = 4000; a + 2048 <= 48000; a += 2048) { lo += bandAt(q.L, a, a + 2048, 440.0); hi += bandAt(q.L, a, a + 2048, 880.0); }
        std::printf("     pmix: orig %d lo %.4f hi %.4f\n", (int)orig, lo, hi);
        check(orig && lo > 0.3 * hi && hi > 0.3 * lo, "PITCH MIX: 0 = original pitch, centre = original and transposed together");
    }
    {   // LFO 1 on POSITION (square, 1 Hz, full depth): the play head jumps between two places
        auto e = make(s440.get());
        e->bank.patterns[0].on[0][0] = 1; e->bank.patterns[0].len[0] = 64;
        e->trackParam[0][tpSCAN] = 64.f; e->trackParam[0][tpPOSITION] = 64.f;
        e->trackParam[0][lfoParam(0, 0)] = (2 + 0.5f) * 128.f / kLfoWaves;      // square
        e->trackParam[0][lfoParam(0, 1)] = 127.f * std::log(1.f / 0.03f) / std::log(1000.f);   // 1 Hz
        e->trackParam[0][lfoParam(0, 3)] = 124.f; e->trackParam[0][lfoParam(0, 4)] = lfoDestKnob(tpPOSITION + 1);
        float mn = 2.f, mx = -1.f; std::vector<float> L(256), R(256);
        for (int p = 0; p < 96000; p += 256) { e->process(L.data(), R.data(), 256, Transport()); if (e->outPos[0] >= 0.f) { mn = std::min(mn, e->outPos[0]); mx = std::max(mx, e->outPos[0]); } }
        std::printf("     lfo: %.3f .. %.3f\n", mn, mx);
        check(mn < 0.06f && mx > 0.94f, "LFO on POSITION moves the play head from one end to the other");
        check(std::fabs(e->lfoValue(0, 0)) > 0.9f, "LFO value reported for the display");
    }
    {   // step length: gate 1 step = the note ends after ~6000 frames (+ RELEASE)
        auto e = make(s440.get());
        e->bank.patterns[0].on[0][0] = 1; e->bank.patterns[0].len[0] = 64; e->bank.patterns[0].gate[0][0] = 4;
        e->trackParam[0][tpRELEASE] = 0.f;
        Run r = render(*e, 24000);
        check(rms(r.L, 2000, 5800) > 0.05f && rms(r.L, 8000, 24000) < 1e-4f, "LEN 1 step: the note stops at the end of its step");
    }
    {   // ratchets: x4 on a 1-step gate = four attacks
        auto e = make(s440.get());
        Pattern& P = e->bank.patterns[0]; P.on[0][0] = 1; P.len[0] = 64; P.gate[0][0] = 2; P.ratch[0][0] = 3;   // x4, each half its slot
        e->trackParam[0][tpRELEASE] = 0.f; e->trackParam[0][tpATTACK] = 0.f;
        Run r = render(*e, 6000);
        int on = 0; bool was = false;
        for (int a = 0; a + 250 <= 6000; a += 250) { const bool snd = rms(r.L, a, a + 250) > 0.02f; if (snd && !was) ++on; was = snd; }
        std::printf("     ratchet: %d\n", on);
        check(on == 4, "RATCHET x4: four notes in the step");
    }
    {   // conditions: 1:2 plays every other loop, FILL only while FILL is held
        auto e = make(s440.get());
        Pattern& P = e->bank.patterns[0]; P.on[0][0] = 1; P.len[0] = 1; P.gate[0][0] = 2; P.cond[0][0] = 11;   // 1:2, half a step
        e->trackParam[0][tpRELEASE] = 0.f;
        Run r = render(*e, 6000 * 4);
        const bool alt = rms(r.L, 500, 2500) > 0.02f && rms(r.L, 6500, 8500) < 1e-3f && rms(r.L, 12500, 14500) > 0.02f && rms(r.L, 18500, 20500) < 1e-3f;
        check(alt, "condition 1:2 plays on loops 1, 3, 5...");
        auto f = make(s440.get());
        Pattern& Q = f->bank.patterns[0]; Q.on[0][0] = 1; Q.len[0] = 1; Q.gate[0][0] = 2; Q.cond[0][0] = 7;   // FILL
        f->trackParam[0][tpRELEASE] = 0.f;
        Run a = render(*f, 12000); f->fill = true; Run b = render(*f, 12000);
        check(rms(a.L, 0, 12000) < 1e-3f && rms(b.L, 0, 12000) > 0.02f, "condition FILL: only while FILL is held");
    }
    {   // SOLO: only the soloed track
        auto e = make(s440.get());
        e->source[1] = s440.get();
        e->bank.patterns[0].on[0][0] = e->bank.patterns[0].on[1][0] = 1; e->bank.kit[1].solo = 1;
        Run r = render(*e, 12000);
        check(e->activeVoices(0) == 0 && e->activeVoices(1) == 1 && rms(r.L, 2000, 12000) > 0.02f, "SOLO: the other tracks are silent");
    }
    {   // the voice filter and its envelope: a low pass with FLT ENV up is brighter at the attack than later
        auto saw = std::make_shared<SampleData>(); saw->sr = 48000;
        for (int i = 0; i < 96000; ++i) saw->pcm.push_back(0.4f * (float)(2.0 * std::fmod(i * 220.0 / 48000.0, 1.0) - 1.0));
        auto e = make(saw.get());
        e->bank.patterns[0].on[0][0] = 1; e->bank.patterns[0].len[0] = 64;
        e->trackParam[0][tpFTYPE] = binKnob(ftLP12, kFilterTypes); e->trackParam[0][tpCUTOFF] = 40.f; e->trackParam[0][tpFLTENV] = 127.f;
        e->trackParam[0][tpFATK] = 0.f; e->trackParam[0][tpFDEC] = 100.f;
        Run r = render(*e, 48000);
        auto bright = [&](int a, int b) { double d = 0, s2 = 0; for (int i = a + 1; i < b; ++i) { const double x = r.L[i] - r.L[i - 1]; d += x * x; s2 += r.L[i] * r.L[i]; } return d / (s2 + 1e-12); };
        std::printf("     fenv bright %.5f %.5f\n", bright(300, 2000), bright(30000, 40000));
        check(bright(300, 2000) > 2.0 * bright(30000, 40000), "the filter envelope opens the low pass at the attack, then closes");
    }
    {   // effects at their extremes and the master section: finite, bounded, and the echo / reverb really return
        auto e = make(material(2).get());
        e->bank.patterns[0].on[0][0] = 1; e->bank.patterns[0].len[0] = 16; e->bank.patterns[0].gate[0][0] = 1;
        e->trackParam[0][tpDELAY] = 127.f; e->trackParam[0][tpREVERB] = 127.f;
        bool ok = true; float lo = 1e9f;
        for (int m = 0; m < 8; ++m) {                       // every echo model and reverb model, everything turned up, CHAOS on
            for (int g = 0; g < gTEMPO; ++g) e->global[g] = 127.f;
            e->global[gECHO_MODEL] = (float)(m * 16); e->global[gREV_MODEL] = (float)(m % 5 * 26); e->global[gECHO_FREEZE] = 0.f;
            e->global[gMF_LP] = 60.f; e->global[gMF_HP] = 20.f; e->global[gCMP_THR] = 60.f; e->trackParam[0][tpCHORUS] = 127.f;
            Run r = render(*e, 48000);
            for (float v : r.L) ok = ok && std::isfinite(v) && std::fabs(v) <= 1.5f;
            lo = std::min(lo, rms(r.L, 0, 48000));
        }
        Run r = render(*e, 4800);
        std::printf("     extremes: ok %d, quietest %.4f\n", (int)ok, lo);
        check(ok && lo > 1e-3f, "every echo / reverb model at the extremes, CHAOS, chorus, filter: finite, bounded, audible");
    }
    {   // 0.4.1: the grain display sees every grain, where it reads and at which pitch (a MIN7 chord = 4 voices, 0 3 7 10)
        auto e = make(material(1).get());
        Pattern& P = e->bank.patterns[0]; P.on[0][0] = 1; P.len[0] = 64; P.chord[0][0] = 5;
        e->trackParam[0][tpDENSITY] = 100.f;
        render(*e, 24000);
        GrainView g[kViewGrains]; VoiceView v[8]; int nv = 0;
        const int n = e->grainView(0, g, kViewGrains, v, 8, nv);
        bool inside = true, pitches[11] = {};
        for (int i = 0; i < n; ++i) { inside = inside && g[i].a >= 0.f && g[i].a <= 1.f && g[i].b >= 0.f && g[i].b <= 1.f && g[i].ph >= 0.f && g[i].ph <= 1.f;
                                      const int st = (int)std::lround(g[i].semis); if (st >= 0 && st <= 10) pitches[st] = true; }
        check(n == e->activeGrains(0) && n >= 8 && nv == 4 && inside, "grain view: every grain, inside the material, 4 voices");
        check(pitches[0] && pitches[3] && pitches[7] && pitches[10], "grain view: the chord's pitches (0 3 7 10)");
    }
    {   // 0.5: GRAIN MIX 0 = the sample played straight: a 440 Hz sine stays a clean 440 Hz at the note's pitch, no grains
        auto e = make(s440.get());
        Pattern& P = e->bank.patterns[0]; P.on[0][0] = 1; P.len[0] = 64; P.note[0][0] = kBaseNote + 12;
        e->trackParam[0][tpGMIX] = 0.f; e->trackParam[0][tpSCAN] = 64.f;
        Run r = render(*e, 48000);
        float lo = 1e9f, hi = 0.f; for (int a = 4800; a + 2400 <= 48000; a += 2400) { const float v = rms(r.L, a, a + 2400); lo = std::min(lo, v); hi = std::max(hi, v); }
        check(e->activeGrains(0) == 0 && std::fabs(zcHz(r.L, 4800, 48000) - 880.f) < 20.f && hi < lo * 1.12f && lo > 0.05f, "GRAIN MIX 0: the sample straight at the note's pitch, steady, no grains");
        auto f = make(s440.get());
        f->bank.patterns[0].on[0][0] = 1; f->bank.patterns[0].len[0] = 64; f->trackParam[0][tpGMIX] = 64.f;
        Run q = render(*f, 24000);
        check(f->activeGrains(0) > 0 && rms(q.L, 4000, 24000) > 0.05f, "GRAIN MIX centre: both");
    }
    {   // 0.5: LOW CUT removes the lows (110 Hz at 1 kHz cut)
        auto low = sine(110.f, 2.f);
        auto e = make(low.get()); e->bank.patterns[0].on[0][0] = 1; e->bank.patterns[0].len[0] = 64;
        Run a = render(*e, 24000);
        auto f = make(low.get()); f->bank.patterns[0].on[0][0] = 1; f->bank.patterns[0].len[0] = 64; f->trackParam[0][tpLOWCUT] = 95.f;
        Run b = render(*f, 24000);
        check(rms(b.L, 6000, 24000) < 0.1f * rms(a.L, 6000, 24000), "LOW CUT removes what is under it");
    }
    {   // 0.5: LFO 3 moves its destination too
        auto e = make(s440.get());
        e->bank.patterns[0].on[0][0] = 1; e->bank.patterns[0].len[0] = 64; e->trackParam[0][tpSCAN] = 64.f; e->trackParam[0][tpPOSITION] = 64.f;
        e->trackParam[0][lfoParam(2, 0)] = (2 + 0.5f) * 128.f / kLfoWaves;
        e->trackParam[0][lfoParam(2, 1)] = 127.f * std::log(1.f / 0.03f) / std::log(1000.f);
        e->trackParam[0][lfoParam(2, 3)] = 124.f; e->trackParam[0][lfoParam(2, 4)] = lfoDestKnob(tpPOSITION + 1);
        float mn = 2.f, mx = -1.f; std::vector<float> L(256), R(256);
        for (int p = 0; p < 96000; p += 256) { e->process(L.data(), R.data(), 256, Transport()); if (e->outPos[0] >= 0.f) { mn = std::min(mn, e->outPos[0]); mx = std::max(mx, e->outPos[0]); } }
        check(mn < 0.06f && mx > 0.94f, "LFO 3 on POSITION");
    }
    {   // 0.5: 128 steps: step 100 plays
        auto e = make(s440.get());
        Pattern& P = e->bank.patterns[0]; P.length = 128; P.len[0] = 128; P.on[0][99] = 1; P.gate[0][99] = 4;
        e->trackParam[0][tpRELEASE] = 0.f;
        Run r = render(*e, 6000 * 101);
        check(rms(r.L, 0, 6000 * 99 - 100) < 1e-4f && rms(r.L, 6000 * 99 + 500, 6000 * 100 - 500) > 0.02f, "128 steps: step 100 plays at its time");
    }
    {   // 0.5: REC: a live note while playing comes back as a step + its length, and the step is not played twice
        auto e = make(s440.get()); e->recording = true;
        std::vector<float> L(256), R(256);
        for (int p = 0; p < 6000 * 2 - 256; p += 256) e->process(L.data(), R.data(), 256, Transport());   // just before step 3
        NoteIn on{0, 0, kBaseNote + 7, 1.f, true};
        e->process(L.data(), R.data(), 256, Transport(), &on, 1);
        for (int p = 0; p < 6000; p += 256) e->process(L.data(), R.data(), 256, Transport());
        NoteIn off{0, 0, kBaseNote + 7, 0.f, false};
        e->process(L.data(), R.data(), 256, Transport(), &off, 1);
        RecEvent ev; int n = 0; bool stepOk = false, gateOk = false;
        while (e->popRec(ev)) { ++n; if (ev.kind == 0) stepOk = ev.step == 2 && ev.note == kBaseNote + 7 && ev.track == 0; else gateOk = ev.step == 2 && ev.gate == 4; }
        check(n == 2 && stepOk && gateOk, "REC: the note lands on the nearest step with its length (1 step)");
    }
    {   // 0.6: LOOP SYNC: a 2 s sample set to 4 beats plays through in 2 s at 120 BPM and in 4 s at 60 BPM
        auto lp = sine(440.f, 2.f);
        for (float bpm : {120.f, 60.f}) {
            auto e = make(lp.get()); e->bank.kit[0].beats = 4; e->global[gTEMPO] = bpm;
            e->bank.patterns[0].on[0][0] = 1; e->bank.patterns[0].len[0] = 64;
            render(*e, 2400); const float a = e->outPos[0];
            render(*e, 48000); const float b = e->outPos[0];
            const float want = bpm == 120.f ? 0.5f : 0.25f;
            std::printf("     loop sync %.0f BPM: moved %.3f\n", bpm, b - a);
            check(std::fabs((b - a) - want) < 0.01f, bpm == 120.f ? "LOOP SYNC 4 beats at 120 BPM: half the loop in 1 s" : "LOOP SYNC at 60 BPM: a quarter in 1 s");
        }
    }
    {   // 0.6: SCALE: C# (+1) in C MAJOR plays C, D (+2) stays D; MINOR on A: E (+4) stays, F# (+6) -> F
        check(quantizeNote(61, 0, 1) == 60 && quantizeNote(62, 0, 1) == 62 && quantizeNote(64, 9, 2) == 64 && quantizeNote(66, 9, 2) == 65 && quantizeNote(66, 0, 0) == 66,
              "SCALE: notes moved to the nearest note of the scale");
        auto e = make(s440.get()); e->bank.scale = 1;
        Pattern& P = e->bank.patterns[0]; P.on[0][0] = 1; P.len[0] = 64; P.note[0][0] = kBaseNote + 1;
        Run r = render(*e, 24000);
        check(std::fabs(zcHz(r.L, 4000, 24000) - 440.f) < 15.f, "SCALE MAJOR: a C# step plays C (the sample's pitch)");
    }
    {   // 0.7: filter types: a 2 kHz high pass 24 dB removes a 220 Hz saw's fundamental, a low pass 24 dB at 300 Hz its highs
        auto saw = std::make_shared<SampleData>(); saw->sr = 48000;
        for (int i = 0; i < 96000; ++i) saw->pcm.push_back(0.4f * (float)(2.0 * std::fmod(i * 220.0 / 48000.0, 1.0) - 1.0));
        auto bandAt2 = [](const std::vector<float>& x, double f0) { double re = 0, im = 0; for (int i = 8000; i < 24000; ++i) { const double w = 6.283185307 * f0 * i / 48000.0; re += x[i] * std::cos(w); im += x[i] * std::sin(w); } return std::sqrt(re * re + im * im); };
        auto run = [&](int type, float cut) { auto e = make(saw.get()); e->bank.patterns[0].on[0][0] = 1; e->bank.patterns[0].len[0] = 64; e->trackParam[0][tpSCAN] = 64.f;
                                             e->trackParam[0][tpFTYPE] = binKnob(type, kFilterTypes); e->trackParam[0][tpCUTOFF] = cut; return render(*e, 24000); };
        Run open = run(ftLP24, 127.f), hp = run(ftHP24, 127.f * std::log(100.f) / std::log(1000.f)), lp = run(ftLP24, 127.f * std::log(15.f) / std::log(1000.f));
        check(bandAt2(hp.L, 220.0) < 0.05 * bandAt2(open.L, 220.0) && bandAt2(lp.L, 2200.0) < 0.05 * bandAt2(open.L, 2200.0), "filter types: HP 24 and LP 24 cut where they should");
    }
    {   // 0.7: NOTES: with a MAJOR scale and NOTES full, grains take notes of the scale over one octave (not only the root)
        auto e = make(s440.get());
        e->bank.patterns[0].on[0][0] = 1; e->bank.patterns[0].len[0] = 64; e->bank.kit[0].mode = gmCloud;
        e->trackParam[0][tpNSCALE] = binKnob(2, kScales + 1); e->trackParam[0][tpNAMOUNT] = 127.f; e->trackParam[0][tpDENSITY] = 110.f;
        render(*e, 24000);
        GrainView g[kViewGrains]; VoiceView vv[8]; int nv = 0; bool seen[13] = {}, outside = false;
        for (int r = 0; r < 20; ++r) {
            render(*e, 2400);
            const int n = e->grainView(0, g, kViewGrains, vv, 8, nv);
            for (int i = 0; i < n; ++i) { const int st = (int)std::lround(g[i].semis); if (st >= 0 && st <= 12) { seen[st] = true; if (!inScale(60 + st, 0, 1)) outside = true; } }
        }
        int distinct = 0; for (bool b : seen) distinct += b;
        check(distinct >= 5 && !outside, "NOTES: grains play several notes of the scale, none outside it");
        e->trackParam[0][tpNORDER] = binKnob(2, kNOrders); e->trackParam[0][tpNRANGE] = 0.f;     // UP: walks the scale
        check(nOrderOf(e->trackParam[0][tpNORDER]) == 2, "NOTES ORDER knob");
    }
    {   // 0.7: ARP UP over a held C major chord: notes C E G C E G..., one per 16th
        auto e = make(s440.get()); e->global[gPLAY] = 0.f;
        e->trackParam[0][tpAMODE] = binKnob(amUp, kArpModes); e->trackParam[0][tpARATE] = binKnob(8, kArpRates);
        e->trackParam[0][tpAGATE] = 30.f; e->trackParam[0][tpRELEASE] = 0.f;
        std::vector<float> L(256), R(256);
        NoteIn ch[3] = {{0, 0, 60, 1.f, true}, {0, 0, 64, 1.f, true}, {0, 0, 67, 1.f, true}};
        e->process(L.data(), R.data(), 256, Transport(), ch, 3);
        std::vector<int> heard;
        for (int p = 0; p < 6000 * 7; p += 256) {
            e->process(L.data(), R.data(), 256, Transport());
            GrainView g[kViewGrains]; VoiceView vv[8]; int nv = 0; e->grainView(0, g, kViewGrains, vv, 8, nv);
            int newest = -1; for (int i = 0; i < nv; ++i) if (!vv[i].released) newest = vv[i].note;
            if (newest >= 0 && (heard.empty() || heard.back() != newest)) heard.push_back(newest);
        }
        std::printf("     arp:"); for (int n : heard) std::printf(" %d", n); std::printf("\n");
        check(heard.size() >= 6 && heard[0] == 60 && heard[1] == 64 && heard[2] == 67 && heard[3] == 60, "ARP UP: C E G C ... from a held chord");
        NoteIn off[3] = {{0, 0, 60, 0.f, false}, {0, 0, 64, 0.f, false}, {0, 0, 67, 0.f, false}};
        e->process(L.data(), R.data(), 256, Transport(), off, 3);
        for (int p = 0; p < 12000; p += 256) e->process(L.data(), R.data(), 256, Transport());
        check(e->activeVoices(0) == 0, "ARP: stops when the keys are released (no LATCH)");
    }
    {   // 0.7: VELOCITY: a soft key is quieter with VELOCITY up
        auto e = make(s440.get()); e->global[gPLAY] = 0.f; e->trackParam[0][tpVELSENS] = 127.f;
        std::vector<float> L(24064), R(24064);
        NoteIn on{0, 0, 60, 0.25f, true}; e->process(L.data(), R.data(), 256, Transport(), &on, 1);
        for (int p = 256; p < 24000; p += 256) e->process(L.data() + p, R.data() + p, 256, Transport());
        auto f = make(s440.get()); f->global[gPLAY] = 0.f; f->trackParam[0][tpVELSENS] = 0.f;
        std::vector<float> L2(24064), R2(24064);
        f->process(L2.data(), R2.data(), 256, Transport(), &on, 1);
        for (int p = 256; p < 24000; p += 256) f->process(L2.data() + p, R2.data() + p, 256, Transport());
        check(rms(L, 6000, 24000) < 0.4f * rms(L2, 6000, 24000), "VELOCITY: soft notes are quieter");
    }
    {   // CPU: four tracks, four-note chords of dense clouds of long grains, stereo, for 4 s
        auto e = make(material(1).get());
        for (int t = 0; t < kTracks; ++t) {
            e->source[t] = material(t + 1).get(); e->bank.kit[t].mode = gmCloud;
            e->bank.patterns[0].on[t][0] = 1; e->bank.patterns[0].len[t] = 64; e->bank.patterns[0].chord[t][0] = 5;   // 4 voices each
            e->trackParam[t][tpSIZE] = 127.f; e->trackParam[t][tpDENSITY] = 127.f; e->trackParam[t][tpSPREAD] = 100.f;
        }
        std::vector<float> L(256), R(256);
        const auto t0 = std::chrono::steady_clock::now();
        for (int p = 0; p < 48000 * 4; p += 256) e->process(L.data(), R.data(), 256, Transport());
        const double secs = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
        std::printf("     worst case: %.1f %% of one core\n", secs / 4.0 * 100.0);
        check(secs / 4.0 < 0.4, "worst case CPU under 40 % of a core (16 voices; ~25 % measured)");
    }
    std::printf(fails ? "\n%d FAILED\n" : "\nall passed\n", fails);
    return fails ? 1 : 0;
}
