// Kinetik — factory presets of each master effect (identical in kinetik-kdsp16 and kinetik-kmsp6).
// Click the active tab of the master section (ECHO, REVERB, CHORUS, CHAOS, FILTER, MASTER) to pick one.
// A preset only sets the parameters of its effect.
#pragma once
#include "Params.hpp"
#include "MasterFx.hpp"
#include <vector>

namespace kn {

enum FxTab { fxEcho, fxReverb, fxChorus, fxChaos, fxFilter, fxMaster, kFxTabs };
struct FxSet { int g; int v; };
struct FxPreset { int tab; const char* name; std::vector<FxSet> set; };

inline int fxDiv(int i) { return (i * 128 + kEchoDivs - 1) / kEchoDivs; }   // echo TIME value of division i
enum { d16 = 3, d8T = 4, d16D = 5, d8 = 6, d4T = 7, d8D = 8, d4 = 9, d4D = 10, d2 = 11 };

inline const std::vector<FxPreset>& fxPresets()
{
    static const std::vector<FxPreset> p = {
        // ---- ECHO: TIME FEEDBACK HIGH CUT LOW CUT WIDTH LEVEL MODEL MOD
        {fxEcho, "Slapback", {{gECHO_TIME, fxDiv(d16)}, {gECHO_FB, 10}, {gECHO_LPF, 90}, {gECHO_HPF, 20}, {gECHO_WIDTH, 30}, {gECHO_LEV, 70}, {gECHO_MODEL, echoModelValue(emTape)}, {gECHO_MOD, 20}}},
        {fxEcho, "Dub Tape", {{gECHO_TIME, fxDiv(d8D)}, {gECHO_FB, 85}, {gECHO_LPF, 70}, {gECHO_HPF, 35}, {gECHO_WIDTH, 80}, {gECHO_LEV, 72}, {gECHO_MODEL, echoModelValue(emTape)}, {gECHO_MOD, 60}}},
        {fxEcho, "Clean Eighths", {{gECHO_TIME, fxDiv(d8)}, {gECHO_FB, 45}, {gECHO_LPF, 110}, {gECHO_HPF, 15}, {gECHO_WIDTH, 50}, {gECHO_LEV, 60}, {gECHO_MODEL, echoModelValue(emDigital)}, {gECHO_MOD, 30}}},
        {fxEcho, "Ping-Pong 1/8D", {{gECHO_TIME, fxDiv(d8D)}, {gECHO_FB, 60}, {gECHO_LPF, 95}, {gECHO_HPF, 25}, {gECHO_WIDTH, 127}, {gECHO_LEV, 66}, {gECHO_MODEL, echoModelValue(emPingPong)}, {gECHO_MOD, 20}}},
        {fxEcho, "Dark Bucket", {{gECHO_TIME, fxDiv(d4)}, {gECHO_FB, 70}, {gECHO_LPF, 80}, {gECHO_HPF, 30}, {gECHO_WIDTH, 70}, {gECHO_LEV, 68}, {gECHO_MODEL, echoModelValue(emBbd)}, {gECHO_MOD, 70}}},
        {fxEcho, "Phone Line", {{gECHO_TIME, fxDiv(d8T)}, {gECHO_FB, 55}, {gECHO_LPF, 100}, {gECHO_HPF, 40}, {gECHO_WIDTH, 40}, {gECHO_LEV, 72}, {gECHO_MODEL, echoModelValue(emLofi)}, {gECHO_MOD, 75}}},
        {fxEcho, "Wide Quarter", {{gECHO_TIME, fxDiv(d4)}, {gECHO_FB, 50}, {gECHO_LPF, 100}, {gECHO_HPF, 20}, {gECHO_WIDTH, 110}, {gECHO_LEV, 62}, {gECHO_MODEL, echoModelValue(emDigital)}, {gECHO_MOD, 90}}},
        {fxEcho, "Runaway", {{gECHO_TIME, fxDiv(d16D)}, {gECHO_FB, 118}, {gECHO_LPF, 75}, {gECHO_HPF, 45}, {gECHO_WIDTH, 90}, {gECHO_LEV, 64}, {gECHO_MODEL, echoModelValue(emTape)}, {gECHO_MOD, 85}}},
        {fxEcho, "Scatter Taps", {{gECHO_TIME, fxDiv(d4)}, {gECHO_FB, 40}, {gECHO_LPF, 100}, {gECHO_HPF, 25}, {gECHO_WIDTH, 100}, {gECHO_LEV, 62}, {gECHO_MODEL, echoModelValue(emMulti)}, {gECHO_MOD, 60}}},
        {fxEcho, "Backwards", {{gECHO_TIME, fxDiv(d4)}, {gECHO_FB, 55}, {gECHO_LPF, 95}, {gECHO_HPF, 30}, {gECHO_WIDTH, 80}, {gECHO_LEV, 70}, {gECHO_MODEL, echoModelValue(emReverse)}, {gECHO_MOD, 0}}},
        {fxEcho, "Smear", {{gECHO_TIME, fxDiv(d8D)}, {gECHO_FB, 80}, {gECHO_LPF, 85}, {gECHO_HPF, 30}, {gECHO_WIDTH, 110}, {gECHO_LEV, 60}, {gECHO_MODEL, echoModelValue(emDiffuse)}, {gECHO_MOD, 70}}},
        {fxEcho, "Ducked Dub", {{gECHO_TIME, fxDiv(d8D)}, {gECHO_FB, 90}, {gECHO_LPF, 75}, {gECHO_HPF, 40}, {gECHO_WIDTH, 90}, {gECHO_LEV, 74}, {gECHO_MODEL, echoModelValue(emTape)}, {gECHO_MOD, 55}, {gECHO_DRIVE, 50}, {gECHO_DUCK, 80}}},
        // ---- REVERB: DECAY DAMPING GATE LOW CUT PRE-DELAY LEVEL MODEL MOD
        {fxReverb, "Small Room", {{gREV_SIZE, 40}, {gREV_DAMP, 70}, {gREV_GATE, 0}, {gREV_HPF, 25}, {gREV_PRE, 5}, {gREV_LEV, 60}, {gREV_MODEL, reverbModelValue(rmRoom)}, {gREV_MOD, 60}}},
        {fxReverb, "Bright Plate", {{gREV_SIZE, 55}, {gREV_DAMP, 35}, {gREV_GATE, 0}, {gREV_HPF, 35}, {gREV_PRE, 12}, {gREV_LEV, 62}, {gREV_MODEL, reverbModelValue(rmPlate)}, {gREV_MOD, 70}}},
        {fxReverb, "Vocal Plate", {{gREV_SIZE, 70}, {gREV_DAMP, 55}, {gREV_GATE, 0}, {gREV_HPF, 40}, {gREV_PRE, 25}, {gREV_LEV, 60}, {gREV_MODEL, reverbModelValue(rmPlate)}, {gREV_MOD, 40}}},
        {fxReverb, "Concert Hall", {{gREV_SIZE, 80}, {gREV_DAMP, 60}, {gREV_GATE, 0}, {gREV_HPF, 30}, {gREV_PRE, 20}, {gREV_LEV, 64}, {gREV_MODEL, reverbModelValue(rmHall)}, {gREV_MOD, 45}}},
        {fxReverb, "Gated Snare", {{gREV_SIZE, 75}, {gREV_DAMP, 40}, {gREV_GATE, 45}, {gREV_HPF, 30}, {gREV_PRE, 0}, {gREV_LEV, 75}, {gREV_MODEL, reverbModelValue(rmPlate)}, {gREV_MOD, 60}}},
        {fxReverb, "Surf Spring", {{gREV_SIZE, 55}, {gREV_DAMP, 50}, {gREV_GATE, 0}, {gREV_HPF, 30}, {gREV_PRE, 0}, {gREV_LEV, 66}, {gREV_MODEL, reverbModelValue(rmSpring)}, {gREV_MOD, 80}}},
        {fxReverb, "Shimmer Pad", {{gREV_SIZE, 95}, {gREV_DAMP, 45}, {gREV_GATE, 0}, {gREV_HPF, 45}, {gREV_PRE, 30}, {gREV_LEV, 62}, {gREV_MODEL, reverbModelValue(rmShimmer)}, {gREV_MOD, 70}}},
        {fxReverb, "Endless Hall", {{gREV_SIZE, 120}, {gREV_DAMP, 70}, {gREV_GATE, 0}, {gREV_HPF, 35}, {gREV_PRE, 15}, {gREV_LEV, 58}, {gREV_MODEL, reverbModelValue(rmHall)}, {gREV_MOD, 90}}},
        // ---- CHORUS: MODEL RATE DEPTH TIME FEEDBACK WIDTH TONE LEVEL
        {fxChorus, "Classic Chorus", {{gCHO_MODEL, chorusModelValue(cmChorus)}, {gCHO_RATE, 35}, {gCHO_DEPTH, 55}, {gCHO_TIME, 45}, {gCHO_FB, 0}, {gCHO_WIDTH, 100}, {gCHO_TONE, 110}, {gCHO_LEV, 66}}},
        {fxChorus, "Slow Wide", {{gCHO_MODEL, chorusModelValue(cmChorus)}, {gCHO_RATE, 15}, {gCHO_DEPTH, 80}, {gCHO_TIME, 60}, {gCHO_FB, 10}, {gCHO_WIDTH, 127}, {gCHO_TONE, 100}, {gCHO_LEV, 64}}},
        {fxChorus, "String Ensemble", {{gCHO_MODEL, chorusModelValue(cmEnsemble)}, {gCHO_RATE, 40}, {gCHO_DEPTH, 70}, {gCHO_TIME, 55}, {gCHO_FB, 0}, {gCHO_WIDTH, 110}, {gCHO_TONE, 95}, {gCHO_LEV, 70}}},
        {fxChorus, "Thick Ensemble", {{gCHO_MODEL, chorusModelValue(cmEnsemble)}, {gCHO_RATE, 60}, {gCHO_DEPTH, 100}, {gCHO_TIME, 75}, {gCHO_FB, 20}, {gCHO_WIDTH, 127}, {gCHO_TONE, 90}, {gCHO_LEV, 72}}},
        {fxChorus, "Jet Flanger", {{gCHO_MODEL, chorusModelValue(cmFlanger)}, {gCHO_RATE, 20}, {gCHO_DEPTH, 110}, {gCHO_TIME, 40}, {gCHO_FB, 100}, {gCHO_WIDTH, 80}, {gCHO_TONE, 127}, {gCHO_LEV, 70}}},
        {fxChorus, "Metal Flanger", {{gCHO_MODEL, chorusModelValue(cmFlanger)}, {gCHO_RATE, 50}, {gCHO_DEPTH, 70}, {gCHO_TIME, 20}, {gCHO_FB, 120}, {gCHO_WIDTH, 60}, {gCHO_TONE, 127}, {gCHO_LEV, 64}}},
        // ---- CHAOS: CHANCE SIZE LENGTH PITCH FADE REVERSE MIX
        {fxChaos, "Stutter", {{gGL_CHANCE, 35}, {gGL_SIZE, 70}, {gGL_LEN, 30}, {gGL_PITCH, 64}, {gGL_FADE, 20}, {gGL_REV, 0}, {gGL_MIX, 127}}},
        {fxChaos, "Roll Up", {{gGL_CHANCE, 25}, {gGL_SIZE, 92}, {gGL_LEN, 55}, {gGL_PITCH, 100}, {gGL_FADE, 10}, {gGL_REV, 0}, {gGL_MIX, 127}}},
        {fxChaos, "Tape Stop Fall", {{gGL_CHANCE, 20}, {gGL_SIZE, 40}, {gGL_LEN, 55}, {gGL_PITCH, 5}, {gGL_FADE, 60}, {gGL_REV, 0}, {gGL_MIX, 127}}},
        {fxChaos, "Reverse Bits", {{gGL_CHANCE, 30}, {gGL_SIZE, 55}, {gGL_LEN, 30}, {gGL_PITCH, 64}, {gGL_FADE, 30}, {gGL_REV, 100}, {gGL_MIX, 110}}},
        {fxChaos, "Buzz Fill", {{gGL_CHANCE, 15}, {gGL_SIZE, 120}, {gGL_LEN, 55}, {gGL_PITCH, 80}, {gGL_FADE, 40}, {gGL_REV, 20}, {gGL_MIX, 127}}},
        {fxChaos, "Gentle Echoes", {{gGL_CHANCE, 20}, {gGL_SIZE, 30}, {gGL_LEN, 80}, {gGL_PITCH, 64}, {gGL_FADE, 90}, {gGL_REV, 0}, {gGL_MIX, 70}}},
        {fxChaos, "Total Chaos", {{gGL_CHANCE, 90}, {gGL_SIZE, 85}, {gGL_LEN, 30}, {gGL_PITCH, 30}, {gGL_FADE, 50}, {gGL_REV, 60}, {gGL_MIX, 127}}},
        // ---- FILTER: HIGH PASS LOW PASS RESONANCE ON
        {fxFilter, "Off", {{gMF_HP, 0}, {gMF_LP, 127}, {gMF_RESO, 0}, {gMF_ON, 0}}},
        {fxFilter, "Telephone", {{gMF_HP, 60}, {gMF_LP, 85}, {gMF_RESO, 40}, {gMF_ON, 1}}},
        {fxFilter, "Muffled Club", {{gMF_HP, 0}, {gMF_LP, 60}, {gMF_RESO, 30}, {gMF_ON, 1}}},
        {fxFilter, "Thin Out", {{gMF_HP, 45}, {gMF_LP, 127}, {gMF_RESO, 20}, {gMF_ON, 1}}},
        {fxFilter, "Resonant Sweep", {{gMF_HP, 0}, {gMF_LP, 75}, {gMF_RESO, 95}, {gMF_ON, 1}}},
        {fxFilter, "Radio", {{gMF_HP, 55}, {gMF_LP, 95}, {gMF_RESO, 60}, {gMF_ON, 1}}},
        // ---- MASTER: LOW HIGH THRESH RATIO ATTACK RELEASE DRIVE VOLUME
        {fxMaster, "Neutral", {{gEQ_LOW, 64}, {gEQ_HIGH, 64}, {gCMP_THR, 127}, {gCMP_RAT, 40}, {gCMP_ATK, 20}, {gCMP_REL, 50}, {gDRIVE, 0}, {gVOLUME, 100}}},
        {fxMaster, "Glue", {{gEQ_LOW, 66}, {gEQ_HIGH, 66}, {gCMP_THR, 95}, {gCMP_RAT, 30}, {gCMP_ATK, 45}, {gCMP_REL, 55}, {gDRIVE, 10}, {gVOLUME, 100}}},
        {fxMaster, "Punchy Drums", {{gEQ_LOW, 72}, {gEQ_HIGH, 68}, {gCMP_THR, 80}, {gCMP_RAT, 55}, {gCMP_ATK, 60}, {gCMP_REL, 35}, {gDRIVE, 20}, {gVOLUME, 98}}},
        {fxMaster, "Smash", {{gEQ_LOW, 70}, {gEQ_HIGH, 70}, {gCMP_THR, 50}, {gCMP_RAT, 100}, {gCMP_ATK, 10}, {gCMP_REL, 30}, {gDRIVE, 45}, {gVOLUME, 92}}},
        {fxMaster, "Warm Tape", {{gEQ_LOW, 70}, {gEQ_HIGH, 56}, {gCMP_THR, 90}, {gCMP_RAT, 35}, {gCMP_ATK, 40}, {gCMP_REL, 60}, {gDRIVE, 35}, {gVOLUME, 98}}},
        {fxMaster, "Bright Air", {{gEQ_LOW, 60}, {gEQ_HIGH, 80}, {gCMP_THR, 105}, {gCMP_RAT, 30}, {gCMP_ATK, 40}, {gCMP_REL, 50}, {gDRIVE, 5}, {gVOLUME, 100}}},
    };
    return p;
}

} // namespace kn
