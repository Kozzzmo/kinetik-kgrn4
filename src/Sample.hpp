// Kinetik KDSP-16 — user samples for the SAMPLE machine (one per track).
// Stored mono, float, at the file's sample rate; serialized as 16-bit PCM in base64 (plugin state, presets).
// Since 0.10 the PCM is packed losslessly first (fixed linear prediction + Rice codes, like FLAC's simplest mode):
// those lines start with 'z' ("z<track> ...") so older versions skip them instead of playing noise.
#pragma once
#include <string>
#include <vector>
#include <memory>
#include <cstdint>
#include <cstring>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <algorithm>

namespace kn {

static constexpr float kMaxSampleSeconds = 20.f;

struct SampleData {
    std::vector<float> pcm;     // mono
    double sr = 48000.0;
    std::string name;           // file name, for the display
    uint32_t frames() const { return (uint32_t)pcm.size(); }
};
using SamplePtr = std::shared_ptr<const SampleData>;

namespace smp {

inline std::string base64(const uint8_t* d, size_t n)
{
    static const char* T = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string o; o.reserve((n + 2) / 3 * 4);
    for (size_t i = 0; i < n; i += 3) {
        const uint32_t v = (uint32_t)d[i] << 16 | (i + 1 < n ? (uint32_t)d[i + 1] << 8 : 0) | (i + 2 < n ? d[i + 2] : 0);
        o += T[v >> 18 & 63]; o += T[v >> 12 & 63];
        o += i + 1 < n ? T[v >> 6 & 63] : '='; o += i + 2 < n ? T[v & 63] : '=';
    }
    return o;
}
inline std::vector<uint8_t> unbase64(const char* s, size_t n)
{
    auto val = [](char c) -> int {
        if (c >= 'A' && c <= 'Z') return c - 'A';
        if (c >= 'a' && c <= 'z') return c - 'a' + 26;
        if (c >= '0' && c <= '9') return c - '0' + 52;
        if (c == '+') return 62;
        if (c == '/') return 63;
        return -1;
    };
    std::vector<uint8_t> o; o.reserve(n / 4 * 3);
    uint32_t acc = 0; int bits = 0;
    for (size_t i = 0; i < n; ++i) {
        const int v = val(s[i]);
        if (v < 0) continue;
        acc = acc << 6 | (uint32_t)v; bits += 6;
        if (bits >= 8) { bits -= 8; o.push_back((uint8_t)(acc >> bits & 0xFF)); }
    }
    return o;
}

inline int16_t toPcm16(float x)
{
    const float c = x < -1.f ? -1.f : (x > 1.f ? 1.f : (x == x ? x : 0.f));
    return (int16_t)std::lround(c * 32767.f);
}

// ---- lossless packing of 16-bit PCM: blocks of kPackBlock samples, each with the fixed predictor (order 0-3)
// that leaves the smallest residuals, coded with the best Rice parameter. Layout (bits, MSB first):
// frames (32) then per block: order (2), k (5; 31 = the block follows as plain 16-bit samples), order warm-up samples (16 each), residuals (zig-zag Rice:
// unary quotient + k bits; a quotient of kEsc or more is written as kEsc ones, a zero, then 20 raw bits).
static constexpr int kPackBlock = 4096, kEsc = 32;
static constexpr uint32_t kVerbatim = 31;   // k value of a block stored without prediction
static constexpr uint32_t kMaxPackFrames = 8000000;

struct BitWriter {
    std::vector<uint8_t> out; uint64_t acc = 0; int n = 0;
    void put(uint32_t v, int bits) { for (int i = bits - 1; i >= 0; --i) bit((v >> i) & 1u); }
    inline void bit(uint32_t b) { acc = acc << 1 | b; if (++n == 8) { out.push_back((uint8_t)acc); acc = 0; n = 0; } }
    void flush() { while (n) bit(0); }
};
struct BitReader {
    const uint8_t* d; size_t size, pos = 0; bool bad = false;
    BitReader(const uint8_t* p, size_t s) : d(p), size(s) {}
    inline uint32_t bit() { if (pos >= size * 8) { bad = true; return 0; } const uint32_t b = d[pos >> 3] >> (7 - (pos & 7)) & 1u; ++pos; return b; }
    uint32_t get(int bits) { uint32_t v = 0; for (int i = 0; i < bits; ++i) v = v << 1 | bit(); return v; }
};
inline int32_t predict(const int32_t* x, size_t i, int order)
{
    switch (order) {
    case 1: return x[i - 1];
    case 2: return 2 * x[i - 1] - x[i - 2];
    case 3: return 3 * x[i - 1] - 3 * x[i - 2] + x[i - 3];
    default: return 0;
    }
}
inline std::vector<uint8_t> pack(const std::vector<int32_t>& x)
{
    BitWriter w;
    w.put((uint32_t)x.size(), 32);
    std::vector<uint32_t> u;
    for (size_t b0 = 0; b0 < x.size(); b0 += kPackBlock) {
        const size_t b1 = std::min(x.size(), b0 + (size_t)kPackBlock), len = b1 - b0;
        int bestOrder = 0; uint64_t bestSum = UINT64_MAX;
        for (int o = 0; o <= 3 && (size_t)o < len; ++o) {
            uint64_t sum = 0;
            for (size_t i = b0 + o; i < b1; ++i) { const int64_t r = (int64_t)x[i] - (i >= (size_t)o ? predict(x.data(), i, o) : 0); sum += (uint64_t)(r < 0 ? -r : r); }
            if (sum < bestSum) { bestSum = sum; bestOrder = o; }
        }
        const int o = bestOrder;
        u.clear();
        for (size_t i = b0 + o; i < b1; ++i) {
            const int32_t r = x[i] - predict(x.data(), i, o);
            u.push_back(r >= 0 ? (uint32_t)r << 1 : ((uint32_t)(-(int64_t)r) << 1) - 1u);
        }
        int bestK = 0; uint64_t bestBits = UINT64_MAX;
        for (int k = 0; k <= 17; ++k) {
            uint64_t bits = 0;
            for (uint32_t v : u) { const uint32_t q = v >> k; bits += q >= (uint32_t)kEsc ? kEsc + 1 + 20 : q + 1 + k; }
            if (bits < bestBits) { bestBits = bits; bestK = k; }
        }
        if (bestBits + (uint64_t)o * 16 >= (uint64_t)len * 16) {   // noise-like block: stored as it is
            w.put(0, 2); w.put(kVerbatim, 5);
            for (size_t i = b0; i < b1; ++i) w.put((uint32_t)(uint16_t)(int16_t)x[i], 16);
            continue;
        }
        w.put((uint32_t)o, 2); w.put((uint32_t)bestK, 5);
        for (int i = 0; i < o; ++i) w.put((uint32_t)(uint16_t)(int16_t)x[b0 + i], 16);
        for (uint32_t v : u) {
            const uint32_t q = v >> bestK;
            if (q >= (uint32_t)kEsc) { for (int i = 0; i < kEsc; ++i) w.bit(1); w.bit(0); w.put(v, 20); }
            else { for (uint32_t i = 0; i < q; ++i) w.bit(1); w.bit(0); w.put(v & ((1u << bestK) - 1u), bestK); }
        }
    }
    w.flush();
    return w.out;
}
// false on damaged data (never reads outside the buffer, never makes more than kMaxPackFrames samples)
inline bool unpack(const uint8_t* d, size_t n, std::vector<int32_t>& x)
{
    BitReader r(d, n);
    const uint32_t frames = r.get(32);
    if (r.bad || frames == 0 || frames > kMaxPackFrames || frames > n * 8) return false;   // every sample costs >= 1 bit
    x.assign(frames, 0);
    for (size_t b0 = 0; b0 < frames; b0 += kPackBlock) {
        const size_t b1 = std::min<size_t>(frames, b0 + (size_t)kPackBlock);
        const int o = (int)r.get(2), k = (int)r.get(5);
        if (r.bad) return false;
        if (k == (int)kVerbatim) {
            for (size_t i = b0; i < b1; ++i) x[i] = (int16_t)(uint16_t)r.get(16);
            if (r.bad) return false;
            continue;
        }
        if (k > 17 || (size_t)o > b1 - b0) return false;
        for (int i = 0; i < o; ++i) x[b0 + i] = (int16_t)(uint16_t)r.get(16);
        for (size_t i = b0 + o; i < b1; ++i) {
            uint32_t q = 0;
            while (q < (uint32_t)kEsc && r.bit()) ++q;
            if (r.bad) return false;
            uint32_t v;
            if (q >= (uint32_t)kEsc) { if (r.bit()) return false; v = r.get(20); }
            else v = q << k | r.get(k);
            if (r.bad) return false;
            const int32_t res = (v & 1u) ? -(int32_t)((v + 1u) >> 1) : (int32_t)(v >> 1);
            const int32_t y = predict(x.data(), i, o) + res;
            if (y < -32768 || y > 32767) return false;
            x[i] = y;
        }
    }
    return true;
}

inline std::string cleanSampleName(const std::string& n)
{
    std::string name;
    for (char c : n) name += (c == '\t' || c == '\n' || c == '\r') ? ' ' : c;
    return name;
}
// "name<TAB>samplerate<TAB>base64 of the packed PCM"   (name without tabs / newlines)
inline std::string encode(const SampleData& s)
{
    std::vector<int32_t> x(s.pcm.size());
    for (size_t i = 0; i < s.pcm.size(); ++i) x[i] = toPcm16(s.pcm[i]);
    const std::vector<uint8_t> b = pack(x);
    char srb[32]; std::snprintf(srb, sizeof(srb), "%.0f", s.sr);
    return cleanSampleName(s.name) + "\t" + srb + "\t" + base64(b.data(), b.size());
}
// the format before 0.10 (plain 16-bit little-endian PCM in base64), kept for the tests
inline std::string encodeRaw16(const SampleData& s)
{
    std::vector<uint8_t> b(s.pcm.size() * 2);
    for (size_t i = 0; i < s.pcm.size(); ++i) {
        const int16_t v = toPcm16(s.pcm[i]);
        b[i * 2] = (uint8_t)(v & 0xFF); b[i * 2 + 1] = (uint8_t)((uint16_t)v >> 8);
    }
    char srb[32]; std::snprintf(srb, sizeof(srb), "%.0f", s.sr);
    return cleanSampleName(s.name) + "\t" + srb + "\t" + base64(b.data(), b.size());
}
// packed = the 'z' format (encode), otherwise the old raw 16-bit one
inline SamplePtr decode(const char* s, size_t n, bool packed = false)
{
    const char* t1 = (const char*)std::memchr(s, '\t', n);
    if (!t1) return nullptr;
    const char* t2 = (const char*)std::memchr(t1 + 1, '\t', n - (size_t)(t1 + 1 - s));
    if (!t2) return nullptr;
    auto d = std::make_shared<SampleData>();
    d->name.assign(s, t1);
    d->sr = std::atof(std::string(t1 + 1, t2).c_str());
    if (!(d->sr >= 4000.0 && d->sr <= 384000.0)) return nullptr;
    const std::vector<uint8_t> b = unbase64(t2 + 1, n - (size_t)(t2 + 1 - s));
    if (packed) {
        std::vector<int32_t> x;
        if (!unpack(b.data(), b.size(), x)) return nullptr;
        d->pcm.resize(x.size());
        for (size_t i = 0; i < x.size(); ++i) d->pcm[i] = (float)x[i] / 32767.f;
    } else {
        d->pcm.resize(b.size() / 2);
        for (size_t i = 0; i < d->pcm.size(); ++i) d->pcm[i] = (int16_t)(b[i * 2] | b[i * 2 + 1] << 8) / 32768.f;
    }
    if (d->pcm.empty()) return nullptr;
    return d;
}
// one line "z<track> <encoded>"
inline std::string encodeLine(int t, const SampleData& s) { return "z" + std::to_string(t) + " " + encode(s); }

// All tracks: lines "<track> <encoded>"
template <class Arr>
std::string encodeAll(const Arr& samples, int tracks)
{
    std::string o;
    for (int t = 0; t < tracks; ++t)
        if (samples[t]) { o += encodeLine(t, *samples[t]) + "\n"; }
    return o;
}
// calls fn(track, SamplePtr) for each line of encodeAll's output (also for a single "<track> <encoded>" command;
// "<track>" alone = remove the sample)
template <class Fn>
void decodeAll(const char* text, Fn fn)
{
    const char* s = text;
    while (s && *s) {
        const char* e = std::strchr(s, '\n');
        const size_t len = e ? (size_t)(e - s) : std::strlen(s);
        if (len > 0) {
            const bool packed = *s == 'z';
            const char* num = packed ? s + 1 : s;
            char* end = nullptr;
            const long t = (*num >= '0' && *num <= '9') ? std::strtol(num, &end, 10) : 0;
            if (end && end != num && (size_t)(end - s) <= len) {
                const char* rest = end; while (rest < s + len && *rest == ' ') ++rest;
                const size_t rl = len - (size_t)(rest - s);
                fn((int)t, rl > 0 ? decode(rest, rl, packed) : SamplePtr());
            }
        }
        s = e ? e + 1 : nullptr;
    }
}

} // namespace smp
} // namespace kn
