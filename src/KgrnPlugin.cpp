// Kinetik KGRN-4 — DPF plugin glue
#include "DistrhoPlugin.hpp"
#include "Engine.hpp"
#include "Materials.hpp"
#include "Presets.hpp"
#include "License.hpp"
#include "GrainDirect.hpp"
#include <atomic>
#include <cmath>
#include <mutex>
#include <memory>
#include <vector>

START_NAMESPACE_DISTRHO

using namespace kn;

static constexpr uint32_t kKitBit = 1u << 16;

class KgrnPlugin : public Plugin, public GrainDirect
{
public:
    // ---- GrainDirect (UI thread)
    SamplePtr captureInput(int bars) override { return engine->capture(bars); }
    void liveWave(float* mn, float* mx, int cols) override { engine->liveWave(mn, mx, cols); }
    int voiceHeads(int t, float* out, int max) override { return engine->voiceHeads(t, out, max); }
    bool popRecorded(RecEvent& e) override { return engine->popRec(e); }
    void setRecording(bool on) override { engine->recording = on; }
    void setClick(bool on) override { engine->click.enabled = on; }
    void setChaosHold(bool on) override { engine->setGlitchHold(on); }
    int grainView(int t, GrainView* out, int max, VoiceView* vout, int vmax, int& nv) override { return engine->grainView(t, out, max, vout, vmax, nv); }
    float lfoValue(int t, int i) override { return t >= 0 && t < kTracks && i >= 0 && i < kLfos ? engine->lfoValue(t, i) : 0.f; }

    KgrnPlugin()
        : Plugin(kTotalParams, 0, 5)
    {
        materials();                                   // build the built-in materials now, never on the audio thread
        engine.reset(new Engine());
        master.reset(new Bank());
        master->initDefault();
        defaultKits(*master);
        engine->bank = *master;
        engine->init(getSampleRate());
        for (int i = 0; i < kTotalParams; ++i) { uiVal[i].store(0.f); uiHold[i].store(0); }
        notes.reserve(256);
        updateSources();
        licensed.store(lic::findInstalled(kProductCode).valid);
    }

protected:
    const char* getLabel() const override { return "KinetikKGRN4"; }
    const char* getDescription() const override { return "Polyphonic granular sampler: four sample tracks cut into grains, two LFOs each, played by a step sequencer with notes, chords and per-step locks"; }
    const char* getMaker() const override { return "Kinetik"; }
    const char* getLicense() const override { return "Proprietary"; }
    uint32_t getVersion() const override { return d_version(KN_VER_MAJOR, KN_VER_MINOR, KN_VER_MICRO); }

    void initAudioPort(bool input, uint32_t index, AudioPort& port) override
    {
        if (input) {                              // the input arrives as a side-chain: instruments have no main input in most hosts
            port.hints = kAudioPortIsSidechain;
            port.name = index == 0 ? "Input Left" : "Input Right";
            port.symbol = index == 0 ? "in_left" : "in_right";
            port.groupId = kPortGroupNone;
            return;
        }
        port.groupId = kPortGroupStereo; Plugin::initAudioPort(input, index, port);
    }
    void ioChanged(uint16_t numInputs, uint16_t) override { numIn.store(numInputs); }

    void initParameter(uint32_t index, Parameter& p) override
    {
        p.hints = kParameterIsAutomatable;
        if ((int)index < kGlobalBase) {
            const int t = (int)index / kTrackParams, k = (int)index % kTrackParams;
            static char names[kGlobalBase][32], syms[kGlobalBase][24];
            std::snprintf(names[index], sizeof(names[index]), "T%d %s", t + 1, kTP[k].name);
            for (char* c = names[index] + 3; *c; ++c) *c = (char)std::tolower((unsigned char)*c);
            names[index][3] = (char)std::toupper((unsigned char)names[index][3]);          // "T1 Position"
            std::snprintf(syms[index], sizeof(syms[index]), "t%d_%d", t + 1, k + 1);
            p.name = names[index]; p.shortName = names[index]; p.symbol = syms[index];
            p.ranges.min = 0; p.ranges.max = 127; p.ranges.def = trackDefault(t, k);
            return;
        }
        const int g = (int)index - kGlobalBase;
        p.ranges.min = 0; p.ranges.max = 127; p.ranges.def = globalDefault(g);
        switch (g) {
        case gMF_ON: p.name = "Filter On"; p.symbol = "mf_on"; p.ranges.max = 1; p.hints |= kParameterIsBoolean | kParameterIsInteger; return;
        case gECHO_RTIME: p.name = "Echo R Time"; p.symbol = "echo_rtime"; return;
        case gECHO_DRIVE: p.name = "Echo Drive"; p.symbol = "echo_drive"; return;
        case gECHO_DUCK: p.name = "Echo Duck"; p.symbol = "echo_duck"; return;
        case gECHO_FREEZE: p.name = "Echo Freeze"; p.symbol = "echo_freeze"; p.ranges.max = 1; p.hints |= kParameterIsBoolean | kParameterIsInteger; return;
        case gTEMPO: p.name = "Internal Tempo"; p.symbol = "tempo"; p.ranges.min = 40; p.ranges.max = 240; p.unit = "BPM"; return;
        case gPLAY: p.name = "Internal Play"; p.symbol = "play"; p.ranges.max = 1; p.hints |= kParameterIsBoolean | kParameterIsInteger; return;
        case gPATTERN: p.name = "Pattern"; p.symbol = "pattern"; p.ranges.max = kPatterns - 1; p.hints |= kParameterIsInteger; return;
        case gFREEZE: p.name = "Freeze Input"; p.symbol = "freeze"; p.ranges.max = 1; p.hints |= kParameterIsBoolean | kParameterIsInteger; return;
        case gCAPLEN: p.name = "Capture Length"; p.symbol = "cap_len"; p.ranges.max = kCapLens - 1; p.hints |= kParameterIsInteger; return;
        case gOUT_IN: p.name = "Input Level"; p.symbol = "out_in"; p.hints = kParameterIsOutput; p.ranges.min = 0; p.ranges.max = 1; p.ranges.def = 0; return;
        case gOUT_STEP: p.name = "Play Step"; p.symbol = "out_step"; p.hints = kParameterIsOutput | kParameterIsInteger; p.ranges.min = -1; p.ranges.max = 32768 * Engine::kFine; return;
        case gOUT_ACTIVE: p.name = "Active Tracks"; p.symbol = "out_active"; p.hints = kParameterIsOutput | kParameterIsInteger; p.ranges.max = 15; return;
        case gOUT_PATTERN: p.name = "Playing Pattern"; p.symbol = "out_pattern"; p.hints = kParameterIsOutput | kParameterIsInteger; p.ranges.max = kPatterns - 1; return;
        default: if (g < kMasterCount) {                          // the master effects, named as in KDSP-16
            static char mn[kMasterCount][40], ms[kMasterCount][24];
            const char* sec = g <= gECHO_LEV || g == gECHO_MODEL || g == gECHO_MOD ? "Echo" : g <= gREV_LEV || g == gREV_MODEL || g == gREV_MOD ? "Reverb"
                            : g <= gVOLUME ? "Master" : g <= gMF_RESO ? "Filter" : g <= gCHO_LEV ? "Chorus" : "Chaos";
            std::string nmS = std::string(sec) + " " + kMasterNames[g];
            for (size_t i = std::strlen(sec) + 2; i < nmS.size(); ++i) nmS[i] = (char)std::tolower((unsigned char)nmS[i]);
            std::snprintf(mn[g], sizeof(mn[g]), "%s", nmS.c_str()); std::snprintf(ms[g], sizeof(ms[g]), "m%d", g);
            p.name = mn[g]; p.symbol = ms[g];
            return;
        } else {
            static char nm[4][32], sy[4][24];
            const int t = g - gOUT_POS1;
            std::snprintf(nm[t], sizeof(nm[t]), "T%d Play Head", t + 1); std::snprintf(sy[t], sizeof(sy[t]), "out_pos%d", t + 1);
            p.name = nm[t]; p.symbol = sy[t]; p.hints = kParameterIsOutput; p.ranges.min = -1; p.ranges.max = 1; p.ranges.def = -1;
            return;
        }
        }
    }

    void initState(uint32_t index, State& state) override
    {
        switch (index) {
        case 0: state.key = "bank"; state.defaultValue = String(serializeBank(*master).c_str()); state.label = "Patterns"; break;
        case 1: state.key = "cmd"; state.defaultValue = ""; state.label = "Edit command"; break;
        case 2: state.key = "skin"; state.defaultValue = ""; state.label = "Interface skin"; break;
        case 3: state.key = "samples"; state.defaultValue = ""; state.label = "Samples"; break;     // every track's sample, saved with the project
        default: state.key = "smp"; state.defaultValue = ""; state.label = "Sample command"; break; // the UI loaded (or removed) one track's sample
        }
    }

    float getParameterValue(uint32_t index) const override
    {
        if ((int)index < kGlobalBase) return engine->trackParam[index / kTrackParams][index % kTrackParams];
        const int g = (int)index - kGlobalBase;
        switch (g) {
        case gOUT_STEP: return (float)engine->outStep;
        case gOUT_ACTIVE: return (float)engine->outActive;
        case gOUT_PATTERN: return (float)engine->outPattern;
        case gOUT_POS1: case gOUT_POS2: case gOUT_POS3: case gOUT_POS4: return engine->outPos[g - gOUT_POS1];
        case gOUT_IN: return std::min(1.f, engine->inPeak);
        default: return g < kGlobalParams ? engine->global[g] : 0.f;
        }
    }

    std::atomic<float> uiVal[kTotalParams];
    std::atomic<int32_t> uiHold[kTotalParams];
    void uiEdit(uint32_t index, float value)
    {
        if (index >= (uint32_t)kSavedParams) return;
        uiVal[index].store(value); uiHold[index].store((int32_t)(getSampleRate() * 1.5));
        applyParameter(index, value);
    }
    void setParameterValue(uint32_t index, float value) override
    {
        if (index >= (uint32_t)kSavedParams) return;
        if (uiHold[index].load(std::memory_order_relaxed) > 0) {
            if (std::fabs(value - uiVal[index].load(std::memory_order_relaxed)) > 0.01f) return;
            uiHold[index].store(0);
        }
        applyParameter(index, value);
    }
    void applyParameter(uint32_t index, float value)
    {
        if ((int)index < kGlobalBase) { engine->trackParam[index / kTrackParams][index % kTrackParams] = value; return; }
        engine->global[index - kGlobalBase] = value;
    }

    String getState(const char* key) const override
    {
        if (std::strcmp(key, "bank") == 0) { std::lock_guard<std::mutex> lk(masterMutex); return String(serializeBank(*master).c_str()); }
        if (std::strcmp(key, "skin") == 0) return skinState;
        if (std::strcmp(key, "samples") == 0) {
            std::lock_guard<std::mutex> lk(masterMutex);
            SamplePtr cur[kTracks];
            for (int t = 0; t < kTracks; ++t) cur[t] = smpSet[t] ? smpPend[t] : smpLive[t];
            return String(smp::encodeAll(cur, kTracks).c_str());
        }
        return String("");
    }

    void setState(const char* key, const char* value) override
    {
        if (std::strcmp(key, "skin") == 0) { skinState = value ? value : ""; return; }
        if (std::strcmp(key, "samples") == 0 || std::strcmp(key, "smp") == 0) {
            const bool all = key[1] == 'a';
            if (!all && !(value && *value)) return;
            std::lock_guard<std::mutex> lk(masterMutex);
            for (int t = 0; t < kTracks; ++t) smpGrave[t].reset();        // free what the audio thread released
            if (all) for (int t = 0; t < kTracks; ++t) { smpPend[t].reset(); smpSet[t] = true; }
            smp::decodeAll(value ? value : "", [this](int t, SamplePtr d) {
                if (t < 0 || t >= kTracks) return;
                smpPend[t] = std::move(d); smpSet[t] = true;
            });
            smpDirty.store(true);
            return;
        }
        if (std::strcmp(key, "bank") == 0) {
            std::lock_guard<std::mutex> lk(masterMutex);
            if (deserializeBank(*master, value)) dirty.store(0xFFFFu | kKitBit);
            return;
        }
        if (std::strcmp(key, "cmd") == 0 && value && *value) {
            std::lock_guard<std::mutex> lk(masterMutex);
            uint32_t d = 0;
            const char* s = value; std::string line;
            while (*s) {
                const char* e = std::strchr(s, '\n');
                const size_t len = e ? (size_t)(e - s) : std::strlen(s);
                line.assign(s, len);
                if (line.rfind("pv ", 0) == 0) { unsigned i = 0; float v = 0.f; if (std::sscanf(line.c_str() + 3, "%u %f", &i, &v) == 2) uiEdit(i, v); }
                else if (line == "panic") panic.store(true);
                else if (line.rfind("rec ", 0) == 0) engine->recording = line[4] == '1';
                else if (line.rfind("click ", 0) == 0) engine->click.enabled = line[6] == '1';
                else if (line.rfind("gh ", 0) == 0) engine->setGlitchHold(line[3] == '1');
                else if (line.rfind("sel ", 0) == 0) uiTrack.store(clampi(std::atoi(line.c_str() + 4), 0, kTracks - 1));
                else if (line == "lic") licensed.store(lic::findInstalled(kProductCode).valid);
                else if (line.rfind("trig ", 0) == 0 || line.rfind("rel ", 0) == 0) {   // on-screen keyboard: trig <track> <note> / rel <track> <note>
                    const bool on = line[0] == 't';
                    int t = 0, n = kBaseNote; std::sscanf(line.c_str() + (on ? 5 : 4), "%d %d", &t, &n);
                    const uint32_t packed = ((uint32_t)clampi(t, 0, kTracks - 1) << 16) | ((uint32_t)clampi(n, 0, 127) << 1) | (on ? 1u : 0u);
                    const int w = keyW.load(); if ((w + 1) % 32 != keyR.load()) { keyQ[w] = packed; keyW.store((w + 1) % 32); }
                } else if (line.rfind("fill ", 0) == 0) fillUi.store(line[5] == '1');
                else {
                    const int p = applyCommand(*master, line.c_str());
                    if (p == kKitTouched) d |= kKitBit; else if (p >= 0) d |= 1u << p;
                }
                s += len; if (*s == '\n') ++s;
            }
            if (d) dirty.fetch_or(d);
        }
    }

    void activate() override { engine->reset(); }
    void sampleRateChanged(double sr) override { engine->init(sr); }

    void updateSources()
    {
        for (int t = 0; t < kTracks; ++t) {
            const int m = engine->bank.kit[t].material;
            engine->source[t] = m == kMatLive ? nullptr : (m > 0 ? material(m).get() : smpLive[t].get());
        }
    }

    void run(const float** inputs, float** outputs, uint32_t frames, const MidiEvent* midi, uint32_t midiCount) override
    {
        for (int i = 0; i < kSavedParams; ++i) {
            const int32_t h = uiHold[i].load(std::memory_order_relaxed);
            if (h > 0) uiHold[i].store(h > (int32_t)frames ? h - (int32_t)frames : 0, std::memory_order_relaxed);
        }
        bool srcChanged = false;
        if (dirty.load(std::memory_order_acquire)) {
            std::unique_lock<std::mutex> lk(masterMutex, std::try_to_lock);
            if (lk.owns_lock()) {
                const uint32_t d = dirty.exchange(0);
                for (int p = 0; p < kPatterns; ++p) if (d & (1u << p)) engine->bank.patterns[p] = master->patterns[p];
                if (d & kKitBit) { for (int t = 0; t < kTracks; ++t) engine->bank.kit[t] = master->kit[t]; engine->bank.chain = master->chain; engine->bank.scale = master->scale; engine->bank.root = master->root; srcChanged = true; }
            }
        }
        // install new samples (the old ones are freed later, outside the audio thread)
        if (smpDirty.load(std::memory_order_acquire)) {
            std::unique_lock<std::mutex> lk(masterMutex, std::try_to_lock);
            if (lk.owns_lock()) {
                bool left = false;
                for (int t = 0; t < kTracks; ++t) {
                    if (!smpSet[t]) continue;
                    if (smpGrave[t]) { left = true; continue; }
                    smpGrave[t] = std::move(smpLive[t]);
                    smpLive[t] = std::move(smpPend[t]);
                    smpSet[t] = false;
                }
                smpDirty.store(left);
                srcChanged = true;
            }
        }
        if (srcChanged) updateSources();
        // live notes: the UI keyboard; MIDI channel 1 = the selected track, channels 2-5 = tracks 1-4 (polyphonic,
        // C3 = the sample's own pitch); channel 16 = C3..D#3 trig tracks 1-4, B2 = FILL, C4..D#5 = patterns A01-A16
        notes.clear();
        while (keyR.load() != keyW.load() && notes.size() < 256) {
            const int r = keyR.load(); const uint32_t k = keyQ[r]; keyR.store((r + 1) % 32);
            notes.push_back({0, (int)(k >> 16), (int)((k >> 1) & 127), 0.9f, (k & 1) != 0});
        }
        for (uint32_t i = 0; i < midiCount && notes.size() < 256; ++i) {
            const MidiEvent& ev = midi[i];
            if (ev.size < 3) continue;
            const uint8_t st = ev.data[0] & 0xF0, ch = ev.data[0] & 0x0F; const int note = ev.data[1];
            if (st == 0xB0 && (note == 123 || note == 120)) { engine->allNotesOff(); continue; }   // all notes / sound off
            const bool on = st == 0x90 && ev.data[2] > 0, off = st == 0x80 || (st == 0x90 && ev.data[2] == 0);
            if (!on && !off) continue;
            if (ch == 15) {                                               // channel 16: the remote (track trigs, FILL, patterns)
                if (note >= 60 && note < 60 + kTracks) notes.push_back({ev.frame, note - 60, kBaseNote, on ? ev.data[2] / 127.f : 0.f, on});
                else if (note == 59) midiFill = on;                       // B2 held = FILL
                else if (on && note >= 72 && note < 72 + kPatterns) engine->midiPattern = note - 72;
            } else if (ch == 0)                                           // channel 1: the selected track, polyphonic
                notes.push_back({ev.frame, uiTrack.load(std::memory_order_relaxed), note, on ? ev.data[2] / 127.f : 0.f, on});
            else if (ch <= kTracks)                                       // channels 2-5: tracks 1-4 (C3 = the sample's own pitch)
                notes.push_back({ev.frame, ch - 1, note, on ? ev.data[2] / 127.f : 0.f, on});
        }
        engine->fill = midiFill || fillUi.load(std::memory_order_relaxed);
        if (engine->midiPattern >= 0 && (int)std::lround(engine->global[gPATTERN]) == engine->midiPattern) engine->midiPattern = -1;
        Transport tp;
        const TimePosition& pos = getTimePosition();
        if (pos.bbt.valid) {
            tp.hostValid = true; tp.hostPlaying = pos.playing;
            const double bt = pos.bbt.beatType > 0 ? pos.bbt.beatType : 4.0;
            const double beats = (pos.bbt.bar - 1) * (double)pos.bbt.beatsPerBar + (pos.bbt.beat - 1) + (pos.bbt.ticksPerBeat > 0 ? pos.bbt.tick / pos.bbt.ticksPerBeat : 0.0);
            tp.beat = beats * (4.0 / bt); tp.bpm = pos.bbt.beatsPerMinute > 0 ? pos.bbt.beatsPerMinute : 120.0;
        }
        const bool in = numIn.load(std::memory_order_relaxed) >= 2 && inputs && inputs[0] && inputs[1];
        engine->inL = in ? inputs[0] : nullptr; engine->inR = in ? inputs[1] : nullptr;
        engine->process(outputs[0], outputs[1], frames, tp, notes.data(), (uint32_t)notes.size());
        if (lic::kDemoModeEnabled && !licensed.load(std::memory_order_relaxed)) demoGate(outputs, frames);
    }
    // Demo mode (no licence): silent for 3 seconds every 45 seconds.
    void demoGate(float** outputs, uint32_t frames)
    {
        const double sr = getSampleRate();
        const uint64_t period = (uint64_t)(45.0 * sr), mute = (uint64_t)(3.0 * sr);
        const float coef = 1.f - std::exp(-1.f / (0.01f * (float)sr));
        for (uint32_t i = 0; i < frames; ++i) {
            const float target = (demoPos % period) >= period - mute ? 0.f : 1.f;
            demoGain += (target - demoGain) * coef; ++demoPos;
            if (demoGain < 0.9999f) { outputs[0][i] *= demoGain; outputs[1][i] *= demoGain; }
        }
    }

private:
    std::unique_ptr<Engine> engine;
    std::unique_ptr<Bank> master;
    mutable std::mutex masterMutex;
    std::atomic<uint32_t> dirty{0};
    std::atomic<bool> licensed{false};
    std::atomic<int> numIn{DISTRHO_PLUGIN_NUM_INPUTS};
    SamplePtr smpPend[kTracks], smpLive[kTracks], smpGrave[kTracks];
    bool smpSet[kTracks] = {};
    std::atomic<bool> smpDirty{false};
    uint32_t keyQ[32] = {}; std::atomic<int> keyW{0}, keyR{0};
    std::atomic<bool> fillUi{false}, panic{false}; bool midiFill = false;
    std::atomic<int> uiTrack{0};                     // the track selected in the UI: MIDI channel 1 plays it
    std::vector<NoteIn> notes;
    uint64_t demoPos = 0; float demoGain = 1.f;
    static constexpr const char* kProductCode = "KGRN-4";
    String skinState;

    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(KgrnPlugin)
};

Plugin* createPlugin() { return new KgrnPlugin(); }

END_NAMESPACE_DISTRHO
