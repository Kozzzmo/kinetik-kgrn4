// Kinetik KGRN-4 — user interface (NanoVG)
#include "DistrhoUI.hpp"
#include "DistrhoPlugin.hpp"
#include "GrainDirect.hpp"
#include "Bank.hpp"
#include "Presets.hpp"
#include "Materials.hpp"
#include "MasterFx.hpp"
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

// NanoVG's blending (compiled into DGL): additive drawing for the grain clouds
struct NVGcontext;
extern "C" {
void nvgGlobalCompositeBlendFunc(NVGcontext* ctx, int sfactor, int dfactor);
void nvgGlobalCompositeOperation(NVGcontext* ctx, int op);
}

START_NAMESPACE_DISTRHO

using namespace kn;
using DGL_NAMESPACE::Color;

static constexpr float W = 1180.f, H = 800.f;
static constexpr int kFineSteps = 24;   // out_step is in 1/24 of a 16th

struct R {
    float x, y, w, h;
    bool in(float px, float py) const { return px >= x && px < x + w && py >= y && py < y + h; }
    float cx() const { return x + w * 0.5f; }
    float cy() const { return y + h * 0.5f; }
};

enum DragKind { dkNone, dkMix, dkKnob, dkGlobal, dkTempo, dkPatLen, dkSwing, dkTrackLen, dkPaint, dkWave, dkStepNote, dkStepLen, dkKey, dkFill, dkChaos, dkResize };
enum Overlay { ovNone, ovPresets, ovSkins, ovSave, ovLicense, ovUpdate, ovChain, ovReset };

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
        clickOn = viewClick();
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
    static bool& viewClick() { static bool c = true; return c; }   // CLICK setting, saved per user with the view
    static void readDefaultView(int& skinOut, int& zoomOut)
    {
        skinOut = kDefaultSkin; zoomOut = 100;
        const std::string f = viewFile();
        if (f.empty()) return;
        if (FILE* fp = fs::open(f, "r")) {
            int a = 0, b = 100, c = 1; const int n = std::fscanf(fp, "%d %d %d", &a, &b, &c); std::fclose(fp);
            if (n >= 3) viewClick() = c != 0;
            if (n >= 1 && a >= 0 && a < kNumThemes) skinOut = a;
            if (n >= 2 && b >= 60 && b <= 300) zoomOut = b;
        }
    }
    static void writeDefaultView(int s, int z)
    {
        const std::string f = viewFile();
        if (f.empty()) return;
        if (FILE* fp = fs::open(f, "w")) { std::fprintf(fp, "%d %d %d\n", s, z, viewClick() ? 1 : 0); std::fclose(fp); }
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
        viewClick() = clickOn;
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
    // mod01 >= 0: an LFO moves this knob; the arc / ticks follow the modulated value in the LFO's colour, the pointer
    // stays on the set value (as KDSP-16 / KMSP-6)
    void drawKnob(const R& r, float v01, const char* name, const std::string& val, bool locked, bool highlight,
                  float rad = 26.f, float cyo = 44.f, float ny = 88.f, float vy = 106.f, float mod01 = -1.f, Color modCol = Color())
    {
        const float cx = r.cx(), cy = r.y + cyo;
        const float a0 = kPi * 0.75f, a1 = kPi * 2.25f;
        v01 = clampf(v01, 0.f, 1.f);
        const Color hi0 = locked ? th->lock : th->knobArc;
        const float a = a0 + (a1 - a0) * v01;
        const bool modOn = mod01 >= 0.f;
        const float e01 = modOn ? clampf(mod01, 0.f, 1.f) : v01;   // where the arc ends
        const Color hi = modOn ? modCol : hi0;                    // arc colour
        switch (th->knob) {
        case ksTicks: {
            for (int i = 0; i < 25; ++i) {
                const float t = i / 24.f, ang = a0 + (a1 - a0) * t; const bool lit = t <= e01 + 1e-4f;
                beginPath(); circle(cx + std::cos(ang) * (rad + 3), cy + std::sin(ang) * (rad + 3), lit ? (modOn ? 1.9f : 1.6f) : 1.1f); fillColor(lit ? hi : th->knobTrack); fill();
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
            beginPath(); arc(cx, cy, rad, a0, a0 + (a1 - a0) * std::max(0.001f, e01), CW); strokeColor(hi); strokeWidth(6.f); stroke();
            beginPath(); circle(cx, cy, rad - 6); fillColor(locked ? th->lock : th->knobBody); fill();
            strokeColor(th->knobRing); strokeWidth(1.2f); stroke();
            beginPath(); moveTo(cx + std::cos(a) * 6, cy + std::sin(a) * 6); lineTo(cx + std::cos(a) * (rad - 9), cy + std::sin(a) * (rad - 9));
            strokeColor(th->knobPointer); strokeWidth(2.4f); lineCap(ROUND); stroke();
            break;
        }
        default: {
            beginPath(); circle(cx, cy, rad + 4); fillColor(th->inner); fill();
            beginPath(); arc(cx, cy, rad, a0, a1, CW); strokeColor(th->knobTrack); strokeWidth(5.f); stroke();
            beginPath(); arc(cx, cy, rad, a0, a0 + (a1 - a0) * std::max(0.001f, e01), CW); strokeColor(hi); strokeWidth(5.f); stroke();
            if (modOn && locked) { beginPath(); circle(cx, cy, rad + 4); strokeColor(th->lock); strokeWidth(1.5f); stroke(); }
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
        const bool live = c.rfind("trig ", 0) == 0 || c.rfind("rel ", 0) == 0 || c.rfind("fill ", 0) == 0 || c == "panic";
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
    // the on-screen keyboard: a key plays its chord (CHORD), LATCH keeps keys down until clicked again
    std::vector<int> latched;                    // latched keys (their chords sound)
    int mouseKey = -1;                           // key held with the mouse
    bool pcDown[128] = {};
    bool keyIsHeld(int n) const { return n == mouseKey || (n >= 0 && n < 128 && pcDown[n]) || std::find(latched.begin(), latched.end(), n) != latched.end(); }
    void keyChordNotes(int n, bool on)
    {
        const ChordDef& c = kChords[keyChord % kChordCount];
        std::string cmd; char b[32];
        for (int i = 0; i < c.n; ++i) { std::snprintf(b, sizeof(b), "%s %d %d\n", on ? "trig" : "rel", selTrack, clampi(n + c.notes[i], 0, 127)); cmd += b; }
        cmd.pop_back(); setState("cmd", cmd.c_str());
    }
    void keyPress(int n)
    {
        if (keyLatch) {
            auto it = std::find(latched.begin(), latched.end(), n);
            if (it != latched.end()) { latched.erase(it); keyChordNotes(n, false); }
            else { latched.push_back(n); keyChordNotes(n, true); }
        } else keyChordNotes(n, true);
        if (lane == lnNote && noteStep >= 0 && noteStep < pat().len[selTrack]) setStepNote(noteStep, n);
        repaint();
    }
    void keyRelease(int n) { if (!keyLatch) keyChordNotes(n, false); repaint(); }
    void releaseAllKeys()
    {
        for (int n : latched) keyChordNotes(n, false);
        latched.clear();
        if (mouseKey >= 0 && !keyLatch) keyChordNotes(mouseKey, false);
        mouseKey = -1;
        for (int n = 0; n < 128; ++n) if (pcDown[n]) { keyChordNotes(n, false); pcDown[n] = false; }
    }
    void keyNote(int t, int n, bool on) { char b[32]; std::snprintf(b, sizeof(b), "%s %d %d", on ? "trig" : "rel", t, n); sendCmd(b); }

    // ------------------------------------------------------------------ layout (0.4: one track at a time, as KMSP-6 / KDSP-16)
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
    // track tabs
    R trackTab(int t) const { return {14.f + t * 289.f, 60.f, 283.f, 50.f}; }
    R soloBtn(int t) const { const R r = trackTab(t); return {r.x + r.w - 64.f, r.y + 27.f, 28.f, 18.f}; }
    R muteBtn(int t) const { const R r = trackTab(t); return {r.x + r.w - 32.f, r.y + 27.f, 28.f, 18.f}; }
    // display (left)
    R modeBtn() const { return {300.f, 122.f, 128.f, 22.f}; }
    R syncBtn() const { return {432.f, 122.f, 60.f, 22.f}; }
    R legatoBtn() const { return {496.f, 122.f, 76.f, 22.f}; }
    R lockClearBtn() const { return {190.f, 122.f, 104.f, 22.f}; }
    R zoomBtn2() const { return {588.f, 122.f, 58.f, 22.f}; }
    R loopBtn() const { return {190.f, 122.f, 104.f, 22.f}; }
    R scaleBox() const { return {626.f, 722.f, 110.f, 64.f}; }
    R rootBox() const { return {740.f, 722.f, 52.f, 64.f}; }
    R waveR() const { return {20.f, 148.f, 620.f, 118.f}; }
    R mixZone() const { return {646.f, 148.f, 52.f, 118.f}; }   // SAMPLE / GRAINS fader, outside the pages
    R trackResetBtn() const { return {592.f, 272.f, 106.f, 26.f}; }
    R loadBtn() const { return {20.f, 272.f, 64.f, 26.f}; }
    R matBtn() const { return {88.f, 272.f, 150.f, 26.f}; }
    R captureBtn() const { return {242.f, 272.f, 90.f, 26.f}; }
    R sliceModeBtn() const { return {336.f, 272.f, 112.f, 26.f}; }
    R amtMinus() const { return {452.f, 272.f, 24.f, 26.f}; }
    R amtBox() const { return {478.f, 272.f, 84.f, 26.f}; }
    R amtPlus() const { return {564.f, 272.f, 24.f, 26.f}; }
    R pageBar() const { return {20.f, 304.f, 678.f, 26.f}; }
    R pageTab(int i) const { return {22.f + i * 67.4f, 306.f, 66.f, 22.f}; }
    int pageCount() const { return kPageLen[page]; }
    R pageKnob(int i) const { return {20.f + i * 84.75f, 334.f, 84.f, 106.f}; }
    R lfoScope() const { return {20.f + 6 * 84.75f + 6.f, 340.f, 156.f, 94.f}; }
    // effects and patterns (right)
    enum FxPage { fxEcho, fxReverb, fxChorus, fxChaos, fxFilter, fxMaster, kFxPages };
    R fxTab(int i) const { const float w = (444.f - (kFxPages - 1) * 4.f) / kFxPages; return {718.f + i * (w + 4.f), 122.f, w, 24.f}; }
    // ECHO: 12 slots, 6 per row; the others 8 slots, 4 per row (as KDSP-16)
    int fxSlots() const { return fxPage == fxEcho ? 12 : 8; }
    R fxKnob(int k) const
    {
        if (fxPage == fxEcho) return {718.f + (k % 6) * 74.f, 152.f + (k / 6) * 100.f, 74.f, 98.f};
        return {720.f + (k % 4) * 110.f, 152.f + (k / 4) * 100.f, 110.f, 98.f};
    }
    R chainBtn() const { return {868.f, 358.f, 70.f, 22.f}; }
    R patCopy() const { return {942.f, 358.f, 70.f, 22.f}; }
    R patPaste() const { return {1016.f, 358.f, 70.f, 22.f}; }
    R patClear() const { return {1090.f, 358.f, 70.f, 22.f}; }
    R patBtn(int i) const { return {720.f + (i % 8) * 55.f, 386.f + (i / 8) * 28.f, 51.f, 24.f}; }
    // bottom bar: REC / CLICK at the left, presets at the right
    R recBtn() const { return {20.f, 722.f, 110.f, 34.f}; }
    R clickBtn() const { return {20.f, 760.f, 110.f, 26.f}; }
    R resetBtn() const { return {146.f, 722.f, 120.f, 64.f}; }
    R fillHoldBtn() const { return {276.f, 722.f, 150.f, 64.f}; }
    R undoBtn() const { return {436.f, 722.f, 180.f, 64.f}; }
    R presetBtn() const { return {800.f, 722.f, 360.f, 34.f}; }
    R saveBtn() const { return {800.f, 760.f, 360.f, 26.f}; }
    R resetBox() const { return {340.f, 260.f, 500.f, 220.f}; }
    R resetOk() const { return {600.f, 420.f, 210.f, 40.f}; }
    R resetCancel() const { return {370.f, 420.f, 150.f, 40.f}; }
    // sequencer
    enum Lane { lnTrig, lnNote, lnLen, lnChord, lnRatch, lnCond, kLanes };
    R laneBtn(int i) const { return {20.f + i * 70.f, 460.f, 66.f, 24.f}; }
    static constexpr int kStepPages = kSteps / 16;
    R stepPageBtn(int i) const { return {442.f + i * 24.f, 460.f, 22.f, 24.f}; }
    R lenMinus() const { return {640.f, 460.f, 22.f, 24.f}; }
    R lenBox() const { return {664.f, 460.f, 56.f, 24.f}; }
    R lenPlus() const { return {722.f, 460.f, 22.f, 24.f}; }
    R speedBtn() const { return {748.f, 460.f, 84.f, 24.f}; }
    R rotL() const { return {836.f, 460.f, 24.f, 24.f}; }
    R rotR() const { return {862.f, 460.f, 24.f, 24.f}; }
    R clearTrackBtn() const { return {890.f, 460.f, 52.f, 24.f}; }
    R copyTrackBtn() const { return {946.f, 460.f, 52.f, 24.f}; }
    R pasteTrackBtn() const { return {1002.f, 460.f, 52.f, 24.f}; }
    R lockClrAll() const { return {1058.f, 460.f, 52.f, 24.f}; }
    R allOnBtn() const { return {1114.f, 460.f, 46.f, 24.f}; }
    R stepCell(int i) const { return {20.f + i * 71.5f, 490.f, 67.f, 80.f}; }
    // keyboard
    static constexpr int kKbOctaves = 3;
    R octDown() const { return {20.f, 580.f, 30.f, 124.f}; }
    R octBox() const { return {54.f, 580.f, 64.f, 124.f}; }
    R octUp() const { return {122.f, 580.f, 30.f, 124.f}; }
    int kbLow() const { return 12 * (kbOct + 2); }
    static bool isBlack(int n) { const int k = n % 12; return k == 1 || k == 3 || k == 6 || k == 8 || k == 10; }
    R keyRect(int n) const
    {
        const float x0 = 162.f, kw = 33.f, y = 580.f, h = 124.f;
        int wi = 0; for (int m = kbLow(); m < n; ++m) if (!isBlack(m)) ++wi;
        if (!isBlack(n)) return {x0 + wi * kw, y, kw - 2.f, h};
        return {x0 + wi * kw - kw * 0.32f, y, kw * 0.62f, h * 0.6f};
    }
    int keyAt(float x, float y) const
    {
        const int lo = kbLow(), hi = lo + 12 * kKbOctaves;
        for (int n = lo; n <= hi; ++n) if (isBlack(n) && keyRect(n).in(x, y)) return n;
        for (int n = lo; n <= hi; ++n) if (!isBlack(n) && keyRect(n).in(x, y)) return n;
        return -1;
    }
    R keyChordBtn() const { return {900.f, 610.f, 128.f, 28.f}; }
    R latchBtn() const { return {1032.f, 610.f, 128.f, 28.f}; }
    R pcKeysBtn() const { return {900.f, 642.f, 260.f, 28.f}; }
    R notesOffBtn() const { return {900.f, 674.f, 260.f, 30.f}; }
    R gripR() const { return {W - 22.f, H - 22.f, 22.f, 22.f}; }
    // overlays
    R viewBox() const { return {240.f, 96.f, 700.f, 520.f}; }
    R skinRow(int i) const { return {264.f + (i % 2) * 330.f, 150.f + (i / 2) * 148.f, 316.f, 136.f}; }
    R zoomBtn(int i) const { return {264.f + i * 109.f, 486.f, 101.f, 36.f}; }
    R presetsBox() const { return {80.f, 60.f, 1020.f, 600.f}; }
    R presetItem(int col, int row) const { return {100.f + col * 500.f, 130.f + row * 24.5f, 480.f, 22.5f}; }
    static constexpr int kPresetRows = 19;
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
        drawDisplay();
        drawPage();
        drawRight();
        drawSequencer();
        drawKeyboard();
        drawBottom();
        if (overlay == ovPresets) drawPresets();
        if (overlay == ovSkins) drawSkins();
        if (overlay == ovSave) drawSave();
        if (overlay == ovLicense) drawLicense();
        if (overlay == ovUpdate) drawUpdate();
        if (overlay == ovChain) drawChain();
        if (overlay == ovReset) drawReset();
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
        const uint32_t act = (uint32_t)std::lround(gp(gOUT_ACTIVE));
        char b[64];
        bool anySolo = false; for (int t = 0; t < kTracks; ++t) anySolo = anySolo || bank->kit[t].solo;
        for (int t = 0; t < kTracks; ++t) {
            const Kit& k = bank->kit[t];
            const bool sel = t == selTrack, silent = k.mute || (anySolo && !k.solo);
            const R r = trackTab(t);
            if (sel) box(r, th->selFill); else { box(r, th->btnFill); frame(r, th->btnEdge); }
            const Color fg = sel ? th->selText : (silent ? th->faint : th->text);
            std::snprintf(b, sizeof(b), "%d", t + 1);
            number(r.x + 12, r.cy() + 1, b, 22.f, fg, ALIGN_LEFT | ALIGN_MIDDLE);
            float sz = 11.f;
            const std::string nm = fitText(srcName(t), r.w - 110.f, sz);
            label(r.x + 40, r.y + 16, nm.c_str(), sz, fg, ALIGN_LEFT | ALIGN_MIDDLE);
            if (liveTrack(t)) std::snprintf(b, sizeof(b), "%s  INPUT", kModeNames[k.mode % kGrainModes]);
            else std::snprintf(b, sizeof(b), "%s  %d %s", kModeNames[k.mode % kGrainModes], std::max(1, (int)k.nSlices), k.nSlices > 1 ? "SLICES" : "SLICE");
            label(r.x + 40, r.y + 36, b, 8.5f, sel ? th->selText : th->dim, ALIGN_LEFT | ALIGN_MIDDLE);
            beginPath(); circle(r.x + r.w - 14, r.y + 13, 4.5f);
            fillColor((act >> t) & 1 ? ledColor() : Color(fg, Color(0, 0, 0, 0.f), 0.25f)); fill();
            const R sb = soloBtn(t), mb = muteBtn(t);
            if (k.solo) box(sb, th->accent, 3.f); else { box(sb, th->btnFill, 3.f); frame(sb, th->btnEdge, 3.f); }
            label(sb.cx(), sb.cy() + 1, "S", 9.f, k.solo ? th->accentText : th->btnText);
            if (k.mute) box(mb, th->lock, 3.f); else { box(mb, th->btnFill, 3.f); frame(mb, th->btnEdge, 3.f); }
            label(mb.cx(), mb.cy() + 1, "M", 9.f, k.mute ? th->selText : th->btnText);
        }
    }

    // waveform envelope cache of a track's material
    struct WaveCache { const SampleData* src = nullptr; float v0 = 0.f, v1 = 1.f; std::vector<float> mn, mx; };
    WaveCache wave[kTracks];
    const WaveCache& waveOf(int t, int cols, float v0 = 0.f, float v1 = 1.f)
    {
        WaveCache& w = wave[t];
        const SampleData* s = srcOf(t);
        if (w.src == s && (int)w.mn.size() == cols && w.v0 == v0 && w.v1 == v1) return w;
        w.src = s; w.v0 = v0; w.v1 = v1; w.mn.assign(cols, 0.f); w.mx.assign(cols, 0.f);
        if (!s || s->pcm.empty()) return w;
        const size_t n = s->pcm.size();
        const double sa = v0 * (double)n, sw = (v1 - v0) * (double)n;
        for (int c = 0; c < cols; ++c) {
            const size_t a = std::min(n - 1, (size_t)(sa + sw * c / cols)), e = std::max(a + 1, (size_t)(sa + sw * (c + 1) / cols));
            float lo = 0.f, hi = 0.f;
            for (size_t i = a; i < e && i < n; ++i) { lo = std::min(lo, s->pcm[i]); hi = std::max(hi, s->pcm[i]); }
            w.mn[c] = lo; w.mx[c] = hi;
        }
        return w;
    }
    // the part of the material the display shows: all of it, or (ZOOM) START .. END with a margin
    float vA = 0.f, vB = 1.f;
    void updateView()
    {
        vA = 0.f; vB = 1.f;
        if (!zoom || liveTrack(selTrack)) return;
        double a, e, p; playRegion(a, e, p);
        const double m = std::max(0.004, (e - a) * 0.1);
        vA = (float)std::max(0.0, a - m); vB = (float)std::min(1.0, e + m);
        if (vB - vA < 0.002f) vB = std::min(1.f, vA + 0.002f);
    }
    float fx(float f) const { const R wr = waveR(); return wr.x + 4 + (f - vA) / (vB - vA) * (wr.w - 8); }
    // where START, END and POSITION are in the display (fractions of the whole material, or of the LIVE window)
    void playRegion(double& a, double& b, double& p)
    {
        const int t = selTrack;
        double ra = 0.0, rb = 1.0;
        const SampleData* s = srcOf(t);
        if (!liveTrack(t) && s && !s->pcm.empty()) {
            const uint32_t frames = (uint32_t)s->pcm.size();
            sliceRegion(bank->kit[t], sliceOf(knobValue(tpSLICE), bank->kit[t].nSlices), frames, ra, rb);
            ra /= frames; rb /= frames;
        }
        double s0 = knobValue(tpSTART) / 127.0, s1 = knobValue(tpEND) / 127.0;
        if (s1 < s0) std::swap(s0, s1);
        a = ra + s0 * (rb - ra); b = ra + s1 * (rb - ra);
        p = a + knobValue(tpPOSITION) / 127.0 * (b - a);
    }
    // ---- the grains: every grain as its window, over the stretch of material it reads
    GrainView gv[kViewGrains]; VoiceView vv[8]; int gvN = 0, vvN = 0;
    void fetchView(int t) { gvN = direct ? direct->grainView(t, gv, kViewGrains, vv, 8, vvN) : 0; if (!direct) vvN = 0; }
    // one colour per voice (no pink); darker on light skins
    Color voiceColor(int slot) const
    {
        static const uint8_t pal[8][3] = {{125, 255, 90}, {255, 214, 64}, {80, 220, 255}, {255, 150, 50},
                                          {235, 235, 235}, {150, 170, 255}, {200, 255, 120}, {255, 110, 80}};
        const uint8_t* c = pal[slot & 7];
        const float lum = 0.3f * th->lcdBg.red + 0.59f * th->lcdBg.green + 0.11f * th->lcdBg.blue;
        const float k = lum > 0.5f ? 0.5f : 1.f;
        return Color((int)(c[0] * k), (int)(c[1] * k), (int)(c[2] * k));
    }
    // the grain window as the engine shapes it (SHAPE: square, triangle, smooth, percussive, reversed percussive; LOOP: crossfade)
    static float winTable(int w, float x)
    {
        x = clampf(x, 0.f, 1.f);
        switch (w) {
        case 0: return std::min(1.f, std::min(x, 1.f - x) / 0.06f);
        case 1: return 1.f - std::fabs(2.f * x - 1.f);
        case 2: return 0.5f - 0.5f * std::cos(kTwoPi * x);
        case 3: return std::min(1.f, x / 0.04f) * std::exp(-4.5f * x) * std::min(1.f, (1.f - x) / 0.05f) * 1.25f;
        default: return winTable(3, 1.f - x);
        }
    }
    float grainProfile(float x, float shape, bool loop) const
    {
        if (loop) { const float xf = (0.005f + shape * 0.5f) / (1.005f + shape * 0.5f); const float d = std::min(x, 1.f - x); return d >= xf ? 1.f : d / std::max(1e-3f, xf); }
        const float a = shape * 4.f; const int w = std::min(3, (int)a); const float f = a - w;
        return std::min(1.f, winTable(w, x) * (1.f - f) + winTable(w + 1, x) * f);
    }
    // grains: x = the stretch of material each one reads (its window drawn over it, time from left to right, so a
    // reversed grain runs right to left), height = its pitch against the sample (+-24 st) with its pan as a small offset,
    // brightness = where it is in its window x the note's envelope, colour = its voice; outline = original pitch (PITCH MIX),
    // white outline = OCTAVE, arrow = REVERSE
    float pitchRange = 7.f;                       // semitones from the centre to the top: follows the grains (7 .. 25)
    float pitchY(float semis, const R& wr) const { return wr.cy() - clampf(semis, -pitchRange, pitchRange) / pitchRange * (wr.h * 0.5f - 10.f); }
    void drawGrains(const R& wr)
    {
        const float shape = knobValue(tpSHAPE) / 127.f;
        float envOf[8] = {}; bool relOf[8] = {};
        for (int i = 0; i < vvN; ++i) { envOf[vv[i].slot & 7] = vv[i].env; relOf[vv[i].slot & 7] = vv[i].released != 0; }
        const float top = wr.y + 4, bot = wr.y + wr.h - 4;
        {   // the pitch scale follows the grains (grows at once, shrinks slowly)
            float m = 6.f; for (int i = 0; i < gvN; ++i) m = std::max(m, std::fabs(gv[i].semis) + 1.f);
            m = std::min(25.f, m);
            pitchRange = m > pitchRange ? m : pitchRange + (m - pitchRange) * 0.03f;
        }
        if (gvN > 0) {                                                   // pitch guides: 0, +-12, +-24
            for (int st : {-24, -12, 0, 12, 24}) {
                if (std::abs(st) > pitchRange) continue;
                const float y = pitchY((float)st, wr);
                beginPath(); rect(wr.x + 4, y, wr.w - 8, 1.f); fillColor(withAlpha(th->lcdDim, st == 0 ? 0.35f : 0.18f)); fill();
                label(wr.x + wr.w - 6, y - 5, st == 0 ? "0" : (st == 12 ? "+12" : st == -12 ? "-12" : st > 0 ? "+24" : "-24"), 7.5f, withAlpha(th->lcdDim, 0.8f), ALIGN_RIGHT | ALIGN_MIDDLE);
            }
        }
        for (int i = 0; i < vvN; ++i) {                                  // the voices' play heads (thin, in their colour)
            const float x = fx(vv[i].head);
            beginPath(); rect(x - 0.5f, top, 1.f, bot - top); fillColor(withAlpha(voiceColor(vv[i].slot), relOf[vv[i].slot & 7] ? 0.2f : 0.5f)); fill();
        }
        ::nvgGlobalCompositeBlendFunc(getContext(), 1 << 6, 1 << 1);   // SRC_ALPHA, ONE: dense clouds glow
        for (int i = 0; i < gvN; ++i) {
            const GrainView& g = gv[i];
            const bool loop = (g.flags & kGvLoop) != 0;
            const float env = std::max(0.02f, envOf[g.voice & 7]);
            float xa = fx(g.a), xb = fx(g.b);
            if (std::fabs(xb - xa) < 8.f) { const float m = 0.5f * (xa + xb), d = xb >= xa ? 4.f : -4.f; xa = m - d; xb = m + d; }
            const float yc = clampf(pitchY(g.semis, wr) + g.pan * 6.f, top + 4.f, bot - 4.f);
            const float H = 2.5f + 7.5f * std::sqrt(env);
            const float lit = clampf(g.amp / env, 0.f, 1.f);              // where the grain is in its window
            const Color c = voiceColor(g.voice);
            const bool orig = (g.flags & kGvOriginal) != 0, oct = (g.flags & kGvOctave) != 0;
            beginPath();
            for (int k = 0; k <= 14; ++k) { const float u = k / 14.f; const float x = xa + (xb - xa) * u; const float y = yc - grainProfile(u, shape, loop) * H; if (k == 0) moveTo(x, y); else lineTo(x, y); }
            for (int k = 14; k >= 0; --k) { const float u = k / 14.f; lineTo(xa + (xb - xa) * u, yc + grainProfile(u, shape, loop) * H); }
            closePath();
            fillColor(withAlpha(c, orig ? 0.06f + 0.16f * lit : 0.16f + 0.5f * lit)); fill();
            strokeColor(withAlpha(oct ? Color(255, 255, 255) : c, (orig ? 0.5f : 0.25f) + 0.45f * lit)); strokeWidth(oct ? 1.3f : 0.8f); stroke();
            const float xn = xa + (xb - xa) * g.ph;                       // where it reads now
            beginPath(); circle(xn, yc, 1.3f + 1.7f * lit); fillColor(withAlpha(c, 0.55f + 0.45f * lit)); fill();
            if (g.flags & kGvReverse) {
                const float d = xb < xa ? -1.f : 1.f;
                beginPath(); moveTo(xn + d * 6.f, yc); lineTo(xn + d * 2.f, yc - 3.f); lineTo(xn + d * 2.f, yc + 3.f); closePath(); fillColor(withAlpha(c, 0.3f + 0.7f * lit)); fill();
            }
        }
        ::nvgGlobalCompositeOperation(getContext(), 0);              // SOURCE_OVER
    }
    void drawDisplay()
    {
        fetchView(selTrack);
        updateView();
        panelBox({14.f, 116.f, 690.f, 330.f});
        const int t = selTrack;
        const Kit& kit = bank->kit[t];
        char b[64];
        std::snprintf(b, sizeof(b), "T%d", t + 1);
        number(26, 133, b, 16.f, th->text, ALIGN_LEFT | ALIGN_MIDDLE);
        float sz = 12.f;
        if (lockMode()) {
            std::snprintf(b, sizeof(b), "LOCK T%d STEP %d", lockTrack + 1, lockStep + 1);
            label(62, 133, b, 11.f, th->lock, ALIGN_LEFT | ALIGN_MIDDLE);
            frame(lockClearBtn(), th->lock); label(lockClearBtn().cx(), lockClearBtn().cy() + 1, "CLEAR LOCKS", 9.f, th->lock);
        } else {
            const std::string nm = fitText(srcName(t), 220.f, sz);
            label(62, 133, nm.c_str(), sz, th->text, ALIGN_LEFT | ALIGN_MIDDLE);
        }
        std::snprintf(b, sizeof(b), "MODE: %s", kModeNames[kit.mode % kGrainModes]);
        button(modeBtn(), b, false, 9.f);
        button(syncBtn(), "SYNC", kit.sync != 0, 9.f);
        button(legatoBtn(), "LEGATO", kit.legato != 0, 9.f);
        if (!liveTrack(t)) button(zoomBtn2(), "ZOOM", zoom, 9.f);
        if (!liveTrack(t) && !lockMode()) {
            if (kit.beats) std::snprintf(b, sizeof(b), "LOOP %d BEAT%s", kit.beats, kit.beats > 1 ? "S" : ""); else std::snprintf(b, sizeof(b), "LOOP SYNC");
            button(loopBtn(), b, kit.beats != 0, 9.f);
        }
        const SampleData* s = srcOf(t);
        const R wr = waveR();
        box(wr, th->lcdBg); frame(wr, th->panelEdge, -1.f, th->panelEdgeWidth);
        if (!lockMode()) {
            if (liveTrack(t)) {
                const int bars = capBars(gp(gCAPLEN));
                std::snprintf(b, sizeof(b), bars == 1 ? "LAST %d BAR" : "LAST %d BARS", bars);
                label(696, 133, b, 10.f, th->dim, ALIGN_RIGHT | ALIGN_MIDDLE);
            } else if (s) {
                if (kit.beats) std::snprintf(b, sizeof(b), "%.0f BPM", kit.beats * 60.0 / (s->pcm.size() / s->sr));   // the loop's own tempo
                else std::snprintf(b, sizeof(b), "%.2f S", s->pcm.size() / s->sr);
                label(696, 133, b, 10.f, th->dim, ALIGN_RIGHT | ALIGN_MIDDLE);
            }
        }
        const int cols = (int)wr.w - 8;
        const float x0 = wr.x + 4, cy = wr.cy(), hh = wr.h * 0.42f;
        scissor(wr.x + 1, wr.y + 1, wr.w - 2, wr.h - 2);
        bool drawn = false;
        if (liveTrack(t)) {
            if (!direct) label(wr.cx(), wr.cy(), "LIVE INPUT (not reachable in this host)", 10.f, th->lcdDim);
            else {
                liveMn.resize(cols); liveMx.resize(cols);
                direct->liveWave(liveMn.data(), liveMx.data(), cols);
                bool any = false; for (int c = 0; c < cols; ++c) if (liveMx[c] - liveMn[c] > 1e-3f) { any = true; break; }
                beginPath();
                for (int c = 0; c < cols; ++c) rect(x0 + c, cy - liveMx[c] * hh, 1.f, std::max(1.f, (liveMx[c] - liveMn[c]) * hh));
                fillColor(withAlpha(gp(gFREEZE) > 0.5f ? th->lock : th->lcdText, 0.85f)); fill();
                if (!any) {
                    label(wr.cx(), wr.cy() - 8, "NOTHING ON THE INPUT", 11.f, th->lcdDim);
                    label(wr.cx(), wr.cy() + 12, "Route a track to KGRN-4's side-chain input in your music software", 9.5f, th->lcdDim);
                }
                label(wr.x + wr.w - 8, wr.y + 10, "NOW", 8.f, th->lcdDim, ALIGN_RIGHT | ALIGN_MIDDLE);
                drawn = true;
            }
        } else if (!s || s->pcm.empty()) {
            label(wr.cx(), wr.cy() - 8, "NO SAMPLE ON THIS TRACK", 11.f, th->lcdDim);
            label(wr.cx(), wr.cy() + 12, "LOAD a WAV / AIFF file, drop one here, or choose a MATERIAL", 9.5f, th->lcdDim);
        } else {
            const WaveCache& w = waveOf(t, cols, vA, vB);
            beginPath();
            for (int c = 0; c < cols; ++c) rect(x0 + c, cy - w.mx[c] * hh, 1.f, std::max(1.f, (w.mx[c] - w.mn[c]) * hh));
            fillColor(withAlpha(th->lcdText, gvN > 0 ? 0.2f : 0.85f)); fill();          // dimmer while grains are drawn on it
            const int slice = sliceOf(knobValue(tpSLICE), kit.nSlices);
            for (int i = 0; i < kit.nSlices; ++i) {
                const float x = fx(kit.slice[i] * 1e-6f);
                beginPath(); rect(x, wr.y + 2, 1.f, wr.h - 4); fillColor(withAlpha(th->lcdDim, i == slice ? 1.f : 0.5f)); fill();
                if (kit.nSlices > 1) {
                    std::snprintf(b, sizeof(b), "%d", i + 1);
                    label(x + 3, wr.y + 9, b, 8.f, i == slice ? th->lcdText : th->lcdDim, ALIGN_LEFT | ALIGN_MIDDLE);
                }
            }
            drawn = true;
        }
        if (drawn) {
            double a, e, p; playRegion(a, e, p);
            const float xa = fx((float)a), xe = fx((float)e), xp = fx((float)p);
            // outside START .. END: darkened
            const Color shade = withAlpha(th->lcdBg, 0.72f);
            if (xa > x0) { beginPath(); rect(x0, wr.y + 2, xa - x0, wr.h - 4); fillColor(shade); fill(); }
            if (xe < x0 + cols) { beginPath(); rect(xe, wr.y + 2, x0 + cols - xe, wr.h - 4); fillColor(shade); fill(); }
            // START / END handles
            for (int i = 0; i < 2; ++i) {
                const float x = i ? xe : xa;
                beginPath(); rect(x - 1, wr.y + 2, 2.f, wr.h - 4); fillColor(accentInk()); fill();
                beginPath();
                if (i == 0) { moveTo(x, wr.y + 2); lineTo(x + 9, wr.y + 2); lineTo(x, wr.y + 12); }
                else { moveTo(x, wr.y + 2); lineTo(x - 9, wr.y + 2); lineTo(x, wr.y + 12); }
                closePath(); fillColor(accentInk()); fill();
            }
            label(xa + 3, wr.y + wr.h - 9, "S", 8.f, accentInk(), ALIGN_LEFT | ALIGN_MIDDLE);
            label(xe - 3, wr.y + wr.h - 9, "E", 8.f, accentInk(), ALIGN_RIGHT | ALIGN_MIDDLE);
            // POSITION
            beginPath(); moveTo(xp, wr.y + wr.h - 2); lineTo(xp - 5, wr.y + wr.h - 10); lineTo(xp + 5, wr.y + wr.h - 10); closePath(); fillColor(th->lock); fill();
            beginPath(); rect(xp, wr.y + 2, 1.f, wr.h - 4); fillColor(withAlpha(th->lock, 0.7f)); fill();
            drawGrains(wr);
        }
        resetScissor();
        button(loadBtn(), "LOAD", false, 10.5f);
        {   // the material, or the sample's own name
            float msz = 9.5f;
            const std::string nm = fitText(kit.material ? std::string(kMaterialNames[std::min<int>(kit.material, kMatLive)]) : srcName(t), matBtn().w - 16.f, msz);
            button(matBtn(), nm.c_str(), kit.material == kMatLive, msz);
        }
        button(trackResetBtn(), "RESET TRACK", false, 9.f);
        drawMix();
        button(captureBtn(), "CAPTURE", false, 10.f);
        button(sliceModeBtn(), kit.sliceMode ? "EQUAL SLICES" : "TRANSIENTS", false, 9.5f);
        button(amtMinus(), "-", false, 12.f); button(amtPlus(), "+", false, 12.f);
        box(amtBox(), th->lcdBg);
        if (kit.sliceMode) std::snprintf(b, sizeof(b), "%d SLICES", std::max(1, (int)kit.sliceAmount)); else std::snprintf(b, sizeof(b), "SENS %d", kit.sliceAmount);
        label(amtBox().cx(), amtBox().cy() + 1, b, 10.f, th->lcdText);
        if (!smpMsg.empty()) label(696, 285, smpMsg.c_str(), 9.f, th->lock, ALIGN_RIGHT | ALIGN_MIDDLE);
    }
    std::vector<float> liveMn, liveMx;
    // SAMPLE / GRAINS: a vertical crossfader beside the display; the sample's waveform fades out towards the top while
    // grains appear, the handle sits on the mix (LFO-moved: a second mark in the LFO's colour)
    void drawMix()
    {
        const R m = mixZone();
        const float v = knobValue(tpGMIX), m01 = v / 127.f;
        box(m, th->lcdBg); frame(m, knobLocked(tpGMIX) ? th->lock : th->panelEdge, -1.f, knobLocked(tpGMIX) ? 2.f : th->panelEdgeWidth);
        label(m.cx(), m.y + 9, "GRAINS", 7.5f, withAlpha(th->lcdText, 0.4f + 0.6f * m01));
        label(m.cx(), m.y + m.h - 8, "SAMPLE", 7.5f, withAlpha(th->lcdText, 1.f - 0.6f * m01));
        const float y0 = m.y + 18.f, y1 = m.y + m.h - 18.f;
        // the sample: little waveform strokes in the lower half, the grains: dots in the upper half
        static const float w[9] = {0.4f, 0.9f, 0.6f, 1.f, 0.5f, 0.8f, 0.35f, 0.7f, 0.45f};
        beginPath();
        for (int i = 0; i < 9; ++i) { const float x = m.x + 8 + i * 4.5f, h = w[i] * 14.f; rect(x, (y0 + y1) * 0.5f + 18.f - h * 0.5f, 1.6f, h); }
        fillColor(withAlpha(th->lcdText, 0.15f + 0.6f * (1.f - m01))); fill();
        static const float gx[7] = {0.15f, 0.42f, 0.7f, 0.28f, 0.85f, 0.55f, 0.1f}, gy[7] = {0.1f, 0.3f, 0.05f, 0.55f, 0.4f, 0.65f, 0.75f};
        for (int i = 0; i < 7; ++i) { beginPath(); circle(m.x + 6 + gx[i] * (m.w - 12), y0 + gy[i] * (y1 - y0) * 0.45f, 2.f); fillColor(withAlpha(ledColor(), 0.1f + 0.8f * m01)); fill(); }
        // the rail and the handle
        beginPath(); roundedRect(m.cx() - 2, y0, 4, y1 - y0, 2); fillColor(withAlpha(th->lcdDim, 0.5f)); fill();
        float off;
        if (lfoMod(tpGMIX, off)) {
            const float ym = y1 - clampf((v + off) / 127.f, 0.f, 1.f) * (y1 - y0);
            beginPath(); circle(m.cx(), ym, 3.5f); fillColor(lfoInk(firstLfo)); fill();
        }
        const float yh = y1 - m01 * (y1 - y0);
        const Color hc = knobLocked(tpGMIX) ? th->lock : th->knobArc;
        box({m.x + 7, yh - 5, m.w - 14, 10}, hc, 3.f);
        char b[16]; std::snprintf(b, sizeof(b), "%d", (int)std::lround(m01 * 100.f));
        label(m.cx(), yh + 0.5f, b, 7.5f, th->lcdBg);
    }

    float knobValue(int k)
    {
        const int t = selTrack;
        if (lockMode() && lockTrack == t) { const uint8_t v = pat().locks[t][lockStep][k]; if (v != kNoLock) return v; }
        return tv(t, k);
    }
    bool knobLocked(int k) { return lockMode() && lockTrack == selTrack && pat().locks[selTrack][lockStep][k] != kNoLock; }
    // the text of a track knob (LFO rates show their note value when synced)
    std::string knobText(int k, float v)
    {
        const Kit& kit = bank->kit[selTrack];
        if (k >= tpL1WAVE && (k - tpL1WAVE) % kLfoParams == 1) {
            const int i = (k - tpL1WAVE) / kLfoParams;
            if (lfoSynced(knobValue(lfoParam(i, 2)))) return kLfoSyncNames[lfoSyncDiv(v)];
        }
        if (k == tpSCAN && kit.beats && !liveTrack(selTrack)) {      // LOOP SYNC: SCAN = speed against the tempo
            const float sp = scanSpeed(v); char b[24];
            if (sp == 0.f) return "FREEZE";
            std::snprintf(b, sizeof(b), "%sx%.2f", sp < 0.f ? "REV " : "", std::fabs(sp)); return b;
        }
        return trackValueText(k, v, kit.nSlices, kit.mode, kit.sync != 0);
    }
    // LOOP SYNC: how many beats a loop of `secs` holds: from a tempo in its name ("_174bpm", "120 BPM"), otherwise the
    // power of two that puts it closest to 120 BPM
    static int guessBeats(double secs, const std::string& name)
    {
        if (secs <= 0.05) return 4;
        std::string n = name; for (auto& c : n) c = (char)std::tolower((unsigned char)c);
        for (size_t i = 0; i < n.size(); ++i) {
            if (!std::isdigit((unsigned char)n[i]) || (i > 0 && std::isdigit((unsigned char)n[i - 1]))) continue;
            size_t j = i; while (j < n.size() && std::isdigit((unsigned char)n[j])) ++j;
            const int v = std::atoi(n.substr(i, j - i).c_str());
            size_t k = j; while (k < n.size() && (n[k] == ' ' || n[k] == '_' || n[k] == '-')) ++k;
            if (v >= 50 && v <= 220 && n.compare(k, 3, "bpm") == 0) return clampi((int)std::lround(secs * v / 60.0), 1, 64);
        }
        int best = 4; double bd = 1e9;
        for (int b = 1; b <= 64; b *= 2) { const double bpm = b * 60.0 / secs, d = std::fabs(std::log(bpm / 120.0)); if (d < bd) { bd = d; best = b; } }
        return best;
    }
    static bool looksLikeLoop(const std::string& name)
    {
        std::string n = name; for (auto& c : n) c = (char)std::tolower((unsigned char)c);
        return n.find("loop") != std::string::npos || n.find("bpm") != std::string::npos || n.find("break") != std::string::npos;
    }
    void setBeats(int t, int beats) { bank->kit[t].beats = (uint8_t)clampi(beats, 0, 64); sendKit(t); repaint(); }
    void stepBeats(int t, int d)
    {
        int i = 0; for (int k = 0; k < kLoopBeatsN; ++k) if (kLoopBeats[k] <= bank->kit[t].beats) i = k;
        setBeats(t, kLoopBeats[clampi(i + d, 0, kLoopBeatsN - 1)]);
    }
    void setScale(int sc, int root) { char b[24]; std::snprintf(b, sizeof(b), "G %d %d", clampi(sc, 0, kScales - 1), clampi(root, 0, 11)); sendCmd(b); repaint(); }
    // how far the track's LFOs move a knob right now (knob units), for the dot on its arc
    // LFO colours (as KDSP-16 / KMSP-6 defaults: green, yellow, white; white = ink on RISO)
    Color lfoInk(int l) const
    {
        static const uint8_t c[3][3] = {{125, 255, 90}, {255, 214, 64}, {236, 238, 248}};
        if (l == 2 && th->logo == lgRiso) return th->text;
        const float lum = 0.3f * th->panel.red + 0.59f * th->panel.green + 0.11f * th->panel.blue;
        const float k = lum > 0.5f ? 0.55f : 1.f;
        return Color((int)(c[l][0] * k), (int)(c[l][1] * k), (int)(c[l][2] * k));
    }
    int firstLfo = -1, lfoCount = 0;            // set by lfoMod: which LFOs move the knob
    bool lfoMod(int k, float& off)
    {
        off = 0.f; bool any = false; firstLfo = -1; lfoCount = 0;
        if (k >= kModTargets || !direct) return false;
        for (int i = 0; i < kLfos; ++i) {
            if (lfoDest(knobValue(lfoParam(i, 4))) != k + 1 || std::fabs(knobValue(lfoParam(i, 3)) - 64.f) < 0.5f) continue;
            off += direct->lfoValue(selTrack, i) * 63.5f; any = true;
            if (firstLfo < 0) firstLfo = i;
            ++lfoCount;
        }
        return any;
    }
    bool lfoActive() { for (int i = 0; i < kLfos; ++i) if (lfoDest(tv(selTrack, lfoParam(i, 4))) > 0 && std::fabs(tv(selTrack, lfoParam(i, 3)) - 64.f) >= 0.5f) return true; return false; }
    void drawPage()
    {
        {   // the page tabs: a recessed bar of tabs over the knobs (not buttons like the row above), the open one lit and joined
            const R pb = pageBar();
            box(pb, th->lcdBg, 5.f); frame(pb, th->panelEdge, 5.f, th->panelEdgeWidth);
            beginPath(); rect(pb.x + 8, pb.y + pb.h + 3, pb.w - 16, 1.f); fillColor(withAlpha(th->dim, 0.35f)); fill();
        }
        for (int i = 0; i < kPages; ++i) {
            const R r = pageTab(i);
            const bool on = page == i;
            if (on) {
                box(r, th->selFill, 4.f);
                beginPath(); moveTo(r.cx() - 6, r.y + r.h); lineTo(r.cx() + 6, r.y + r.h); lineTo(r.cx(), r.y + r.h + 5); closePath(); fillColor(th->selFill); fill();
            }
            label(r.cx(), r.cy() + 1, kPageNames[i], 9.f, on ? th->selText : th->lcdDim, ALIGN_CENTER | ALIGN_MIDDLE, th->fontButton);
            if (i >= kLfoPage0) {                           // a lit dot when that LFO moves something
                const int l = i - kLfoPage0;
                if (lfoDest(tv(selTrack, lfoParam(l, 4))) > 0 && std::fabs(tv(selTrack, lfoParam(l, 3)) - 64.f) >= 0.5f) {
                    beginPath(); circle(r.x + 9, r.cy(), 3.f); fillColor(page == i ? th->selText : lfoInk(l)); fill();
                }
            }
        }
        const int n = pageCount();
        for (int i = 0; i < n; ++i) {
            const int k = kPageFirst[page] + i;
            const float v = knobValue(k);
            const R r = pageKnob(i);
            float off;
            const bool mod = lfoMod(k, off);
            drawKnob(r, v / 127.f, kTP[k].name, knobText(k, v), knobLocked(k), drag == dkKnob && dragIndex == k, 22.f, 34.f, 74.f, 92.f,
                     mod ? clampf((v + off) / 127.f, 0.f, 1.f) : -1.f, mod ? lfoInk(firstLfo) : Color());
            if (mod) {                                          // badge: which LFO moves it
                char b[8]; std::snprintf(b, sizeof(b), lfoCount > 1 ? "L%d+" : "L%d", firstLfo + 1);
                label(r.x + r.w - 6, r.y + 6, b, 8.5f, lfoInk(firstLfo), ALIGN_RIGHT | ALIGN_MIDDLE);
            }
        }
        if (page >= kLfoPage0) {                             // LFO page: its shape and where it is now
            const int l = page - kLfoPage0;
            const R sc = lfoScope();
            box(sc, th->lcdBg); frame(sc, th->panelEdge, -1.f, th->panelEdgeWidth);
            const int wv = lfoWave(knobValue(lfoParam(l, 0)));
            const float dep = (knobValue(lfoParam(l, 3)) - 64.f) / 63.f;
            beginPath();
            for (int i = 0; i <= 64; ++i) {
                const float x = i / 64.f;
                float y;
                switch (wv) {
                case 0: y = x < 0.5f ? 4.f * x - 1.f : 3.f - 4.f * x; break;
                case 1: y = std::sin(kTwoPi * x); break;
                case 2: y = x < 0.5f ? 1.f : -1.f; break;
                case 3: y = 2.f * x - 1.f; break;
                case 4: y = 1.f - 2.f * x; break;
                default: { static const float r8[8] = {0.6f, -0.3f, 0.9f, -0.8f, 0.1f, 0.5f, -0.6f, 0.2f}; y = r8[std::min(7, (int)(x * 8))]; break; }
                }
                y *= dep;
                const float px = sc.x + 8 + x * (sc.w - 16), py = sc.cy() - y * (sc.h * 0.36f);
                if (i == 0) moveTo(px, py); else lineTo(px, py);
            }
            strokeColor(lfoInk(l)); strokeWidth(1.6f); stroke();
            if (direct) {
                const float y = direct->lfoValue(selTrack, l);
                box({sc.x + sc.w - 7, sc.cy() - std::max(0.f, y) * sc.h * 0.36f, 3.f, std::fabs(y) * sc.h * 0.36f + 1.f}, lfoInk(l), 1.f);
            }
            const int d = lfoDest(knobValue(lfoParam(l, 4)));
            char b[48]; std::snprintf(b, sizeof(b), "-> %s", d ? kTP[d - 1].name : "NONE");
            label(sc.x + 8, sc.y + 11, b, 8.5f, th->lcdDim, ALIGN_LEFT | ALIGN_MIDDLE);
        }
    }

    // effects: the master section of KDSP-16 (MasterFx.hpp): ECHO REVERB CHORUS CHAOS FILTER MASTER
    int fxParam(int k) const
    {
        switch (fxPage) {
        case fxEcho: { static const int e[12] = {gECHO_TIME, gECHO_FB, gECHO_LPF, gECHO_HPF, gECHO_WIDTH, gECHO_LEV, gECHO_MODEL, gECHO_MOD, gECHO_RTIME, gECHO_DRIVE, gECHO_DUCK, -1}; return e[k % 12]; }   // 12 = FREEZE pad
        case fxReverb: return k < 6 ? gREV_SIZE + k : (k == 6 ? gREV_MODEL : gREV_MOD);
        case fxFilter: return k < 3 ? gMF_HP + k : -1;            // 4 = ON pad
        case fxChorus: return gCHO_MODEL + k;
        case fxChaos: return k < 7 ? gGL_CHANCE + k : -1;         // 8 = HOLD pad
        default: return gEQ_LOW + k;
        }
    }
    const char* masterName(int g) const
    {
        if (g == gECHO_MOD) return kEchoModLabel[echoModelIndex((int)gp(gECHO_MODEL))];
        if (g == gREV_MOD) return kReverbModLabel[reverbModelIndex((int)gp(gREV_MODEL))];
        if (g == gECHO_RTIME) return "R TIME";
        if (g == gECHO_DRIVE) return "DRIVE";
        if (g == gECHO_DUCK) return "DUCK";
        return kMasterNames[g - gECHO_TIME];
    }
    static std::string hzText(float h) { char b[24]; if (h >= 1000.f) std::snprintf(b, sizeof(b), "%.1fk", h / 1000.f); else std::snprintf(b, sizeof(b), "%d", (int)std::lround(h)); return b; }
    std::string masterText(int g, float v) const
    {
        char b[32];
        const int iv = (int)std::lround(v);
        auto pct = [&](float x) { std::snprintf(b, sizeof(b), "%d%%", (int)std::lround(x / 127.f * 100.f)); return std::string(b); };
        if (g == gMF_HP) return iv <= 0 ? std::string("OFF") : hzText(cutoffHz(v));
        if (g == gMF_LP) return iv >= 127 ? std::string("OFF") : hzText(cutoffHz(v));
        if (g == gMF_RESO || g == gCHO_DEPTH || g == gCHO_FB || g == gCHO_WIDTH || g == gGL_CHANCE || g == gGL_FADE || g == gGL_REV || g == gGL_MIX)
            return (g == gGL_CHANCE && iv == 0) ? std::string("OFF") : pct(v);
        if (g == gCHO_MODEL) return kChorusModelNames[chorusModelIndex(iv)];
        if (g == gCHO_RATE) { std::snprintf(b, sizeof(b), "%.2fHz", expmap(v / 127.f, 0.05f, 8.f)); return b; }
        if (g == gCHO_TIME) {
            const bool fl = chorusModelIndex((int)gp(gCHO_MODEL)) == cmFlanger;
            std::snprintf(b, sizeof(b), "%.1fms", fl ? expmap(v / 127.f, 0.3f, 6.f) : expmap(v / 127.f, 4.f, 30.f)); return b;
        }
        if (g == gCHO_TONE) return iv >= 127 ? std::string("OPEN") : hzText(expmap(v / 127.f, 1200.f, 20000.f));
        if (g == gCHO_LEV || g == gECHO_LEV || g == gREV_LEV) { const float gn = fxReturnGain(v); if (gn < 0.001f) return "OFF"; std::snprintf(b, sizeof(b), "%+.1fdB", 20.f * std::log10(gn)); return b; }
        if (g == gGL_SIZE) return kGlSizeNames[glSizeIndex(iv)];
        if (g == gGL_LEN) return kGlLenNames[glLenIndex(iv)];
        if (g == gGL_PITCH) { const int st = (int)glPitchSemis(v); if (st == 0) return "0 ST"; std::snprintf(b, sizeof(b), "%+d ST", st); return b; }
        if (g == gECHO_TIME) return kEchoNames[echoDivIndex(iv)];
        if (g == gECHO_MODEL) return kEchoModelNames[echoModelIndex(iv)];
        if (g == gREV_MODEL) return kReverbModelNames[reverbModelIndex(iv)];
        if (g == gREV_GATE && iv == 0) return "OFF";
        if (g == gECHO_RTIME) { if (iv == 64) return "= L"; std::snprintf(b, sizeof(b), "x%.2f", 1.f + (v - 64.f) / 128.f); return b; }
        if (g == gECHO_DRIVE || g == gECHO_DUCK || g == gDRIVE) return iv == 0 ? std::string("OFF") : pct(v);
        if (g == gEQ_LOW || g == gEQ_HIGH) { std::snprintf(b, sizeof(b), "%+d", iv - 64); return b; }
        if (g == gVOLUME) { const float mv = v / 127.f, gn = mv * mv * 1.2f; if (gn < 1e-4f) return "-INF"; std::snprintf(b, sizeof(b), "%+.1fdB", 20.f * std::log10(gn)); return b; }
        std::snprintf(b, sizeof(b), "%d", iv); return b;
    }
    // the square pads in the effect knob rows: echo FREEZE, filter ON, CHAOS HOLD
    int padSlot() const { return fxPage == fxEcho ? 11 : (fxPage == fxFilter ? 3 : (fxPage == fxChaos ? 7 : -1)); }
    R padRect() const { const R k = fxKnob(padSlot()); return {k.x + k.w * 0.5f - 30.f, k.y + 12.f, 60.f, 60.f}; }
    bool padOn() const
    {
        if (fxPage == fxEcho) return gp(gECHO_FREEZE) > 0.5f;
        if (fxPage == fxFilter) return gp(gMF_ON) > 0.5f;
        return chaosHeld;
    }
    void drawRight()
    {
        panelBox({712.f, 116.f, 454.f, 330.f});
        static const char* names[kFxPages] = {"ECHO", "REVERB", "CHORUS", "CHAOS", "FILTER", "MASTER"};
        const bool active[kFxPages] = {gp(gECHO_LEV) > 0.5f, gp(gREV_LEV) > 0.5f, gp(gCHO_LEV) > 0.5f, gp(gGL_CHANCE) > 0.5f || chaosHeld,
                                       gp(gMF_ON) > 0.5f && (gp(gMF_HP) > 0.5f || gp(gMF_LP) < 126.5f), false};
        for (int i = 0; i < kFxPages; ++i) {
            button(fxTab(i), names[i], fxPage == i, 9.5f);
            if (active[i] && i >= 2) { const R r = fxTab(i); beginPath(); circle(r.x + 7, r.cy(), 2.5f); fillColor(fxPage == i ? th->selText : ledColor()); fill(); }
        }
        for (int k = 0; k < fxSlots(); ++k) {
            if (k == padSlot()) {
                const R p = padRect(); const bool on = padOn();
                if (on) box(p, th->accent, 6.f); else { box(p, th->btnFill, 6.f); frame(p, th->btnEdge, 6.f); }
                label(p.cx(), p.cy() + 1, fxPage == fxEcho ? "FREEZE" : (fxPage == fxFilter ? "ON" : "HOLD"), 10.f, on ? th->accentText : th->btnText);
                continue;
            }
            const int g = fxParam(k);
            if (g < 0) continue;
            drawKnob(fxKnob(k), gp(g) / 127.f, masterName(g), masterText(g, gp(g)), false, drag == dkGlobal && dragIndex == g, 21.f, 32.f, 68.f, 85.f);
        }
        label(724, 369, "PATTERNS", 10.f, th->dim, ALIGN_LEFT | ALIGN_MIDDLE);
        button(chainBtn(), "CHAIN", bank->chain.active(), 9.f);
        button(patCopy(), "COPY", false, 9.f); button(patPaste(), "PASTE", false, 9.f); button(patClear(), "CLEAR", false, 9.f);
        const int ep = editPattern(), pp = (int)std::lround(gp(gOUT_PATTERN));
        const bool running = gp(gOUT_STEP) >= 0.f;
        for (int i = 0; i < kPatterns; ++i) {
            char b[8]; std::snprintf(b, sizeof(b), "A%02d", i + 1);
            const R r = patBtn(i);
            if (i == ep) box(r, th->selFill); else { box(r, th->btnFill); frame(r, th->btnEdge); }
            bool any = false; for (int t = 0; t < kTracks && !any; ++t) any = !bank->patterns[i].trackEmpty(t);
            number(r.cx(), r.cy() + 1, b, 13.f, i == ep ? th->selText : (any ? th->text : th->faint));
            if (running && i == pp) { beginPath(); rect(r.x + 4, r.y + r.h - 3, r.w - 8, 2); fillColor(th->accent); fill(); }
        }
    }
    // the bottom bar: REC / CLICK, RESET, FILL, UNDO, presets
    void drawBottom()
    {
        const R rb = recBtn();
        if (recOn) box(rb, Color(236, 36, 50)); else { box(rb, th->btnFill); frame(rb, th->btnEdge); }
        beginPath(); circle(rb.x + 22, rb.cy(), 5.f); fillColor(recOn ? Color(255, 255, 255) : Color(236, 36, 50)); fill();
        label(rb.x + 62, rb.cy() + 1, "REC", 12.f, recOn ? Color(255, 255, 255) : th->btnText, ALIGN_CENTER | ALIGN_MIDDLE, th->fontButton);
        button(clickBtn(), clickOn ? "CLICK ON" : "CLICK OFF", clickOn, 9.f);
        button(resetBtn(), "RESET", false, 11.f);
        button(fillHoldBtn(), "FILL (HOLD)", fillHeld, 10.5f);
        const bool can = !undoStack.empty();
        {   // UNDO in red, as the other Kinetik plug-ins
            const R u = undoBtn();
            if (can) { box(u, th->btnFill); frame(u, th->lock, -1.f, 1.5f); } else { box(u, th->btnFill); frame(u, Color(th->btnEdge, th->bg, 0.5f)); }
            label(u.cx(), u.y + 24, "UNDO", 11.f, can ? th->lock : th->faint, ALIGN_CENTER | ALIGN_MIDDLE, th->fontButton);
            label(u.cx(), u.y + 44, can ? undoStack.back().what.c_str() : (!redoStack.empty() ? "right-click: redo" : ""), 9.f, th->dim);
        }
        const R pb = presetBtn();
        box(pb, th->btnFill); frame(pb, th->text, -1.f, 1.5f);
        label(pb.x + 12, pb.cy() + 1, "PRESETS", 9.f, th->dim, ALIGN_LEFT | ALIGN_MIDDLE);
        number(pb.x + pb.w - 30, pb.cy() + 1, presetNameStr.c_str(), 15.f, th->text, ALIGN_RIGHT | ALIGN_MIDDLE);
        beginPath(); moveTo(pb.x + pb.w - 18, pb.cy() - 6); lineTo(pb.x + pb.w - 10, pb.cy()); lineTo(pb.x + pb.w - 18, pb.cy() + 6); closePath(); fillColor(th->text); fill();
        button(saveBtn(), "SAVE AS PRESET", false, 9.5f);
        lcdCell(scaleBox(), "SCALE", kScaleNames[bank->scale % kScales]);
        lcdCell(rootBox(), "ROOT", kRootNames[bank->root % 12]);
    }
    void drawReset()
    {
        const R bx = resetBox();
        overlayBox(bx);
        number(bx.x + 30, bx.y + 40, "Reset the sound?", 22.f, th->text, ALIGN_LEFT | ALIGN_MIDDLE);
        label(bx.x + 30, bx.y + 82, "Every knob and effect goes back to its default.", 10.5f, th->text, ALIGN_LEFT | ALIGN_MIDDLE);
        label(bx.x + 30, bx.y + 104, "The samples, materials, slices and patterns stay. UNDO brings it back.", 10.f, th->dim, ALIGN_LEFT | ALIGN_MIDDLE);
        button(resetCancel(), "CANCEL", false, 11.f);
        button(resetOk(), "RESET", true, 12.f);
    }

    // the sequencer of the selected track: lanes as KMSP-6
    void drawSequencer()
    {
        panelBox({14.f, 454.f, 1152.f, 258.f});
        static const char* lanes[kLanes] = {"TRIG", "NOTE", "LEN", "CHORD", "RATCH", "COND"};
        for (int i = 0; i < kLanes; ++i) button(laneBtn(i), lanes[i], lane == i, 10.f);
        Pattern& p = pat();
        const int t = selTrack, len = p.len[t];
        const int now = trackStepNow(t);
        char b[48];
        for (int i = 0; i < kStepPages; ++i) {
            std::snprintf(b, sizeof(b), "%d", i + 1);
            const R r = stepPageBtn(i);
            if (i * 16 < len) {
                button(r, b, stepPg == i, 9.5f);
                if (now >= 0 && now / 16 == i) { beginPath(); rect(r.x + 4, r.y + r.h - 3, r.w - 8, 2); fillColor(th->accent); fill(); }
            } else label(r.cx(), r.cy() + 1, b, 10.f, th->faint);
        }
        button(lenMinus(), "-", false, 12.f); button(lenPlus(), "+", false, 12.f);
        box(lenBox(), th->lcdBg); std::snprintf(b, sizeof(b), "L:%d", len);
        label(lenBox().cx(), lenBox().cy() + 1, b, 11.f, th->lcdText);
        std::snprintf(b, sizeof(b), "SPEED %s", kSpeedNames[p.speed[t] % kSpeeds]);
        button(speedBtn(), b, p.speed[t] != 0, 9.5f);
        for (const R& r : {rotL(), rotR()}) {
            button(r, "", false, 10.f);
            label(r.cx(), r.cy() + 1, r.x == rotL().x ? "\xE2\x97\x80" : "\xE2\x96\xB6", 9.f, th->btnText, ALIGN_CENTER | ALIGN_MIDDLE, fnSans);
        }
        button(clearTrackBtn(), "CLEAR", false, 9.5f);
        button(copyTrackBtn(), trackClip ? "COPIED" : "COPY", false, 9.5f);
        button(pasteTrackBtn(), "PASTE", false, 9.5f);
        button(lockClrAll(), "-LOCKS", false, 9.5f);
        button(allOnBtn(), "ALL", false, 9.5f);
        if (!patMsg.empty()) label(1160, 446, patMsg.c_str(), 9.f, th->lock, ALIGN_RIGHT | ALIGN_MIDDLE);
        const bool neon = th->logo == lgNeon;
        for (int i = 0; i < 16; ++i) {
            const int s = stepPg * 16 + i;
            const R r = stepCell(i);
            const bool inLen = s < len, on = inLen && p.on[t][s];
            const Color bg = inLen ? ((i / 4) % 2 ? th->stepAlt : th->stepFill) : th->stepOff;
            box(r, bg);
            if (s == now && inLen) frame(r, th->logo == lgRiso ? th->text : th->selFill, -1.f, 2.5f);
            else if (lockStep == s && lockTrack == t) frame(r, th->lock, -1.f, 3.f);
            else if (lane == lnNote && s == noteStep) frame(r, th->lock, -1.f, 2.f);
            else frame(r, Color(th->btnEdge, bg, 0.5f));
            const R led{r.x + 8, r.y + 7, r.w - 16, 9};
            if (on) {
                const Color lc = neon ? Color(236, 36, 50) : th->accent;
                if (th->glow) { beginPath(); rect(led.x - 4, led.y - 4, led.w + 8, led.h + 8); fillPaint(boxGradient(led.x, led.y, led.w, led.h, 2, 8, Color(lc, Color(0, 0, 0, 0.f), 0.5f), Color(0, 0, 0, 0.f))); fill(); }
                box(led, lc, 1.5f);
            } else box(led, inLen ? th->ledOff : Color(th->ledOff, th->bg, 0.5f), 2.f);
            std::snprintf(b, sizeof(b), "%d", s + 1);
            label(r.x + 6, r.y + 24, b, 9.f, inLen ? th->dim : th->faint, ALIGN_LEFT | ALIGN_MIDDLE);
            if (s % 4 == 0) box({r.x + 6.f, r.y + 30.f, s + 1 < 10 ? 7.f : 13.f, 1.f}, inLen ? th->dim : th->faint);
            if (inLen && p.hasLocks(t, s)) { beginPath(); circle(r.x + r.w - 9, r.y + 24, 2.8f); fillColor(th->lock); fill(); }
            if (!on) continue;
            const char* big = nullptr; char nn[16];
            switch (lane) {
            case lnLen: big = kGateNames[p.gate[t][s] % kGateCount]; break;
            case lnChord: big = kChords[p.chord[t][s] % kChordCount].name; break;
            case lnRatch: std::snprintf(nn, sizeof(nn), "x%d", kRatchValues[p.ratch[t][s] % kRatchCount]); big = nn; break;
            case lnCond: big = p.cond[t][s] ? kConds[p.cond[t][s] % kNumConds].name : "-"; break;
            default: big = noteName(p.note[t][s], nn, sizeof(nn)); break;
            }
            number(r.cx(), r.y + 44, big, 16.f, th->stepText);
            // gate bar (one step = full width, longer = arrow, HOLD = until the next note)
            const int gi = p.gate[t][s] % kGateCount;
            const float g = gi == kGateHold ? 99.f : kGateSteps[gi];
            const Color gc = Color(th->stepText, bg, 0.45f);
            box({r.x + 8, r.y + 58, (r.w - 16) * std::min(1.f, g), 3}, gc, 1.f);
            if (g > 1.f) { beginPath(); moveTo(r.x + r.w - 8, r.y + 56); lineTo(r.x + r.w - 3, r.y + 59.5f); lineTo(r.x + r.w - 8, r.y + 63); closePath(); fillColor(gc); fill(); }
            std::string ex;
            if (lane != lnChord && p.chord[t][s]) { ex += kChords[p.chord[t][s] % kChordCount].name; ex += " "; }
            if (lane != lnRatch && p.ratch[t][s]) { std::snprintf(nn, sizeof(nn), "x%d ", kRatchValues[p.ratch[t][s] % kRatchCount]); ex += nn; }
            if (lane != lnCond && p.cond[t][s]) { ex += kConds[p.cond[t][s] % kNumConds].name; ex += " "; }
            if (p.locks[t][s][tpSLICE] != kNoLock && bank->kit[t].nSlices > 1) {
                const int sl = sliceOf(p.locks[t][s][tpSLICE], bank->kit[t].nSlices);
                ex += sl < 0 ? std::string("ALL") : "S" + std::to_string(sl + 1);
            }
            if (!ex.empty()) label(r.cx(), r.y + 71, ex.c_str(), 8.5f, th->extra);
        }
    }

    void drawKeyboard()
    {
        button(octDown(), "-", false, 14.f); button(octUp(), "+", false, 14.f);
        const R ob = octBox();
        box(ob, th->lcdBg); frame(ob, th->panelEdge, -1.f, th->panelEdgeWidth);
        label(ob.cx(), ob.y + 16, "OCT", 9.f, th->lcdDim);
        char nb[16];
        number(ob.cx(), ob.cy(), noteName(kbLow(), nb, sizeof(nb)), 17.f, th->lcdText);
        const int lo = kbLow(), hi = lo + 12 * kKbOctaves;
        const int stepNote = (lane == lnNote && noteStep >= 0 && pat().on[selTrack][noteStep]) ? pat().note[selTrack][noteStep] : -1;
        auto lum = [](const Color& c) { return 0.3f * c.red + 0.59f * c.green + 0.11f * c.blue; };
        const Color whiteKey = lum(th->text) > lum(th->stepFill) ? Color(th->text, th->panel, 0.12f) : th->stepFill;
        const Color blackKey = lum(th->knobBody) < lum(th->text) ? th->knobBody : th->text;
        int lit[128]; for (int& v : lit) v = -1;            // keys of the sounding voices, in their colour
        for (int i = 0; i < vvN; ++i) if (!vv[i].released && vv[i].note >= 0 && vv[i].note < 128) lit[vv[i].note] = vv[i].slot;
        auto keyColor = [&](int n, bool black) -> Color {
            if (lit[n] >= 0) return Color(voiceColor(lit[n]), black ? blackKey : whiteKey, 0.15f);
            if (keyIsHeld(n)) return th->accent;
            if (n == stepNote) return th->lock;
            if (bank->scale && !inScale(n, bank->root, bank->scale)) return Color(black ? blackKey : whiteKey, th->panel, 0.6f);   // out of the SCALE
            if (n == kBaseNote) return Color(black ? blackKey : whiteKey, th->accent, 0.25f);   // the sample's own pitch
            return black ? blackKey : whiteKey;
        };
        for (int n = lo; n <= hi; ++n) {
            if (isBlack(n)) continue;
            const R r = keyRect(n);
            box(r, keyColor(n, false), 3.f);
            if (n % 12 == 0) label(r.cx(), r.y + r.h - 10, noteName(n, nb, sizeof(nb)), 8.5f, Color(blackKey, whiteKey, 0.3f));
        }
        for (int n = lo; n <= hi; ++n) if (isBlack(n)) box(keyRect(n), keyColor(n, true), 2.f);
        std::snprintf(nb, sizeof(nb), "T%d", selTrack + 1);
        label(904, 594, (std::string("KEYS + MIDI CH 1 PLAY ") + nb).c_str(), 10.f, th->text, ALIGN_LEFT | ALIGN_MIDDLE);
        button(keyChordBtn(), (std::string("CHORD: ") + kChords[keyChord % kChordCount].name).c_str(), keyChord != 0, 9.5f);
        button(latchBtn(), "LATCH", keyLatch, 10.f);
        button(pcKeysBtn(), pcKeys ? "PC KEYS: ON (A W S E D F...)" : "PC KEYS: OFF", pcKeys, 9.5f);
        button(notesOffBtn(), "ALL NOTES OFF", false, 10.f);
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
        for (int i = 0; i < kNumPresets && i < kPresetRows; ++i) {
            const R r = presetItem(0, i);
            const bool sel = presetNameStr == kPresets[i].name;
            if (sel) box(r, th->selFill); else box(r, th->btnFill);
            number(r.x + 10, r.cy() + 1, kPresets[i].name, 13.f, sel ? th->selText : th->text, ALIGN_LEFT | ALIGN_MIDDLE);
            label(r.x + r.w - 10, r.cy() + 1, kPresets[i].desc, 8.5f, sel ? th->selText : th->dim, ALIGN_RIGHT | ALIGN_MIDDLE);
        }
        if (userPresets.empty()) label(600, 145, "none yet: SAVE AS PRESET stores the knobs, the 16 patterns and your samples", 9.5f, th->faint, ALIGN_LEFT | ALIGN_MIDDLE);
        for (int i = 0; i < (int)userPresets.size() && i < kPresetRows; ++i) {
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
        for (int i = 0; i < kNumPresets && i < kPresetRows; ++i) if (presetItem(0, i).in(x, y)) { loadFactory(i); return; }
        for (int i = 0; i < (int)userPresets.size() && i < kPresetRows; ++i) if (presetItem(1, i).in(x, y)) { loadUser(userPresets[i]); return; }
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
        // older files: 0.2 = 16 knobs per track ("KGRN4-PRESET 1"), 0.3 = 20 ("2"), 0.4 = 40 ("3"), 0.5 / 0.6 = 50 ("4"):
        // their knobs go through the 0.5 layout to the current one; old globals are mapped where they have an equivalent
        int ver = 5;
        for (int vv = 1; vv <= 4; ++vv) if (all.rfind("KGRN4-PRESET " + std::to_string(vv), 0) == 0) ver = vv;
        std::vector<float> nv(values, values + kTotalParams);
        if (ver < 5) for (int t = 0; t < kTracks; ++t) for (int k = 0; k < kTrackParams; ++k) nv[tpIndex(t, k)] = trackParamDefault(k);
        if (ver < 4) for (int g = 0; g < gTEMPO; ++g) nv[kGlobalBase + g] = globalDefault(g);
        // old echo time (8 values) -> the 12 echo divisions
        auto echoTime = [](float v) { static const int to[8] = {1, 2, 3, 4, 5, 6, 8, 9}; return (float)(to[bin8(v)] * 128 / 12 + 5); };
        auto oldGlobal = [&](int i, float& v) -> int {          // index after the tracks in an old file -> global (-1 = none)
            if (ver == 4) return i < gOUT_STEP ? i : -1;          // 0.5 / 0.6: the same globals
            if (ver < 3) {
                switch (i) { case 0: return gREV_SIZE; case 2: v = echoTime(v); return gECHO_TIME; case 3: v *= 0.88f; return gECHO_FB;
                             case 5: return gTEMPO; case 6: return gPLAY; case 7: return gPATTERN; case 8: return gFREEZE; case 9: return gCAPLEN; default: return -1; }
            }
            switch (i) { case 0: return gREV_SIZE; case 5: v = echoTime(v); return gECHO_TIME; case 6: v *= 0.88f; return gECHO_FB;
                         case 18: return gTEMPO; case 19: return gPLAY; case 20: return gPATTERN; case 21: return gFREEZE; case 22: return gCAPLEN; default: return -1; }
        };
        std::string smpLines;
        const size_t bankPos = all.find("\nBANK\n");
        const std::string head = all.substr(0, bankPos == std::string::npos ? all.size() : bankPos);
        size_t st = 0;
        while (st < head.size()) {
            size_t en = head.find('\n', st); if (en == std::string::npos) en = head.size();
            int idx; float v;
            if (head.compare(st, 4, "SMP ") == 0) smpLines += head.substr(st + 4, en - st - 4) + "\n";
            else if (std::sscanf(head.c_str() + st, "V %d %f", &idx, &v) == 2 && std::isfinite(v)) {
                if (ver < 5) {
                    const int per = ver == 1 ? 16 : (ver == 2 ? 20 : (ver == 3 ? 40 : 50)), base = per * kTracks;
                    if (idx < base) {
                        const int k = idx % per, t = idx / per;
                        int k5 = ver == 1 ? kKnobs02[k] : (ver == 2 ? kKnobs03[k] : (ver == 3 ? kKnobs04[k] : k));
                        int iv = (int)std::lround(v);
                        if (ver == 3) iv = convertKnob04(k, iv);
                        else if (ver < 3 && k5 == o5PITCH) k5 = -1;      // pitch: now the step notes
                        else if (ver < 3 && k5 == o5DECAY) { k5 = o5RELEASE; if (iv >= 127) iv = 60; }
                        int ks[2], vs[2]; const int n = k5 < 0 ? 0 : convertKnob05(k5, iv, ks, vs);
                        for (int j = 0; j < n; ++j) nv[tpIndex(t, ks[j])] = clampf((float)vs[j], 0.f, 127.f);
                        idx = -1;
                    } else { const int g = oldGlobal(idx - base, v); idx = g < 0 ? -1 : kGlobalBase + g; }
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
        std::fprintf(fp, "KGRN4-PRESET 5\nNAME %s\n", saveName.c_str());
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
        if (m > 0) bank->kit[t].beats = kMaterialBeats[bank->kit[t].material];   // rhythmic materials follow the tempo
        else { const SampleData* sd = srcOf(t); bank->kit[t].beats = sd && looksLikeLoop(sd->name) ? (uint8_t)guessBeats(sd->pcm.size() / sd->sr, sd->name) : 0; }
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
        // a file named as a loop ("loop", "break", "... bpm") is synced to the tempo at once
        bank->kit[t].beats = looksLikeLoop(d->name) ? (uint8_t)guessBeats(d->pcm.size() / d->sr, d->name) : 0;
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
        for (int i = 0; i < kTracks; ++i) if (trackTab(i).in((float)x, (float)y)) t = i;
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
            if (soloBtn(t).in(x, y)) return "SOLO: hear only the soloed tracks.";
            if (muteBtn(t).in(x, y)) return "MUTE: silence the track without losing its steps.";
            if (trackTab(t).in(x, y)) return "Select the track (drop a sample on it to load it there).";
        }
        if (waveR().in(x, y)) return liveTrack(selTrack) ? "LIVE: the last bars of the input (right = now). Drag the S / E flags: START and END; click elsewhere: POSITION."
                                                         : "Drag the S / E flags: START and END of what plays. Click: the slice under the mouse and the start (POSITION). Green lines: the voices playing.";
        if (mixZone().in(x, y)) return "MIX: SAMPLE (bottom, the sample played straight from POSITION at the note's pitch) to GRAINS (top). Drag, scroll; double-click = 50 / 50. Can be locked per step.";
        if (trackResetBtn().in(x, y)) return "RESET TRACK: this track's knobs back to their defaults (MIX 50 / 50); its sample, slices and steps stay. UNDO brings them back.";
        for (int i = 0; i < 16; ++i) if (stepCell(i).in(x, y))
            return "Click: edit the step for the lane chosen above (right-click on TRIG / NOTE / LEN: lock knobs). Wheel: note; SHIFT + wheel: length; CTRL + wheel: octave; ALT + wheel: ratchet; CTRL + SHIFT: chord; ALT + SHIFT: condition.";
        if (loadBtn().in(x, y)) return "Load a WAV or AIFF file on the selected track (or drop one on the window).";
        if (matBtn().in(x, y)) return "Built-in materials to play without your own samples, or LIVE: the audio input, granulated as it comes. Click = next, right-click = previous (SAMPLE = your file).";
        if (captureBtn().in(x, y)) return "CAPTURE: the last bars of the input (see CAPTURE at the top) become this track's sample, sliced and saved with the project.";
        if (modeBtn().in(x, y)) return std::string(kModeHelp[bank->kit[selTrack].mode % kGrainModes]) + ". Click = next mode, right-click = previous.";
        if (loopBtn().in(x, y) && !liveTrack(selTrack) && !lockMode()) return "LOOP SYNC: the sample is a loop of n beats and plays in time at the tempo (time stretched by the grains: the pitch stays). Click = on (beats guessed) / off, right-click or scroll = beats. SCAN = speed against the tempo.";
        if (scaleBox().in(x, y)) return "SCALE: every grain's pitch (step notes, keys, PITCH, LFOs) moves to the nearest note of the scale. Click = next, right-click = previous.";
        if (rootBox().in(x, y)) return "ROOT of the scale (C = the sample's own pitch). Click = up, right-click = down.";
        if (zoomBtn2().in(x, y)) return "ZOOM: the display shows START to END, so every grain is visible; off = the whole sample.";
        if (syncBtn().in(x, y)) return "SYNC: SIZE in note values (1/64 to 2 bars); in CLOUD, regular grains fall on the tempo grid.";
        if (legatoBtn().in(x, y)) return "LEGATO: a single-note step does not start the sound again, it changes the playing note (pitch, locks, length).";
        if (inBox().in(x, y)) return "Input level. KGRN-4 hears a track routed to its side-chain input (LIVE tracks, CAPTURE).";
        if (freezeBtn().in(x, y)) return "FREEZE: the input buffer stops recording; LIVE tracks keep playing what it holds.";
        if (capLenBox().in(x, y)) return "How much of the input CAPTURE takes and LIVE tracks see: 1, 2, 4 or 8 bars. Click / scroll to change.";
        if (sliceModeBtn().in(x, y)) return "How the sample is cut: at its transients (hits) or into equal slices.";
        if (amtBox().in(x, y) || amtMinus().in(x, y) || amtPlus().in(x, y)) return "TRANSIENTS: sensitivity (more = more slices). EQUAL: number of slices.";
        for (int i = 0; i < kPages; ++i) if (pageTab(i).in(x, y)) return "Knob pages of the selected track. A dot on an LFO tab: that LFO moves a knob (its colour on the knob it moves).";
        for (int i = 0; i < pageCount(); ++i) if (pageKnob(i).in(x, y)) {
            const int k = kPageFirst[page] + i;
            return std::string(kTP[k].name) + ": " + kTP[k].help + ". Double-click = reset; with a step locked it sets that step only.";
        }
        for (int i = 0; i < kFxPages; ++i) if (fxTab(i).in(x, y)) return "ECHO, REVERB, CHORUS and CHAOS are fed by each track's sends (FX page); FILTER and MASTER work on the whole mix.";
        if (padSlot() >= 0 && padRect().in(x, y))
            return fxPage == fxEcho ? "FREEZE: the echo keeps repeating what it holds, nothing new goes in." : (fxPage == fxFilter ? "Master filter on / off." : "HOLD: CHAOS repeats the current slice while pressed. Right-click: latch on / off.");
        for (int i = 0; i < fxSlots(); ++i) if (fxKnob(i).in(x, y) && fxParam(i) >= 0) {
            const int g = fxParam(i);
            if (g == gECHO_MODEL) return std::string("Echo model: ") + kEchoModelInfo[echoModelIndex((int)gp(g))];
            if (g == gREV_MODEL) return std::string("Reverb model: ") + kReverbModelInfo[reverbModelIndex((int)gp(g))];
            if (g == gCHO_MODEL) return std::string("Chorus model: ") + kChorusModelInfo[chorusModelIndex((int)gp(g))];
            if (g == gGL_CHANCE) return "CHAOS: chance per bar that a slice of the sent tracks is caught and repeated (SIZE, LENGTH, PITCH, FADE, REVERSE, MIX).";
            return std::string(masterName(g)) + ". Double-click = reset.";
        }
        if (recBtn().in(x, y)) return "REC: while playing, notes you play (keyboard, MIDI) are written on the selected track's steps, and knob moves become locks.";
        if (clickBtn().in(x, y)) return "Metronome while recording.";
        if (resetBtn().in(x, y)) return "RESET: every knob and effect back to its default; the samples, materials, slices and patterns stay (UNDO brings it back).";
        for (int i = 0; i < kLanes; ++i) if (laneBtn(i).in(x, y)) {
            static const char* t[kLanes] = {"TRIG: click = a note on the step (drag to paint). Right-click: lock knobs on this step.",
                                            "NOTE: drag a step up / down (semitones), double-click = C3 (the sample's pitch); the keyboard sets the selected step.",
                                            "LEN: drag a step up / down: how long its note lasts (HOLD = until the next note).",
                                            "CHORD: click = next chord, right-click = previous: the step plays several voices.",
                                            "RATCH: click = more repeats in the step, right-click = fewer.",
                                            "COND: when the step plays: chance, FILL, first loop, every n loops."};
            return t[i];
        }
        for (int i = 0; i < kStepPages; ++i) if (stepPageBtn(i).in(x, y)) return "Show steps 1-16 ... 113-128. A page past the end makes the track (and the pattern) that long.";
        if (chainBtn().in(x, y)) return "CHAIN: play a list of patterns in a loop, each a number of times.";
        if (lenBox().in(x, y) || lenMinus().in(x, y) || lenPlus().in(x, y)) return "Length of the selected track (1-64 steps): tracks of different lengths drift against each other.";
        if (speedBtn().in(x, y)) return "Speed of the selected track's steps (1x = 16ths). Click = next, right-click = previous.";
        if (rotL().in(x, y) || rotR().in(x, y)) return "Shift the selected track's steps (notes, locks) one step earlier or later.";
        if (allOnBtn().in(x, y)) return "A note on every step of the selected track.";
        if (lockClrAll().in(x, y)) return "Remove every lock of the selected track in this pattern.";
        if (clearTrackBtn().in(x, y)) return "Clear the selected track's steps and locks.";
        if (copyTrackBtn().in(x, y) || pasteTrackBtn().in(x, y)) return "Copy the selected track's steps, notes and locks, then paste them on another track or pattern.";
        if (octDown().in(x, y) || octUp().in(x, y) || octBox().in(x, y)) return "Octave of the keyboard.";
        if (keyAt(x, y) >= 0) return "Play the selected track (6 voices). In the NOTE lane it also sets the selected step's note.";
        if (notesOffBtn().in(x, y)) return "Stop every sounding note.";
        if (fillHoldBtn().in(x, y)) return "FILL while held: steps with the FILL / !FILL condition change (MIDI B2 on channel 1 too). Right-click = latch.";
        if (patLenBox().in(x, y)) return "Pattern length (16ths): a pattern chosen while playing starts when this length ends. Drag or scroll.";
        if (swingBox().in(x, y)) return "Swing: delays every second 16th (50% = straight). Drag or scroll.";
        if (playBtn().in(x, y)) return "Internal play when your music software is stopped (KGRN-4 follows its transport when it plays).";
        if (tempoBox().in(x, y)) return "Internal tempo. Drag or scroll.";
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
    // a step's note / gate / chord / ratchet / condition (editing one of them also turns the step on)
    void setStepData(int s, int note, int gate, int chord, int ratch, int cond)
    {
        const int t = selTrack, p = editPattern();
        char b[96]; std::string c;
        if (!pat().on[t][s]) { std::snprintf(b, sizeof(b), "s %d %d %d 1\n", p, t, s); c += b; }
        std::snprintf(b, sizeof(b), "N %d %d %d %d %d %d %d %d", p, t, s, clampi(note, 0, 127), clampi(gate, 0, kGateCount - 1), clampi(chord, 0, kChordCount - 1),
                      clampi(ratch, 0, kRatchCount - 1), clampi(cond, 0, kNumConds - 1));
        c += b; sendLines(c);
    }
    void setStepNote(int s, int n) { const Pattern& P = pat(); const int t = selTrack; setStepData(s, n, P.gate[t][s], P.chord[t][s], P.ratch[t][s], P.cond[t][s]); }
    void setStepGate(int s, int g) { const Pattern& P = pat(); const int t = selTrack; setStepData(s, P.note[t][s], g, P.chord[t][s], P.ratch[t][s], P.cond[t][s]); }
    void stepExtra(int s, bool right, bool dbl)
    {
        const Pattern& P = pat(); const int t = selTrack;
        int ch = P.chord[t][s], ra = P.ratch[t][s], co = P.cond[t][s];
        if (lane == lnChord) ch = dbl ? 0 : (ch + (right ? kChordCount - 1 : 1)) % kChordCount;
        else if (lane == lnRatch) ra = dbl ? 0 : (ra + (right ? kRatchCount - 1 : 1)) % kRatchCount;
        else co = dbl ? 0 : (co + (right ? kNumConds - 1 : 1)) % kNumConds;
        setStepData(s, P.note[t][s], P.gate[t][s], ch, ra, co);
    }
    bool knobIsHostParam() const { return !(lockMode() && lockTrack == selTrack); }
    float mixAt(float y) const { const R m = mixZone(); return std::round(clampf((m.y + m.h - 8.f - y) / (m.h - 16.f), 0.f, 1.f) * 127.f); }   // top = GRAINS
    void setKnobGesture(int k, float v) { if (knobIsHostParam()) { editParameter(tpIndex(selTrack, k), true); setKnob(k, v); editParameter(tpIndex(selTrack, k), false); } else setKnob(k, v); }
    void setKnob(int k, float v)
    {
        v = clampf(v, 0.f, 127.f);
        if (!knobIsHostParam()) {
            const int iv = (int)std::lround(v);
            if (pat().locks[selTrack][lockStep][k] == iv) return;
            char b[48]; std::snprintf(b, sizeof(b), "k %d %d %d %d %d", editPattern(), selTrack, lockStep, k, iv); sendCmd(b);
        } else {
            setParam(tpIndex(selTrack, k), v);
            if (recOn && playingThis()) {                       // REC: the knob move becomes a lock on the step playing
                const int st = trackStepNow(selTrack);
                if (st >= 0 && pat().locks[selTrack][st][k] != (int)std::lround(v)) {
                    char b[48]; std::snprintf(b, sizeof(b), "k %d %d %d %d %d", editPattern(), selTrack, st, k, (int)std::lround(v)); sendCmd(b);
                }
            }
        }
        repaint();
    }
    void setTrackLen(int n) { pat().len[selTrack] = (uint8_t)clampi(n, 1, kSteps); sendTrackSettings(selTrack); stepPg = std::min(stepPg, (pat().len[selTrack] - 1) / 16); repaint(); }
    void showPatMsg(const std::string& m) { patMsg = m; patMsgUntil = nowSec() + 2.0; repaint(); }
    void selectTrack(int t)
    {
        if (t != selTrack) releaseAllKeys();
        selTrack = t; if (lockTrack != t) lockStep = -1; noteStep = -1;
        char b[16]; std::snprintf(b, sizeof(b), "sel %d", t); setState("cmd", b);   // MIDI channel 1 follows the selection
        repaint();
    }
    void setFill(bool on) { if (fillHeld == on) return; fillHeld = on; sendCmd(on ? "fill 1" : "fill 0"); repaint(); }
    void setChaos(bool on) { chaosHeld = on; if (direct) direct->setChaosHold(on); else setState("cmd", on ? "gh 1" : "gh 0"); repaint(); }
    // REC: notes played while the sequencer runs are written on the steps (one undo per take); knob moves become locks
    void setRec(bool on)
    {
        if (on == recOn) return;
        recOn = on;
        if (on) { snapshot("rec"); recTake.clear(); }
        if (direct) { direct->setClick(clickOn); direct->setRecording(on); }
        else { setState("cmd", clickOn ? "click 1" : "click 0"); setState("cmd", on ? "rec 1" : "rec 0"); }
        repaint();
    }
    struct TakeStep { int p, t, s; std::vector<int> notes; };
    std::vector<TakeStep> recTake;
    void pollRecorded()
    {
        if (!direct) return;
        RecEvent e; std::string cmd; char b[96];
        const bool was = restoring; restoring = true;          // the take is one undo step (taken when REC was armed)
        while (direct->popRecorded(e)) {
            if (e.pattern < 0 || e.pattern >= kPatterns || e.track < 0 || e.track >= kTracks || e.step < 0 || e.step >= kSteps) continue;
            Pattern& P = bank->patterns[e.pattern];
            TakeStep* ts = nullptr;
            for (auto& x : recTake) if (x.p == e.pattern && x.t == e.track && x.s == e.step) ts = &x;
            if (e.kind == 0) {
                if (!ts) { recTake.push_back({e.pattern, e.track, e.step, {}}); ts = &recTake.back(); }
                if (std::find(ts->notes.begin(), ts->notes.end(), (int)e.note) == ts->notes.end()) ts->notes.push_back(e.note);
                // several notes on one step: a chord if their intervals match one, otherwise the lowest note
                std::vector<int> n = ts->notes; std::sort(n.begin(), n.end());
                int chord = 0;
                for (int c = 1; c < kChordCount && n.size() > 1; ++c) {
                    if (kChords[c].n != (int)n.size()) continue;
                    bool ok = true; for (size_t i = 0; i < n.size(); ++i) ok = ok && n[i] - n[0] == kChords[c].notes[i];
                    if (ok) { chord = c; break; }
                }
                std::snprintf(b, sizeof(b), "s %d %d %d 1\nN %d %d %d %d %d %d %d %d\n", e.pattern, e.track, e.step, e.pattern, e.track, e.step,
                              n[0], P.gate[e.track][e.step], chord, P.ratch[e.track][e.step], P.cond[e.track][e.step]);
                cmd += b;
            } else if (ts) {
                std::snprintf(b, sizeof(b), "N %d %d %d %d %d %d %d %d\n", e.pattern, e.track, e.step, P.note[e.track][e.step], e.gate,
                              P.chord[e.track][e.step], P.ratch[e.track][e.step], P.cond[e.track][e.step]);
                cmd += b;
            }
            if (!cmd.empty()) { cmd.pop_back(); sendLines(cmd); cmd.clear(); }
        }
        restoring = was;
    }
    // RESET: every knob and effect to its default; the samples, materials, slices, patterns, tempo stay
    // RESET TRACK: the selected track's knobs to their defaults (MIX 50 / 50), its sample, slices and steps stay
    void resetTrack(int t)
    {
        snapshot("reset track");
        std::vector<float> nv(values, values + kTotalParams);
        for (int k = 0; k < kTrackParams; ++k) nv[tpIndex(t, k)] = trackDefault(t, k);
        std::unique_ptr<Bank> nb(new Bank(*bank));
        static const int defMode[kTracks] = {gmCloud, gmClassic, gmLoop, gmCloud};
        static const uint8_t defSync[kTracks] = {0, 0, 1, 0};
        Kit& k = nb->kit[t]; k.mode = (uint8_t)defMode[t]; k.sync = defSync[t]; k.legato = 0;
        const bool was = restoring; restoring = true;
        applyState(nv.data(), nb.get());
        restoring = was;
        lockStep = -1;
        repaint();
    }
    void resetSound()
    {
        snapshot("reset");
        std::vector<float> nv(values, values + kTotalParams);
        for (int t = 0; t < kTracks; ++t) for (int k = 0; k < kTrackParams; ++k) nv[tpIndex(t, k)] = trackDefault(t, k);
        for (int g = 0; g < gTEMPO; ++g) nv[kGlobalBase + g] = globalDefault(g);
        std::unique_ptr<Bank> nb(new Bank(*bank));
        static const int defMode[kTracks] = {gmCloud, gmClassic, gmLoop, gmCloud};
        static const uint8_t defSync[kTracks] = {0, 0, 1, 0};
        for (int t = 0; t < kTracks; ++t) { Kit& k = nb->kit[t]; k.mode = (uint8_t)defMode[t]; k.sync = defSync[t]; k.legato = 0; k.mute = 0; k.solo = 0; }
        const bool was = restoring; restoring = true;
        applyState(nv.data(), nb.get());
        restoring = was;
        presetNameStr = "Init"; lockStep = -1;
        repaint();
    }
    // the waveform: drag S / E (START / END), click = slice + POSITION
    float waveFrac(float x) const { const R wr = waveR(); return clampf(vA + (x - wr.x - 4) / (wr.w - 8) * (vB - vA), 0.f, 1.f); }
    void waveSet(float x)
    {
        const int t = selTrack;
        const double f = waveFrac(x);
        double ra = 0.0, rb = 1.0;
        const SampleData* s = srcOf(t);
        const Kit& kit = bank->kit[t];
        if (!liveTrack(t) && s && !s->pcm.empty()) {
            const uint32_t frames = (uint32_t)s->pcm.size();
            sliceRegion(kit, sliceOf(knobValue(tpSLICE), kit.nSlices), frames, ra, rb);
            ra /= frames; rb /= frames;
        }
        const double rl = std::max(1e-6, rb - ra);
        if (waveHandle == 1) { setKnob(tpSTART, std::round(clampf((float)((f - ra) / rl), 0.f, 1.f) * 127.f)); return; }
        if (waveHandle == 2) { setKnob(tpEND, std::round(clampf((float)((f - ra) / rl), 0.f, 1.f) * 127.f)); return; }
        double a, e, p; playRegion(a, e, p);
        setKnob(tpPOSITION, std::round(clampf((float)((f - a) / std::max(1e-6, e - a)), 0.f, 1.f) * 127.f));
    }
    void waveDown(float x)
    {
        updateView();
        const int t = selTrack;
        const R wr = waveR(); const float cols = wr.w - 8;
        double a, e, p; playRegion(a, e, p);
        const float xa = fx((float)a), xe = fx((float)e); (void)cols;
        waveHandle = std::fabs(x - xa) < 8.f && std::fabs(x - xa) <= std::fabs(x - xe) ? 1 : (std::fabs(x - xe) < 8.f ? 2 : 0);
        if (waveHandle == 0 && !liveTrack(t)) {               // a click outside the slice picks the slice under the mouse
            const SampleData* s = srcOf(t);
            const Kit& kit = bank->kit[t];
            const int slice = sliceOf(knobValue(tpSLICE), kit.nSlices);
            if (s && slice >= 0) {
                const double f = waveFrac(x);
                int ns = 0; for (int i = 0; i < kit.nSlices; ++i) if (f * 1e6 >= kit.slice[i]) ns = i;
                if (ns != slice) setKnob(tpSLICE, std::round(sliceKnob(ns, kit.nSlices)));
            }
        }
        waveSet(x);
    }
    void pasteTrack()
    {
        const Pattern& c = *trackClip; const int p = editPattern(), t = selTrack, ct = trackClipTrack;
        std::string cmd, row(kSteps, '0');
        char b[96];
        for (int s = 0; s < kSteps; ++s) row[s] = c.on[ct][s] ? '1' : '0';
        cmd += "clt " + std::to_string(p) + " " + std::to_string(t) + "\n";
        cmd += "S " + std::to_string(p) + " " + std::to_string(t) + " " + row + "\n";
        cmd += "L " + std::to_string(p) + " " + std::to_string(t) + " " + std::to_string(c.len[ct]) + " " + std::to_string(c.speed[ct]) + "\n";
        for (int s = 0; s < kSteps; ++s) {
            if (!c.stepDefault(ct, s)) { std::snprintf(b, sizeof(b), "N %d %d %d %d %d %d %d %d\n", p, t, s, c.note[ct][s], c.gate[ct][s], c.chord[ct][s], c.ratch[ct][s], c.cond[ct][s]); cmd += b; }
            for (int k = 0; k < kTrackParams; ++k)
                if (c.locks[ct][s][k] != kNoLock) { std::snprintf(b, sizeof(b), "k %d %d %d %d %d\n", p, t, s, k, c.locks[ct][s][k]); cmd += b; }
        }
        cmd.pop_back(); sendLines(cmd); showPatMsg("pasted"); repaint();
    }

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
        if (ev.press && ev.key == kKeyEscape) { if (overlay != ovNone) overlay = ovNone; else { lockStep = -1; noteStep = -1; } repaint(); return true; }
        if (pcKeys && overlay == ovNone && !(ev.mod & (kModifierControl | kModifierSuper | kModifierAlt))) {
            static const char* row = "awsedftgyhujkolp;";          // C D E F G A B C D E... as a piano, from the keyboard's 2nd octave
            uint k = ev.key; if (k >= 'A' && k <= 'Z') k += 32;
            if (k == 'z' || k == 'x') { if (ev.press) { kbOct = clampi(kbOct + (k == 'x' ? 1 : -1), -1, 6); repaint(); } return true; }
            const char* f = k < 128 ? std::strchr(row, (int)k) : nullptr;
            if (f && k) {
                const int n = clampi(kbLow() + 12 + (int)(f - row), 0, 127);
                if (ev.press && !pcDown[n]) { pcDown[n] = true; keyPress(n); }
                else if (!ev.press && pcDown[n]) { pcDown[n] = false; keyRelease(n); }
                return true;
            }
        }
        return false;
    }

    bool onMouse(const MouseEvent& ev) override
    {
        const float x = ev.pos.getX() / sx, y = ev.pos.getY() / sx;
        if (tipShown) { tipShown = false; repaint(); }
        tipBlockUntil = nowSec() + 1.0;
        if (!ev.press) {
            if (drag == dkKnob && knobIsHostParam()) editParameter(tpIndex(selTrack, dragIndex), false);
            if (drag == dkMix && knobIsHostParam()) editParameter(tpIndex(selTrack, tpGMIX), false);
            if (drag == dkGlobal) editParameter(kGlobalBase + dragIndex, false);
            if (drag == dkTempo) editParameter(kGlobalBase + gTEMPO, false);
            if (drag == dkPatLen || drag == dkSwing) sendPatternSettings();
            if (drag == dkTrackLen) sendTrackSettings(selTrack);
            if (drag == dkKey && mouseKey >= 0) { keyRelease(mouseKey); mouseKey = -1; }
            if (drag == dkFill && !fillLatch) setFill(false);
            if (drag == dkChaos && !chaosLatch) setChaos(false);
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
        if (overlay == ovReset) {
            if (resetOk().in(x, y)) { resetSound(); overlay = ovNone; }
            else if (resetCancel().in(x, y) || !resetBox().in(x, y)) overlay = ovNone;
            repaint(); return true;
        }
        if (recBtn().in(x, y)) { setRec(!recOn); return true; }
        if (clickBtn().in(x, y)) { clickOn = !clickOn; if (direct) direct->setClick(clickOn); else setState("cmd", clickOn ? "click 1" : "click 0"); saveView(); repaint(); return true; }
        if (resetBtn().in(x, y)) { overlay = ovReset; repaint(); return true; }
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
            if (soloBtn(t).in(x, y)) { char b[24]; std::snprintf(b, sizeof(b), "O %d %d", t, bank->kit[t].solo ? 0 : 1); sendCmd(b); repaint(); return true; }
            if (muteBtn(t).in(x, y)) { char b[24]; std::snprintf(b, sizeof(b), "U %d %d", t, bank->kit[t].mute ? 0 : 1); sendCmd(b); repaint(); return true; }
            if (trackTab(t).in(x, y)) { selectTrack(t); return true; }
        }
        // display
        if (lockMode() && lockClearBtn().in(x, y)) { char b[40]; std::snprintf(b, sizeof(b), "kc %d %d %d", editPattern(), lockTrack, lockStep); sendCmd(b); repaint(); return true; }
        if (zoomBtn2().in(x, y) && !liveTrack(selTrack)) { zoom = !zoom; repaint(); return true; }
        if (loopBtn().in(x, y) && !liveTrack(selTrack) && !lockMode()) {
            const SampleData* sd = srcOf(selTrack);
            if (right) stepBeats(selTrack, 1);
            else setBeats(selTrack, bank->kit[selTrack].beats ? 0 : (sd ? guessBeats(sd->pcm.size() / sd->sr, sd->name) : 4));
            return true;
        }
        if (scaleBox().in(x, y)) { setScale((bank->scale + (right ? kScales - 1 : 1)) % kScales, bank->root); return true; }
        if (rootBox().in(x, y)) { setScale(bank->scale, (bank->root + (right ? 11 : 1)) % 12); return true; }
        if (trackResetBtn().in(x, y)) { resetTrack(selTrack); return true; }
        if (mixZone().in(x, y)) {
            if (dbl) { setKnobGesture(tpGMIX, 64.f); return true; }
            drag = dkMix; if (knobIsHostParam()) editParameter(tpIndex(selTrack, tpGMIX), true);
            setKnob(tpGMIX, mixAt(y)); return true;
        }
        if (waveR().in(x, y)) { waveDown(x); drag = dkWave; repaint(); return true; }
        if (loadBtn().in(x, y)) { openSampleBrowser(); return true; }
        if (matBtn().in(x, y)) { setMaterial(selTrack, (bank->kit[selTrack].material + (right ? kMatLive : 1)) % (kMatLive + 1)); return true; }
        if (captureBtn().in(x, y)) { captureTo(selTrack); return true; }
        if (modeBtn().in(x, y)) { Kit& k = bank->kit[selTrack]; k.mode = (uint8_t)((k.mode + (right ? kGrainModes - 1 : 1)) % kGrainModes); sendKit(selTrack); repaint(); return true; }
        if (syncBtn().in(x, y)) { Kit& k = bank->kit[selTrack]; k.sync = k.sync ? 0 : 1; sendKit(selTrack); repaint(); return true; }
        if (legatoBtn().in(x, y)) { Kit& k = bank->kit[selTrack]; k.legato = k.legato ? 0 : 1; sendKit(selTrack); repaint(); return true; }
        if (sliceModeBtn().in(x, y)) { Kit& k = bank->kit[selTrack]; k.sliceMode = k.sliceMode ? 0 : 1; k.sliceAmount = k.sliceMode ? 16 : 50; reslice(selTrack); return true; }
        if (amtMinus().in(x, y) || amtPlus().in(x, y)) {
            Kit& k = bank->kit[selTrack]; const int d = amtPlus().in(x, y) ? 1 : -1;
            k.sliceAmount = (uint8_t)(k.sliceMode ? clampi(k.sliceAmount + d, 1, kMaxSlices) : clampi(k.sliceAmount + d * 10, 0, 100));
            reslice(selTrack); return true;
        }
        for (int i = 0; i < kPages; ++i) if (pageTab(i).in(x, y)) { page = i; repaint(); return true; }
        for (int i = 0; i < pageCount(); ++i) if (pageKnob(i).in(x, y)) {
            const int k = kPageFirst[page] + i;
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
        // effects, patterns, presets
        for (int i = 0; i < kFxPages; ++i) if (fxTab(i).in(x, y)) { fxPage = i; repaint(); return true; }
        if (padSlot() >= 0 && padRect().in(x, y)) {
            if (fxPage == fxEcho) setParamGesture(kGlobalBase + gECHO_FREEZE, gp(gECHO_FREEZE) > 0.5f ? 0.f : 1.f);
            else if (fxPage == fxFilter) setParamGesture(kGlobalBase + gMF_ON, gp(gMF_ON) > 0.5f ? 0.f : 1.f);
            else if (right) { chaosLatch = !chaosHeld; setChaos(!chaosHeld); }
            else { chaosLatch = false; setChaos(true); drag = dkChaos; }
            repaint(); return true;
        }
        for (int i = 0; i < fxSlots(); ++i) if (fxKnob(i).in(x, y) && fxParam(i) >= 0) {
            const int g = fxParam(i);
            if (dbl) { setParamGesture(kGlobalBase + g, globalDefault(g)); repaint(); return true; }
            drag = dkGlobal; dragIndex = g; dragY = y; dragStart = gp(g); editParameter(kGlobalBase + g, true); return true;
        }
        if (chainBtn().in(x, y)) { overlay = ovChain; repaint(); return true; }
        for (int i = 0; i < kPatterns; ++i) if (patBtn(i).in(x, y)) { setParamGesture(kGlobalBase + gPATTERN, (float)i); lockStep = -1; noteStep = -1; repaint(); return true; }
        if (patCopy().in(x, y)) { patClip = editPattern(); showPatMsg("A" + std::to_string(patClip + 1) + " copied"); return true; }
        if (patPaste().in(x, y) && patClip >= 0) { char b[32]; std::snprintf(b, sizeof(b), "cp %d %d", patClip, editPattern()); sendCmd(b); showPatMsg("pasted"); return true; }
        if (patClear().in(x, y)) { char b[24]; std::snprintf(b, sizeof(b), "cl %d", editPattern()); sendCmd(b); lockStep = -1; repaint(); return true; }
        if (undoBtn().in(x, y)) { if (right) redo(); else undo(); return true; }
        if (presetBtn().in(x, y)) { scanUser(); overlay = ovPresets; repaint(); return true; }
        if (saveBtn().in(x, y)) { scanUser(); openSave(); return true; }
        if (fillHoldBtn().in(x, y)) {
            if (right) { fillLatch = !fillHeld; setFill(!fillHeld); }
            else { fillLatch = false; setFill(true); drag = dkFill; }
            return true;
        }
        // sequencer toolbar
        for (int i = 0; i < kLanes; ++i) if (laneBtn(i).in(x, y)) { lane = i; repaint(); return true; }
        for (int i = 0; i < kStepPages; ++i) if (stepPageBtn(i).in(x, y)) { showPage(i); return true; }
        if (lenMinus().in(x, y)) { setTrackLen(pat().len[selTrack] - 1); return true; }
        if (lenPlus().in(x, y)) { setTrackLen(pat().len[selTrack] + 1); return true; }
        if (lenBox().in(x, y)) { drag = dkTrackLen; dragY = y; dragStart = pat().len[selTrack]; return true; }
        if (speedBtn().in(x, y)) { pat().speed[selTrack] = (uint8_t)((pat().speed[selTrack] + (right ? kSpeeds - 1 : 1)) % kSpeeds); sendTrackSettings(selTrack); repaint(); return true; }
        if (rotL().in(x, y) || rotR().in(x, y)) { char b[48]; std::snprintf(b, sizeof(b), "rot %d %d %d", editPattern(), selTrack, rotL().in(x, y) ? -1 : 1); sendCmd(b); repaint(); return true; }
        if (allOnBtn().in(x, y)) {
            std::string row(kSteps, '0'); for (int s = 0; s < pat().len[selTrack]; ++s) row[s] = '1';
            sendCmd("S " + std::to_string(editPattern()) + " " + std::to_string(selTrack) + " " + row); repaint(); return true;
        }
        if (lockClrAll().in(x, y)) {
            std::string c; char b[40];
            for (int s = 0; s < kSteps; ++s) if (pat().hasLocks(selTrack, s)) { std::snprintf(b, sizeof(b), "kc %d %d %d\n", editPattern(), selTrack, s); c += b; }
            if (!c.empty()) { c.pop_back(); sendLines(c); }
            lockStep = -1; repaint(); return true;
        }
        if (clearTrackBtn().in(x, y)) { char b[32]; std::snprintf(b, sizeof(b), "clt %d %d", editPattern(), selTrack); sendCmd(b); lockStep = -1; noteStep = -1; repaint(); return true; }
        if (copyTrackBtn().in(x, y)) { trackClip.reset(new Pattern(pat())); trackClipTrack = selTrack; showPatMsg("T" + std::to_string(selTrack + 1) + " copied"); return true; }
        if (pasteTrackBtn().in(x, y) && trackClip) { pasteTrack(); return true; }
        // steps
        for (int i = 0; i < 16; ++i) {
            if (!stepCell(i).in(x, y)) continue;
            const int t = selTrack, s = stepPg * 16 + i;
            if (s >= pat().len[t]) return true;
            if (right && (lane == lnTrig || lane == lnNote || lane == lnLen)) {
                if (lockStep == s && lockTrack == t) lockStep = -1; else { lockStep = s; lockTrack = t; }
                repaint(); return true;
            }
            switch (lane) {
            case lnTrig:
                paintVal = pat().on[t][s] == 0;
                setStep(t, s, paintVal);
                drag = dkPaint;
                break;
            case lnNote:
                noteStep = s;
                if (dbl) { setStepNote(s, kBaseNote); break; }
                if (!pat().on[t][s]) setStep(t, s, true);
                drag = dkStepNote; dragIndex = s; dragY = y; dragStart = pat().note[t][s];
                break;
            case lnLen:
                if (dbl) { setStepGate(s, kDefaultGate); break; }
                if (!pat().on[t][s]) setStep(t, s, true);
                drag = dkStepLen; dragIndex = s; dragY = y; dragStart = pat().gate[t][s];
                break;
            default: stepExtra(s, right, dbl); break;
            }
            repaint(); return true;
        }
        // keyboard
        if (octDown().in(x, y)) { kbOct = std::max(-1, kbOct - 1); repaint(); return true; }
        if (octUp().in(x, y)) { kbOct = std::min(6, kbOct + 1); repaint(); return true; }
        if (notesOffBtn().in(x, y)) { latched.clear(); setState("cmd", "panic"); repaint(); return true; }
        if (keyChordBtn().in(x, y)) { keyChord = dbl ? 0 : (keyChord + (right ? kChordCount - 1 : 1)) % kChordCount; repaint(); return true; }
        if (latchBtn().in(x, y)) { if (keyLatch) releaseAllKeys(); keyLatch = !keyLatch; repaint(); return true; }
        if (pcKeysBtn().in(x, y)) { if (pcKeys) releaseAllKeys(); pcKeys = !pcKeys; repaint(); return true; }
        {
            const int n = keyAt(x, y);
            if (n >= 0) {
                mouseKey = n; drag = dkKey; keyPress(n);
                return true;
            }
        }
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
        case dkGlobal: setParam(kGlobalBase + dragIndex, clampf(v, 0.f, 127.f)); repaint(); break;
        case dkTempo: setParam(kGlobalBase + gTEMPO, clampf(std::round((dragStart + (dragY - y) * ((ev.mod & kModifierShift) ? 0.05f : 0.5f)) * 10.f) / 10.f, 40.f, 240.f)); repaint(); break;
        case dkPatLen: pat().length = (uint8_t)clampi((int)std::lround(dragStart + (dragY - y) * 0.15f), 1, kSteps); repaint(); break;
        case dkSwing: pat().swing = (uint8_t)clampi((int)std::lround(dragStart + (dragY - y) * 0.15f), 50, 80); repaint(); break;
        case dkTrackLen: pat().len[selTrack] = (uint8_t)clampi((int)std::lround(dragStart + (dragY - y) * 0.15f), 1, kSteps); repaint(); break;
        case dkWave: waveSet(x); break;
        case dkMix: setKnob(tpGMIX, mixAt(y)); break;
        case dkStepNote: {
            const int n = clampi((int)std::lround(dragStart + (dragY - y) / 6.f), 0, 127);
            if (n != pat().note[selTrack][dragIndex]) setStepNote(dragIndex, n);
            repaint(); break;
        }
        case dkStepLen: {
            const int g = clampi((int)std::lround(dragStart + (dragY - y) / 10.f), 0, kGateCount - 1);
            if (g != pat().gate[selTrack][dragIndex]) setStepGate(dragIndex, g);
            repaint(); break;
        }
        case dkKey: {
            const int n = keyAt(x, y);
            if (n >= 0 && n != mouseKey && !keyLatch) { if (mouseKey >= 0) keyRelease(mouseKey); mouseKey = n; keyPress(n); }
            break;
        }
        case dkPaint:
            for (int i = 0; i < 16; ++i) {
                const R r = stepCell(i);
                if (x >= r.x && x < r.x + r.w + 4.5f) { const int s = stepPg * 16 + i; if (s < pat().len[selTrack]) setStep(selTrack, s, paintVal); break; }
            }
            repaint(); break;
        default: break;
        }
        return true;
    }

    bool onScroll(const ScrollEvent& ev) override
    {
        const float x = ev.pos.getX() / sx, y = ev.pos.getY() / sx;
        const float d = ev.delta.getY() > 0 ? 1.f : (ev.delta.getY() < 0 ? -1.f : 0.f);
        if (d == 0.f || overlay != ovNone) return false;
        if (loopBtn().in(x, y) && !liveTrack(selTrack) && !lockMode() && bank->kit[selTrack].beats) { stepBeats(selTrack, (int)d); return true; }
        if (scaleBox().in(x, y)) { setScale(clampi(bank->scale + (int)d, 0, kScales - 1), bank->root); return true; }
        if (rootBox().in(x, y)) { setScale(bank->scale, (bank->root + (int)d + 12) % 12); return true; }
        for (int i = 0; i < pageCount(); ++i) if (pageKnob(i).in(x, y)) {
            const int k = kPageFirst[page] + i;
            if (knobIsHostParam()) { editParameter(tpIndex(selTrack, k), true); setKnob(k, knobValue(k) + d); editParameter(tpIndex(selTrack, k), false); }
            else setKnob(k, knobValue(k) + d);
            return true;
        }
        for (int i = 0; i < fxSlots(); ++i) if (fxKnob(i).in(x, y) && fxParam(i) >= 0) { const int g = fxParam(i); setParamGesture(kGlobalBase + g, clampf(gp(g) + d, 0.f, 127.f)); repaint(); return true; }
        if (mixZone().in(x, y)) { setKnobGesture(tpGMIX, clampf(knobValue(tpGMIX) + d * 4.f, 0.f, 127.f)); return true; }
        // the wheel on a step (whatever the lane): note; SHIFT = length; CTRL / CMD = octave; ALT = ratchet;
        // CTRL + SHIFT = chord; ALT + SHIFT = condition
        for (int i = 0; i < 16; ++i) if (stepCell(i).in(x, y)) {
            const int s = stepPg * 16 + i, t = selTrack;
            if (s >= pat().len[t]) return true;
            const Pattern& P = pat();
            const bool sh = ev.mod & kModifierShift, ct = ev.mod & (kModifierControl | kModifierSuper), al = ev.mod & kModifierAlt;
            const int di = (int)d;
            int note = P.note[t][s], gate = P.gate[t][s], chord = P.chord[t][s], ratch = P.ratch[t][s], cond = P.cond[t][s];
            if (ct && sh) chord = (chord + di + kChordCount) % kChordCount;
            else if (al && sh) cond = (cond + di + kNumConds) % kNumConds;
            else if (sh) gate = clampi(gate + di, 0, kGateCount - 1);
            else if (ct) note = clampi(note + 12 * di, 0, 127);
            else if (al) ratch = clampi(ratch + di, 0, kRatchCount - 1);
            else note = clampi(note + di, 0, 127);
            setStepData(s, note, gate, chord, ratch, cond);
            repaint(); return true;
        }
        if (tempoBox().in(x, y)) { setParamGesture(kGlobalBase + gTEMPO, clampf(gp(gTEMPO) + d, 40.f, 240.f)); repaint(); return true; }
        if (capLenBox().in(x, y)) { setParamGesture(kGlobalBase + gCAPLEN, (float)clampi((int)std::lround(gp(gCAPLEN)) + (int)d, 0, kCapLens - 1)); repaint(); return true; }
        if (patLenBox().in(x, y)) { pat().length = (uint8_t)clampi(pat().length + (int)d, 1, kSteps); sendPatternSettings(); repaint(); return true; }
        if (swingBox().in(x, y)) { pat().swing = (uint8_t)clampi(pat().swing + (int)d, 50, 80); sendPatternSettings(); repaint(); return true; }
        if (lenBox().in(x, y)) { setTrackLen(pat().len[selTrack] + (int)d); return true; }
        return false;
    }
    void uiIdle() override
    {
        if (recOn) pollRecorded();
        if (!sentSel) { sentSel = true; char b[16]; std::snprintf(b, sizeof(b), "sel %d", selTrack); setState("cmd", b); }
        if (!tipShown && !tipNone && drag == dkNone && overlay == ovNone && hovX >= 0.f && nowSec() - hovSince > 0.8 && nowSec() > tipBlockUntil) {
            tipText = tipAt(hovX, hovY);
            if (!tipText.empty()) { tipShown = true; repaint(); } else tipNone = true;
        }
        flushPv();
        if (liveTrack(selTrack) && overlay == ovNone && nowSec() - lastLiveDraw > 0.05) { lastLiveDraw = nowSec(); repaint(); }
        // voices moving through the material, LFO dots: 30 frames a second while something moves
        if (overlay == ovNone && nowSec() - lastAnim > 0.033 && ((((uint32_t)std::lround(gp(gOUT_ACTIVE)) >> selTrack) & 1) || lfoActive())) { lastAnim = nowSec(); repaint(); }
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
    int page = 0, fxPage = 0, lane = 0, kbOct = 2, keyChord = 0, noteStep = -1, waveHandle = 0;
    bool fillHeld = false, fillLatch = false, keyLatch = false, pcKeys = false, sentSel = false, zoom = true, recOn = false, clickOn = true, chaosHeld = false, chaosLatch = false; double lastAnim = 0.0;
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
