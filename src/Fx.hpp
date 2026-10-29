// Kinetik KGRN-4 — send effects: tempo-synced ping-pong echo and an 8-line feedback-delay-network reverb
#pragma once
#include "Params.hpp"
#include <vector>
#include <algorithm>

namespace kn {

struct PingPong {
    std::vector<float> l, r; int w = 0; double time = 1000; float fb = 0.5f; OnePole lpL, lpR, hpL, hpR; float sr = 48000.f; int quiet = 0;
    float width = 1.f;
    void init(float sampleRate) { sr = sampleRate; l.assign((size_t)(sr * 4.5f), 0.f); r.assign(l.size(), 0.f); lpL.setLP(5000.f, sr); lpR.setLP(5000.f, sr); hpL.setLP(60.f, sr); hpR.setLP(60.f, sr); clear(); }
    void clear() { std::fill(l.begin(), l.end(), 0.f); std::fill(r.begin(), r.end(), 0.f); w = 0; quiet = 1 << 30; hpL.y = hpR.y = lpL.y = lpR.y = 0.f; }
    // 0.4: HIGH CUT / LOW CUT in the repeats, WIDTH (0 = mono .. 64 = ping-pong .. 127 = wider)
    void set(float timeKnob, float fbKnob, double spb, float hiCut = 96.f, float loCut = 20.f, float widthKnob = 110.f)
    {
        time = std::min((double)l.size() - 4.0, std::max(16.0, kDelTime[bin8(timeKnob)] * spb)); fb = fbKnob / 127.f * 0.92f;
        lpL.setLP(cutHz(hiCut, 800.f, 18000.f), sr); lpR.a = lpL.a;
        hpL.setLP(cutHz(loCut, 20.f, 2000.f), sr); hpR.a = hpL.a;
        width = widthKnob / 64.f;
    }
    bool asleep() const { return quiet > (int)l.size(); }
    inline void process(float inL, float inR, float& outL, float& outR)
    {
        if (inL != 0.f || inR != 0.f) quiet = 0;
        if (asleep()) { outL = outR = 0.f; return; }
        double pos = (double)w - time; if (pos < 0) pos += l.size();
        const size_t i0 = (size_t)pos % l.size(), i1 = (i0 + 1) % l.size(); const float fr = (float)(pos - std::floor(pos));
        const float el = l[i0] + (l[i1] - l[i0]) * fr, er = r[i0] + (r[i1] - r[i0]) * fr;
        float fl = lpL.lp(el), fr2 = lpR.lp(er);
        fl -= hpL.lp(fl); fr2 -= hpR.lp(fr2);
        l[(size_t)w] = (inL + inR) * 0.5f + fr2 * fb;        // mono in on the left, cross feedback = ping-pong
        r[(size_t)w] = fl * fb;
        if (++w >= (int)l.size()) w = 0;
        const float m = 0.5f * (el + er), sd = 0.5f * (el - er) * width;
        outL = m + sd; outR = m - sd;
        if (std::fabs(el) + std::fabs(er) < 1e-6f) ++quiet; else quiet = 0;
    }
};

struct Reverb {
    static constexpr int kLines = 8;
    std::vector<float> buf[kLines]; int len[kLines] = {}, pos[kLines] = {};
    float g[kLines] = {}, z[kLines] = {}, damp = 0.3f, sr = 48000.f;
    std::vector<float> pre; int pw = 0, preLen = 1;
    int quiet = 1 << 30;
    // 0.4: input diffusion (smears the attacks: no flutter) and two slowly modulated lines (no metallic ringing)
    struct Ap { std::vector<float> b; int w = 0; float g = 0.7f; inline float tick(float x) { const float d = b[(size_t)w]; const float y = -g * x + d; b[(size_t)w] = x + g * y; if (++w >= (int)b.size()) w = 0; return y; } };
    Ap dif[4], difS[2]; float mph = 0.f, mdepth = 8.f, width = 1.f;
    // 0.4: PRE-DELAY 0 .. 200 ms, WIDTH 0 = mono .. 127 = wider than the room
    void setSpace(float preKnob, float widthKnob) { preLen = std::max(1, std::min((int)pre.size() - 1, (int)(preKnob / 127.f * 0.2f * sr))); width = widthKnob / 80.f; }
    void init(float sampleRate)
    {
        sr = sampleRate;
        static const float ms[kLines] = {29.7f, 37.1f, 41.1f, 43.7f, 53.3f, 59.9f, 67.7f, 73.1f};
        for (int i = 0; i < kLines; ++i) { len[i] = std::max(8, (int)(ms[i] * 0.001f * sr * 1.6f)); buf[i].assign((size_t)len[i], 0.f); }
        pre.assign((size_t)(0.25f * sr) + 2, 0.f); preLen = (int)(0.02f * sr);
        static const float dms[4] = {4.77f, 3.59f, 12.73f, 9.31f}, sms[2] = {5.3f, 7.9f};
        for (int i = 0; i < 4; ++i) { dif[i].b.assign((size_t)std::max(2, (int)(dms[i] * 0.001f * sr)), 0.f); dif[i].g = i < 2 ? 0.75f : 0.625f; }
        for (int i = 0; i < 2; ++i) { difS[i].b.assign((size_t)std::max(2, (int)(sms[i] * 0.001f * sr)), 0.f); difS[i].g = 0.6f; }
        mdepth = 0.0004f * sr;
        clear(); set(80.f, 70.f);
    }
    void clear()
    {
        for (int i = 0; i < kLines; ++i) { std::fill(buf[i].begin(), buf[i].end(), 0.f); pos[i] = 0; z[i] = 0.f; }
        std::fill(pre.begin(), pre.end(), 0.f); pw = 0; quiet = 1 << 30;
        for (auto& a : dif) std::fill(a.b.begin(), a.b.end(), 0.f);
        for (auto& a : difS) std::fill(a.b.begin(), a.b.end(), 0.f);
    }
    // size knob: decay 0.4 .. 14 s; tone: damping cutoff 1.5 .. 14 kHz
    void set(float sizeKnob, float toneKnob)
    {
        const float rt = 0.4f * std::pow(35.f, sizeKnob / 127.f);
        for (int i = 0; i < kLines; ++i) g[i] = std::pow(10.f, -3.f * len[i] / (rt * sr));
        const float fc = 1500.f * std::pow(9.3f, toneKnob / 127.f);
        damp = std::exp(-kTwoPi * std::min(fc, sr * 0.45f) / sr);
    }
    bool asleep() const { return quiet > (int)(sr * 15.f); }
    inline void process(float inL, float inR, float& outL, float& outR)
    {
        if (inL != 0.f || inR != 0.f) quiet = 0;
        if (asleep()) { outL = outR = 0.f; return; }
        // pre-delay (mono)
        const int pn = (int)pre.size();
        pre[(size_t)pw] = (inL + inR) * 0.5f; float x = pre[(size_t)((pw - preLen + pn) % pn)]; pw = (pw + 1) % pn;
        float side = (inL - inR) * 0.5f;
        for (auto& a : dif) x = a.tick(x);
        for (auto& a : difS) side = a.tick(side);
        float o[kLines];
        for (int i = 0; i < kLines; ++i) o[i] = buf[i][(size_t)pos[i]];
        mph += 0.37f / sr; if (mph >= 1.f) mph -= 1.f;
        for (int k = 0; k < 2; ++k) {                       // lines 1 and 6 read a little earlier / later, slowly
            const int i = k ? 6 : 1;
            const float m = mdepth * (1.f + std::sin(kTwoPi * (mph + k * 0.5f)));
            float p = (float)pos[i] + m; const int L = len[i]; if (p >= (float)L) p -= (float)L;
            const int a = (int)p; const float f = p - (float)a;
            o[i] = buf[i][(size_t)a] * (1.f - f) + buf[i][(size_t)((a + 1) % L)] * f;
        }
        // Hadamard 8x8 (fast, normalised)
        float a[kLines];
        for (int i = 0; i < kLines; ++i) a[i] = o[i];
        for (int h = 1; h < kLines; h <<= 1)
            for (int i = 0; i < kLines; i += h * 2)
                for (int j = i; j < i + h; ++j) { const float u = a[j], v = a[j + h]; a[j] = u + v; a[j + h] = u - v; }
        const float nrm = 0.35355339f;
        for (int i = 0; i < kLines; ++i) {
            float y = a[i] * nrm * g[i];
            z[i] = y + (z[i] - y) * damp; y = z[i];
            const float in = (i & 1) ? x - side : x + side;
            buf[i][(size_t)pos[i]] = y + in * 0.35f;
            if (++pos[i] >= len[i]) pos[i] = 0;
        }
        const float ol = (o[0] + o[2] + o[4] + o[6]) * 0.5f, orr = (o[1] + o[3] + o[5] + o[7]) * 0.5f;
        const float m = 0.5f * (ol + orr), sd = 0.5f * (ol - orr) * width;
        outL = m + sd; outR = m - sd;
        if (std::fabs(outL) + std::fabs(outR) < 1e-6f) ++quiet; else quiet = 0;
    }
};

// master chorus: two modulated delays in quadrature, mixed with the dry sound
struct Chorus {
    std::vector<float> bl, br; int w = 0; float ph = 0.f, inc = 0.f, depth = 0.f, mix = 0.f, sr = 48000.f;
    void init(float s) { sr = s; bl.assign((size_t)(0.06f * s), 0.f); br.assign(bl.size(), 0.f); w = 0; }
    void clear() { std::fill(bl.begin(), bl.end(), 0.f); std::fill(br.begin(), br.end(), 0.f); }
    void set(float rateK, float depthK, float mixK) { inc = 0.05f * std::pow(100.f, rateK / 127.f) / sr; depth = depthK / 127.f * 0.006f * sr; mix = mixK / 127.f; }
    inline float rd(const std::vector<float>& b, float d) const { float p = (float)w - d; const float n = (float)b.size(); while (p < 0.f) p += n; const int i = (int)p; const float f = p - i; return b[(size_t)i % b.size()] * (1.f - f) + b[(size_t)(i + 1) % b.size()] * f; }
    inline void process(float& l, float& r)
    {
        if (mix < 0.001f) return;
        bl[(size_t)w] = l; br[(size_t)w] = r;
        ph += inc; if (ph >= 1.f) ph -= 1.f;
        const float base = 0.012f * sr;
        const float dl = base + depth * (0.5f + 0.5f * std::sin(kTwoPi * ph)), dr = base + depth * (0.5f + 0.5f * std::cos(kTwoPi * ph));
        const float wl = rd(bl, dl), wr = rd(br, dr);
        if (++w >= (int)bl.size()) w = 0;
        l += (wl - l) * mix * 0.5f; r += (wr - r) * mix * 0.5f;
        l += wr * mix * 0.25f; r += wl * mix * 0.25f;      // a little cross: width
    }
};

} // namespace kn
