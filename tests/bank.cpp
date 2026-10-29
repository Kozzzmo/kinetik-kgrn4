// KGRN-4 bank: text round trip, commands, kit + slices, factory presets build and play.
//   g++ -O2 -std=c++17 tests/bank.cpp -o /tmp/t && /tmp/t
#include "../src/Presets.hpp"
#include "../src/Engine.hpp"
#include <cstdio>
#include <cstring>
#include <memory>
using namespace kn;

static int fails = 0;
static void check(bool ok, const char* what) { std::printf("%s %s\n", ok ? "ok  " : "FAIL", what); if (!ok) ++fails; }
static bool same(const Bank& a, const Bank& b) { return serializeBank(a) == serializeBank(b); }

int main()
{
    static Bank a, b;
    a.initDefault();
    check(deserializeBank(b, serializeBank(a).c_str()) && same(a, b), "empty bank round trip");
    applyCommand(a, "P 2 48 60");
    applyCommand(a, "L 2 1 40 3");
    applyCommand(a, "S 2 1 1001");
    applyCommand(a, "s 2 1 9 1");
    applyCommand(a, "k 2 1 9 3 100");
    applyCommand(a, "k 2 1 0 15 30");
    check(a.patterns[2].length == 48 && a.patterns[2].swing == 60 && a.patterns[2].len[1] == 40 && a.patterns[2].speed[1] == 3, "P / L");
    check(a.patterns[2].on[1][0] && !a.patterns[2].on[1][1] && a.patterns[2].on[1][3] && a.patterns[2].on[1][9], "S / s");
    check(a.patterns[2].locks[1][9][3] == 100 && a.patterns[2].locks[1][0][15] == 30, "k sets locks");
    check(applyCommand(a, "M 3 6 1 1 16") == kKitTouched && a.kit[3].material == 6 && a.kit[3].mute && a.kit[3].sliceMode == 1 && a.kit[3].sliceAmount == 16, "M sets the kit");
    applyCommand(a, "X 3 4 0 250000 200000 750000");
    check(a.kit[3].nSlices == 3 && a.kit[3].slice[2] == 750000, "X keeps the slices ascending");
    check(deserializeBank(b, serializeBank(a).c_str()) && same(a, b), "text round trip");
    applyCommand(a, "k 2 1 9 3 255"); check(a.patterns[2].locks[1][9][3] == kNoLock, "k 255 clears a lock");
    applyCommand(a, "kc 2 1 0"); check(!a.patterns[2].hasLocks(1, 0), "kc clears a step");
    applyCommand(a, "rot 2 1 1"); check(a.patterns[2].on[1][1] && a.patterns[2].on[1][10], "rot");
    applyCommand(a, "cp 2 5"); check(a.patterns[5].len[1] == 40, "cp");
    applyCommand(a, "clt 5 1"); check(a.patterns[5].trackEmpty(1), "clt");
    applyCommand(a, "cl 2"); check(a.patterns[2].isDefault(), "cl");
    check(applyCommand(a, "k 0 4 0 0 1") == -1 && applyCommand(a, "s 0 0 128 1") == -1 && applyCommand(a, "zz") == -1, "bad commands refused");
    check(!deserializeBank(b, "KFX81\n"), "other formats refused");
    applyCommand(a, "C 1 2 4:3 1:1");
    check(deserializeBank(b, serializeBank(a).c_str()) && b.chain.on && b.chain.n == 2 && b.chain.pat[0] == 4 && b.chain.rep[0] == 3, "chain survives");
    // 0.4: step notes, gates, chords, ratchets, conditions, solo; older banks converted
    applyCommand(a, "N 3 2 7 67 5 2 3 11"); applyCommand(a, "O 2 1");
    {
        const Pattern& P = a.patterns[3];
        check(P.note[2][7] == 67 && P.gate[2][7] == 5 && P.chord[2][7] == 2 && P.ratch[2][7] == 3 && P.cond[2][7] == 11 && a.kit[2].solo, "N sets the step, O the solo");
        check(deserializeBank(b, serializeBank(a).c_str()) && same(a, b), "steps and solo round trip");
        applyCommand(a, "rot 3 2 1"); check(a.patterns[3].note[2][8] == 67 && a.patterns[3].chord[2][8] == 2, "rot moves the step data");
    }
    {
        Bank o; deserializeBank(o, "KGRN43\nS 0 0 1000\nk 0 0 0 4 96\nk 0 0 0 15 30\n");   // 0.3: PITCH +12, DECAY
        check(o.patterns[0].note[0][0] == kBaseNote + 12 && o.patterns[0].locks[0][0][tpPITCH] == kNoLock, "0.3 banks: a PITCH lock becomes the step's note");
        check(o.patterns[0].locks[0][0][tpRELEASE] == 30 && o.patterns[0].gate[0][0] == kGateHold, "0.3 banks: DECAY becomes RELEASE, notes last until the next");
    }
    {   // 0.4 banks (40 knobs, 2 LFOs): VOLUME (20) and an LFO 2 DEST (38) pointing at DELAY (26) move to their 0.5 places
        Bank o; deserializeBank(o, "KGRN44\nk 0 1 5 20 90\nk 0 1 5 38 121\n");
        const int d = lfoDest(o.patterns[0].locks[1][5][tpL2DEST]);
        check(o.patterns[0].locks[1][5][tpVOLUME] == 90 && d == tpDELAY + 1, "0.4 banks: locks and LFO destinations converted");
    }
    {   // 0.5 / 0.6 banks: the bipolar FILTER lock becomes TYPE + CUTOFF, GRAIN MIX moves
        Bank o; deserializeBank(o, "KGRN45\nk 0 0 3 24 40\nk 0 0 3 20 10\n");
        check(filterTypeOf(o.patterns[0].locks[0][3][tpFTYPE]) == ftLP12 && o.patterns[0].locks[0][3][tpCUTOFF] < 100 && o.patterns[0].locks[0][3][tpGMIX] == 10,
              "0.5 banks: FILTER -> TYPE + CUTOFF, GRAIN MIX moved");
    }
    {   // 0.6: LOOP SYNC beats and the SCALE are saved
        Bank b6; b6.initDefault(); b6.kit[2].beats = 8; b6.scale = 2; b6.root = 9;
        Bank c6; deserializeBank(c6, serializeBank(b6).c_str());
        check(c6.kit[2].beats == 8 && c6.scale == 2 && c6.root == 9, "LOOP SYNC and SCALE saved");
    }
    double ra, rb; Kit k; k.nSlices = 2; k.slice[0] = 0; k.slice[1] = 500000;
    sliceRegion(k, 1, 1000, ra, rb); check(ra == 500 && rb == 1000, "slice region");
    sliceRegion(k, -1, 1000, ra, rb); check(ra == 0 && rb == 1000, "whole sample region");

    // presets: build, round trip, play
    bool rt = true, sound = true;
    for (int i = 0; i < kNumPresets; ++i) {
        std::vector<float> v(kTotalParams, 0.f); Bank pb; buildPreset(i, v.data(), pb);
        Bank r; if (!deserializeBank(r, serializeBank(pb).c_str()) || !same(pb, r)) rt = false;
        if (i == 0) continue;
        std::unique_ptr<Engine> e(new Engine()); e->init(48000.0);
        for (int p = 0; p < kSavedParams; ++p) { if (p < kGlobalBase) e->trackParam[p / kTrackParams][p % kTrackParams] = v[p]; else e->global[p - kGlobalBase] = v[p]; }
        e->global[gPLAY] = 1.f; e->bank = pb;
        for (int t = 0; t < kTracks; ++t) e->source[t] = material(pb.kit[t].material).get();
        std::vector<float> L(48000 * 3), R(L.size());
        const std::vector<float>& in = material(3)->pcm;          // LIVE tracks hear the voice material on the input
        for (size_t p = 0; p < L.size(); p += 512) {
            e->inL = e->inR = in.data() + (p % (in.size() - 1024));
            e->process(L.data() + p, R.data() + p, (uint32_t)std::min<size_t>(512, L.size() - p), Transport());
        }
        double s = 0; float pk = 0; bool fin = true;
        for (size_t j = 0; j < L.size(); ++j) { s += L[j] * L[j] + R[j] * R[j]; pk = std::max(pk, std::max(std::fabs(L[j]), std::fabs(R[j]))); fin = fin && std::isfinite(L[j]); }
        const double rms = std::sqrt(s / (2.0 * L.size()));
        std::printf("  %-15s rms %.3f peak %.2f\n", kPresets[i].name, rms, pk);
        if (rms < 0.02 || pk > 1.5f || !fin) sound = false;
    }
    check(rt, "presets round trip");
    check(sound, "every preset plays at a sane level");
    std::printf(fails ? "\n%d FAILED\n" : "\nall passed\n", fails);
    return fails ? 1 : 0;
}
