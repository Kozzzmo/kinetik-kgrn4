// Kinetik KGRN-4 — built-in materials: eight stereo recordings synthesized in code (nothing is sampled), all in
// C minor and at 120 BPM, so the tracks and the factory presets sound good together before you load your own samples.
// 0.4: band-limited oscillators, detuned ensembles, a stereo room, movement everywhere (they were thin, mono and static).
#pragma once
#include "Sample.hpp"
#include "Dsp.hpp"
#include "Fx.hpp"
#include <array>
#include <complex>
#include <thread>
#include <vector>

namespace kn {

static constexpr int kMaterials = 8;   // ids 1..8 (0 = the track's own sample)
static constexpr int kMatLive = kMaterials + 1;   // the audio input (LIVE), after the built-in ones
static const char* const kMaterialNames[kMaterials + 2] = {"SAMPLE", "PAD", "BELLS", "CHOIR", "RAIN", "PLUCKS", "BEAT", "DRONE", "KEYS", "LIVE"};
// LOOP SYNC of the built-in materials: the rhythmic ones are 2 bars at 120 BPM (8 beats), the others are free
static const uint8_t kMaterialBeats[kMaterials + 2] = {0, 0, 8, 0, 0, 8, 8, 0, 8, 0};

namespace mat {

static constexpr double kSr = 48000.0;
static constexpr float kFs = 48000.f;
struct Rnd { uint32_t s; explicit Rnd(uint32_t seed) : s(seed) {} float u() { s ^= s << 13; s ^= s >> 17; s ^= s << 5; return (s & 0xFFFFFF) / 16777216.f; } float b() { return u() * 2.f - 1.f; } };
// sine of a phase in turns (any value), from a table: the materials are built fast at start-up
inline float fsin(float turns)
{
    static float t[4097]; static bool made = false;
    if (!made) { for (int i = 0; i <= 4096; ++i) t[i] = (float)std::sin(6.283185307179586 * i / 4096.0); made = true; }
    float p = turns - std::floor(turns); p *= 4096.f; const int i = (int)p;
    return t[i] + (t[i + 1] - t[i]) * (p - (float)i);
}
inline float mtof(float n) { return 440.f * std::pow(2.f, (n - 69.f) / 12.f); }
struct Stereo { std::vector<float> l, r; explicit Stereo(double secs) : l((size_t)(secs * kSr), 0.f), r((size_t)(secs * kSr), 0.f) {} int n() const { return (int)l.size(); } };
inline void panAdd(Stereo& s, int i, float x, float pan) { const float a = (pan + 1.f) * 0.25f * kPi; s.l[(size_t)i] += x * std::cos(a); s.r[(size_t)i] += x * std::sin(a); }

// band-limited saw
struct Saw { float ph = 0.f; inline float tick(float f) { const float dt = f / kFs; ph += dt; if (ph >= 1.f) ph -= 1.f; return 2.f * ph - 1.f - polyBlep(ph, dt); } };
// a decaying sine computed by rotation (exact pitch, cheap): amplitude a, decay per second d
struct Partial {
    std::complex<float> z, w;
    void set(float f, float a, float d, float phase = 0.f) { z = std::polar(a, phase); w = std::polar(std::exp(-d / kFs), kTwoPi * f / kFs); }
    inline float tick() { const float y = z.imag(); z *= w; return y; }
};
// stereo ensemble: two modulated delays, opposite phases (width and motion)
struct Ensemble {
    std::vector<float> b; int w = 0; float ph = 0.f;
    Ensemble() : b(4096, 0.f) {}
    inline float rd(float d) const { float p = (float)w - d; while (p < 0.f) p += (float)b.size(); const int i = (int)p; const float f = p - i; return b[(size_t)i % b.size()] * (1.f - f) + b[(size_t)(i + 1) % b.size()] * f; }
    void process(float x, float& l, float& r, float depth = 1.f)
    {
        b[(size_t)w] = x; ph += 0.43f / kFs; if (ph >= 1.f) ph -= 1.f;
        const float m1 = std::sin(kTwoPi * ph), m2 = std::sin(kTwoPi * (ph + 0.37f));
        l = rd(480.f + 220.f * depth * m1); r = rd(560.f + 220.f * depth * m2);
        if (++w >= (int)b.size()) w = 0;
    }
};
// the room / hall every material goes through (wet amount, size and tone like the send reverb)
inline void space(Stereo& s, float wet, float size, float tone)
{
    Reverb rv; rv.init(kFs); rv.set(size, tone);
    for (int i = 0; i < s.n(); ++i) {
        float a, c; rv.process(s.l[(size_t)i], s.r[(size_t)i], a, c);
        s.l[(size_t)i] += a * wet; s.r[(size_t)i] += c * wet;
    }
}
// a ping-pong echo
inline void echo(Stereo& s, double beats, float fb, float mix)
{
    const int d = (int)(beats * 0.5 * kSr);
    std::vector<float> bl((size_t)d, 0.f), br((size_t)d, 0.f); int w = 0;
    for (int i = 0; i < s.n(); ++i) {
        const float el = bl[(size_t)w], er = br[(size_t)w];
        bl[(size_t)w] = (s.l[(size_t)i] + s.r[(size_t)i]) * 0.5f + er * fb; br[(size_t)w] = el * fb;
        s.l[(size_t)i] += el * mix; s.r[(size_t)i] += er * mix;
        if (++w >= d) w = 0;
    }
}
// RMS to a common level (peaks under -1 dBFS), short fades at both ends
inline void finish(Stereo& s, float rmsTarget = 0.17f)
{
    double q = 0; float pk = 0.f;
    for (int i = 0; i < s.n(); ++i) { q += s.l[(size_t)i] * s.l[(size_t)i] + s.r[(size_t)i] * s.r[(size_t)i]; pk = std::max(pk, std::max(std::fabs(s.l[(size_t)i]), std::fabs(s.r[(size_t)i]))); }
    const float rms = (float)std::sqrt(q / (2.0 * s.n()));
    float g = rms > 1e-6f ? rmsTarget / rms : 1.f;
    if (pk * g > 0.89f) g = 0.89f / pk;
    const int f = std::min(s.n() / 4, 240);
    for (int i = 0; i < s.n(); ++i) {
        float k = g;
        if (i < f) k *= (float)i / f; else if (s.n() - 1 - i < f) k *= (float)(s.n() - 1 - i) / f;
        s.l[(size_t)i] *= k; s.r[(size_t)i] *= k;
    }
}

// a warm string pad, two chords (Cm9, Abmaj9), detuned saws in three places, moving filters, ensemble, hall
inline Stereo pad()
{
    Stereo s(6.0);
    struct Ch { float t0, t1; float n[5]; };
    static const Ch ch[2] = {{0.f, 3.5f, {48, 55, 58, 62, 63}}, {2.6f, 6.f, {44, 51, 55, 58, 60}}};
    Rnd r(11);
    std::vector<float> L((size_t)s.n(), 0.f), R((size_t)s.n(), 0.f);
    for (const Ch& c : ch)
        for (float note : c.n)
            for (int v = 0; v < 3; ++v) {
                Saw o; o.ph = r.u();
                const float det = (v - 1) * 9.f + r.b() * 2.f, pan = (v - 1) * 0.8f + r.b() * 0.15f;
                const float f = mtof(note) * std::pow(2.f, det / 1200.f), dr = 0.15f + r.u() * 0.2f, dp = r.u() * kTwoPi;
                const int a = (int)(c.t0 * kSr), b = std::min(s.n(), (int)(c.t1 * kSr));
                const float pa = (pan + 1.f) * 0.25f * kPi, gl = std::cos(pa), gr = std::sin(pa);
                float fm = f, env = 0.f;
                for (int i = a; i < b; ++i) {
                    if (((i - a) & 31) == 0) {
                        const float t = (i - a) / kFs, rem = (b - i) / kFs;
                        env = std::min(1.f, t / 0.9f) * std::min(1.f, rem / 0.9f) * 0.11f;
                        fm = f * (1.f + 0.0015f * fsin(dr * t + dp / kTwoPi));
                    }
                    const float x = o.tick(fm) * env;
                    L[(size_t)i] += x * gl; R[(size_t)i] += x * gr;
                }
            }
    Svf fl, fr; Ensemble en; Rnd nz(5); OnePole air;
    air.setLP(5000.f, kFs);
    for (int i = 0; i < s.n(); ++i) {
        const float t = i / kFs;
        if ((i & 31) == 0) {
            fl.setup(2600.f + 4500.f * (0.5f - 0.5f * std::cos(kTwoPi * t / 5.5f)), 0.85f, kFs);
            fr.setup(2600.f + 4500.f * (0.5f - 0.5f * std::cos(kTwoPi * t / 5.5f + 1.3f)), 0.85f, kFs);
        }
        fl.tick(L[(size_t)i]); fr.tick(R[(size_t)i]);
        float el, er; en.process((fl.lp + fr.lp) * 0.5f, el, er);
        const float n = nz.b(); const float hiss = (n - air.lp(n)) * 0.02f;   // a little air on top
        s.l[(size_t)i] = fl.lp * 0.7f + el * 0.5f + hiss; s.r[(size_t)i] = fr.lp * 0.7f + er * 0.5f + hiss * 0.8f;
    }
    space(s, 0.45f, 105.f, 100.f);
    finish(s); return s;
}

// a music-box / celesta melody, 8ths at 120 BPM, FM bells panned left and right, echo and room
inline Stereo bells()
{
    Stereo s(4.0);
    static const float mel[16] = {72, 75, 79, 82, 80, 79, 75, 74, 72, 79, 75, 84, 82, 79, 80, 75};
    Rnd r(7);
    for (int k = 0; k < 16; ++k) {
        const int s0 = (int)(k * 0.25 * kSr);
        const float fc = mtof(mel[k]), pan = (k & 1 ? 0.55f : -0.55f) + r.b() * 0.2f, vel = 0.75f + 0.25f * r.u();
        float pc = 0.f, pm = 0.f, p2 = 0.f, e6 = 1.f, e19 = 1.f, e7 = 1.f;
        const float k6 = std::exp(-6.f / kFs), k19 = std::exp(-1.9f / kFs), k7 = std::exp(-7.f / kFs);
        for (int i = s0; i < s.n(); ++i) {
            const float t = (i - s0) / kFs;
            if (t > 2.4f) break;
            pc += fc / kFs; pm += fc * 3.5f / kFs; p2 += fc * 2.f / kFs;
            const float idx = 2.2f * e6 + 0.25f;
            const float x = fsin(pc + idx * fsin(pm) / kTwoPi) * e19 + 0.35f * fsin(p2) * e7;
            e6 *= k6; e19 *= k19; e7 *= k7;
            panAdd(s, i, x * 0.3f * vel * std::min(1.f, t / 0.002f), pan);
        }
    }
    echo(s, 0.75, 0.35f, 0.25f);
    space(s, 0.4f, 95.f, 95.f);
    finish(s); return s;
}

// a choir on "ah" opening towards "oh": Cm then Bb / D, vibrato, breath, formants per side, hall
inline Stereo choir()
{
    Stereo s(6.0);
    struct Ch { float t0, t1; float n[5]; };
    static const Ch ch[2] = {{0.f, 3.4f, {48, 55, 60, 63, 67}}, {2.8f, 6.f, {50, 58, 62, 65, 70}}};
    std::vector<float> L((size_t)s.n(), 0.f), R((size_t)s.n(), 0.f);
    Rnd r(23);
    for (const Ch& c : ch)
        for (float note : c.n)
            for (int v = 0; v < 2; ++v) {
                Saw o; o.ph = r.u();
                const float f = mtof(note) * std::pow(2.f, ((v ? 6.f : -6.f) + r.b() * 3.f) / 1200.f);
                const float vr = 5.f + r.u() * 0.9f, vp = r.u() * kTwoPi, pan = r.b() * 0.85f;
                const float pa = (pan + 1.f) * 0.25f * kPi, gl = std::cos(pa), gr = std::sin(pa);
                const int a = (int)(c.t0 * kSr), b = std::min(s.n(), (int)(c.t1 * kSr));
                float fv = f, env = 0.f;
                for (int i = a; i < b; ++i) {
                    if (((i - a) & 31) == 0) {
                        const float t = (i - a) / kFs, rem = (b - i) / kFs;
                        env = std::min(1.f, t / 0.6f) * std::min(1.f, rem / 0.7f) * 0.1f;
                        fv = f * (1.f + 0.009f * std::min(1.f, t / 0.8f) * fsin(vr * t + vp / kTwoPi));
                    }
                    const float x = o.tick(fv) * env;
                    L[(size_t)i] += x * gl; R[(size_t)i] += x * gr;
                }
            }
    // formants: "a" (800 1150 2900 3900) to "o" (450 800 2830 3800)
    static const float A[4] = {800, 1150, 2900, 3900}, O[4] = {450, 800, 2830, 3800}, G[4] = {1.f, 0.7f, 0.45f, 0.35f};
    Svf fl[4], fr[4]; Rnd nz(9); Svf breath; breath.setup(3200.f, 0.8f, kFs);
    for (int i = 0; i < s.n(); ++i) {
        const float t = i / kFs;
        if ((i & 63) == 0) {
            const float m = 0.5f - 0.5f * std::cos(kTwoPi * t / 6.f);
            for (int k = 0; k < 4; ++k) { const float f = A[k] + (O[k] - A[k]) * m; fl[k].setup(f * 1.01f, 7.f + k * 2.f, kFs); fr[k].setup(f * 0.99f, 7.f + k * 2.f, kFs); }
        }
        float yl = 0.f, yr = 0.f;
        for (int k = 0; k < 4; ++k) { fl[k].tick(L[(size_t)i]); fr[k].tick(R[(size_t)i]); yl += fl[k].bp * G[k]; yr += fr[k].bp * G[k]; }
        breath.tick(nz.b());
        const float br = breath.bp * 0.03f * (0.6f + 0.4f * std::sin(t * 3.1f));
        s.l[(size_t)i] = yl * 2.2f + br; s.r[(size_t)i] = yr * 2.2f + br * 0.7f;
    }
    space(s, 0.55f, 112.f, 95.f);
    finish(s); return s;
}

// rain: droplets everywhere in the stereo field, a soft bed and a few low drops (no pitch: grain rain on anything)
inline Stereo rain()
{
    Stereo s(6.0);
    Rnd r(77);
    OnePole bl, br, hl, hr; bl.setLP(700.f, kFs); br.setLP(650.f, kFs); hl.setLP(6000.f, kFs); hr.setLP(6500.f, kFs);
    for (int i = 0; i < s.n(); ++i) {
        const float a = r.b(), b = r.b();
        const float sw = 0.8f + 0.2f * std::sin(i / kFs * 0.7f);
        s.l[(size_t)i] = bl.lp(a) * 0.35f * sw + (a - hl.lp(a)) * 0.012f;
        s.r[(size_t)i] = br.lp(b) * 0.35f * sw + (b - hr.lp(b)) * 0.012f;
    }
    double t = 0.0;
    while (true) {
        t += -std::log(std::max(1e-4f, r.u())) / 38.0;          // ~38 drops a second
        const int s0 = (int)(t * kSr);
        if (s0 >= s.n()) break;
        const bool low = r.u() < 0.12f;
        const float f0 = low ? 300.f + 400.f * r.u() : 1400.f + 4200.f * r.u(), len = low ? 0.09f : 0.012f + 0.03f * r.u();
        const float amp = (low ? 0.5f : 0.22f) * (0.3f + 0.7f * r.u()), pan = r.b();
        float ph = 0.f, e = amp; const float dk = std::exp(-2.5f / (len * kFs));
        const float pa = (pan + 1.f) * 0.25f * kPi, gl = std::cos(pa), gr = std::sin(pa);
        for (int i = s0; i < s.n() && i < s0 + (int)(len * 4.f * kSr); ++i) {
            const float u = (i - s0) / kFs;
            ph += f0 * (1.f - 0.45f * std::min(1.f, u / len)) / kFs;
            const float x = fsin(ph) * e * std::min(1.f, u / 0.0008f); e *= dk;
            s.l[(size_t)i] += x * gl; s.r[(size_t)i] += x * gr;
        }
    }
    space(s, 0.3f, 70.f, 100.f);
    finish(s, 0.15f); return s;
}

// a harp arpeggio, 16ths at 120 BPM over Cm then Ab, tuned partials that decay (higher ones faster), panned across
inline Stereo plucks()
{
    Stereo s(4.0);
    static const int arp[32] = {60, 63, 67, 70, 72, 70, 67, 63, 60, 63, 67, 72, 75, 72, 67, 63,
                                56, 60, 63, 67, 68, 67, 63, 60, 56, 60, 63, 68, 72, 68, 63, 60};
    Rnd r(5);
    for (int k = 0; k < 32; ++k) {
        const int s0 = (int)(k * 0.125 * kSr);
        const float f = mtof((float)arp[k]), pan = std::sin(k * 0.55f) * 0.75f, vel = (k % 4 == 0 ? 1.f : 0.7f) * (0.85f + 0.15f * r.u());
        Partial p[12]; int np = 0;
        for (int h = 1; h <= 12; ++h) {
            if (f * h > 17000.f) break;
            const float a = vel * (0.25f + std::fabs(std::sin(h * kPi * 0.13f))) / std::pow((float)h, 0.8f);
            p[np++].set(f * h * (1.f + 0.0004f * h * h), a, 1.4f + 0.6f * h, r.u() * 0.3f);
        }
        const int end = std::min(s.n(), s0 + (int)(1.7 * kSr));
        const float pa = (pan + 1.f) * 0.25f * kPi, gl = std::cos(pa), gr = std::sin(pa);
        for (int i = s0; i < end; ++i) {
            float x = 0.f; for (int q = 0; q < np; ++q) x += p[q].tick();
            x *= 0.16f * std::min(1.f, (i - s0) / 8.f);
            s.l[(size_t)i] += x * gl; s.r[(size_t)i] += x * gr;
        }
    }
    echo(s, 0.75, 0.3f, 0.2f);
    space(s, 0.35f, 90.f, 90.f);
    finish(s); return s;
}

// a 2-bar groove at 120 BPM: kick, snare with ghosts, closed and open hats, shaker on the right, rim on the left, a room
inline Stereo beat()
{
    Stereo s(4.0);
    const double s16 = kSr * 0.125;
    static const char* kick  = "x......x..x.....x.........x..x..";
    static const char* snare = "....x.......x.......x.......x..g";
    static const char* hat   = "x.x.x.xxx.x.x.x.x.x.x.x.x.x.x.o.";
    static const char* shk   = ".xxx.xxx.xxx.xxx.xxx.xxx.xxx.xxx";
    static const char* rim   = "...x.....x.....x......x....x....";
    Rnd r(3);
    std::vector<float> M((size_t)s.n(), 0.f);
    for (int k = 0; k < 32; ++k) {
        const int s0 = (int)(k * s16);
        if (kick[k] == 'x') {
            float ph = 0.f;
            for (int i = 0; i < (int)(0.45 * kSr) && s0 + i < s.n(); ++i) {
                const float t = i / kFs;
                ph += (47.f + 115.f * std::exp(-t * 28.f)) / kFs;
                M[(size_t)(s0 + i)] += 0.95f * std::sin(kTwoPi * ph) * std::exp(-t * 6.f) + (i < 96 ? r.b() * 0.3f * (1.f - i / 96.f) : 0.f);
            }
        }
        if (snare[k] != '.') {
            const float g = snare[k] == 'g' ? 0.35f : 1.f;
            Svf bp; bp.setup(2400.f, 0.6f, kFs); OnePole hp; hp.setLP(5000.f, kFs);
            for (int i = 0; i < (int)(0.3 * kSr) && s0 + i < s.n(); ++i) {
                const float t = i / kFs, n = r.b();
                bp.tick(n);
                const float body = 0.4f * std::sin(kTwoPi * 186.f * t) * std::exp(-t * 28.f) + 0.2f * std::sin(kTwoPi * 330.f * t) * std::exp(-t * 35.f);
                const float x = g * (body + 0.45f * bp.bp * std::exp(-t * 15.f) + 0.25f * (n - hp.lp(n)) * std::exp(-t * 11.f));
                s.l[(size_t)(s0 + i)] += x * 0.75f; s.r[(size_t)(s0 + i)] += x * 0.75f;
            }
        }
        if (hat[k] != '.') {
            const bool open = hat[k] == 'o';
            const float dec = open ? 5.f : (k % 2 ? 55.f : 34.f), g = open ? 0.42f : (k % 4 == 0 ? 0.38f : 0.27f);
            OnePole a, b; a.setLP(7500.f, kFs); b.setLP(7800.f, kFs);
            for (int i = 0; i < (int)((open ? 0.5 : 0.12) * kSr) && s0 + i < s.n(); ++i) {
                const float e = std::exp(-(i / kFs) * dec) * g, x = r.b(), y = r.b();
                s.l[(size_t)(s0 + i)] += (x - a.lp(x)) * e * 0.9f; s.r[(size_t)(s0 + i)] += (y - b.lp(y)) * e;
            }
        }
        if (shk[k] == 'x') {
            Svf bp; bp.setup(6500.f, 1.2f, kFs);
            for (int i = 0; i < (int)(0.07 * kSr) && s0 + i < s.n(); ++i) {
                const float t = i / kFs; bp.tick(r.b());
                panAdd(s, s0 + i, bp.bp * 0.2f * std::min(1.f, t / 0.01f) * std::exp(-t * 45.f), 0.7f);
            }
        }
        if (rim[k] == 'x')
            for (int i = 0; i < (int)(0.05 * kSr) && s0 + i < s.n(); ++i) {
                const float t = i / kFs;
                panAdd(s, s0 + i, (0.5f * std::sin(kTwoPi * 820.f * t) + 0.3f * std::sin(kTwoPi * 1720.f * t)) * std::exp(-t * 90.f) * 0.5f, -0.6f);
            }
    }
    for (int i = 0; i < s.n(); ++i) { s.l[(size_t)i] += M[(size_t)i] * 0.7f; s.r[(size_t)i] += M[(size_t)i] * 0.7f; }
    space(s, 0.12f, 30.f, 90.f);
    for (int i = 0; i < s.n(); ++i) { s.l[(size_t)i] = std::tanh(s.l[(size_t)i] * 1.3f) / 1.3f; s.r[(size_t)i] = std::tanh(s.r[(size_t)i] * 1.3f) / 1.3f; }
    finish(s, 0.2f); return s;
}

// a slowly breathing drone on C and G: detuned saws through resonant filters that move apart left and right,
// a shimmer of high partials above
inline Stereo drone()
{
    Stereo s(6.0);
    static const float notes[4] = {36, 43, 48, 55};
    std::vector<float> L((size_t)s.n(), 0.f), R((size_t)s.n(), 0.f);
    Rnd r(31);
    for (float note : notes)
        for (int v = 0; v < 2; ++v) {
            Saw o; o.ph = r.u();
            const float f = mtof(note) * std::pow(2.f, (v ? 5.f : -5.f) / 1200.f), pan = v ? 0.6f : -0.6f;
            const float pa = (pan + 1.f) * 0.25f * kPi, gl = std::cos(pa), gr = std::sin(pa);
            for (int i = 0; i < s.n(); ++i) { const float x = o.tick(f) * 0.12f; L[(size_t)i] += x * gl; R[(size_t)i] += x * gr; }
        }
    Svf fl, fr;
    Partial sh[3]; static const float hi[3] = {72, 79, 86};
    float shp[3] = {0, 0, 0};
    for (int i = 0; i < s.n(); ++i) {
        const float t = i / kFs;
        if ((i & 31) == 0) {
            fl.setup(700.f * std::pow(11.f, 0.5f - 0.5f * std::cos(kTwoPi * t / 6.f)), 2.4f, kFs);
            fr.setup(700.f * std::pow(11.f, 0.5f - 0.5f * std::cos(kTwoPi * t / 6.f + 2.f)), 2.4f, kFs);
        }
        fl.tick(L[(size_t)i]); fr.tick(R[(size_t)i]);
        float shl = 0.f, shr = 0.f;
        for (int k = 0; k < 3; ++k) {
            shp[k] += mtof(hi[k]) / kFs;
            const float a = 0.04f * (0.5f + 0.5f * fsin(t * (0.21f + 0.07f * k) + k / kTwoPi));
            const float x = fsin(shp[k]) * a;
            shl += x * (k == 1 ? 0.4f : 1.f); shr += x * (k == 1 ? 1.f : 0.5f);
        }
        s.l[(size_t)i] = fl.lp + shl; s.r[(size_t)i] = fr.lp + shr;
    }
    (void)sh;
    space(s, 0.4f, 115.f, 90.f);
    finish(s); return s;
}

// electric piano chords, one every 2 beats (Cm9 Fm9 Abmaj7 Gm7), FM tines, auto-pan, a little room
inline Stereo keys()
{
    Stereo s(4.0);
    static const float ch[4][5] = {{48, 55, 58, 62, 63}, {53, 56, 60, 63, 67}, {44, 56, 60, 63, 67}, {43, 55, 58, 62, 65}};
    Rnd r(13);
    for (int c = 0; c < 4; ++c) {
        const int s0 = (int)(c * kSr), s1 = std::min(s.n(), (int)((c + 1) * kSr + 0.25 * kSr));
        for (int k = 0; k < 5; ++k) {
            const float f = mtof(ch[c][k]), vel = 0.8f + 0.2f * r.u();
            const int o = s0 + (int)(k * 0.006f * kSr);                 // a slight strum
            float pc = r.u(), pt = 0.f, pb = 0.f;
            for (int i = o; i < s1; ++i) {
                const float t = (i - o) / kFs, rel = std::min(1.f, (s1 - i) / (0.2f * kFs));
                pc += f / kFs; pt += f * 14.f / kFs; pb += f * 2.f / kFs;
                const float idx = 2.8f * std::exp(-t * 2.2f) + 0.35f;
                const float bark = 0.35f * fsin(pb + 1.5f * fsin(pc) / kTwoPi) * std::exp(-t * 6.f);      // the bright attack
                const float x = fsin(pc + idx * fsin(pc) / kTwoPi) * std::exp(-t * 0.9f) + 0.3f * fsin(pt) * std::exp(-t * 22.f) + bark;
                const float trem = 0.5f + 0.5f * fsin(3.2f * (i / kFs));
                const float g = x * 0.12f * vel * rel * std::min(1.f, t / 0.001f);
                s.l[(size_t)i] += g * (1.f - 0.45f * trem); s.r[(size_t)i] += g * (0.55f + 0.45f * trem);
            }
        }
    }
    {   // an ensemble around the dry chords: width
        Ensemble en;
        for (int i = 0; i < s.n(); ++i) {
            float el, er; en.process((s.l[(size_t)i] + s.r[(size_t)i]) * 0.5f, el, er, 0.7f);
            s.l[(size_t)i] = s.l[(size_t)i] * 0.75f + el * 0.45f; s.r[(size_t)i] = s.r[(size_t)i] * 0.75f + er * 0.45f;
        }
    }
    space(s, 0.3f, 80.f, 95.f);
    finish(s); return s;
}

inline Stereo build(int id)
{
    switch (id) {
    case 1: return pad(); case 2: return bells(); case 3: return choir(); case 4: return rain();
    case 5: return plucks(); case 6: return beat(); case 7: return drone(); case 8: return keys();
    default: return Stereo(1.0);
    }
}

} // namespace mat

inline void Dec2Taps() { std::vector<float> a(64, 0.f), b; smp::halfDown(a, b); }   // makes halfDown's table

// all materials, built on the first call (thread-safe); index 0 is empty
inline const std::array<SamplePtr, kMaterials + 1>& materials()
{
    static const std::array<SamplePtr, kMaterials + 1> all = [] {
        std::array<SamplePtr, kMaterials + 1> a;
        auto one = [&a](int i) {
            mat::Stereo st = mat::build(i);
            auto d = std::make_shared<SampleData>();
            d->sr = mat::kSr; d->name = kMaterialNames[i];
            d->pcm.resize(st.l.size());
            for (size_t k = 0; k < st.l.size(); ++k) d->pcm[k] = 0.5f * (st.l[k] + st.r[k]);
            d->l = std::move(st.l); d->r = std::move(st.r);
            smp::buildMips(*d);
            a[i] = d;
        };
        mat::fsin(0.f); Dec2Taps();                       // the shared tables, before the threads
        std::vector<std::thread> th;                      // built side by side (all joined here: nothing left running)
        try { for (int i = 2; i <= kMaterials; ++i) th.emplace_back(one, i); } catch (...) {}
        one(1);
        for (auto& x : th) x.join();
        for (int i = 2; i <= kMaterials; ++i) if (!a[i]) one(i);   // if a thread could not start
        return a;
    }();
    return all;
}
inline SamplePtr material(int id) { return id >= 1 && id <= kMaterials ? materials()[id] : SamplePtr(); }

} // namespace kn
