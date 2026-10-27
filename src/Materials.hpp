// Kinetik KGRN-4 — built-in materials: eight short recordings synthesized in code (nothing is sampled), so the
// tracks and the factory presets make sound before you load your own samples. 48 kHz mono, built once.
#pragma once
#include "Sample.hpp"
#include "Dsp.hpp"
#include <array>

namespace kn {

static constexpr int kMaterials = 8;   // ids 1..8 (0 = the track's own sample)
static const char* const kMaterialNames[kMaterials + 1] = {"SAMPLE", "PAD", "BELLS", "VOICE", "WIND", "PLUCKS", "BEAT", "DRONE", "GLASS"};

namespace mat {

static constexpr double kSr = 48000.0;
struct Rnd { uint32_t s; explicit Rnd(uint32_t seed) : s(seed) {} float u() { s ^= s << 13; s ^= s >> 17; s ^= s << 5; return (s & 0xFFFFFF) / 16777216.f; } float b() { return u() * 2.f - 1.f; } };
inline float mtof(float n) { return 440.f * std::pow(2.f, (n - 69.f) / 12.f); }
inline void normalize(std::vector<float>& x, float peak = 0.9f)
{
    float m = 0.f; for (float v : x) m = std::max(m, std::fabs(v));
    if (m > 1e-6f) for (float& v : x) v *= peak / m;
    const size_t f = std::min<size_t>(x.size(), 240);       // 5 ms fades at both ends
    for (size_t i = 0; i < f; ++i) { const float g = (float)i / f; x[i] *= g; x[x.size() - 1 - i] *= g; }
}

inline std::vector<float> pad()
{
    const int n = (int)(kSr * 4.0);
    std::vector<float> x(n, 0.f);
    static const float notes[5] = {48, 51, 55, 58, 62};
    float ph[15] = {};
    Rnd r(11); for (float& p : ph) p = r.u();
    Svf lp; lp.setup(1800.f, 0.8f, (float)kSr);
    for (int i = 0; i < n; ++i) {
        float s = 0.f;
        for (int v = 0; v < 15; ++v) {
            const float f = mtof(notes[v / 3]) * (1.f + (v % 3 - 1) * 0.004f);
            ph[v] += f / (float)kSr; if (ph[v] >= 1.f) ph[v] -= 1.f;
            s += ph[v] * 2.f - 1.f;
        }
        const float t = i / (float)kSr;
        if ((i & 63) == 0) lp.setup(900.f + 1400.f * (0.5f - 0.5f * std::cos(t * 1.4f)), 0.9f, (float)kSr);
        lp.tick(s);
        x[i] = lp.lp * std::min(1.f, t / 0.6f);
    }
    normalize(x); return x;
}

inline std::vector<float> bells()
{
    const int n = (int)(kSr * 4.0);
    std::vector<float> x(n, 0.f);
    static const float seq[8] = {72, 79, 75, 84, 77, 72, 82, 87};
    for (int k = 0; k < 8; ++k) {
        const int s0 = (int)(k * 0.5 * kSr);
        const float fc = mtof(seq[k]), fm = fc * 3.5f;
        for (int i = s0; i < n; ++i) {
            const float t = (i - s0) / (float)kSr;
            const float env = std::exp(-t * 2.2f), ienv = std::exp(-t * 5.f);
            x[i] += 0.5f * env * std::sin(kTwoPi * fc * t + 2.4f * ienv * std::sin(kTwoPi * fm * t));
        }
    }
    normalize(x); return x;
}

inline std::vector<float> voice()
{
    const int n = (int)(kSr * 4.0);
    std::vector<float> x(n, 0.f);
    // vowels a o i e u: formant frequencies
    static const float F[5][3] = {{730, 1090, 2440}, {570, 840, 2410}, {270, 2290, 3010}, {530, 1840, 2480}, {300, 870, 2240}};
    Svf f[3];
    float ph = 0.f;
    for (int i = 0; i < n; ++i) {
        const float t = i / (float)kSr;
        const float pos = t / 4.f * 4.f; const int a = std::min(3, (int)pos); const float u = pos - a, w = u * u * (3.f - 2.f * u);
        if ((i & 31) == 0)
            for (int k = 0; k < 3; ++k) f[k].setup(F[a][k] + (F[a + 1][k] - F[a][k]) * w, 9.f + k * 3.f, (float)kSr);
        const float f0 = 155.f * (1.f + 0.012f * std::sin(kTwoPi * 5.2f * t) * std::min(1.f, t)) * (t > 2.f ? 1.122f : 1.f);
        ph += f0 / (float)kSr; if (ph >= 1.f) ph -= 1.f;
        const float src = ph < 0.4f ? std::sin(kPi * ph / 0.4f) : 0.f;      // glottal-like pulse
        float s = 0.f;
        for (int k = 0; k < 3; ++k) { f[k].tick(src); s += f[k].bp * (k == 0 ? 1.f : (k == 1 ? 0.6f : 0.3f)); }
        x[i] = s * std::min(1.f, t / 0.15f);
    }
    normalize(x); return x;
}

inline std::vector<float> wind()
{
    const int n = (int)(kSr * 4.0);
    std::vector<float> x(n, 0.f);
    Rnd r(77); Svf a, b;
    for (int i = 0; i < n; ++i) {
        const float t = i / (float)kSr;
        if ((i & 31) == 0) {
            a.setup(400.f * std::pow(6.f, 0.5f + 0.5f * std::sin(t * 1.3f)), 7.f, (float)kSr);
            b.setup(1300.f * std::pow(3.f, 0.5f + 0.5f * std::sin(t * 2.1f + 1.f)), 5.f, (float)kSr);
        }
        const float w = r.b();
        a.tick(w); b.tick(w);
        x[i] = (a.bp + 0.6f * b.bp) * (0.6f + 0.4f * std::sin(t * 2.7f));
    }
    normalize(x); return x;
}

inline std::vector<float> plucks()
{
    const int n = (int)(kSr * 4.0);
    std::vector<float> x(n, 0.f);
    static const int arp[8] = {57, 60, 64, 67, 69, 67, 64, 60};
    Rnd r(5);
    for (int k = 0; k < 32; ++k) {
        const int s0 = (int)(k * 0.125 * kSr);
        const int note = arp[k % 8] + (k >= 16 ? -2 : 0);
        const int period = std::max(2, (int)std::lround(kSr / mtof((float)note)));
        std::vector<float> d(period);
        for (float& v : d) v = r.b();
        float last = 0.f;
        for (int i = s0, j = 0; i < n && i < s0 + (int)(kSr * 1.2); ++i, ++j) {
            const int idx = j % period;
            const float y = 0.5f * (d[idx] + d[(idx + 1) % period]) * 0.996f;
            last = d[idx]; d[idx] = y;
            x[i] += last * 0.35f;
        }
    }
    normalize(x); return x;
}

inline std::vector<float> beat()
{
    const int n = (int)(kSr * 4.0);            // 2 bars at 120 BPM
    std::vector<float> x(n, 0.f);
    const double s16 = kSr * 0.125;
    static const char* kick  = "x.........x.....x.....x...x.....";
    static const char* snare = "....x.......x.......x.......x..x";
    static const char* hat   = "x.x.x.xxx.x.x.x.x.x.x.xxx.x.x.xx";
    Rnd r(3);
    for (int k = 0; k < 32; ++k) {
        const int s0 = (int)(k * s16);
        if (kick[k] == 'x') {
            float ph = 0.f;
            for (int i = 0; i < (int)(0.4 * kSr) && s0 + i < n; ++i) {
                const float t = i / (float)kSr;
                ph += (48.f + 140.f * std::exp(-t * 30.f)) / (float)kSr;
                x[s0 + i] += 0.9f * std::sin(kTwoPi * ph) * std::exp(-t * 7.f);
            }
        }
        if (snare[k] == 'x') {
            Svf bp; bp.setup(1800.f, 0.7f, (float)kSr);
            for (int i = 0; i < (int)(0.25 * kSr) && s0 + i < n; ++i) {
                const float t = i / (float)kSr;
                bp.tick(r.b());
                x[s0 + i] += 0.5f * bp.bp * std::exp(-t * 16.f) + 0.35f * std::sin(kTwoPi * 190.f * t) * std::exp(-t * 25.f);
            }
        }
        if (hat[k] == 'x') {
            Svf hp; hp.setup(7000.f, 0.8f, (float)kSr);
            const float dec = (k % 4 == 2) ? 20.f : 55.f;
            for (int i = 0; i < (int)(0.15 * kSr) && s0 + i < n; ++i) {
                hp.tick(r.b());
                x[s0 + i] += 0.16f * hp.hp * std::exp(-(i / (float)kSr) * dec);
            }
        }
    }
    for (float& v : x) v = std::tanh(v * 1.2f);
    normalize(x); return x;
}

inline std::vector<float> drone()
{
    const int n = (int)(kSr * 4.0);
    std::vector<float> x(n, 0.f);
    static const float notes[4] = {36, 43, 48, 55};
    float ph[8] = {};
    for (int i = 0; i < n; ++i) {
        const float t = i / (float)kSr;
        float s = 0.f;
        for (int v = 0; v < 8; ++v) {
            const float f = mtof(notes[v / 2]) * (v % 2 ? 1.003f : 0.998f);
            ph[v] += f / (float)kSr; if (ph[v] >= 1.f) ph[v] -= 1.f;
            const float p = ph[v] * kTwoPi;
            s += std::sin(p) + 0.5f * std::sin(2.f * p) + 0.25f * std::sin(3.f * p) * (0.5f + 0.5f * std::sin(t * 0.9f + v));
        }
        x[i] = std::tanh(s * 0.25f);
    }
    normalize(x); return x;
}

inline std::vector<float> glass()
{
    const int n = (int)(kSr * 4.0);
    std::vector<float> x(n, 0.f);
    static const float roots[4] = {64, 60, 67, 62};
    static const float ratio[6] = {1.f, 2.76f, 5.40f, 8.93f, 1.5f, 2.f};
    for (int c = 0; c < 4; ++c) {
        const int s0 = (int)(c * kSr);
        for (int p = 0; p < 6; ++p) {
            const float f = mtof(roots[c]) * ratio[p], a = 0.6f / (1.f + p);
            if (f > 18000.f) continue;
            for (int i = s0; i < n; ++i) {
                const float t = (i - s0) / (float)kSr;
                x[i] += a * std::sin(kTwoPi * f * t) * std::exp(-t * (0.9f + p * 0.5f)) * std::min(1.f, t / 0.01f);
            }
        }
    }
    normalize(x); return x;
}

inline std::vector<float> build(int id)
{
    switch (id) {
    case 1: return pad(); case 2: return bells(); case 3: return voice(); case 4: return wind();
    case 5: return plucks(); case 6: return beat(); case 7: return drone(); case 8: return glass();
    default: return {};
    }
}

} // namespace mat

// all materials, built on the first call (thread-safe); index 0 is empty
inline const std::array<SamplePtr, kMaterials + 1>& materials()
{
    static const std::array<SamplePtr, kMaterials + 1> all = [] {
        std::array<SamplePtr, kMaterials + 1> a;
        for (int i = 1; i <= kMaterials; ++i) {
            auto d = std::make_shared<SampleData>();
            d->pcm = mat::build(i); d->sr = mat::kSr; d->name = kMaterialNames[i];
            a[i] = d;
        }
        return a;
    }();
    return all;
}
inline SamplePtr material(int id) { return id >= 1 && id <= kMaterials ? materials()[id] : SamplePtr(); }

} // namespace kn
