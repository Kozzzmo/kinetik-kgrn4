// Kinetik KGRN-4 — user interface (NanoVG)
#include "DistrhoUI.hpp"
#include "DistrhoPlugin.hpp"
#include "GrainDirect.hpp"
#include "Bank.hpp"
#include "Presets.hpp"
#include "Materials.hpp"
#include "Skins.hpp"
#include "LogoMark.hpp"
#include "Fonts.hpp"
#include "License.hpp"
#include "Updater.hpp"
#include "FsUtf8.hpp"
#include "FileDrop.hpp"
#define DR_WAV_IMPLEMENTATION
#include "third_party/dr_libs/dr_wav.h"
#include <cstdlib>
#include <algorithm>
#include <vector>
#include <cmath>
#include <string>
#include <memory>
#include <deque>
#include <chrono>

START_NAMESPACE_DISTRHO

using namespace kn;
using DGL_NAMESPACE::Color;

static constexpr float W = 1180.f, H = 720.f;
static constexpr int kFineSteps = 24;   // out_step is in 1/24 of a 16th

struct R {
    float x, y, w, h;
    bool in(float px, float py) const { return px >= x && px < x + w && py >= y && py < y + h; }
    float cx() const { return x + w * 0.5f; }
    float cy() const { return y + h * 0.5f; }
};

enum DragKind { dkNone, dkKnob, dkGlobal, dkTempo, dkPatLen, dkSwing, dkTrackLen, dkPaint, dkWave, dkTrig, dkResize };
enum Overlay { ovNone, ovPresets, ovSkins, ovSave, ovLicense, ovUpdate, ovChain };

class KgrnUI : public UI
{
public:
    KgrnUI()
        : UI(W, H)
    {
        loadSharedResources();
        const double s = getScaleFactor();
        if (s > 1.01) setSize((uint)(W * s), (uint)(H * s));
        setGeometryConstraints((uint)(W * 0.6 * (s > 1.01 ? s : 1.0)), (uint)(H * 0.6 * (s > 1.01 ? s : 1.0)), true, false);
        for (int i = 0; i < kTotalParams; ++i) values[i] = 0.f;
        for (int t = 0; t < kTracks; ++t) for (int k = 0; k < kTrackParams; ++k) values[tpIndex(t, k)] = trackDefault(t, k);
        for (int g = 0; g < kGlobalParams; ++g) values[kGlobalBase + g] = globalDefault(g);
        bank.reset(new Bank());
        bank->initDefault();
        defaultKits(*bank);
        fonts[fnSerif] = createFontFromMemory("kn-serif", kFontSerif, sizeof(kFontSerif), false);
        fonts[fnSerifItalic] = createFontFromMemory("kn-serif-i", kFontSerifItalic, sizeof(kFontSerifItalic), false);
        fonts[fnMono] = createFontFromMemory("kn-mono", kFontMono, sizeof(kFontMono), false);
        fonts[fnBlack] = createFontFromMemory("kn-black", kFontBlack, sizeof(kFontBlack), false);
        fonts[fnLogo] = createFontFromMemory("kn-logo", kFontUnbounded, sizeof(kFontUnbounded), false);
        int sk, zm;
        readDefaultView(sk, zm);
        setSkin(sk, false);
        if (zm != 100) setSize((uint)std::lround(W * baseScale() * zm / 100.0), (uint)std::lround(H * baseScale() * zm / 100.0));
        viewDirty = false;
        licInfo = lic::findInstalled("KGRN-4");
        updater.reset(new upd::Checker("KGRN-4", KN_VERSION, "https://github.com/Kozzzmo/kinetik-kgrn4/releases/latest/download/update.txt"));
        updater->check(false);
        if (void* p = getPluginInstancePointer()) direct = dynamic_cast<GrainDirect*>(static_cast<Plugin*>(p));
        dropToken = fileDropAttach(getWindow().getNativeWindowHandle(),
                                   [this](const std::string& path, double fx, double fy) { fileDropped(path, fx * W, fy * H); });
    }
    ~KgrnUI() override { fileDropDetach(dropToken); }

    // ------------------------------------------------------------------ skins / view
    static std::string viewFile()
    {
       #ifdef _WIN32
        const std::string base = fs::env("APPDATA");
        return !base.empty() ? base + "\\KinetikKGRN4-view.txt" : std::string();
       #else
        const std::string base = fs::env("HOME");
       #ifdef __APPLE__
        return !base.empty() ? base + "/Library/Preferences/KinetikKGRN4-view.txt" : std::string();
       #else
        return !base.empty() ? base + "/.config/KinetikKGRN4-view.txt" : std::string();
       #endif
       #endif
    }
    static void readDefaultView(int& skinOut, int& zoomOut)
    {
        skinOut = kDefaultSkin; zoomOut = 100;
        const std::string f = viewFile();
        if (f.empty()) return;
        if (FILE* fp = fs::open(f, "r")) {
            int a = 0, b = 100; const int n = std::fscanf(fp, "%d %d", &a, &b); std::fclose(fp);
            if (n >= 1 && a >= 0 && a < kNumThemes) skinOut = a;
            if (n >= 2 && b >= 60 && b <= 300) zoomOut = b;
        }
    }
    static void writeDefaultView(int s, int z)
    {
        const std::string f = viewFile();
        if (f.empty()) return;
        if (FILE* fp = fs::open(f, "w")) { std::fprintf(fp, "%d %d\n", s, z); std::fclose(fp); }
    }
    void setSkin(int i, bool save) { skin = clampi(i, 0, kNumThemes - 1); th = &kThemes[skin]; if (save) saveView(); repaint(); }
    double baseScale() const { const double s = getScaleFactor(); return s > 0.1 ? s : 1.0; }
    int zoomPercent() const { return (int)std::lround(getWidth() / (W * baseScale()) * 100.0); }
    void setZoom(double z)
    {
        z = std::max(0.6, std::min(3.0, z));
        const uint w = (uint)std::lround(W * baseScale() * z), h = (uint)std::lround(H * baseScale() * z);
        if (std::abs((int)w - (int)getWidth()) > 1) setSize(w, h);
        viewDirty = true; repaint();
    }
    void saveView()
    {
        char b[32]; std::snprintf(b, sizeof(b), "%d %d", skin, zoomPercent());
        setState("skin", b);
        writeDefaultView(skin, zoomPercent());
        viewDirty = false;
    }

protected:
    // ------------------------------------------------------------------ DSP -> UI
    void parameterChanged(uint32_t index, float value) override
    {
        if (index >= (uint32_t)kTotalParams || values[index] == value) return;
        values[index] = value;
        repaint();
    }
    void stateChanged(const char* key, const char* value) override
    {
        if (std::strcmp(key, "samples") == 0) {
            for (auto& sp : uiSmp) sp.reset();
            smp::decodeAll(value ? value : "", [this](int t, SamplePtr d) { if (t >= 0 && t < kTracks) uiSmp[t] = std::move(d); });
            repaint();
            return;
        }
        if (std::strcmp(key, "bank") == 0) { deserializeBank(*bank, value); repaint(); }
        if (std::strcmp(key, "skin") == 0 && value && *value) {
            int sk = kDefaultSkin, zm = 0;
            const int n = std::sscanf(value, "%d %d", &sk, &zm);
            if (n >= 1) setSkin(sk, false);
            if (n >= 2 && zm >= 60 && zm <= 300 && std::abs(zm - zoomPercent()) > 1) setZoom(zm / 100.0);
            viewDirty = false;
        }
    }
    void onResize(const ResizeEvent& ev) override { UI::onResize(ev); viewDirty = true; viewDirtyTicks = 0; }

    // ------------------------------------------------------------------ drawing primitives (same as the other Kinetik plug-ins)
    void useFont(int f)
    {
        if (f >= fnSerif && f < kFontCount) fontFaceId(fonts[f]); else fontFace(NANOVG_DEJAVU_SANS_TTF);
    }
    void box(const R& r, const Color& c, float rad = -1.f) { beginPath(); roundedRect(r.x, r.y, r.w, r.h, rad < 0 ? th->radius : rad); fillColor(c); fill(); }
    void frame(const R& r, const Color& c, float rad = -1.f, float wdt = 1.f)
    {
        beginPath(); roundedRect(r.x + 0.5f, r.y + 0.5f, r.w - 1, r.h - 1, rad < 0 ? th->radius : rad); strokeColor(c); strokeWidth(wdt); stroke();
    }
    static float textSize(float size, int font) { const float s = std::max(size, 9.5f); return font == fnMono ? s * 1.13f * 1.08f : s * 1.08f; }
    void label(float x, float y, const char* s, float size, const Color& c, int align = ALIGN_CENTER | ALIGN_MIDDLE, int font = -1)
    {
        if (s == nullptr || *s == '\0') return;
        const int f = font < 0 ? th->fontLabel : font;
        useFont(f); fontSize(textSize(size, f));
        textLetterSpacing(f == fnMono ? th->labelSpacing * (size / 12.f) : 0.f);
        fillColor(c); textAlign(align); text(x, y, s, nullptr);
        textLetterSpacing(0.f);
    }
    void number(float x, float y, const char* s, float size, const Color& c, int align = ALIGN_CENTER | ALIGN_MIDDLE, int font = -1)
    {
        if (s == nullptr || *s == '\0') return;
        const int f = font < 0 ? th->fontNum : font;
        useFont(f);
        float sz = f == fnSerif || f == fnSerifItalic ? size * 1.25f : (f == fnBlack ? size * 0.95f : size);
        if (size <= 16.f) sz *= 1.15f;
        fontSize(sz); textAlign(align);
        if (th->glow) { fontBlur(sz * 0.18f); fillColor(Color(c, Color(0, 0, 0, 0.f), 0.35f)); text(x, y, s, nullptr); fontBlur(0.f); }
        fillColor(c); text(x, y, s, nullptr);
    }
    enum Icon { icPlay, icStop };
    void iconLabel(float cx, float cy, Icon ic, const char* s, float size, const Color& c)
    {
        const int f = th->fontButton;
        useFont(f); fontSize(textSize(size, f));
        textLetterSpacing(f == fnMono ? th->labelSpacing * (size / 12.f) : 0.f);
        DGL_NAMESPACE::Rectangle<float> bnd; textBounds(0, 0, s, nullptr, bnd);
        textLetterSpacing(0.f);
        const float isz = size * 0.62f, gap = size * 0.55f, total = isz + gap + bnd.getWidth();
        const float x = cx - total * 0.5f;
        beginPath();
        if (ic == icPlay) { moveTo(x, cy - isz * 0.55f); lineTo(x + isz * 0.9f, cy); lineTo(x, cy + isz * 0.55f); closePath(); }
        else rect(x, cy - isz * 0.45f, isz * 0.9f, isz * 0.9f);
        fillColor(c); fill();
        label(x + isz + gap, cy, s, size, c, ALIGN_LEFT | ALIGN_MIDDLE, f);
    }
    void panelBox(const R& r) { box(r, th->panel, th->radius + 2.f); frame(r, th->panelEdge, th->radius + 2.f, th->panelEdgeWidth); }
    void button(const R& r, const char* s, bool on, float size = 11.5f)
    {
        if (on) box(r, th->selFill); else { box(r, th->btnFill); frame(r, th->btnEdge, -1.f, 1.f); }
        label(r.cx(), r.cy() + 1, s, size, on ? th->selText : th->btnText, ALIGN_CENTER | ALIGN_MIDDLE, th->fontButton);
    }
    void overlayBox(const R& r)
    {
        beginPath(); rect(0, 0, W, H); fillColor(th->overlay); fill();
        box(r, th->panel, th->radius + 3.f); frame(r, th->panelEdge, th->radius + 3.f, th->panelEdgeWidth + 0.5f);
    }
    Color accentInk() const { return th->logo == lgRiso ? th->text : th->accent; }
    Color onColor() const { return th->logo == lgNeon ? Color(236, 36, 50) : th->accent; }   // lit steps (NEON red, as KDSP-16)
    void drawKnob(const R& r, float v01, const char* name, const std::string& val, bool locked, bool highlight,
                  float rad = 26.f, float cyo = 44.f, float ny = 88.f, float vy = 106.f)
    {
        const float cx = r.cx(), cy = r.y + cyo;
        const float a0 = kPi * 0.75f, a1 = kPi * 2.25f;
        v01 = clampf(v01, 0.f, 1.f);
        const Color hi = locked ? th->lock : th->knobArc;
        const float a = a0 + (a1 - a0) * v01;
        switch (th->knob) {
        case ksTicks: {
            for (int i = 0; i < 25; ++i) {
                const float t = i / 24.f, ang = a0 + (a1 - a0) * t; const bool lit = t <= v01 + 1e-4f;
                beginPath(); circle(cx + std::cos(ang) * (rad + 3), cy + std::sin(ang) * (rad + 3), lit ? 1.6f : 1.1f); fillColor(lit ? hi : th->knobTrack); fill();
            }
            if (th->glow) { beginPath(); circle(cx, cy, rad - 3); fillPaint(radialGradient(cx, cy, rad - 8, rad + 8, Color(hi, Color(0, 0, 0, 0.f), 0.75f), Color(0, 0, 0, 0.f))); fill(); }
            beginPath(); circle(cx, cy, rad - 5); fillColor(th->knobBody); fill();
            strokeColor(locked ? th->lock : th->knobRing); strokeWidth(locked ? 2.f : 1.2f); stroke();
            beginPath(); moveTo(cx + std::cos(a) * 4, cy + std::sin(a) * 4); lineTo(cx + std::cos(a) * (rad - 9), cy + std::sin(a) * (rad - 9));
            strokeColor(locked ? th->lock : th->knobPointer); strokeWidth(2.4f); lineCap(ROUND); stroke();
            break;
        }
        case ksArcThick: {
            beginPath(); arc(cx, cy, rad, a0, a1, CW); strokeColor(th->knobTrack); strokeWidth(6.f); lineCap(BUTT); stroke();
            beginPath(); arc(cx, cy, rad, a0, a0 + (a1 - a0) * std::max(0.001f, v01), CW); strokeColor(hi); strokeWidth(6.f); stroke();
            beginPath(); circle(cx, cy, rad - 6); fillColor(locked ? th->lock : th->knobBody); fill();
            strokeColor(th->knobRing); strokeWidth(1.2f); stroke();
            beginPath(); moveTo(cx + std::cos(a) * 6, cy + std::sin(a) * 6); lineTo(cx + std::cos(a) * (rad - 9), cy + std::sin(a) * (rad - 9));
            strokeColor(th->knobPointer); strokeWidth(2.4f); lineCap(ROUND); stroke();
            break;
        }
        default: {
            beginPath(); circle(cx, cy, rad + 4); fillColor(th->inner); fill();
            beginPath(); arc(cx, cy, rad, a0, a1, CW); strokeColor(th->knobTrack); strokeWidth(5.f); stroke();
            beginPath(); arc(cx, cy, rad, a0, a0 + (a1 - a0) * std::max(0.001f, v01), CW); strokeColor(hi); strokeWidth(5.f); stroke();
            beginPath(); circle(cx, cy, rad - 7); fillColor(highlight ? Color(th->knobBody, Color(255, 255, 255), 0.1f) : th->knobBody); fill();
            beginPath(); moveTo(cx + std::cos(a) * (rad - 18), cy + std::sin(a) * (rad - 18)); lineTo(cx + std::cos(a) * (rad - 8), cy + std::sin(a) * (rad - 8));
            strokeColor(th->knobPointer); strokeWidth(3.f); stroke();
            break;
        }
        }
        label(cx, r.y + ny, name, rad < 24.f ? 10.5f : 12.f, locked ? th->lock : th->text);
        number(cx, r.y + vy, val.c_str(), rad < 24.f ? 12.5f : 14.f, locked ? th->lock : (th->fontNum == fnSans ? th->dim : th->text));
    }
    void drawLogo()
    {
        drawOrbitMark(*this, 14.f, 7.f, 46.f, th->logo);
        const char* nm = "KGRN-4";
        useFont(fnLogo); fontSize(30.f); textAlign(ALIGN_LEFT | ALIGN_MIDDLE);
        const float nx = 68.f, ny = 23.f;
        if (th->glow) { fontBlur(10.f); fillColor(Color(th->accent, Color(0, 0, 0, 0.f), 0.45f)); text(nx, ny, nm, nullptr); fontBlur(0.f); }
        fillColor(th->glow ? Color(232, 236, 255) : th->text); text(nx, ny, nm, nullptr);
        const int ust = updater ? (int)updater->getState() : -1;
        if (ust == upd::Checker::stAvailable || ust == upd::Checker::stInstalling || ust == upd::Checker::stInstalled) {
            const std::string v = updater->manifest().version;
            const std::string t = ust == upd::Checker::stAvailable ? "UPDATE " + v + " AVAILABLE - CLICK"
                                : ust == upd::Checker::stInstalling ? "INSTALLING UPDATE " + v + "..." : "UPDATED TO " + v + " - REOPEN TO USE IT";
            label(nx + 1, 45.f, t.c_str(), 9.5f, accentInk(), ALIGN_LEFT | ALIGN_MIDDLE, fnMono);
        } else if (licInfo.valid || !lic::kDemoModeEnabled) label(nx + 1, 45.f, "KINETIK  GRANULAR PROGRAM", 9.5f, th->dim, ALIGN_LEFT | ALIGN_MIDDLE, fnMono);
        else label(nx + 1, 45.f, "DEMO - CLICK TO REGISTER", 9.5f, accentInk(), ALIGN_LEFT | ALIGN_MIDDLE, fnMono);
    }

    // ------------------------------------------------------------------ helpers
    float gp(int g) const { return values[kGlobalBase + g]; }
    float tv(int t, int k) const { return values[tpIndex(t, k)]; }
    int editPattern() const { return clampi((int)std::lround(gp(gPATTERN)), 0, kPatterns - 1); }
    Pattern& pat() { return bank->patterns[editPattern()]; }
    int64_t fineStep() const { return (int64_t)std::lround(gp(gOUT_STEP)); }
    bool playingThis() const { return gp(gOUT_STEP) >= 0.f && (int)std::lround(gp(gOUT_PATTERN)) == editPattern(); }
    int trackStepNow(int t)
    {
        if (!playingThis()) return -1;
        const int64_t k = (int64_t)std::floor((double)fineStep() / kFineSteps * kSpeedMult[pat().speed[t] % kSpeeds] + 1e-9);
        return pat().trackStep(t, k);
    }
    bool lockMode() const { return lockStep >= 0; }
    bool liveTrack(int t) const { return bank->kit[t].material == kMatLive; }
    const SampleData* srcOf(int t) const { const int m = bank->kit[t].material; return m == kMatLive ? nullptr : (m > 0 ? material(m).get() : uiSmp[t].get()); }
    std::string srcName(int t) const
    {
        const int m = bank->kit[t].material;
        if (m == kMatLive) return "LIVE INPUT";
        if (m > 0) return kMaterialNames[m];
        return uiSmp[t] ? uiSmp[t]->name : std::string("NO SAMPLE");
    }

    void setParam(uint32_t idx, float v)
    {
        if (values[idx] != v && idx != (uint32_t)(kGlobalBase + gPLAY) && idx != (uint32_t)(kGlobalBase + gPATTERN)) autoSnap("sound", 1.2);
        values[idx] = v;
        setParameterValue(idx, v);
        char b[40]; std::snprintf(b, sizeof(b), "pv %u %.4f\n", (unsigned)idx, v); pvQueue += b;
    }
    std::string pvQueue;
    void flushPv() { if (pvQueue.empty()) return; pvQueue.pop_back(); setState("cmd", pvQueue.c_str()); pvQueue.clear(); }
    void setParamGesture(uint32_t idx, float v) { editParameter(idx, true); setParam(idx, v); editParameter(idx, false); }
    void sendCmd(const std::string& c)
    {
        const bool live = c.rfind("trig ", 0) == 0 || c.rfind("rel ", 0) == 0;
        if (!live) { autoSnap("edit", 0.5); applyCommand(*bank, c.c_str()); }
        setState("cmd", c.c_str());
    }
    void sendLines(const std::string& c)
    {
        autoSnap("edit", 0.5);
        size_t st = 0;
        while (st < c.size()) {
            size_t e = c.find('\n', st); if (e == std::string::npos) e = c.size();
            applyCommand(*bank, c.substr(st, e - st).c_str());
            st = e + 1;
        }
        setState("cmd", c.c_str());
    }
    void sendPatternSettings() { char b[48]; std::snprintf(b, sizeof(b), "P %d %d %d", editPattern(), pat().length, pat().swing); sendCmd(b); }
    void sendTrackSettings(int t) { char b[48]; std::snprintf(b, sizeof(b), "L %d %d %d %d", editPattern(), t, pat().len[t], pat().speed[t]); sendCmd(b); }
    void trigTrack(int t, bool on) { char b[32]; if (on) std::snprintf(b, sizeof(b), "trig %d 0", t); else std::snprintf(b, sizeof(b), "rel %d", t); sendCmd(b); }

    // ------------------------------------------------------------------ layout
    R logoArea() const { return {12.f, 4.f, 284.f, 50.f}; }
    R inBox() const { return {300.f, 9.f, 72.f, 40.f}; }
    R freezeBtn() const { return {376.f, 9.f, 74.f, 40.f}; }
    R capLenBox() const { return {454.f, 9.f, 76.f, 40.f}; }
    R patLenBox() const { return {536.f, 9.f, 76.f, 40.f}; }
    R swingBox() const { return {618.f, 9.f, 76.f, 40.f}; }
    R patBox() const { return {700.f, 9.f, 130.f, 40.f}; }
    R playBtn() const { return {840.f, 9.f, 84.f, 40.f}; }
    R tempoBox() const { return {932.f, 9.f, 110.f, 40.f}; }
    R skinBtn() const { return {1050.f, 9.f, 116.f, 40.f}; }
    // tracks
    R trackBtn(int t) const { return {24.f, 68.f + t * 48.f, 166.f, 44.f}; }
    R muteBtn(int t) const { return {194.f, 68.f + t * 48.f, 48.f, 20.f}; }
    R trigBtn(int t) const { return {194.f, 92.f + t * 48.f, 48.f, 20.f}; }
    // material / wave
    R waveR() const { return {270.f, 88.f, 598.f, 128.f}; }
    R loadBtn() const { return {270.f, 224.f, 64.f, 28.f}; }
    R matBtn() const { return {340.f, 224.f, 136.f, 28.f}; }
    R captureBtn() const { return {482.f, 224.f, 96.f, 28.f}; }
    R sliceModeBtn() const { return {584.f, 224.f, 110.f, 28.f}; }
    R amtMinus() const { return {700.f, 224.f, 24.f, 28.f}; }
    R amtBox() const { return {726.f, 224.f, 86.f, 28.f}; }
    R amtPlus() const { return {814.f, 224.f, 24.f, 28.f}; }
    R modeBtn() const { return {444.f, 62.f, 120.f, 22.f}; }
    R syncBtn() const { return {568.f, 62.f, 60.f, 22.f}; }
    R legatoBtn() const { return {632.f, 62.f, 76.f, 22.f}; }
    R lockClearBtn() const { return {760.f, 64.f, 108.f, 20.f}; }
    // knobs: two rows of ten (SOURCE GRAIN / COLOUR ENV MIX), a gap between groups
    R knobR(int k) const
    {
        const int row = k / 10, c = k % 10;
        int gaps = 0; for (int g = 1; g < kGroups; ++g) if (kGroupStart[g] / 10 == row && kGroupStart[g] % 10 != 0 && c >= kGroupStart[g] % 10) ++gaps;
        return {20.f + c * 83.f + gaps * 10.f, 288.f + row * 98.f, 82.f, 90.f};
    }
    // sequencer
    R stepPageBtn(int i) const { return {24.f + i * 52.f, 494.f, 48.f, 24.f}; }
    R lenMinus() const { return {238.f, 494.f, 24.f, 24.f}; }
    R lenBox() const { return {264.f, 494.f, 70.f, 24.f}; }
    R lenPlus() const { return {336.f, 494.f, 24.f, 24.f}; }
    R speedBtn() const { return {366.f, 494.f, 92.f, 24.f}; }
    R rotL() const { return {464.f, 494.f, 26.f, 24.f}; }
    R rotR() const { return {492.f, 494.f, 26.f, 24.f}; }
    R fillBtn() const { return {524.f, 494.f, 54.f, 24.f}; }
    R clearTrackBtn() const { return {582.f, 494.f, 60.f, 24.f}; }
    R copyTrackBtn() const { return {646.f, 494.f, 58.f, 24.f}; }
    R pasteTrackBtn() const { return {708.f, 494.f, 58.f, 24.f}; }
    float rowY(int t) const { return 526.f + t * 42.f; }
    R rowBtn(int t) const { return {24.f, rowY(t), 96.f, 38.f}; }
    R stepCell(int t, int i) const { return {128.f + i * 46.f, rowY(t), 42.f, 38.f}; }
    // right column
    R patBtn(int i) const { return {898.f + (i % 4) * 66.f, 84.f + (i / 4) * 32.f, 60.f, 28.f}; }
    R patCopy() const { return {898.f, 214.f, 60.f, 26.f}; }
    R patPaste() const { return {964.f, 214.f, 60.f, 26.f}; }
    R patClear() const { return {1030.f, 214.f, 60.f, 26.f}; }
    R chainBtn() const { return {1096.f, 214.f, 60.f, 26.f}; }
    R gKnob(int i) const { return i < 3 ? R{892.f + i * 90.f, 276.f, 90.f, 120.f} : R{937.f + (i - 3) * 90.f, 392.f, 90.f, 120.f}; }
    R presetName() const { return {898.f, 526.f, 258.f, 40.f}; }
    R presetBtn() const { return {898.f, 574.f, 258.f, 34.f}; }
    R saveBtn() const { return {898.f, 614.f, 258.f, 34.f}; }
    R undoBtn() const { return {898.f, 660.f, 258.f, 34.f}; }
    R gripR() const { return {W - 22.f, H - 22.f, 22.f, 22.f}; }
    // overlays
    R viewBox() const { return {240.f, 96.f, 700.f, 520.f}; }
    R skinRow(int i) const { return {264.f + (i % 2) * 330.f, 150.f + (i / 2) * 148.f, 316.f, 136.f}; }
    R zoomBtn(int i) const { return {264.f + i * 109.f, 486.f, 101.f, 36.f}; }
    R presetsBox() const { return {80.f, 60.f, 1020.f, 600.f}; }
    R presetItem(int col, int row) const { return {100.f + col * 500.f, 130.f + row * 30.f, 480.f, 27.f}; }
    R presetsClose() const { return {980.f, 606.f, 100.f, 36.f}; }
    R saveBox() const { return {290.f, 200.f, 600.f, 270.f}; }
    R saveField() const { return {320.f, 290.f, 540.f, 52.f}; }
    R saveOk() const { return {620.f, 400.f, 240.f, 40.f}; }
    R saveCancel() const { return {320.f, 400.f, 140.f, 40.f}; }
    R licBox() const { return {290.f, 170.f, 600.f, 330.f}; }
    R licLoad() const { return {320.f, 420.f, 190.f, 40.f}; }
    R licPaste() const { return {520.f, 420.f, 190.f, 40.f}; }
    R licClose() const { return {720.f, 420.f, 140.f, 40.f}; }
    R updBox() const { return {290.f, 150.f, 600.f, 380.f}; }
    R updAuto() const { return {320.f, 400.f, 170.f, 34.f}; }
    R updAutoInst() const { return {496.f, 400.f, 190.f, 34.f}; }
    R updLic() const { return {692.f, 400.f, 168.f, 34.f}; }
    R updMain() const { return {320.f, 456.f, 250.f, 40.f}; }
    R updSkip() const { return {580.f, 456.f, 150.f, 40.f}; }
    R updClose() const { return {740.f, 456.f, 120.f, 40.f}; }

    // ------------------------------------------------------------------ main draw
    void onNanoDisplay() override
    {
        sx = getWidth() / W;
        scale(sx, sx);
        if (!haloImage.isValid()) makeTextures();
        beginPath(); rect(0, 0, W, H); fillColor(th->bg); fill();
        if (th->halftone && haloImage.isValid()) {
            Paint p = imagePattern(0, 0, 6.f, 6.f, 0.f, haloImage, 1.f); p.innerColor = th->dots; p.outerColor = th->dots;
            beginPath(); rect(0, 0, W, H); fillPaint(p); fill();
        }
        drawHeader();
        drawTracks();
        drawWave();
        drawKnobs();
        drawSequencer();
        drawRight();
        if (overlay == ovPresets) drawPresets();
        if (overlay == ovSkins) drawSkins();
        if (overlay == ovSave) drawSave();
        if (overlay == ovLicense) drawLicense();
        if (overlay == ovUpdate) drawUpdate();
        if (overlay == ovChain) drawChain();
        if (tipShown && overlay == ovNone) drawTip();
        drawGrip();
    }
    void makeTextures()
    {
        uint8_t h[6 * 6 * 4] = {};
        for (int i : {7, 8, 13, 14}) { h[i * 4] = 255; h[i * 4 + 1] = 255; h[i * 4 + 2] = 255; h[i * 4 + 3] = 140; }
        haloImage = createImageFromRGBA(6, 6, h, IMAGE_REPEAT_X | IMAGE_REPEAT_Y | (1 << 5));
    }
    static Color withAlpha(Color c, float a) { c.alpha *= a; return c; }
    Color ledColor() const { return th->logo == lgNeon ? Color(125, 255, 90) : th->accent; }

    void lcdCell(const R& r, const char* top, const char* val)
    {
        box(r, th->lcdBg); frame(r, th->panelEdge, -1.f, th->panelEdgeWidth);
        label(r.cx(), r.y + 11, top, 9.f, th->lcdDim);
        number(r.cx(), r.y + 28, val, 16.f, th->lcdText);
    }
    void drawHeader()
    {
        drawLogo();
        char b[48];
        std::snprintf(b, sizeof(b), "%d", pat().length); lcdCell(patLenBox(), "LENGTH", b);
        std::snprintf(b, sizeof(b), "%d%%", pat().swing); lcdCell(swingBox(), "SWING", b);
        const int ep = editPattern(), pp = (int)std::lround(gp(gOUT_PATTERN));
        const bool running = gp(gOUT_STEP) >= 0.f;
        const R pr = patBox();
        box(pr, th->lcdBg); frame(pr, th->panelEdge, -1.f, th->panelEdgeWidth);
        label(pr.x + 10, pr.cy() + 1, "PAT", 10.f, th->lcdDim, ALIGN_LEFT | ALIGN_MIDDLE);
        if (running && pp != ep) std::snprintf(b, sizeof(b), "A%02d>A%02d", pp + 1, ep + 1); else std::snprintf(b, sizeof(b), "A%02d", ep + 1);
        number(pr.x + pr.w - 10, pr.cy() + 1, b, running && pp != ep ? 16.f : 20.f, th->lcdText, ALIGN_RIGHT | ALIGN_MIDDLE);
        const bool play = gp(gPLAY) > 0.5f;
        const R pb = playBtn();
        if (play) box(pb, th->selFill); else { box(pb, th->btnFill); frame(pb, th->btnEdge); }
        iconLabel(pb.cx(), pb.cy() + 1, play ? icStop : icPlay, play ? "STOP" : "PLAY", 12.f, play ? th->selText : th->btnText);
        const R tb = tempoBox();
        box(tb, th->lcdBg); frame(tb, th->panelEdge, -1.f, th->panelEdgeWidth);
        std::snprintf(b, sizeof(b), "%.1f", gp(gTEMPO));
        number(tb.x + 70, tb.cy() + 1, b, 20.f, th->lcdText, ALIGN_RIGHT | ALIGN_MIDDLE);
        label(tb.x + 75, tb.cy() + 2, "BPM", 10.f, th->lcdDim, ALIGN_LEFT | ALIGN_MIDDLE);
        {   // the input: level, FREEZE, CAPTURE / LIVE length
            const R ib = inBox();
            box(ib, th->lcdBg); frame(ib, th->panelEdge, -1.f, th->panelEdgeWidth);
            label(ib.cx(), ib.y + 11, "INPUT", 9.f, th->lcdDim);
            const float lv = clampf(gp(gOUT_IN), 0.f, 1.f), db = lv > 1e-4f ? 20.f * std::log10(lv) : -80.f;
            const float u = clampf((db + 48.f) / 48.f, 0.f, 1.f);
            const R m{ib.x + 8, ib.y + 24, ib.w - 16, 8};
            box(m, withAlpha(th->lcdDim, 0.25f), 2.f);
            if (u > 0.f) box({m.x, m.y, m.w * u, m.h}, db > -1.f ? th->lock : ledColor(), 2.f);
            const bool fz = gp(gFREEZE) > 0.5f;
            const R fb = freezeBtn();
            if (fz) box(fb, th->selFill); else { box(fb, th->btnFill); frame(fb, th->btnEdge); }
            label(fb.cx(), fb.y + 12, "LIVE", 9.f, fz ? th->selText : th->dim);
            label(fb.cx(), fb.y + 28, fz ? "FROZEN" : "FREEZE", 10.5f, fz ? th->selText : th->text);
            const int bars = capBars(gp(gCAPLEN));
            std::snprintf(b, sizeof(b), bars == 1 ? "%d BAR" : "%d BARS", bars);
            lcdCell(capLenBox(), "CAPTURE", b);
        }
        const R sk = skinBtn();
        box(sk, th->btnFill); frame(sk, th->btnEdge);
        label(sk.cx(), sk.y + 12, "SKIN / SIZE", 9.f, th->dim);
        std::snprintf(b, sizeof(b), "%s %d%%", th->name, zoomPercent());
        label(sk.cx(), sk.y + 28, b, 10.5f, th->text);
    }

    // a label that fits in maxW (smaller, then cut)
    std::string fitText(const std::string& s, float maxW, float& size)
    {
        useFont(th->fontLabel);
        std::string t = s;
        for (;;) {
            fontSize(textSize(size, th->fontLabel));
            DGL_NAMESPACE::Rectangle<float> bnd; textBounds(0, 0, t.c_str(), nullptr, bnd);
            if (bnd.getWidth() <= maxW || t.size() <= 3) break;
            if (size > 8.f) size -= 0.5f; else t = t.substr(0, t.size() - 3) + "..";
        }
        return t;
    }

    void drawTracks()
    {
        panelBox({14.f, 58.f, 236.f, 204.f});
        const uint32_t act = (uint32_t)std::lround(gp(gOUT_ACTIVE));
        char b[16];
        for (int t = 0; t < kTracks; ++t) {
            const bool sel = t == selTrack, muted = bank->kit[t].mute != 0;
            const R r = trackBtn(t);
            if (sel) box(r, th->selFill); else { box(r, th->btnFill); frame(r, th->btnEdge); }
            const Color fg = sel ? th->selText : (muted ? th->faint : th->text);
            std::snprintf(b, sizeof(b), "T%d", t + 1);
            number(r.x + 10, r.cy() + 1, b, 16.f, fg, ALIGN_LEFT | ALIGN_MIDDLE);
            float sz = 10.f;
            const std::string nm = fitText(srcName(t), r.w - 64.f, sz);
            label(r.x + 42, r.y + 15, nm.c_str(), sz, fg, ALIGN_LEFT | ALIGN_MIDDLE);
            std::snprintf(b, sizeof(b), "%d SLICES", bank->kit[t].nSlices);
            label(r.x + 42, r.y + 32, bank->kit[t].nSlices > 1 ? b : "1 SLICE", 8.5f, sel ? th->selText : th->dim, ALIGN_LEFT | ALIGN_MIDDLE);
            beginPath(); circle(r.x + r.w - 11, r.y + 13, 4.5f);
            fillColor((act >> t) & 1 ? ledColor() : Color(fg, Color(0, 0, 0, 0.f), 0.25f)); fill();
            const R mb = muteBtn(t);
            if (muted) box(mb, th->lock, 3.f); else { box(mb, th->btnFill, 3.f); frame(mb, th->btnEdge, 3.f); }
            label(mb.cx(), mb.cy() + 1, muted ? "OFF" : "ON", 8.5f, muted ? th->selText : th->btnText);
            const R tb = trigBtn(t);
            const bool held = drag == dkTrig && dragIndex == t;
            if (held) box(tb, th->accent, 3.f); else { box(tb, th->btnFill, 3.f); frame(tb, th->btnEdge, 3.f); }
            label(tb.cx(), tb.cy() + 1, "PLAY", 8.5f, held ? th->accentText : th->btnText);
        }
    }

    // waveform envelope cache of a track's material
    struct WaveCache { const SampleData* src = nullptr; std::vector<float> mn, mx; };
    WaveCache wave[kTracks];
    const WaveCache& waveOf(int t, int cols)
    {
        WaveCache& w = wave[t];
        const SampleData* s = srcOf(t);
        if (w.src == s && (int)w.mn.size() == cols) return w;
        w.src = s; w.mn.assign(cols, 0.f); w.mx.assign(cols, 0.f);
        if (!s || s->pcm.empty()) return w;
        const size_t n = s->pcm.size();
        for (int c = 0; c < cols; ++c) {
            const size_t a = n * c / cols, e = std::max(a + 1, n * (c + 1) / cols);
            float lo = 0.f, hi = 0.f;
            for (size_t i = a; i < e && i < n; ++i) { lo = std::min(lo, s->pcm[i]); hi = std::max(hi, s->pcm[i]); }
            w.mn[c] = lo; w.mx[c] = hi;
        }
        return w;
    }
    void drawWave()
    {
        panelBox({258.f, 58.f, 622.f, 204.f});
        const int t = selTrack;
        const Kit& kit = bank->kit[t];
        char b[64];
        std::snprintf(b, sizeof(b), "T%d", t + 1);
        number(270, 73, b, 16.f, th->text, ALIGN_LEFT | ALIGN_MIDDLE);
        float sz = 12.f;
        if (lockMode()) {
            std::snprintf(b, sizeof(b), "LOCK T%d STEP %d", lockTrack + 1, lockStep + 1);
            label(304, 73, b, 11.f, th->lock, ALIGN_LEFT | ALIGN_MIDDLE);
        } else {
            const std::string nm = fitText(srcName(t), 134.f, sz);
            label(304, 73, nm.c_str(), sz, th->text, ALIGN_LEFT | ALIGN_MIDDLE);
        }
        std::snprintf(b, sizeof(b), "MODE: %s", kModeNames[kit.mode % kGrainModes]);
        button(modeBtn(), b, false, 9.f);
        button(syncBtn(), "SYNC", kit.sync != 0, 9.f);
        button(legatoBtn(), "LEGATO", kit.legato != 0, 9.f);
        const SampleData* s = srcOf(t);
        const R wr = waveR();
        box(wr, th->lcdBg); frame(wr, th->panelEdge, -1.f, th->panelEdgeWidth);
        if (lockMode()) {
            frame(lockClearBtn(), th->lock); label(lockClearBtn().cx(), lockClearBtn().cy() + 1, "CLEAR LOCKS", 9.f, th->lock);
        } else if (liveTrack(t)) {
            const int bars = capBars(gp(gCAPLEN));
            std::snprintf(b, sizeof(b), bars == 1 ? "LAST %d BAR" : "LAST %d BARS", bars);
            label(868, 74, b, 10.f, th->dim, ALIGN_RIGHT | ALIGN_MIDDLE);
        } else if (s) {
            std::snprintf(b, sizeof(b), "%.2f S   %d %s", s->pcm.size() / s->sr, std::max<int>(1, kit.nSlices), kit.nSlices > 1 ? "SLICES" : "SLICE");
            label(868, 74, b, 10.f, th->dim, ALIGN_RIGHT | ALIGN_MIDDLE);
        }
        if (liveTrack(t)) drawLiveWave(t, wr);
        else if (!s || s->pcm.empty()) {
            label(wr.cx(), wr.cy() - 8, "NO SAMPLE ON THIS TRACK", 11.f, th->lcdDim);
            label(wr.cx(), wr.cy() + 12, "LOAD a WAV / AIFF file, drop one here, or choose a MATERIAL", 9.5f, th->lcdDim);
        } else {
            const int cols = (int)wr.w - 8;
            const WaveCache& w = waveOf(t, cols);
            // region of the selected slice
            const uint32_t frames = (uint32_t)s->pcm.size();
            const int slice = sliceOf(knobValue(tpSLICE), kit.nSlices);
            double ra, rb; sliceRegion(kit, slice, frames, ra, rb);
            const float x0 = wr.x + 4, xa = x0 + (float)(ra / frames) * cols, xb = x0 + (float)(rb / frames) * cols;
            if (slice >= 0) { beginPath(); rect(xa, wr.y + 2, xb - xa, wr.h - 4); fillColor(withAlpha(th->lcdText, 0.10f)); fill(); }
            const float cy = wr.cy(), hh = wr.h * 0.42f;
            beginPath();
            for (int c = 0; c < cols; ++c) { const float x = x0 + c; rect(x, cy - w.mx[c] * hh, 1.f, std::max(1.f, (w.mx[c] - w.mn[c]) * hh)); }
            fillColor(withAlpha(th->lcdText, 0.85f)); fill();
            // slices
            for (int i = 0; i < kit.nSlices; ++i) {
                const float x = x0 + kit.slice[i] * 1e-6f * cols;
                beginPath(); rect(x, wr.y + 2, 1.f, wr.h - 4); fillColor(withAlpha(th->lcdDim, i == slice ? 1.f : 0.6f)); fill();
                if (kit.nSlices > 1 && kit.nSlices <= 32) {
                    std::snprintf(b, sizeof(b), "%d", i + 1);
                    label(x + 3, wr.y + 9, b, 8.f, i == slice ? th->lcdText : th->lcdDim, ALIGN_LEFT | ALIGN_MIDDLE);
                }
            }
            // start position (POSITION inside the region)
            const float xp = xa + (xb - xa) * knobValue(tpPOSITION) / 127.f;
            beginPath(); moveTo(xp, wr.y + wr.h - 2); lineTo(xp - 5, wr.y + wr.h - 10); lineTo(xp + 5, wr.y + wr.h - 10); closePath(); fillColor(th->lock); fill();
            beginPath(); rect(xp, wr.y + 2, 1.f, wr.h - 4); fillColor(withAlpha(th->lock, 0.7f)); fill();
            // play head
            const float ph = gp(gOUT_POS1 + t);
            if (ph >= 0.f) {
                const float x = x0 + ph * cols;
                beginPath(); rect(x - 1, wr.y + 2, 2.f, wr.h - 4); fillColor(ledColor()); fill();
            }
        }
        button(loadBtn(), "LOAD", false, 10.5f);
        std::snprintf(b, sizeof(b), "MATERIAL: %s", kit.material ? kMaterialNames[std::min<int>(kit.material, kMatLive)] : "SAMPLE");
        button(matBtn(), b, kit.material == kMatLive, 9.5f);
        button(captureBtn(), "CAPTURE", false, 10.f);
        button(sliceModeBtn(), kit.sliceMode ? "EQUAL SLICES" : "TRANSIENTS", false, 9.5f);
        button(amtMinus(), "-", false, 12.f); button(amtPlus(), "+", false, 12.f);
        box(amtBox(), th->lcdBg);
        if (kit.sliceMode) std::snprintf(b, sizeof(b), "%d SLICES", std::max(1, (int)kit.sliceAmount)); else std::snprintf(b, sizeof(b), "SENS %d", kit.sliceAmount);
        label(amtBox().cx(), amtBox().cy() + 1, b, 10.f, th->lcdText);
        if (!smpMsg.empty()) label(868, 238, smpMsg.c_str(), 9.f, th->lock, ALIGN_RIGHT | ALIGN_MIDDLE);
    }

    // LIVE: the input window, the start (POSITION) and the play head, redrawn all the time
    std::vector<float> liveMn, liveMx;
    void drawLiveWave(int t, const R& wr)
    {
        const int cols = (int)wr.w - 8;
        if (!direct) { label(wr.cx(), wr.cy(), "LIVE INPUT (not reachable in this host)", 10.f, th->lcdDim); return; }
        liveMn.resize(cols); liveMx.resize(cols);
        direct->liveWave(liveMn.data(), liveMx.data(), cols);
        const float x0 = wr.x + 4, cy = wr.cy(), hh = wr.h * 0.42f;
        bool any = false; for (int c = 0; c < cols; ++c) if (liveMx[c] - liveMn[c] > 1e-3f) { any = true; break; }
        beginPath();
        for (int c = 0; c < cols; ++c) { const float x = x0 + c; rect(x, cy - liveMx[c] * hh, 1.f, std::max(1.f, (liveMx[c] - liveMn[c]) * hh)); }
        fillColor(withAlpha(gp(gFREEZE) > 0.5f ? th->lock : th->lcdText, 0.85f)); fill();
        if (!any) {
            label(wr.cx(), wr.cy() - 8, "NOTHING ON THE INPUT", 11.f, th->lcdDim);
            label(wr.cx(), wr.cy() + 12, "Route a track to KGRN-4's side-chain input in your music software", 9.5f, th->lcdDim);
        }
        const float xp = x0 + cols * knobValue(tpPOSITION) / 127.f;
        beginPath(); moveTo(xp, wr.y + wr.h - 2); lineTo(xp - 5, wr.y + wr.h - 10); lineTo(xp + 5, wr.y + wr.h - 10); closePath(); fillColor(th->lock); fill();
        beginPath(); rect(xp, wr.y + 2, 1.f, wr.h - 4); fillColor(withAlpha(th->lock, 0.7f)); fill();
        const float ph = gp(gOUT_POS1 + t);
        if (ph >= 0.f) { const float x = x0 + ph * cols; beginPath(); rect(x - 1, wr.y + 2, 2.f, wr.h - 4); fillColor(ledColor()); fill(); }
        label(wr.x + wr.w - 8, wr.y + 10, "NOW", 8.f, th->lcdDim, ALIGN_RIGHT | ALIGN_MIDDLE);
    }

    float knobValue(int k)
    {
        const int t = selTrack;
        if (lockMode() && lockTrack == t) { const uint8_t v = pat().locks[t][lockStep][k]; if (v != kNoLock) return v; }
        return tv(t, k);
    }
    bool knobLocked(int k) { return lockMode() && lockTrack == selTrack && pat().locks[selTrack][lockStep][k] != kNoLock; }
    void drawKnobs()
    {
        panelBox({14.f, 270.f, 866.f, 216.f});
        for (int gi = 0; gi < kGroups; ++gi) {
            const R a = knobR(kGroupStart[gi]), z = knobR(kGroupStart[gi + 1] - 1);
            label(a.x + 4, a.y - 8, kGroupNames[gi], 9.f, th->dim, ALIGN_LEFT | ALIGN_MIDDLE);
            const float lx = a.x + 4 + 62;
            if (z.x + z.w - 6 > lx) { beginPath(); rect(lx, a.y - 8, z.x + z.w - 6 - lx, 1.f); fillColor(withAlpha(th->dim, 0.35f)); fill(); }
        }
        const Kit& kit = bank->kit[selTrack];
        const int n = kit.nSlices;
        for (int k = 0; k < kTrackParams; ++k) {
            const float v = knobValue(k);
            drawKnob(knobR(k), v / 127.f, kTP[k].name, trackValueText(k, v, n, kit.mode, kit.sync != 0), knobLocked(k), drag == dkKnob && dragIndex == k, 19.f, 30.f, 64.f, 80.f);
        }
    }

    void drawSequencer()
    {
        panelBox({14.f, 486.f, 866.f, 222.f});
        Pattern& p = pat();
        char b[32];
        for (int i = 0; i < 4; ++i) {
            std::snprintf(b, sizeof(b), "%d-%d", i * 16 + 1, i * 16 + 16);
            bool used = false; for (int t = 0; t < kTracks; ++t) used = used || p.len[t] > i * 16;
            const R r = stepPageBtn(i);
            if (stepPg == i) button(r, b, true, 9.5f);
            else { box(r, th->btnFill); frame(r, th->btnEdge); label(r.cx(), r.cy() + 1, b, 9.5f, used ? th->btnText : th->faint); }
        }
        button(lenMinus(), "-", false, 12.f); button(lenPlus(), "+", false, 12.f);
        box(lenBox(), th->lcdBg); std::snprintf(b, sizeof(b), "LEN %d", p.len[selTrack]);
        label(lenBox().cx(), lenBox().cy() + 1, b, 10.f, th->lcdText);
        std::snprintf(b, sizeof(b), "SPEED %s", kSpeedNames[p.speed[selTrack] % kSpeeds]);
        button(speedBtn(), b, p.speed[selTrack] != 0, 9.5f);
        button(rotL(), "<", false, 11.f); button(rotR(), ">", false, 11.f);
        button(fillBtn(), "FILL", false, 9.5f);
        button(clearTrackBtn(), "CLEAR", false, 9.5f);
        button(copyTrackBtn(), "COPY", false, 9.5f);
        button(pasteTrackBtn(), "PASTE", false, 9.5f);
        if (!patMsg.empty()) label(868, 506, patMsg.c_str(), 9.f, th->lock, ALIGN_RIGHT | ALIGN_MIDDLE);
        for (int t = 0; t < kTracks; ++t) {
            const bool sel = t == selTrack, muted = bank->kit[t].mute != 0;
            const R rb = rowBtn(t);
            if (sel) box(rb, th->selFill); else { box(rb, th->btnFill); frame(rb, th->btnEdge); }
            const Color fg = sel ? th->selText : (muted ? th->faint : th->text);
            std::snprintf(b, sizeof(b), "T%d", t + 1);
            number(rb.x + 10, rb.y + 14, b, 14.f, fg, ALIGN_LEFT | ALIGN_MIDDLE);
            std::snprintf(b, sizeof(b), "%d  %s", p.len[t], kSpeedNames[p.speed[t] % kSpeeds]);
            label(rb.x + 10, rb.y + 30, b, 8.5f, sel ? th->selText : th->dim, ALIGN_LEFT | ALIGN_MIDDLE);
            const int now = trackStepNow(t);
            for (int i = 0; i < 16; ++i) {
                const int s = stepPg * 16 + i;
                const R r = stepCell(t, i);
                const bool inLen = s < p.len[t], on = p.on[t][s] != 0;
                const Color cellCol = inLen ? ((i / 4) % 2 ? th->stepAlt : th->stepFill) : Color(th->stepOff, th->bg, 0.4f);
                box(r, cellCol, 3.f);
                if (on && inLen) {
                    const Color lc = onColor();
                    if (th->glow) { beginPath(); rect(r.x - 3, r.y - 3, r.w + 6, r.h + 6); fillPaint(boxGradient(r.x, r.y, r.w, r.h, 3, 8, Color(lc, Color(0, 0, 0, 0.f), 0.45f), Color(0, 0, 0, 0.f))); fill(); }
                    box({r.x + 3, r.y + 3, r.w - 6, r.h - 6}, lc, 2.f);
                } else if (inLen) box({r.x + 4, r.y + 4, r.w - 8, 5}, th->ledOff, 1.f);
                if (inLen && p.hasLocks(t, s)) { beginPath(); circle(r.x + r.w - 8, r.y + r.h - 8, 2.8f); fillColor(on ? th->selText : th->lock); fill(); }
                if (inLen && p.locks[t][s][tpSLICE] != kNoLock && bank->kit[t].nSlices > 1) {
                    const int sl = sliceOf(p.locks[t][s][tpSLICE], bank->kit[t].nSlices);
                    std::snprintf(b, sizeof(b), "%s", sl < 0 ? "ALL" : std::to_string(sl + 1).c_str());
                    label(r.x + 7, r.y + r.h - 9, b, 8.f, on ? th->selText : th->lock, ALIGN_LEFT | ALIGN_MIDDLE);
                }
                if (s == now && inLen) frame(r, th->text, 3.f, 2.f);
                if (lockStep == s && lockTrack == t) frame({r.x - 2, r.y - 2, r.w + 4, r.h + 4}, th->lock, 4.f, 2.f);
            }
        }
        for (int i = 0; i < 16; ++i) {
            const R r = stepCell(kTracks - 1, i);
            std::snprintf(b, sizeof(b), "%d", stepPg * 16 + i + 1);
            label(r.cx(), r.y + r.h + 10, b, 8.f, i % 4 == 0 ? th->dim : th->faint);
        }
    }

    void drawRight()
    {
        panelBox({888.f, 58.f, 278.f, 190.f});
        label(900, 72, "PATTERNS", 10.f, th->dim, ALIGN_LEFT | ALIGN_MIDDLE);
        const int ep = editPattern(), pp = (int)std::lround(gp(gOUT_PATTERN));
        const bool running = gp(gOUT_STEP) >= 0.f;
        for (int i = 0; i < kPatterns; ++i) {
            char b[8]; std::snprintf(b, sizeof(b), "A%02d", i + 1);
            const R r = patBtn(i);
            if (i == ep) box(r, th->selFill); else { box(r, th->btnFill); frame(r, th->btnEdge); }
            bool any = false; for (int t = 0; t < kTracks && !any; ++t) any = !bank->patterns[i].trackEmpty(t);
            number(r.cx(), r.cy() + 1, b, 13.f, i == ep ? th->selText : (any ? th->text : th->faint));
            if (running && i == pp && pp != ep) frame(r, th->accent, -1.f, 2.f);
        }
        button(patCopy(), "COPY", false, 9.f); button(patPaste(), "PASTE", false, 9.f); button(patClear(), "CLEAR", false, 9.f);
        button(chainBtn(), "CHAIN", bank->chain.active(), 9.f);
        panelBox({888.f, 256.f, 278.f, 262.f});
        label(900, 270, "ECHO / REVERB / OUT", 10.f, th->dim, ALIGN_LEFT | ALIGN_MIDDLE);
        for (int i = 0; i < 5; ++i) {
            const int g = kFxG[i];
            drawKnob(gKnob(i), gp(g) / 127.f, kFxNames[i], globalText(g), false, drag == dkGlobal && dragIndex == i, 22.f, 38.f, 76.f, 93.f);
        }
        panelBox({888.f, 526.f - 8.f, 278.f, 190.f});
        const R pn = presetName();
        label(pn.x + 2, pn.y + 10, "PRESET", 9.f, th->dim, ALIGN_LEFT | ALIGN_MIDDLE);
        number(pn.x + 2, pn.y + 30, presetNameStr.c_str(), 15.f, th->text, ALIGN_LEFT | ALIGN_MIDDLE);
        box(presetBtn(), th->btnFill); frame(presetBtn(), th->text, -1.f, 1.5f);
        label(presetBtn().cx(), presetBtn().cy() + 1, "PRESETS", 11.f, th->text);
        button(saveBtn(), "SAVE AS PRESET", false, 10.f);
        const bool can = !undoStack.empty();
        if (can) button(undoBtn(), ("UNDO " + undoStack.back().what).c_str(), false, 10.f);
        else { box(undoBtn(), th->btnFill); frame(undoBtn(), Color(th->btnEdge, th->bg, 0.5f)); label(undoBtn().cx(), undoBtn().cy() + 1, "UNDO", 10.f, th->faint); }
    }
    static constexpr int kFxG[5] = {gREV_SIZE, gREV_TONE, gOUTPUT, gDLY_TIME, gDLY_FB};
    static constexpr const char* kFxNames[5] = {"REV SIZE", "REV TONE", "OUTPUT", "ECHO TIME", "FEEDBACK"};
    std::string globalText(int g) const
    {
        const float v = gp(g);
        char b[32];
        switch (g) {
        case gREV_SIZE: std::snprintf(b, sizeof(b), "%.1f S", 0.4f * std::pow(35.f, v / 127.f)); return b;
        case gREV_TONE: { const float hz = 1500.f * std::pow(9.3f, v / 127.f); std::snprintf(b, sizeof(b), "%.1fK", hz / 1000.f); return b; }
        case gDLY_TIME: return kDelTimeNames[bin8(v)];
        case gDLY_FB: std::snprintf(b, sizeof(b), "%d%%", (int)std::lround(v / 127.f * 92.f)); return b;
        default: { const float db = trimDb(v); if (db < -100.f) return "-INF"; std::snprintf(b, sizeof(b), "%+.1fdB", db); return b; }
        }
    }
    // ------------------------------------------------------------------ presets (factory + user files)
    static std::string userDir()
    {
       #ifdef _WIN32
        const std::string base = fs::env("APPDATA");
        if (base.empty()) return std::string();
        std::string d = base + "\\Kinetik"; fs::makeDir(d); d += "\\KGRN-4"; fs::makeDir(d); d += "\\Presets"; fs::makeDir(d);
        return d + "\\";
       #else
        const std::string base = fs::env("HOME");
        if (base.empty()) return std::string();
       #ifdef __APPLE__
        std::string d = base + "/Library/Application Support";
       #else
        std::string d = base + "/.config";
       #endif
        fs::makeDir(d); d += "/Kinetik"; fs::makeDir(d); d += "/KGRN-4"; fs::makeDir(d); d += "/Presets"; fs::makeDir(d);
        return d + "/";
       #endif
    }
    void scanUser()
    {
        userPresets.clear();
        const std::string d = userDir();
        if (d.empty()) return;
        for (const std::string& f : fs::list(d, ".kgrn")) userPresets.push_back(f.substr(0, f.size() - 5));
        std::sort(userPresets.begin(), userPresets.end());
    }
    void drawPresets()
    {
        const R bx = presetsBox();
        overlayBox(bx);
        number(100, 92, "Presets", 22.f, th->text, ALIGN_LEFT | ALIGN_MIDDLE);
        label(100, 120, "FACTORY", 10.f, th->dim, ALIGN_LEFT | ALIGN_MIDDLE);
        label(600, 120, "YOURS", 10.f, th->dim, ALIGN_LEFT | ALIGN_MIDDLE);
        for (int i = 0; i < kNumPresets && i < 15; ++i) {
            const R r = presetItem(0, i);
            const bool sel = presetNameStr == kPresets[i].name;
            if (sel) box(r, th->selFill); else box(r, th->btnFill);
            number(r.x + 10, r.cy() + 1, kPresets[i].name, 13.f, sel ? th->selText : th->text, ALIGN_LEFT | ALIGN_MIDDLE);
            label(r.x + r.w - 10, r.cy() + 1, kPresets[i].desc, 8.5f, sel ? th->selText : th->dim, ALIGN_RIGHT | ALIGN_MIDDLE);
        }
        if (userPresets.empty()) label(600, 145, "none yet: SAVE AS PRESET stores the knobs, the 16 patterns and your samples", 9.5f, th->faint, ALIGN_LEFT | ALIGN_MIDDLE);
        for (int i = 0; i < (int)userPresets.size() && i < 15; ++i) {
            const R r = presetItem(1, i);
            const bool sel = presetNameStr == userPresets[i];
            if (sel) box(r, th->selFill); else box(r, th->btnFill);
            number(r.x + 10, r.cy() + 1, userPresets[i].c_str(), 13.f, sel ? th->selText : th->text, ALIGN_LEFT | ALIGN_MIDDLE);
        }
        if (!presetMsg.empty()) label(100, 624, presetMsg.c_str(), 10.f, th->lock, ALIGN_LEFT | ALIGN_MIDDLE);
        button(presetsClose(), "CLOSE", true, 11.f);
    }
    void presetsClick(float x, float y)
    {
        for (int i = 0; i < kNumPresets && i < 15; ++i) if (presetItem(0, i).in(x, y)) { loadFactory(i); return; }
        for (int i = 0; i < (int)userPresets.size() && i < 15; ++i) if (presetItem(1, i).in(x, y)) { loadUser(userPresets[i]); return; }
        if (presetsClose().in(x, y) || !presetsBox().in(x, y)) { overlay = ovNone; presetMsg.clear(); }
        repaint();
    }
    void loadFactory(int i)
    {
        snapshot("preset");
        std::vector<float> nv(values, values + kTotalParams);
        std::unique_ptr<Bank> nb(new Bank());
        buildPreset(i, nv.data(), *nb);
        applyState(nv.data(), nb.get());
        setParamGesture(kGlobalBase + gPATTERN, 0.f);
        presetNameStr = kPresets[i].name; lockStep = -1; stepPg = 0;
        repaint();
    }
    void loadUser(const std::string& name)
    {
        FILE* fp = fs::open(userDir() + name + ".kgrn", "rb");
        if (!fp) { presetMsg = "Could not open " + name; repaint(); return; }
        std::string all; char buf[4096]; size_t n;
        while ((n = std::fread(buf, 1, sizeof(buf), fp)) > 0) all.append(buf, n);
        std::fclose(fp);
        if (all.rfind("KGRN4-PRESET", 0) != 0) { presetMsg = "Not a KGRN-4 preset: " + name; repaint(); return; }
        snapshot("preset");
        const bool v1 = all.rfind("KGRN4-PRESET 1", 0) == 0;       // 0.2: 16 knobs per track
        std::vector<float> nv(values, values + kTotalParams);
        std::string smpLines;
        const size_t bankPos = all.find("\nBANK\n");
        const std::string head = all.substr(0, bankPos == std::string::npos ? all.size() : bankPos);
        size_t st = 0;
        while (st < head.size()) {
            size_t en = head.find('\n', st); if (en == std::string::npos) en = head.size();
            int idx; float v;
            if (head.compare(st, 4, "SMP ") == 0) smpLines += head.substr(st + 4, en - st - 4) + "\n";
            else if (std::sscanf(head.c_str() + st, "V %d %f", &idx, &v) == 2 && std::isfinite(v)) {
                if (v1) {
                    static const int to[16] = {tpPOSITION, tpSLICE, tpSCAN, tpPITCH, tpSIZE, tpDENSITY, tpSPRAY, tpSPREAD,
                                               tpATTACK, tpDECAY, tpFILTER, tpRESO, tpVOLUME, tpPAN, tpDELAY, tpREVERB};
                    idx = idx < 64 ? tpIndex(idx / 16, to[idx % 16]) : (idx - 64 < 8 ? kGlobalBase + (idx - 64) : -1);
                }
                if (idx >= 0 && idx < kGlobalBase + gOUT_STEP) {
                    const int g = idx - kGlobalBase;
                    nv[idx] = g == gTEMPO ? clampf(v, 40.f, 240.f) : g == gCAPLEN ? clampf(v, 0.f, kCapLens - 1) : g == gFREEZE ? clampf(v, 0.f, 1.f) : clampf(v, 0.f, 127.f);
                }
            }
            st = en + 1;
        }
        std::unique_ptr<Bank> nb(new Bank());
        if (bankPos == std::string::npos || !deserializeBank(*nb, all.c_str() + bankPos + 6)) nb.reset(new Bank(*bank));
        if (!smpLines.empty()) {
            smp::decodeAll(smpLines.c_str(), [this](int t, SamplePtr d) { if (t >= 0 && t < kTracks && d) uiSmp[t] = std::move(d); });
            smpLines.pop_back();
            setState("smp", smpLines.c_str());
        }
        applyState(nv.data(), nb.get());
        presetNameStr = name; lockStep = -1;
        repaint();
    }
    void openSave() { saveName = presetNameStr == "Init" ? "" : presetNameStr; saveConfirm = false; overlay = ovSave; repaint(); }
    bool saveExists() const { return std::find(userPresets.begin(), userPresets.end(), saveName) != userPresets.end(); }
    void commitSave()
    {
        while (!saveName.empty() && saveName.back() == ' ') saveName.pop_back();
        if (saveName.empty()) return;
        if (saveExists() && !saveConfirm) { saveConfirm = true; repaint(); return; }
        FILE* fp = fs::open(userDir() + saveName + ".kgrn", "wb");
        if (!fp) { presetMsg = "Could not write the preset."; overlay = ovNone; repaint(); return; }
        std::fprintf(fp, "KGRN4-PRESET 2\nNAME %s\n", saveName.c_str());
        for (int i = 0; i < kGlobalBase + gOUT_STEP; ++i) {
            if (i == kGlobalBase + gPLAY || i == kGlobalBase + gPATTERN) continue;
            std::fprintf(fp, "V %d %.4f\n", i, values[i]);
        }
        for (int t = 0; t < kTracks; ++t)
            if (bank->kit[t].material == 0 && uiSmp[t]) std::fprintf(fp, "SMP %s\n", smp::encodeLine(t, *uiSmp[t]).c_str());
        std::fprintf(fp, "BANK\n%s", serializeBank(*bank).c_str());
        std::fclose(fp);
        presetNameStr = saveName; scanUser();
        overlay = ovNone; repaint();
    }
    void drawSave()
    {
        const R bx = saveBox();
        overlayBox(bx);
        number(320, 236, "Save as preset", 22.f, th->text, ALIGN_LEFT | ALIGN_MIDDLE);
        label(320, 270, "Name (type on the keyboard, Enter = save, Esc = cancel)", 10.5f, th->dim, ALIGN_LEFT | ALIGN_MIDDLE);
        const R f = saveField();
        box(f, th->lcdBg); frame(f, th->lcdText, -1.f, 1.5f);
        number(f.x + 16, f.cy() + 1, saveName.c_str(), 20.f, th->lcdText, ALIGN_LEFT | ALIGN_MIDDLE);
        if (saveExists()) label(320, 366, saveConfirm ? "Click SAVE again to replace it." : "A preset with this name exists: it will be replaced.", 10.5f, th->lock, ALIGN_LEFT | ALIGN_MIDDLE);
        else label(320, 366, "Saves every knob, the 16 patterns and your samples.", 10.5f, th->dim, ALIGN_LEFT | ALIGN_MIDDLE);
        button(saveCancel(), "CANCEL", false, 11.f);
        button(saveOk(), saveConfirm ? "REPLACE" : "SAVE", true, 12.f);
    }
    bool onCharacterInput(const CharacterInputEvent& ev) override
    {
        if (overlay != ovSave) return false;
        const uint c = ev.character;
        if (c >= 32 && c < 127 && saveName.size() < 28) {
            const char ch = (char)c;
            if ((ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') || (ch >= '0' && ch <= '9') || ch == ' ' || ch == '-' || ch == '_') { saveName.push_back(ch); saveConfirm = false; repaint(); }
        }
        return true;
    }

    // ------------------------------------------------------------------ skins window
    void drawSkins()
    {
        overlayBox(viewBox());
        number(264, 124, "Skin and size", 22.f, th->text, ALIGN_LEFT | ALIGN_MIDDLE);
        label(916, 124, "saved with the project and for new instances", 10.f, th->dim, ALIGN_RIGHT | ALIGN_MIDDLE);
        for (int i = 0; i < kNumThemes; ++i) {
            const R r = skinRow(i);
            const Theme& t = kThemes[i];
            box(r, t.bg, 4.f); frame(r, i == skin ? th->accent : th->btnEdge, 4.f, i == skin ? 3.f : 1.f);
            const Theme* keep = th; th = &t;
            number(r.x + 14, r.y + 22, t.name, 16.f, t.text, ALIGN_LEFT | ALIGN_MIDDLE);
            box({r.x + 14, r.y + 40, 120, 52}, t.lcdBg);
            label(r.x + 22, r.y + 52, "SIZE", 8.f, t.lcdDim, ALIGN_LEFT | ALIGN_MIDDLE);
            number(r.x + 22, r.y + 74, "75", 20.f, t.lcdText, ALIGN_LEFT | ALIGN_MIDDLE);
            drawKnob({r.x + 140, r.y + 26, 80, 110}, 0.6f, "", "", false, false);
            for (int k = 0; k < 4; ++k) {
                const R st{r.x + 224 + (k % 2) * 40.f, r.y + 40 + (k / 2) * 44.f, 36, 40};
                box(st, t.stepFill, 3.f);
                if (k == 0 || k == 3) box({st.x + 3, st.y + 3, st.w - 6, st.h - 6}, onColor(), 2.f);
            }
            th = keep;
        }
        label(264, 468, "WINDOW SIZE", 11.f, th->dim, ALIGN_LEFT | ALIGN_MIDDLE);
        static const int zooms[6] = {80, 100, 120, 140, 160, 200};
        for (int i = 0; i < 6; ++i) { char b[16]; std::snprintf(b, sizeof(b), "%d%%", zooms[i]); button(zoomBtn(i), b, std::abs(zoomPercent() - zooms[i]) <= 2, 12.f); }
        label(264, 546, "You can also drag the corner at the bottom right of the window to any size.", 10.5f, th->dim, ALIGN_LEFT | ALIGN_MIDDLE);
    }
    void drawGrip()
    {
        const Color c = drag == dkResize ? th->accent : Color(th->dim, th->bg, 0.2f);
        for (int i = 0; i < 3; ++i) {
            const float o = 6.f + i * 5.f;
            beginPath(); moveTo(W - 3, H - o); lineTo(W - o, H - 3); strokeColor(c); strokeWidth(1.6f); lineCap(ROUND); stroke();
        }
    }

    // ------------------------------------------------------------------ About / updates and licence windows
    void drawUpdate()
    {
        const R bx = updBox();
        overlayBox(bx);
        using C = upd::Checker;
        const C::State st = updater->getState();
        const upd::Manifest m = updater->manifest();
        number(320, bx.y + 36, "Kinetik KGRN-4", 22.f, th->text, ALIGN_LEFT | ALIGN_MIDDLE);
        label(860, bx.y + 36, (std::string("version ") + KN_VERSION).c_str(), 11.f, th->dim, ALIGN_RIGHT | ALIGN_MIDDLE);
        std::string l1, l2;
        switch (st) {
        case C::stChecking: l1 = "Checking for updates..."; break;
        case C::stAvailable: l1 = "Version " + m.version + " is available."; l2 = m.notes; break;
        case C::stUpToDate: l1 = "You have the latest version."; break;
        case C::stDownloading: l1 = "Downloading and checking the installer..."; break;
        case C::stInstalling: l1 = "Installing version " + m.version + "..."; l2 = "Accept the system prompt. Keep working: the music does not stop."; break;
        case C::stInstalled: l1 = "Version " + m.version + " is installed."; l2 = "It is used the next time the plug-in loads."; break;
        case C::stOpened: l1 = "The installer is open."; l2 = "Close your music software before you finish the installation, then open it again."; break;
        case C::stFailed: l1 = updater->message(); l2 = "Check your internet connection and try again."; break;
        default: l1 = "Click CHECK NOW to look for a new version."; break;
        }
        label(320, bx.y + 86, l1.c_str(), 13.f, th->text, ALIGN_LEFT | ALIGN_MIDDLE);
        if (!l2.empty()) { fontFace(NANOVG_DEJAVU_SANS_TTF); fontSize(12.5f); fillColor(th->dim); textAlign(ALIGN_LEFT | ALIGN_TOP); textBox(320, bx.y + 104, 540, l2.c_str(), nullptr); }
        button(updAuto(), updater->autoCheck() ? "AUTO CHECK: ON" : "AUTO CHECK: OFF", updater->autoCheck(), 10.5f);
        button(updAutoInst(), updater->autoInstall() ? "AUTO INSTALL: ON" : "AUTO INSTALL: OFF", updater->autoInstall(), 10.5f);
        button(updLic(), licInfo.valid ? "LICENCE: REGISTERED" : "ENTER A LICENCE", false, 10.5f);
        const bool avail = st == C::stAvailable, busy = st == C::stChecking || st == C::stDownloading || st == C::stInstalling;
        button(updMain(), busy ? "PLEASE WAIT..." : (avail ? (m.url.empty() ? "OPEN DOWNLOAD PAGE" : "DOWNLOAD & INSTALL") : "CHECK NOW"), avail, 11.f);
        if (avail) button(updSkip(), "SKIP THIS ONE", false, 10.5f);
        button(updClose(), "CLOSE", false, 11.f);
    }
    bool updateClick(float x, float y)
    {
        using C = upd::Checker;
        const C::State st = updater->getState();
        if (updMain().in(x, y)) { if (st == C::stAvailable) updater->install(); else if (st != C::stChecking && st != C::stDownloading && st != C::stInstalling) updater->check(true); }
        else if (st == C::stAvailable && updSkip().in(x, y)) updater->skipThisVersion();
        else if (updAuto().in(x, y)) updater->setAutoCheck(!updater->autoCheck());
        else if (updAutoInst().in(x, y)) updater->setAutoInstall(!updater->autoInstall());
        else if (updLic().in(x, y)) { overlay = ovLicense; licMsg.clear(); }
        else if (updClose().in(x, y) || !updBox().in(x, y)) overlay = ovNone;
        repaint(); return true;
    }
    void drawLicense()
    {
        const R bx = licBox();
        overlayBox(bx);
        number(320, bx.y + 36, licInfo.valid ? "Registered" : "Register KGRN-4", 22.f, th->text, ALIGN_LEFT | ALIGN_MIDDLE);
        if (licInfo.valid) {
            label(320, bx.y + 82, ("Licensed to " + licInfo.name).c_str(), 13.f, th->text, ALIGN_LEFT | ALIGN_MIDDLE);
            label(320, bx.y + 108, licInfo.email.c_str(), 11.f, th->dim, ALIGN_LEFT | ALIGN_MIDDLE);
            label(320, bx.y + 134, ("Licence " + licInfo.product + "  -  order " + licInfo.order + "  -  " + licInfo.issued).c_str(), 10.5f, th->dim, ALIGN_LEFT | ALIGN_MIDDLE);
        } else {
            label(320, bx.y + 80, "This copy runs in demo mode: the sound drops out for 3 seconds every 45 seconds.", 10.5f, th->text, ALIGN_LEFT | ALIGN_MIDDLE);
            label(320, bx.y + 106, "After your purchase you receive a licence file by e-mail.", 10.5f, th->dim, ALIGN_LEFT | ALIGN_MIDDLE);
            label(320, bx.y + 130, "Load that file, or copy its text and press PASTE LICENSE (or Ctrl/Cmd+V).", 10.5f, th->dim, ALIGN_LEFT | ALIGN_MIDDLE);
        }
        label(320, bx.y + 210, licMsg.c_str(), 10.5f, th->lock, ALIGN_LEFT | ALIGN_MIDDLE);
        button(licLoad(), "LOAD LICENSE FILE", false, 10.5f);
        button(licPaste(), "PASTE LICENSE", false, 10.5f);
        button(licClose(), "CLOSE", true, 11.f);
    }
    void applyLicenseText(const std::string& t)
    {
        if (t.empty()) { licMsg = "Nothing to read."; repaint(); return; }
        const lic::Info i = lic::install(t, "KGRN-4");
        if (i.valid) { licInfo = i; licMsg = "Licence installed. The demo limits are removed."; setState("cmd", "lic"); }
        else licMsg = i.error;
        repaint();
    }
    void pasteLicense()
    {
        size_t n = 0;
        const void* d = getWindow().getClipboard(n);
        if (d == nullptr || n == 0) { licMsg = "The clipboard is empty. Copy the licence text first."; repaint(); return; }
        std::string t((const char*)d, n);
        while (!t.empty() && t.back() == '\0') t.pop_back();
        applyLicenseText(t);
    }
    void uiFileBrowserSelected(const char* filename) override
    {
        if (filename == nullptr) return;
        std::string text;
        if (!lic::readFile(filename, text)) { licMsg = "Could not read that file."; repaint(); return; }
        applyLicenseText(text);
    }
    bool licenseClick(float x, float y)
    {
        if (licLoad().in(x, y)) {
            FileBrowserOptions o; o.title = "Load your Kinetik licence";
            if (!openFileBrowser(o)) licMsg = "The file browser is not available here: use PASTE LICENSE.";
            repaint(); return true;
        }
        if (licPaste().in(x, y)) { pasteLicense(); return true; }
        if (licClose().in(x, y) || !licBox().in(x, y)) { overlay = ovNone; licMsg.clear(); repaint(); }
        return true;
    }

    // ------------------------------------------------------------------ whole-state helpers (presets, random, undo)
    void applyState(const float* nv, const Bank* nb)
    {
        const bool was = restoring; restoring = true;
        for (int i = 0; i < kGlobalBase + gOUT_STEP; ++i) {
            if (i == kGlobalBase + gPLAY || i == kGlobalBase + gPATTERN) continue;
            if (std::fabs(values[i] - nv[i]) > 1e-4f) setParamGesture((uint32_t)i, nv[i]);
        }
        if (nb) { *bank = *nb; setState("bank", serializeBank(*bank).c_str()); }
        restoring = was;
        repaint();
    }
    struct Snap { std::vector<float> values; std::string bank; std::string what; };
    static double nowSec() { return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count(); }
    Snap capture(const char* what) const { Snap s; s.values.assign(values, values + kTotalParams); s.bank = serializeBank(*bank); s.what = what; return s; }
    void snapshot(const char* what)
    {
        if (restoring) return;
        undoStack.push_back(capture(what));
        if (undoStack.size() > 40) undoStack.pop_front();
        redoStack.clear();
        lastSnapAt = nowSec(); lastSnapWhat = what;
    }
    void autoSnap(const char* what, double window)
    {
        if (restoring) return;
        const double t = nowSec();
        if (t - lastSnapAt < 0.15 || (lastSnapWhat == what && t - lastSnapAt < window)) { if (lastSnapWhat == what) lastSnapAt = t; return; }
        snapshot(what);
    }
    void restoreSnap(const Snap& s)
    {
        std::unique_ptr<Bank> b(new Bank()); deserializeBank(*b, s.bank.c_str());
        std::vector<float> v = s.values; applyState(v.data(), b.get());
        lastSnapAt = -10.0; lastSnapWhat.clear();
    }
    void undo() { if (undoStack.empty()) return; Snap s = std::move(undoStack.back()); undoStack.pop_back(); redoStack.push_back(capture(s.what.c_str())); restoreSnap(s); repaint(); }
    void redo() { if (redoStack.empty()) return; Snap s = std::move(redoStack.back()); redoStack.pop_back(); undoStack.push_back(capture(s.what.c_str())); restoreSnap(s); repaint(); }

    void drawTip()
    {
        if (tipText.empty()) return;
        const float wMax = 300.f;
        fontFace(NANOVG_DEJAVU_SANS_TTF); fontSize(12.f); textAlign(ALIGN_LEFT | ALIGN_TOP); textLineHeight(1.25f);
        float b[4]; textBoxBounds(0, 0, wMax, tipText.c_str(), nullptr, b);
        const float w = b[2] - b[0] + 20.f, h = b[3] - b[1] + 14.f;
        float x = hovX + 14.f, y = hovY + 18.f;
        if (x + w > W - 6) x = W - 6 - w;
        if (y + h > H - 6) y = hovY - h - 10.f;
        beginPath(); roundedRect(x + 2, y + 3, w, h, 6); fillColor(Color(0, 0, 0, 0.25f)); fill();
        box({x, y, w, h}, th->panel, 6.f); frame({x, y, w, h}, th->text, 6.f);
        fillColor(th->text); textBox(x + 10, y + 7, wMax, tipText.c_str(), nullptr);
    }
    // ------------------------------------------------------------------ samples and materials
    static std::string baseName(const std::string& p)
    {
        const size_t a = p.find_last_of("/\\");
        std::string n = a == std::string::npos ? p : p.substr(a + 1);
        const size_t d = n.find_last_of('.');
        return d == std::string::npos ? n : n.substr(0, d);
    }
    // WAV / AIFF file -> mono sample normalized to -1 dBFS (nullptr if it cannot be read)
    static std::shared_ptr<SampleData> decodeSampleFile(const std::string& path)
    {
        std::vector<uint8_t> bytes;
        if (FILE* fp = fs::open(path, "rb")) {
            uint8_t buf[65536]; size_t n;
            while ((n = std::fread(buf, 1, sizeof(buf), fp)) > 0 && bytes.size() < 400u * 1024u * 1024u) bytes.insert(bytes.end(), buf, buf + n);
            std::fclose(fp);
        }
        unsigned int ch = 0, rate = 0; drwav_uint64 frames = 0;
        float* pcm = bytes.empty() ? nullptr : drwav_open_memory_and_read_pcm_frames_f32(bytes.data(), bytes.size(), &ch, &rate, &frames, nullptr);
        if (pcm == nullptr || ch == 0 || rate == 0 || frames == 0) { if (pcm) drwav_free(pcm, nullptr); return nullptr; }
        auto d = std::make_shared<SampleData>();
        d->sr = rate;
        d->name = smp::cleanSampleName(baseName(path));
        const drwav_uint64 n = std::min<drwav_uint64>(frames, (drwav_uint64)(kMaxSampleSeconds * rate));
        d->pcm.resize((size_t)n);
        const bool st = ch >= 2;                    // stereo files stay stereo (the first two channels)
        if (st) { d->l.resize((size_t)n); d->r.resize((size_t)n); }
        float peak = 0.f;
        for (drwav_uint64 i = 0; i < n; ++i) {
            float v = 0.f;
            for (unsigned int c = 0; c < ch; ++c) v += pcm[i * ch + c];
            v /= (float)ch;
            d->pcm[(size_t)i] = v;
            if (st) { d->l[(size_t)i] = pcm[i * ch]; d->r[(size_t)i] = pcm[i * ch + 1]; peak = std::max(peak, std::max(std::fabs(d->l[(size_t)i]), std::fabs(d->r[(size_t)i]))); }
            else peak = std::max(peak, std::fabs(v));
        }
        drwav_free(pcm, nullptr);
        if (peak > 1e-4f) { const float g = 0.89f / peak; for (float& v : d->pcm) v *= g; for (float& v : d->l) v *= g; for (float& v : d->r) v *= g; }
        return d;
    }
    void sendKit(int t) { sendLines(kitLines(*bank, t)); }
    void reslice(int t) { sliceKit(bank->kit[t], srcOf(t)); sendKit(t); repaint(); }
    void setMaterial(int t, int m)
    {
        bank->kit[t].material = (uint8_t)clampi(m, 0, kMatLive);
        sliceKit(bank->kit[t], srcOf(t));
        sendKit(t);
        repaint();
    }
    bool loadSampleFile(const std::string& path, int t)
    {
        auto d = decodeSampleFile(path);
        if (!d) { showSmpMsg("Could not read " + baseName(path) + " (WAV or AIFF only)"); return false; }
        return installSample(t, d);
    }
    // CAPTURE: the last bars of the input become the track's own sample (sliced, saved with the project)
    void captureTo(int t)
    {
        if (!direct) { showSmpMsg("CAPTURE is not available in this host"); return; }
        const int bars = capBars(gp(gCAPLEN));
        SamplePtr c = direct->captureInput(bars);
        if (!c) { showSmpMsg("Nothing on the input: route a track to the side-chain input of KGRN-4"); return; }
        auto d = std::make_shared<SampleData>(*c);
        static int n = 0;
        char nm[48]; std::snprintf(nm, sizeof(nm), "CAPTURE %d (%d %s)", ++n, bars, bars == 1 ? "BAR" : "BARS");
        d->name = nm;
        installSample(t, d);
        showSmpMsg(std::string("Captured into T") + std::to_string(t + 1));
    }
    bool installSample(int t, std::shared_ptr<SampleData> d)
    {
        uiSmp[t] = d;
        setState("smp", smp::encodeLine(t, *d).c_str());
        selTrack = t;
        // a new sample: the slices and the start refer to the old one
        setParamGesture(tpIndex(t, tpSLICE), 0.f);
        setParamGesture(tpIndex(t, tpPOSITION), 0.f);
        bank->kit[t].material = 0;
        reslice(t);
        smpMsg.clear();
        presetNameStr = presetNameStr == "Init" ? presetNameStr : presetNameStr;
        return true;
    }
    void showSmpMsg(const std::string& m) { smpMsg = m; smpMsgUntil = nowSec() + 4.0; repaint(); }
    void fileDropped(const std::string& path, double x, double y)
    {
        std::string ext = path.substr(path.find_last_of('.') == std::string::npos ? path.size() : path.find_last_of('.'));
        for (auto& c : ext) c = (char)std::tolower((unsigned char)c);
        if (ext == ".kinetik-license" || ext == ".txt") {
            std::string text;
            if (lic::readFile(path, text)) { overlay = ovLicense; applyLicenseText(text); }
            return;
        }
        int t = selTrack;
        for (int i = 0; i < kTracks; ++i) if (trackBtn(i).in((float)x, (float)y) || rowBtn(i).in((float)x, (float)y)) t = i;
        overlay = ovNone;
        loadSampleFile(path, t);
    }
    void openSampleBrowser()
    {
        FileBrowserOptions o;
        o.title = "Load a sample (WAV or AIFF)";
        browseSample = true;
        if (!openFileBrowser(o)) { browseSample = false; showSmpMsg("File browser not available: drop a file on the window"); }
        repaint();
    }
    // click in the waveform: the slice under the mouse (if the sample is sliced) and the start inside it
    void waveClick(float x, bool pickSlice)
    {
        const int t = selTrack;
        const R wr = waveR();
        const int cols = (int)wr.w - 8;
        if (liveTrack(t)) { setKnob(tpPOSITION, std::round(clampf((x - wr.x - 4) / cols, 0.f, 1.f) * 127.f)); return; }
        const SampleData* s = srcOf(t);
        if (!s || s->pcm.empty()) return;
        const double f = clampf((x - wr.x - 4) / cols, 0.f, 0.99999f);
        const Kit& kit = bank->kit[t];
        int slice = sliceOf(knobValue(tpSLICE), kit.nSlices);
        if (pickSlice && slice >= 0) {
            slice = 0;
            for (int i = 0; i < kit.nSlices; ++i) if (f * 1e6 >= kit.slice[i]) slice = i;
            setKnob(tpSLICE, std::round(sliceKnob(slice, kit.nSlices)));
        }
        double ra, rb; sliceRegion(kit, slice, (uint32_t)s->pcm.size(), ra, rb);
        const double pos = (f * s->pcm.size() - ra) / std::max(1.0, rb - ra);
        setKnob(tpPOSITION, std::round(clampf((float)pos, 0.f, 1.f) * 127.f));
    }

    // a page past the end of the selected track makes the track (and the pattern) that long
    void showPage(int i)
    {
        const int need = (i + 1) * 16;
        if (pat().len[selTrack] < need) {
            std::string c = "L " + std::to_string(editPattern()) + " " + std::to_string(selTrack) + " " + std::to_string(need) + " " + std::to_string(pat().speed[selTrack]);
            if (pat().length < need) c += "\nP " + std::to_string(editPattern()) + " " + std::to_string(need) + " " + std::to_string(pat().swing);
            sendLines(c);
        }
        stepPg = i; repaint();
    }
    int chainPlayingPattern() const { return gp(gOUT_STEP) >= 0.f ? (int)std::lround(gp(gOUT_PATTERN)) : -1; }
    // ------------------------------------------------------------------ pattern CHAIN window (same in every Kinetik plug-in)
    R chainBox() const { return {240.f, 110.f, 700.f, 470.f}; }
    R chainOnBtn() const { return {700.f, 132.f, 216.f, 36.f}; }
    R chainAddBtn(int i) const { return {264.f + (i % 8) * 82.f, 214.f + (i / 8) * 40.f, 76.f, 34.f}; }
    R chainChip(int i) const { return {264.f + (i % 8) * 82.f, 340.f + (i / 8) * 52.f, 76.f, 46.f}; }
    R chainClearBtn() const { return {264.f, 524.f, 140.f, 36.f}; }
    R chainCloseBtn() const { return {796.f, 524.f, 120.f, 36.f}; }
    void sendChain(const Chain& c) { sendCmd(c.line()); repaint(); }
    void drawChain()
    {
        const R bx = chainBox();
        overlayBox(bx);
        const Chain& c = bank->chain;
        number(264, bx.y + 40, "Pattern chain", 22.f, th->text, ALIGN_LEFT | ALIGN_MIDDLE);
        button(chainOnBtn(), c.on ? "CHAIN ON" : "CHAIN OFF", c.on != 0, 11.f);
        label(264, 198, "ADD A PATTERN AT THE END", 9.5f, th->dim, ALIGN_LEFT | ALIGN_MIDDLE);
        char b[24];
        for (int i = 0; i < kPatterns; ++i) {
            std::snprintf(b, sizeof(b), "A%02d", i + 1);
            const R r = chainAddBtn(i);
            box(r, th->btnFill); frame(r, th->btnEdge);
            number(r.cx(), r.cy() + 1, b, 13.f, th->text);
        }
        label(264, 322, "THE CHAIN (CLICK = MORE REPEATS, RIGHT-CLICK = REMOVE)", 9.5f, th->dim, ALIGN_LEFT | ALIGN_MIDDLE);
        if (c.n == 0) label(264, 362, "empty: add patterns above, then switch CHAIN ON", 10.f, th->faint, ALIGN_LEFT | ALIGN_MIDDLE);
        const int playing = chainPlayingPattern();
        for (int i = 0; i < c.n; ++i) {
            const R r = chainChip(i);
            const bool now = c.on && playing == c.pat[i];
            if (now) box(r, th->selFill); else { box(r, th->btnFill); frame(r, th->btnEdge); }
            std::snprintf(b, sizeof(b), "A%02d", c.pat[i] + 1);
            number(r.cx(), r.y + 17, b, 13.f, now ? th->selText : th->text);
            std::snprintf(b, sizeof(b), "x%d", c.rep[i]);
            label(r.cx(), r.y + 35, b, 9.5f, now ? th->selText : th->dim);
        }
        button(chainClearBtn(), "CLEAR", false, 10.5f);
        button(chainCloseBtn(), "CLOSE", true, 11.f);
    }
    bool chainClick(float x, float y, bool right)
    {
        Chain c = bank->chain;
        if (chainOnBtn().in(x, y)) { c.on = c.on ? 0 : (c.n > 0 ? 1 : 0); sendChain(c); return true; }
        for (int i = 0; i < kPatterns; ++i) if (chainAddBtn(i).in(x, y)) { if (c.n < Chain::kMax) { c.add(i, 1); sendChain(c); } return true; }
        for (int i = 0; i < c.n; ++i) if (chainChip(i).in(x, y)) {
            if (right) c.remove(i); else c.rep[i] = (uint8_t)(c.rep[i] >= 16 ? 1 : c.rep[i] + 1);
            sendChain(c); return true;
        }
        if (chainClearBtn().in(x, y)) { c.clear(); sendChain(c); return true; }
        if (chainCloseBtn().in(x, y) || !chainBox().in(x, y)) { overlay = ovNone; repaint(); }
        return true;
    }

    // ------------------------------------------------------------------ tooltips
    std::string tipAt(float x, float y)
    {
        for (int t = 0; t < kTracks; ++t) {
            if (trackBtn(t).in(x, y)) return "Select the track (drop a sample on it to load it there).";
            if (muteBtn(t).in(x, y)) return "ON / OFF: mute the track without losing its steps.";
            if (trigBtn(t).in(x, y)) return "PLAY: hear the track while you hold the mouse button (MIDI C3 to D#3 play tracks 1 to 4).";
            if (rowBtn(t).in(x, y)) return "Select the track; the number is its length and speed.";
            for (int i = 0; i < 16; ++i) if (stepCell(t, i).in(x, y))
                return "Click: a note on this step (drag to paint). Right-click: lock knobs on this step (SLICE, PITCH, POSITION... any knob).";
        }
        if (waveR().in(x, y)) return liveTrack(selTrack) ? "LIVE: the last bars of the input (right = now). Click: where the play head starts (POSITION)."
                                                         : "Click: the slice under the mouse and where the play head starts in it. Drag: move the start.";
        if (loadBtn().in(x, y)) return "Load a WAV or AIFF file on the selected track (or drop one on the window).";
        if (matBtn().in(x, y)) return "Built-in materials to play without your own samples, or LIVE: the audio input, granulated as it comes. Click = next, right-click = previous (SAMPLE = your file).";
        if (captureBtn().in(x, y)) return "CAPTURE: the last bars of the input (complete bars while playing, see CAPTURE at the top) become this track's sample, sliced and saved with the project.";
        if (modeBtn().in(x, y)) return std::string(kModeHelp[bank->kit[selTrack].mode % kGrainModes]) + ". Click = next mode, right-click = previous.";
        if (syncBtn().in(x, y)) return "SYNC: SIZE in note values (1/64 to 2 bars); in CLOUD, regular grains fall on the tempo grid.";
        if (legatoBtn().in(x, y)) return "LEGATO: a step does not start the sound again, it only brings its locks (pitch, position, any knob): the cloud goes on and changes.";
        if (inBox().in(x, y)) return "Input level. KGRN-4 hears a track routed to its side-chain input (LIVE tracks, CAPTURE).";
        if (freezeBtn().in(x, y)) return "FREEZE: the input buffer stops recording; LIVE tracks keep playing what it holds.";
        if (capLenBox().in(x, y)) return "How much of the input CAPTURE takes and LIVE tracks see: 1, 2, 4 or 8 bars. Click / scroll to change.";
        if (sliceModeBtn().in(x, y)) return "How the sample is cut: at its transients (hits) or into equal slices.";
        if (amtBox().in(x, y) || amtMinus().in(x, y) || amtPlus().in(x, y)) return "TRANSIENTS: sensitivity (more = more slices). EQUAL: number of slices.";
        for (int i = 0; i < 4; ++i) if (stepPageBtn(i).in(x, y)) return "Show steps 1-16, 17-32, 33-48 or 49-64. A page past the end makes the selected track (and the pattern) that long.";
        if (chainBtn().in(x, y)) return "CHAIN: play a list of patterns in a loop, each a number of times.";
        if (lenBox().in(x, y) || lenMinus().in(x, y) || lenPlus().in(x, y)) return "Length of the selected track (1-64 steps): tracks of different lengths drift against each other.";
        if (speedBtn().in(x, y)) return "Speed of the selected track's steps (1x = 16ths). Click = next, right-click = previous.";
        if (rotL().in(x, y) || rotR().in(x, y)) return "Shift the selected track's steps (and their locks) one step left or right.";
        if (fillBtn().in(x, y)) return "A note on every step of the selected track.";
        if (clearTrackBtn().in(x, y)) return "Clear the selected track's steps and locks.";
        if (copyTrackBtn().in(x, y) || pasteTrackBtn().in(x, y)) return "Copy the selected track's steps and locks, then paste them on another track or pattern.";
        if (patLenBox().in(x, y)) return "Pattern length (16ths): a pattern chosen while playing starts when this length ends. Drag or scroll.";
        if (swingBox().in(x, y)) return "Swing: delays every second 16th (50% = straight). Drag or scroll.";
        if (playBtn().in(x, y)) return "Internal play when your music software is stopped (KGRN-4 follows its transport when it plays).";
        if (tempoBox().in(x, y)) return "Internal tempo. Drag or scroll.";
        for (int k = 0; k < kTrackParams; ++k) if (knobR(k).in(x, y)) return std::string(kTP[k].name) + ": " + kTP[k].help + ". Double-click = reset; with a step locked it sets that step only.";
        for (int i = 0; i < 5; ++i) if (gKnob(i).in(x, y)) {
            static const char* t[5] = {"Reverb decay time.", "Reverb brightness.", "Output level of KGRN-4.", "Echo time in tempo.", "Echo repeats (ping-pong)."};
            return t[i];
        }
        for (int i = 0; i < kPatterns; ++i) if (patBtn(i).in(x, y)) return "Choose the pattern (while playing it starts at the end of the current one). MIDI C4 to D#5 choose A01 to A16.";
        if (undoBtn().in(x, y)) return "Undo the last change (Ctrl/Cmd+Z). Right-click = redo. Loading a sample is not undone.";
        if (presetBtn().in(x, y)) return "Factory presets and your own.";
        if (saveBtn().in(x, y)) return "Save the knobs, the 16 patterns and your samples as a preset of your own.";
        if (skinBtn().in(x, y)) return "Skin and window size. Right-click = next skin.";
        if (logoArea().in(x, y)) return "About KGRN-4, updates and licence.";
        return std::string();
    }

    // ------------------------------------------------------------------ editing
    void setStep(int t, int s, bool on)
    {
        if ((pat().on[t][s] != 0) == on) return;
        char b[48]; std::snprintf(b, sizeof(b), "s %d %d %d %d", editPattern(), t, s, on ? 1 : 0); sendCmd(b);
    }
    bool knobIsHostParam() const { return !(lockMode() && lockTrack == selTrack); }
    void setKnob(int k, float v)
    {
        v = clampf(v, 0.f, 127.f);
        if (!knobIsHostParam()) {
            const int iv = (int)std::lround(v);
            if (pat().locks[selTrack][lockStep][k] == iv) return;
            char b[48]; std::snprintf(b, sizeof(b), "k %d %d %d %d %d", editPattern(), selTrack, lockStep, k, iv); sendCmd(b);
        } else setParam(tpIndex(selTrack, k), v);
        repaint();
    }
    void setTrackLen(int n) { pat().len[selTrack] = (uint8_t)clampi(n, 1, kSteps); sendTrackSettings(selTrack); stepPg = std::min(stepPg, (pat().len[selTrack] - 1) / 16); repaint(); }
    void showPatMsg(const std::string& m) { patMsg = m; patMsgUntil = nowSec() + 2.0; repaint(); }
    void selectTrack(int t) { selTrack = t; if (lockTrack != t) lockStep = -1; repaint(); }

    bool onKeyboard(const KeyboardEvent& ev) override
    {
        if (overlay == ovLicense) {
            if (ev.press && (ev.key == 'v' || ev.key == 'V') && (ev.mod & (kModifierControl | kModifierSuper))) pasteLicense();
            else if (ev.press && ev.key == kKeyEscape) { overlay = ovNone; licMsg.clear(); repaint(); }
            return true;
        }
        if (overlay == ovSave) {
            if (ev.press) {
                if (ev.key == kKeyEscape) { overlay = ovNone; repaint(); }
                else if (ev.key == kKeyEnter || ev.key == '\n') commitSave();
                else if (ev.key == kKeyBackspace || ev.key == kKeyDelete) { if (!saveName.empty()) { saveName.pop_back(); saveConfirm = false; repaint(); } }
            }
            return true;
        }
        if (ev.press && (ev.mod & (kModifierControl | kModifierSuper)) && overlay == ovNone) {
            if (ev.key == 'z' || ev.key == 'Z') { if (ev.mod & kModifierShift) redo(); else undo(); return true; }
            if (ev.key == 'y' || ev.key == 'Y') { redo(); return true; }
        }
        if (ev.press && ev.key == kKeyEscape) { if (overlay != ovNone) overlay = ovNone; else lockStep = -1; repaint(); return true; }
        return false;
    }

    bool onMouse(const MouseEvent& ev) override
    {
        const float x = ev.pos.getX() / sx, y = ev.pos.getY() / sx;
        if (tipShown) { tipShown = false; repaint(); }
        tipBlockUntil = nowSec() + 1.0;
        if (!ev.press) {
            if (drag == dkKnob && knobIsHostParam()) editParameter(tpIndex(selTrack, dragIndex), false);
            if (drag == dkGlobal) editParameter(kGlobalBase + kFxG[dragIndex], false);
            if (drag == dkTempo) editParameter(kGlobalBase + gTEMPO, false);
            if (drag == dkPatLen || drag == dkSwing) sendPatternSettings();
            if (drag == dkTrackLen) sendTrackSettings(selTrack);
            if (drag == dkTrig) trigTrack(dragIndex, false);
            if (drag == dkResize) saveView();
            if (drag != dkNone) { drag = dkNone; repaint(); }
            return true;
        }
        const bool dbl = (ev.time - lastClickTime) < 350 && std::fabs(x - lastX) < 4 && std::fabs(y - lastY) < 4;
        lastClickTime = ev.time; lastX = x; lastY = y;
        const bool right = ev.button == kMouseButtonRight;

        if (gripR().in(x, y) && overlay == ovNone) { drag = dkResize; resizeStartW = getWidth(); grabX = ev.pos.getX(); grabY = ev.pos.getY(); return true; }
        if (overlay == ovPresets) { presetsClick(x, y); return true; }
        if (overlay == ovLicense) return licenseClick(x, y);
        if (overlay == ovUpdate) return updateClick(x, y);
        if (overlay == ovChain) return chainClick(x, y, right);
        if (overlay == ovSave) { if (saveOk().in(x, y)) commitSave(); else if (saveCancel().in(x, y) || !saveBox().in(x, y)) { overlay = ovNone; repaint(); } return true; }
        if (overlay == ovSkins) {
            static const int zooms[6] = {80, 100, 120, 140, 160, 200};
            for (int i = 0; i < kNumThemes; ++i) if (skinRow(i).in(x, y)) { setSkin(i, true); return true; }
            for (int i = 0; i < 6; ++i) if (zoomBtn(i).in(x, y)) { setZoom(zooms[i] / 100.0); saveView(); return true; }
            if (!viewBox().in(x, y)) { overlay = ovNone; repaint(); }
            return true;
        }
        if (logoArea().in(x, y)) { overlay = ovUpdate; repaint(); return true; }
        if (skinBtn().in(x, y)) { if (right) setSkin((skin + 1) % kNumThemes, true); else overlay = ovSkins; repaint(); return true; }
        if (playBtn().in(x, y)) { setParamGesture(kGlobalBase + gPLAY, gp(gPLAY) > 0.5f ? 0.f : 1.f); repaint(); return true; }
        if (freezeBtn().in(x, y)) { setParamGesture(kGlobalBase + gFREEZE, gp(gFREEZE) > 0.5f ? 0.f : 1.f); repaint(); return true; }
        if (capLenBox().in(x, y)) { setParamGesture(kGlobalBase + gCAPLEN, (float)((clampi((int)std::lround(gp(gCAPLEN)), 0, kCapLens - 1) + (right ? kCapLens - 1 : 1)) % kCapLens)); repaint(); return true; }
        if (inBox().in(x, y)) return true;
        if (tempoBox().in(x, y)) { drag = dkTempo; dragY = y; dragStart = gp(gTEMPO); editParameter(kGlobalBase + gTEMPO, true); return true; }
        if (patLenBox().in(x, y)) { drag = dkPatLen; dragY = y; dragStart = pat().length; return true; }
        if (swingBox().in(x, y)) { drag = dkSwing; dragY = y; dragStart = pat().swing; return true; }
        // tracks
        for (int t = 0; t < kTracks; ++t) {
            if (trackBtn(t).in(x, y) || rowBtn(t).in(x, y)) { selectTrack(t); return true; }
            if (muteBtn(t).in(x, y)) { char b[24]; std::snprintf(b, sizeof(b), "U %d %d", t, bank->kit[t].mute ? 0 : 1); sendCmd(b); repaint(); return true; }
            if (trigBtn(t).in(x, y)) { selectTrack(t); drag = dkTrig; dragIndex = t; trigTrack(t, true); repaint(); return true; }
            for (int i = 0; i < 16; ++i) {
                if (!stepCell(t, i).in(x, y)) continue;
                const int s = stepPg * 16 + i;
                if (s >= pat().len[t]) return true;
                if (right) {
                    if (lockStep == s && lockTrack == t) lockStep = -1; else { lockStep = s; lockTrack = t; selTrack = t; }
                    repaint(); return true;
                }
                selectTrack(t);
                paintVal = pat().on[t][s] == 0;
                setStep(t, s, paintVal);
                drag = dkPaint; dragIndex = t;
                repaint(); return true;
            }
        }
        // material / wave
        if (waveR().in(x, y)) { waveClick(x, true); drag = dkWave; repaint(); return true; }
        if (loadBtn().in(x, y)) { openSampleBrowser(); return true; }
        if (matBtn().in(x, y)) { setMaterial(selTrack, (bank->kit[selTrack].material + (right ? kMatLive : 1)) % (kMatLive + 1)); return true; }
        if (captureBtn().in(x, y)) { captureTo(selTrack); return true; }
        if (modeBtn().in(x, y)) { Kit& k = bank->kit[selTrack]; k.mode = (uint8_t)((k.mode + (right ? kGrainModes - 1 : 1)) % kGrainModes); sendKit(selTrack); repaint(); return true; }
        if (syncBtn().in(x, y)) { Kit& k = bank->kit[selTrack]; k.sync = k.sync ? 0 : 1; sendKit(selTrack); repaint(); return true; }
        if (legatoBtn().in(x, y)) { Kit& k = bank->kit[selTrack]; k.legato = k.legato ? 0 : 1; sendKit(selTrack); repaint(); return true; }
        if (sliceModeBtn().in(x, y)) {
            Kit& k = bank->kit[selTrack]; k.sliceMode = k.sliceMode ? 0 : 1; k.sliceAmount = k.sliceMode ? 16 : 50;
            reslice(selTrack); return true;
        }
        if (amtMinus().in(x, y) || amtPlus().in(x, y)) {
            Kit& k = bank->kit[selTrack]; const int d = amtPlus().in(x, y) ? 1 : -1;
            k.sliceAmount = (uint8_t)(k.sliceMode ? clampi(k.sliceAmount + d, 1, kMaxSlices) : clampi(k.sliceAmount + d * 10, 0, 100));
            reslice(selTrack); return true;
        }
        if (lockMode() && lockClearBtn().in(x, y)) { char b[40]; std::snprintf(b, sizeof(b), "kc %d %d %d", editPattern(), lockTrack, lockStep); sendCmd(b); repaint(); return true; }
        // knobs
        for (int k = 0; k < kTrackParams; ++k) if (knobR(k).in(x, y)) {
            if (dbl) {
                if (!knobIsHostParam()) { char b[48]; std::snprintf(b, sizeof(b), "k %d %d %d %d %d", editPattern(), selTrack, lockStep, k, (int)kNoLock); sendCmd(b); }
                else setParamGesture(tpIndex(selTrack, k), trackDefault(selTrack, k));
                repaint(); return true;
            }
            drag = dkKnob; dragIndex = k; dragY = y; dragStart = knobValue(k);
            if (knobIsHostParam()) editParameter(tpIndex(selTrack, k), true);
            else if (pat().locks[selTrack][lockStep][k] == kNoLock) setKnob(k, dragStart);
            repaint(); return true;
        }
        // sequencer toolbar
        for (int i = 0; i < 4; ++i) if (stepPageBtn(i).in(x, y)) { showPage(i); return true; }
        if (chainBtn().in(x, y)) { overlay = ovChain; repaint(); return true; }
        if (lenMinus().in(x, y)) { setTrackLen(pat().len[selTrack] - 1); return true; }
        if (lenPlus().in(x, y)) { setTrackLen(pat().len[selTrack] + 1); return true; }
        if (lenBox().in(x, y)) { drag = dkTrackLen; dragY = y; dragStart = pat().len[selTrack]; return true; }
        if (speedBtn().in(x, y)) { pat().speed[selTrack] = (uint8_t)((pat().speed[selTrack] + (right ? kSpeeds - 1 : 1)) % kSpeeds); sendTrackSettings(selTrack); repaint(); return true; }
        if (rotL().in(x, y) || rotR().in(x, y)) { char b[48]; std::snprintf(b, sizeof(b), "rot %d %d %d", editPattern(), selTrack, rotL().in(x, y) ? -1 : 1); sendCmd(b); repaint(); return true; }
        if (fillBtn().in(x, y)) {
            std::string row(kSteps, '0'); for (int s = 0; s < pat().len[selTrack]; ++s) row[s] = '1';
            sendCmd("S " + std::to_string(editPattern()) + " " + std::to_string(selTrack) + " " + row); repaint(); return true;
        }
        if (clearTrackBtn().in(x, y)) { char b[32]; std::snprintf(b, sizeof(b), "clt %d %d", editPattern(), selTrack); sendCmd(b); lockStep = -1; repaint(); return true; }
        if (copyTrackBtn().in(x, y)) { trackClip.reset(new Pattern(pat())); trackClipTrack = selTrack; showPatMsg("T" + std::to_string(selTrack + 1) + " copied"); return true; }
        if (pasteTrackBtn().in(x, y) && trackClip) {
            const Pattern& c = *trackClip; const int p = editPattern(), t = selTrack, ct = trackClipTrack;
            std::string cmd, row(kSteps, '0');
            for (int s = 0; s < kSteps; ++s) row[s] = c.on[ct][s] ? '1' : '0';
            cmd += "clt " + std::to_string(p) + " " + std::to_string(t) + "\n";
            cmd += "S " + std::to_string(p) + " " + std::to_string(t) + " " + row + "\n";
            cmd += "L " + std::to_string(p) + " " + std::to_string(t) + " " + std::to_string(c.len[ct]) + " " + std::to_string(c.speed[ct]) + "\n";
            for (int s = 0; s < kSteps; ++s) for (int k = 0; k < kTrackParams; ++k)
                if (c.locks[ct][s][k] != kNoLock) cmd += "k " + std::to_string(p) + " " + std::to_string(t) + " " + std::to_string(s) + " " + std::to_string(k) + " " + std::to_string(c.locks[ct][s][k]) + "\n";
            cmd.pop_back(); sendLines(cmd); showPatMsg("pasted"); repaint(); return true;
        }
        // right column
        for (int i = 0; i < kPatterns; ++i) if (patBtn(i).in(x, y)) { setParamGesture(kGlobalBase + gPATTERN, (float)i); lockStep = -1; repaint(); return true; }
        if (patCopy().in(x, y)) { patClip = editPattern(); showPatMsg("A" + std::to_string(patClip + 1) + " copied"); return true; }
        if (patPaste().in(x, y) && patClip >= 0) { char b[32]; std::snprintf(b, sizeof(b), "cp %d %d", patClip, editPattern()); sendCmd(b); showPatMsg("pasted"); return true; }
        if (patClear().in(x, y)) { char b[24]; std::snprintf(b, sizeof(b), "cl %d", editPattern()); sendCmd(b); lockStep = -1; repaint(); return true; }
        for (int i = 0; i < 5; ++i) if (gKnob(i).in(x, y)) {
            const int g = kFxG[i];
            if (dbl) { setParamGesture(kGlobalBase + g, globalDefault(g)); repaint(); return true; }
            drag = dkGlobal; dragIndex = i; dragY = y; dragStart = gp(g); editParameter(kGlobalBase + g, true); return true;
        }
        if (undoBtn().in(x, y)) { if (right) redo(); else undo(); return true; }
        if (presetBtn().in(x, y)) { scanUser(); overlay = ovPresets; repaint(); return true; }
        if (saveBtn().in(x, y)) { scanUser(); openSave(); return true; }
        return false;
    }

    bool onMotion(const MotionEvent& ev) override
    {
        {
            const float hx = ev.pos.getX() / sx, hy = ev.pos.getY() / sx;
            if (std::fabs(hx - hovX) > 3.f || std::fabs(hy - hovY) > 3.f) { hovX = hx; hovY = hy; hovSince = nowSec(); tipNone = false; if (tipShown) { tipShown = false; repaint(); } }
        }
        if (drag == dkNone) return false;
        if (drag == dkResize) {
            const double dx = ev.pos.getX() - grabX, dy = (ev.pos.getY() - grabY) * (double)W / H;
            const double nw = resizeStartW + (dx + dy) * 0.5;
            const double z = std::max(0.6, std::min(3.0, nw / (W * baseScale())));
            const uint w = (uint)std::lround(W * baseScale() * z), h = (uint)std::lround(H * baseScale() * z);
            if (std::abs((int)w - (int)getWidth()) >= 3) setSize(w, h);
            return true;
        }
        const float x = ev.pos.getX() / sx, y = ev.pos.getY() / sx;
        const float sens = (ev.mod & kModifierShift) ? 0.12f : 0.6f;
        const float v = dragStart + (dragY - y) * sens;
        switch (drag) {
        case dkKnob: setKnob(dragIndex, v); break;
        case dkGlobal: setParam(kGlobalBase + kFxG[dragIndex], clampf(v, 0.f, 127.f)); repaint(); break;
        case dkTempo: setParam(kGlobalBase + gTEMPO, clampf(std::round((dragStart + (dragY - y) * ((ev.mod & kModifierShift) ? 0.05f : 0.5f)) * 10.f) / 10.f, 40.f, 240.f)); repaint(); break;
        case dkPatLen: pat().length = (uint8_t)clampi((int)std::lround(dragStart + (dragY - y) * 0.15f), 1, kSteps); repaint(); break;
        case dkSwing: pat().swing = (uint8_t)clampi((int)std::lround(dragStart + (dragY - y) * 0.15f), 50, 80); repaint(); break;
        case dkTrackLen: pat().len[selTrack] = (uint8_t)clampi((int)std::lround(dragStart + (dragY - y) * 0.15f), 1, kSteps); repaint(); break;
        case dkWave: waveClick(x, false); break;
        case dkPaint: {
            const int t = dragIndex;
            for (int i = 0; i < 16; ++i) {
                const R r = stepCell(t, i);
                if (x >= r.x && x < r.x + r.w + 4.f) { const int s = stepPg * 16 + i; if (s < pat().len[t]) setStep(t, s, paintVal); break; }
            }
            repaint(); break;
        }
        default: break;
        }
        return true;
    }

    bool onScroll(const ScrollEvent& ev) override
    {
        const float x = ev.pos.getX() / sx, y = ev.pos.getY() / sx;
        const float d = ev.delta.getY() > 0 ? 1.f : (ev.delta.getY() < 0 ? -1.f : 0.f);
        if (d == 0.f || overlay != ovNone) return false;
        for (int k = 0; k < kTrackParams; ++k) if (knobR(k).in(x, y)) {
            if (knobIsHostParam()) { editParameter(tpIndex(selTrack, k), true); setKnob(k, knobValue(k) + d); editParameter(tpIndex(selTrack, k), false); }
            else setKnob(k, knobValue(k) + d);
            return true;
        }
        for (int i = 0; i < 5; ++i) if (gKnob(i).in(x, y)) { const int g = kFxG[i]; setParamGesture(kGlobalBase + g, clampf(gp(g) + d, 0.f, 127.f)); repaint(); return true; }
        if (tempoBox().in(x, y)) { setParamGesture(kGlobalBase + gTEMPO, clampf(gp(gTEMPO) + d, 40.f, 240.f)); repaint(); return true; }
        if (capLenBox().in(x, y)) { setParamGesture(kGlobalBase + gCAPLEN, (float)clampi((int)std::lround(gp(gCAPLEN)) + (int)d, 0, kCapLens - 1)); repaint(); return true; }
        if (patLenBox().in(x, y)) { pat().length = (uint8_t)clampi(pat().length + (int)d, 1, kSteps); sendPatternSettings(); repaint(); return true; }
        if (swingBox().in(x, y)) { pat().swing = (uint8_t)clampi(pat().swing + (int)d, 50, 80); sendPatternSettings(); repaint(); return true; }
        if (lenBox().in(x, y)) { setTrackLen(pat().len[selTrack] + (int)d); return true; }
        return false;
    }

    void uiIdle() override
    {
        if (!tipShown && !tipNone && drag == dkNone && overlay == ovNone && hovX >= 0.f && nowSec() - hovSince > 0.8 && nowSec() > tipBlockUntil) {
            tipText = tipAt(hovX, hovY);
            if (!tipText.empty()) { tipShown = true; repaint(); } else tipNone = true;
        }
        flushPv();
        if (liveTrack(selTrack) && overlay == ovNone && nowSec() - lastLiveDraw > 0.05) { lastLiveDraw = nowSec(); repaint(); }
        if (updater && (int)updater->getState() != updLastState) { updLastState = (int)updater->getState(); repaint(); }
        if (!patMsg.empty() && nowSec() > patMsgUntil) { patMsg.clear(); repaint(); }
        if (!smpMsg.empty() && nowSec() > smpMsgUntil) { smpMsg.clear(); repaint(); }
        if (bank->chain.active() && drag == dkNone) {          // in a chain, the view follows the music
            const int pp = chainPlayingPattern();
            if (pp >= 0 && pp != editPattern()) { setParamGesture(kGlobalBase + gPATTERN, (float)pp); lockStep = -1; repaint(); }
        }
        if (viewDirty && drag != dkResize && ++viewDirtyTicks > 30) saveView();
    }

private:
    float values[kTotalParams];
    std::unique_ptr<Bank> bank;
    SamplePtr uiSmp[kTracks];
    float sx = 1.f;
    int skin = kDefaultSkin;
    const Theme* th = &kThemes[kDefaultSkin];
    FontId fonts[kFontCount] = {};
    NanoImage haloImage;
    void* dropToken = nullptr;
    GrainDirect* direct = nullptr;
    double lastLiveDraw = 0.0;
    int selTrack = 0, stepPg = 0, lockTrack = 0, lockStep = -1;
    Overlay overlay = ovNone;
    DragKind drag = dkNone; int dragIndex = 0; float dragY = 0, dragStart = 0; bool paintVal = true;
    uint lastClickTime = 0; float lastX = -100, lastY = -100;
    int patClip = -1;
    std::unique_ptr<Pattern> trackClip; int trackClipTrack = 0;
    std::string patMsg; double patMsgUntil = 0;
    std::string smpMsg; double smpMsgUntil = 0; bool browseSample = false;
    std::string presetNameStr = "Init", presetMsg, saveName; bool saveConfirm = false;
    std::vector<std::string> userPresets;
    std::deque<Snap> undoStack, redoStack; bool restoring = false; double lastSnapAt = -10.0; std::string lastSnapWhat;
    std::unique_ptr<upd::Checker> updater; int updLastState = -1;
    lic::Info licInfo; std::string licMsg;
    bool viewDirty = false; int viewDirtyTicks = 0;
    double resizeStartW = 0, grabX = 0, grabY = 0;
    float hovX = -1.f, hovY = -1.f; double hovSince = 0, tipBlockUntil = 0; bool tipShown = false, tipNone = false; std::string tipText;

    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(KgrnUI)
};

UI* createUI() { return new KgrnUI(); }

END_NAMESPACE_DISTRHO
