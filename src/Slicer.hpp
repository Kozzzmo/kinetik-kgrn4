// Kinetik KGRN-4 — sample slicer: the transient detection of the Kinetik break slicer (KJNG-2 / J-Slice), without
// JUCE: multi-band "super-flux" onset detection, sample-accurate refinement, pre-roll + zero-crossing snap; or equal
// slices. Runs on the UI side (and in the preset builder); the result goes to the bank as slice starts.
#pragma once
#include <vector>
#include <cmath>
#include <cstdint>
#include <algorithm>
#include <complex>

namespace kn {
namespace slicer {

inline void fft(std::vector<std::complex<float>>& a)
{
    const size_t n = a.size();
    for (size_t i = 1, j = 0; i < n; ++i) {
        size_t bit = n >> 1;
        for (; j & bit; bit >>= 1) j ^= bit;
        j ^= bit;
        if (i < j) std::swap(a[i], a[j]);
    }
    for (size_t len = 2; len <= n; len <<= 1) {
        const float ang = -6.28318530718f / (float)len;
        const std::complex<float> wl(std::cos(ang), std::sin(ang));
        for (size_t i = 0; i < n; i += len) {
            std::complex<float> w(1.f, 0.f);
            for (size_t j = 0; j < len / 2; ++j) {
                const std::complex<float> u = a[i + j], v = a[i + j + len / 2] * w;
                a[i + j] = u + v; a[i + j + len / 2] = u - v;
                w *= wl;
            }
        }
    }
}

inline float percentile(const std::vector<float>& in, float p)
{
    if (in.empty()) return 0.f;
    std::vector<float> v(in.size());
    std::copy(in.begin(), in.end(), v.begin());
    const size_t k = (size_t)std::min<long>((long)v.size() - 1, std::max<long>(0, std::lround(p * (float)(v.size() - 1))));
    std::nth_element(v.begin(), v.begin() + (long)k, v.end());
    return v[k];
}

struct Onset { int64_t pos; float strength; };

// onset detection function, one value per hop (frames centred on f * hop), normalised to 0..1
inline std::vector<float> odf(const std::vector<float>& x, double sr, int& hopOut, int& nOut)
{
    const int N = sr > 64000.0 ? 2048 : 1024, hop = N / 4;
    hopOut = hop; nOut = N;
    const int frames = (int)(x.size() / (size_t)hop) + 1, bins = N / 2 + 1;
    std::vector<float> win(N), prev(bins, 0.f), cur(bins, 0.f);
    for (int i = 0; i < N; ++i) win[i] = 0.5f - 0.5f * std::cos(6.28318530718f * i / (N - 1));
    const double binHz = sr / N;
    const double edgesHz[5] = {30.0, 150.0, 500.0, 4000.0, std::min(16000.0, sr * 0.5 - binHz)};
    int edges[5];
    for (int b = 0; b < 5; ++b) edges[b] = std::max(1, std::min(bins - 2, (int)std::lround(edgesHz[b] / binHz)));
    std::vector<float> flux[4];
    for (auto& f : flux) f.assign(frames, 0.f);
    std::vector<std::complex<float>> buf(N);
    const float scale = 1000.f / N;
    const int64_t len = (int64_t)x.size();
    for (int f = 0; f < frames; ++f) {
        const int64_t start = (int64_t)f * hop - N / 2;
        for (int i = 0; i < N; ++i) { const int64_t idx = start + i; buf[i] = std::complex<float>(idx >= 0 && idx < len ? x[(size_t)idx] * win[i] : 0.f, 0.f); }
        fft(buf);
        for (int k = 0; k < bins; ++k) cur[k] = std::log1p(std::abs(buf[k]) * scale);
        for (int b = 0; b < 4; ++b) {
            float sum = 0.f;
            for (int k = edges[b]; k < edges[b + 1]; ++k) {
                const float ref = std::max(prev[k - 1], std::max(prev[k], prev[k + 1]));
                const float d = cur[k] - ref;
                if (d > 0.f) sum += d;
            }
            flux[b][f] = sum / (float)std::max(1, edges[b + 1] - edges[b]);
        }
        std::swap(prev, cur);
    }
    static const float weights[4] = {1.f, 1.f, 1.f, 0.8f};
    std::vector<float> o(frames, 0.f);
    for (int b = 0; b < 4; ++b) {
        const float p = percentile(flux[b], 0.98f);
        if (p < 1e-9f) continue;
        for (int f = 0; f < frames; ++f) o[f] += weights[b] * std::min(3.f, flux[b][f] / p);
    }
    const float mx = o.empty() ? 0.f : *std::max_element(o.begin(), o.end());
    if (mx > 0.f) for (auto& v : o) v /= mx;
    return o;
}

// start of the energy rise around the estimated position
inline int64_t refine(const std::vector<float>& x, double sr, int64_t est, int n)
{
    const int64_t len = (int64_t)x.size();
    const int64_t a = std::max<int64_t>(0, est - n / 2), b = std::min<int64_t>(len - 1, est + n / 2);
    if (b <= a + 4) return std::max<int64_t>(0, std::min<int64_t>(len - 1, est));
    const int w = std::max(8, (int)(sr * 0.001));
    std::vector<float> e((size_t)(b - a + 1), 0.f);
    auto sq = [&](int64_t i) -> double { return i >= 0 && i < len ? (double)x[(size_t)i] * x[(size_t)i] : 0.0; };
    double run = 0.0;
    for (int64_t i = a - w / 2; i < a + w / 2; ++i) run += sq(i);
    for (int64_t i = a; i <= b; ++i) { e[(size_t)(i - a)] = (float)run; run += sq(i + w / 2) - sq(i - w / 2); }
    size_t ip = 0;
    for (size_t i = 1; i < e.size(); ++i) if (e[i] > e[ip]) ip = i;
    float base = e[ip];
    for (size_t i = 0; i <= ip; ++i) base = std::min(base, e[i]);
    const float thr = base + 0.1f * (e[ip] - base);
    size_t i0 = ip;
    while (i0 > 0 && e[i0] > thr) --i0;
    return a + (int64_t)i0;
}

// RMS of the 15 ms after pos over the RMS of the 40 ms before it
inline float rise(const std::vector<float>& x, double sr, int64_t pos)
{
    const int64_t len = (int64_t)x.size(), a = (int64_t)(0.015 * sr), b = (int64_t)(0.04 * sr);
    double e1 = 0, e0 = 0; int64_t n1 = 0, n0 = 0;
    for (int64_t i = pos; i < std::min(len, pos + a); ++i) { e1 += (double)x[(size_t)i] * x[(size_t)i]; ++n1; }
    for (int64_t i = std::max<int64_t>(0, pos - b); i < pos; ++i) { e0 += (double)x[(size_t)i] * x[(size_t)i]; ++n0; }
    if (n1 == 0) return 0.f;
    if (n0 == 0) return 100.f;
    return (float)std::sqrt((e1 / n1) / (e0 / n0 + 1e-12));
}

inline int64_t zeroCross(const std::vector<float>& x, int64_t p, int maxBack)
{
    for (int i = 0; i < maxBack && p - i - 1 >= 0; ++i) {
        const float s0 = x[(size_t)(p - i - 1)], s1 = x[(size_t)(p - i)];
        if ((s0 <= 0.f && s1 >= 0.f) || (s0 >= 0.f && s1 <= 0.f)) return p - i;
    }
    return p;
}

// slice starts in frames (first one = 0). sensitivity 0..1 (more = more slices)
inline std::vector<uint32_t> transients(const std::vector<float>& x, double sr, float sensitivity, int maxSlices)
{
    std::vector<uint32_t> out;
    if (x.size() < 512) { out.push_back(0); return out; }
    int hop = 256, n = 1024;
    const std::vector<float> o = odf(x, sr, hop, n);
    const int nf = (int)o.size();
    const float sens = std::max(0.f, std::min(1.f, sensitivity));
    const float thr = 0.5f + (0.03f - 0.5f) * sens;
    const int64_t minGap = (int64_t)((0.06f + (0.02f - 0.06f) * sens) * sr);
    const float minRise = 2.0f - 0.8f * sens;              // level after / before: x2 (+6 dB) .. x1.2
    std::vector<Onset> picks;
    for (int t = 0; t < nf; ++t) {
        const float v = o[t];
        if (v < std::max(0.03f, thr)) continue;
        bool isMax = true;
        for (int k = std::max(0, t - 3); k <= std::min(nf - 1, t + 3) && isMax; ++k) if (o[k] > v || (k < t && o[k] >= v)) isMax = false;
        if (!isMax) continue;
        std::vector<float> loc;
        for (int k = std::max(0, t - 10); k <= std::min(nf - 1, t + 5); ++k) loc.push_back(o[k]);
        if (v - percentile(loc, 0.5f) < 0.02f) continue;
        const int64_t pos = refine(x, sr, (int64_t)t * hop, n);
        // a real hit: the level jumps (slow swells and filter sweeps of pads do not)
        if (rise(x, sr, pos) < minRise) continue;
        if (!picks.empty() && pos - picks.back().pos < minGap) { if (v > picks.back().strength) picks.back() = {pos, v}; continue; }
        picks.push_back({pos, v});
    }
    // keep the strongest ones if there are too many
    if ((int)picks.size() > maxSlices) {
        std::vector<Onset> s = picks;
        std::sort(s.begin(), s.end(), [](const Onset& a, const Onset& b) { return a.strength > b.strength; });
        s.resize((size_t)maxSlices);
        std::sort(s.begin(), s.end(), [](const Onset& a, const Onset& b) { return a.pos < b.pos; });
        picks = s;
    }
    const int64_t preroll = (int64_t)(0.0015 * sr), zc = (int)(0.001 * sr);
    const int64_t lead = (int64_t)(0.02 * sr);
    out.push_back(0);
    for (const Onset& p : picks) {
        const int64_t q = zeroCross(x, std::max<int64_t>(0, p.pos - preroll), (int)zc);
        if (q <= lead && out.size() == 1) continue;                   // a hit at the very start = slice 1
        if ((int64_t)q - (int64_t)out.back() < minGap / 2) continue;
        if ((int)out.size() >= maxSlices) break;
        out.push_back((uint32_t)q);
    }
    return out;
}

inline std::vector<uint32_t> equal(size_t frames, int n)
{
    std::vector<uint32_t> out;
    n = std::max(1, n);
    for (int i = 0; i < n; ++i) out.push_back((uint32_t)((double)frames * i / n));
    return out;
}

// as millionths of the length (the bank format)
inline std::vector<uint32_t> toMillionths(const std::vector<uint32_t>& s, size_t frames)
{
    std::vector<uint32_t> o;
    for (uint32_t f : s) { const uint32_t m = frames ? (uint32_t)std::min<double>(999999.0, std::floor((double)f / frames * 1e6)) : 0; if (o.empty() || m > o.back()) o.push_back(m); }
    return o;
}

} // namespace slicer
} // namespace kn
