# Kinetik KGRN-4 — project memory for Claude

Kinetik KGRN-4 is a granular sampler plug-in (VST3 / AU / CLAP, built with DPF and NanoVG): four sample tracks cut
into slices and played as grains by a step sequencer with per-step locks (owner's brief: "granulaire, quatre pistes
d'échantillons découpés en grains et séquencés", reuse KDSP-16 sample loading and the J-Slice slicing, workflow in the
spirit of a well-known granular groovebox — **no other maker's names or trade dress anywhere**). All DSP is original
code. Family: KDSP-16, KMSP-6, KFX-8, KBAS-1, KJNG-2, KDYN-10.
Repository: `Kozzzmo/kinetik-kgrn4` (public, created 2026-10-27; first built locally in `/home/claude/kinetik-kgrn4`, with `dpf` symlinked to the KDSP-16 checkout).

## Source map (`src/`)

| File | Role |
|---|---|
| `Params.hpp` | (0.1 layout, see 0.4.0 below) 4 tracks, 64 steps, 16 patterns, 16 knobs per track (`TrackParam`, `kTP` names / defaults / help; groups MATERIAL GRAIN SHAPE MIX), host params `tpIndex(t,k)` = t*16+k, globals after `kGlobalBase` = 64: REV_SIZE REV_TONE DLY_TIME DLY_FB OUTPUT TEMPO PLAY PATTERN, outputs OUT_STEP (1/24 16ths) OUT_ACTIVE OUT_PATTERN OUT_POS1-4 (play head, fraction of the sample, -1 silent); mappings (`scanSpeed`, `grainSec`, `grainRate`, `spraySec`, `attackSec`, `decaySec` + HOLD, `filterHz` bipolar LP/HP, `sliceOf` / `sliceKnob`), `trackValueText` |
| `Bank.hpp` | `Pattern` (master length, swing, per track `len` / `speed`, `on`, `locks[t][s][16]`), `Kit` per track (material 0 = own sample / 1-8 built-in, mute, slice mode + amount, `slice[]` starts in millionths of the length); text header `KGRN41`; commands `P L S s k kc M X U cl clt cp rot`; `sliceRegion` |
| `Engine.hpp` | transport as KFX-8; per track 2 voices (new trig: the old one fades in 15 ms); a voice snapshots its step's locks at the trig (unlocked knobs stay live); play head = region start + POSITION + SCAN travel (wraps in the slice); grains spawned every 1/DENSITY with SPRAY offset, SPREAD = random pan + detune, raised-cosine window (table), first grain of a note starts at full level; AR envelope (DECAY 127 = HOLD until the next note / note off); per track SVF LP/HP, volume, echo / reverb sends; live notes `NoteIn` (MIDI, UI `trig` / `rel`) |
| `Fx.hpp` | `PingPong` echo (synced, sleeps), `Reverb` (8-line FDN, Hadamard, damping, sleeps) |
| `Materials.hpp` | the 8 built-in materials synthesized at first use (48 kHz mono, 4 s): PAD BELLS VOICE WIND PLUCKS BEAT (2 bars at 120) DRONE GLASS |
| `Slicer.hpp` | port of the J-Slice / KJNG-2 onset detection without JUCE (own FFT, 4-band super-flux, refine, pre-roll + zero crossing) + a level-rise gate (pads give no slices); `equal`; `toMillionths` |
| `Sample.hpp`, `FileDrop.*`, `third_party/dr_libs` | copied from KDSP-16 (sample codec, file drops, WAV / AIFF decoder) |
| `KgrnPlugin.cpp` | DPF glue (synth, stereo out, MIDI in, unique id `KGR4`, AU `aumu`): states `bank` / `cmd` / `skin` / `samples` / `smp` (sample swap as KDSP-16), `pv` UI edits, cmds `trig t semis` / `rel t`; MIDI ch 1 C3-D#3 tracks, C4-D#5 patterns, ch 2-5 chromatic tracks; demo gate |
| `KgrnUI.cpp` | 1180x720: header, track list (name, slices, LED, ON/OFF, PLAY), waveform of the selected track (slices, selected region, POSITION marker, play head; click = slice + start), LOAD / MATERIAL / slice mode / amount, 16 knobs (two rows), sequencer (pages, LEN, SPEED, rotate, FILL, CLEAR, COPY, PASTE; right-click a step = lock mode, CLEAR LOCKS; slice number shown on steps that lock SLICE), patterns, echo / reverb / output, presets, save (user presets `.kgrn` keep the samples as `SMP` lines), undo (samples are not undone), skins, about, licence, tooltips. Generic blocks copied from `KfxUI.cpp` |
| `Presets.hpp` | `sliceKit`, `kitLines`, `defaultKits` (new instance / Init: PAD BELLS BEAT VOICE), 13 factory presets on the materials (`PB`: `mat`, `tp`, `glob`, `row`, `lock`, `sliceLock`, `pitchLock`) |
| shared (identical in the family, `scripts/check-shared.sh`) | `Skins.hpp`, `Fonts.hpp`, `Dsp.hpp`, `FsUtf8.hpp`, `Updater.*`, `License.hpp`, `LogoMark.hpp`, monocypher, `fonts/OFL-Unbounded.txt` |

## Working with the owner (read first)

- The owner (GitHub **Kozzzmo**) writes in **French**: answer in French. UI, code, comments and README in **English**.
- Git: commit as `Kozzzmo <Kozzzmo@users.noreply.github.com>`. **No `Co-Authored-By` or any Claude attribution**.
- Proprietary code (`LICENSE`); permissive third-party code only, listed in `THIRD_PARTY_NOTICES.txt` (dr_wav added).
- No other makers' names, graphics or trade dress in product text (Torso, Elektron...), no artist-named presets.
  Never bundle third-party samples without the owner's written permission.
- Family look: Orbit logo + product name in Unbounded, skins NEON (default) / MIDNIGHT / RISO / CLASSIC, large text,
  NEON lit steps red, activity LEDs green. Licence product string `KGRN-4` (or BUNDLE).

## Build, test

- `make`; Windows: `make WINDOWS=true CC=x86_64-w64-mingw32-gcc CXX=x86_64-w64-mingw32-g++`.
- Tests: `tests/engine.cpp` (materials, trigs, pitch vs scan, freeze, backwards, slices, locks, mute, decay, live notes,
  extreme values, pattern change, slicer), `tests/bank.cpp` (format, commands, every preset plays at a sane level),
  `tests/license.cpp`, `tests/update.cpp`. VST3 validator 47/47. Worst case CPU (4 tracks, max density / size): ~4 % of a core.
- CI workflow adapted (auval `aumu KGR4 Kntk`); every push to `main` builds and publishes the release `v$(cat VERSION)`.

## Added in 0.2.0

- Pattern CHAIN (`Chain.hpp`, shared by the five Kinetik sequencers): `Bank::chain` (line `C`), `Engine::chainPl` (start at a
  restart, `next()` at the end of the master length), CHAIN button + `ovChain` window, the view follows the chain.
- `showPage(i)`: a step page past the end of the selected track makes the track (and the pattern) that long.

## 0.3.0 — new grain engine (owner: "le moteur granulaire ne fonctionne pas vraiment"; choices: 3 modes, live input +
CAPTURE of the last bars + LIVE material, free redesign of the knobs)

- `Params.hpp`: 20 knobs (`kTrackParams`, groups `kGroupNames` / `kGroupStart`: SOURCE GRAIN COLOUR ENV MIX), modes
  `GrainMode` (CLASSIC CLOUD LOOP, `kModeNames` / `kModeHelp`), SYNC sizes `kSyncBeats` / `syncBin`, DENSITY per mode
  (`classicOverlap` 2-6, `cloudOverlap` 1.5-16 + `cloudRandom` right of centre, `loopCopies` 1-4), globals `gFREEZE gCAPLEN`
  (+ output `gOUT_IN`), `kLiveSeconds` input buffer; `trackValueText(k, v, slices, mode, sync)`.
- `Bank.hpp`: `Kit::mode`, `Kit::sync` (line `M t mat mute sliceMode amount mode sync`), header `KGRN42`; `KGRN41` banks:
  `convertLock41` moves the 16-knob locks. Material `kMatLive` (= 9, `Materials.hpp`) = the input.
- `Engine.hpp` rewritten: `Src` (sample or input ring, cubic read, `lo()/hi()` safe range), `spawn()` (start pushed back
  so the whole grain fits: never a wrap / gap; REVERSE / OCTAVE / JITTER / SPREAD with a right-channel delay `offR`;
  a few ms of random start when pitched against the scan, against combing), windows `GrainWindows` (5 tables morphed by
  SHAPE; LOOP = trapezoid crossfade `xf`), level `1 / max(1, A^(1-incoh) * P^incoh)` from the smoothed sum of windows
  (A) and of their squares (P) with `incoh` from how far apart the grains read; first grain at its loudest point
  (`firstSkip` brings the next one sooner); continuous `safety()` clip. Input: `inL / inR` written into `liveL / liveR`
  unless FREEZE, `barW` = buffer position of the last bar start, `capture(bars)` (last complete bars while playing,
  normalized, faded), `liveWave()`. Test hooks `activeGrains`, `spawnStats`.
- `Sample.hpp` (no longer identical to KDSP-16's): stereo `l` / `r` + mono `pcm`; stereo lines `s<track> name\tsr\tL\tR`.
- Plugin: 2 inputs as a side-chain bus (`DISTRHO_PLUGIN_EXTRA_IO { 0, 2 }` for AU), `ioChanged`, direct access
  (`GrainDirect.hpp`: `captureInput`, `liveWave`).
- UI: knobs 2 x 10 (`knobR` with group gaps), MODE / SYNC buttons over the waveform, header INPUT meter / FREEZE /
  CAPTURE length, CAPTURE button, LIVE waveform (`drawLiveWave`, redrawn 20x/s), stereo WAV loading, user presets
  `KGRN4-PRESET 2` (version 1 files remapped).
- 17 factory presets (Shimmer Freeze, Grid Cloud, Loop Lab, Live Granulator added). Tests: clicks (second difference on
  a sine near the sample end), level on PAD / VOICE for every mode / size / density, CLASSIC steady level (also frozen),
  LOOP + SYNC periodicity, REVERSE / OCTAVE ratios, stereo, LIVE / FREEZE / CAPTURE, bank conversion, CPU (~10 %).

## 0.4.0 — polyphony, KMSP-style interface (owner: "ajouter de la polyphonie 4 ou 6 voix, revoir l'interface, des LFO,
étendre les effets, pas fan des 4 séquenceurs, START / END, mix pitch d'origine / transformé")

- `Params.hpp` rewritten (the 0.3 notes above are history): `kVoices = 6`; 40 knobs per track (`TrackParam`): SOURCE
  START END POSITION SLICE SCAN SPRAY PITCH PMIX, GRAIN SIZE DENSITY SHAPE SPREAD JITTER REVERSE OCTAVE FINE, AMP ATTACK
  DECAY SUSTAIN RELEASE VOLUME PAN, FX FILTER RESO FLTENV DRIVE DELAY REVERB, LFO 1 / LFO 2 (`lfoParam(i,k)`: WAVE RATE
  SYNC DEPTH DEST TRIG; DEST 0 = none / 1 + knob < `kModTargets` = 28); pages `kPageNames` / `kPageStart`; per-track
  defaults `trackDefault`, neutral `trackParamDefault`. Globals: REV SIZE TONE PRE WIDTH LEVEL, DLY TIME FB HICUT LOCUT
  WIDTH LEVEL, CHO RATE DEPTH MIX, OUTPUT DRIVE LOWCUT HIGHCUT, TEMPO PLAY PATTERN FREEZE CAPLEN, outputs. Step tables as
  KMSP-6: `kGateSteps` (last = HOLD), `kChords`, `kRatchValues`, `kConds`; `kBaseNote` 60 = the sample's own pitch.
- `Bank.hpp`: header `KGRN44`; per step `note gate chord ratch cond` (command `N p t s note gate chord ratch cond`),
  `Kit::solo` (`O t 0/1`, M line has legato + solo); `convertOldLock` maps 0.2 / 0.3 locks (PITCH lock -> step note with
  HOLD gate, DECAY -> RELEASE), old banks get HOLD gates (a note lasted until the next one).
- `Engine.hpp`: `kSlots` = 6 voices + 2 fading; stealing chokes the oldest (8 ms); voice = note, ADSR, gate in frames
  (HOLD = released by the track's next step), own stereo SVF with FLT ENV (5 octaves), locks snapshot; LEGATO with one
  note updates the newest voice; ratchets (`ratLeft`...), conditions (`condPasses`, `fill`), mute / solo. START / END =
  sub-region of the slice; PITCH MIX = chance a grain keeps the original rate (`origInc`). `runLfos` per 16-sample
  segment -> `TrackState::mod[]` added in `kv()`. Grain budget 128 grains shared (`voiceCount` share per voice). Live
  ring mipmaps only while a track is LIVE (`liveMips`). Master: chorus -> drive -> low / high cut -> output -> safety.
  `noteOn/noteOff(t, note)`, `allNotesOff`, `voiceHeads`, `lfoValue`, `activeVoices`.
- Plugin: cmds `trig t note` / `rel t note`, `fill 0/1`, `panic`; MIDI ch 1 B2 = FILL, CC 120 / 123 = all notes off,
  ch 2-5 polyphonic; `GrainDirect` gained `voiceHeads`, `lfoValue`.
- UI rebuilt (KMSP-6 layout): track tabs (S / M), display (`drawDisplay`: START / END flags dragged = `waveHandle`,
  POSITION, voice heads), page tabs + 8 knobs (`pageKnob`, LFO dot on the arc `lfoMod`, LFO scope), FX tabs ECHO / REVERB
  / CHORUS / MASTER (`fxG`), patterns, presets / save / undo / FILL (HOLD); sequencer lanes (`Lane`), 16 step cells,
  3-octave keyboard (`keyRect`, `kbOct`, NOTE lane: keys set `noteStep`). User presets `KGRN4-PRESET 3` (1 / 2 remapped).
- Presets: `PB::row` sets HOLD gates; helpers `chord`, `gate`, `ratch`, `cond`, `lfo`; 19 presets (Grain Chords added).
- Tests: polyphony / stealing, START / END, PITCH MIX, LFO, LEN, RATCHET, conditions, SOLO, FLT ENV, effects extremes;
  CPU (16 voices of dense clouds) ~25 % of a core on this VM, limit 40 % (CI machines vary).

## 0.4.1 (owner: "toujours pas de vraie polyphonie", "pousse la représentation de chaque grain", "affiche les effets des réglages")

- MIDI: ch 1 = the UI's selected track (cmd `sel t`, `uiTrack`), ch 2-5 = tracks 1-4, ch 16 = C3-D#3 trigs / B2 FILL /
  C4-D#5 patterns. Default kits without LEGATO (it made sequenced notes monophonic).
- Engine: `Grain` keeps `p0 travel semis flags`; `writeView()` at the end of every block copies each track's grains
  (`GrainView`, `kViewGrains` = 160) and voices (`VoiceView`) into `view[t]` under a seqlock; `grainView()` reads it
  (GrainDirect). Structs and `kGv*` flags in `Params.hpp`.
- UI: `drawGrains` (additive blending through the C functions `nvgGlobalCompositeBlendFunc` / `nvgGlobalCompositeOperation`
  declared `extern "C"` at the top of KgrnUI.cpp): x = material read (`fx()` with ZOOM view `vA..vB`, `updateView`), y =
  pitch (`pitchY`, auto range `pitchRange` 7..25 st) + pan x 6 px, window profile from SHAPE (`grainProfile`), colour per
  voice slot (`voiceColor`, no pink, darker on light skins). Keyboard: `keyChord`, LATCH (`latched`), PC KEYS (`pcDown`,
  row "awsedftgyhujkolp;", Z / X octave), voice keys lit. Test: grain view counts / chord pitches.

## 0.5.0 (owner: REC bottom left, presets bottom right, mix original / grains, RESET keeping the sample, KDSP-16 effects +
CHAOS, no text under the display, 128 steps, LFO display of the other plug-ins, LOW CUT, a third LFO)

- `Params.hpp`: 50 knobs per track (AMP = ATTACK DECAY SUSTAIN RELEASE GRAIN MIX DRIVE VOLUME PAN, FX = FILTER RESO FLT ENV
  LOW CUT DELAY REVERB CHORUS CHAOS, LFO 1-3), `kModTargets` 32, `kPages` 7, `kSteps` 128. Globals = the KDSP-16 master
  block in its order (`gECHO_TIME .. gGL_MIX`, `kMasterNames` / `kMasterDefaults`, VOLUME default 116 = unity), then
  `gMF_ON gECHO_RTIME gECHO_DRIVE gECHO_DUCK gECHO_FREEZE`, TEMPO PLAY PATTERN FREEZE CAPLEN, outputs. `RecEvent` here.
- `MasterFx.hpp` and `Click.hpp` copied from KDSP-16 (identical, in `scripts/shared.sha256`); `Fx.hpp` is now only used by
  `Materials.hpp`. Engine: `process` cuts host blocks into `kBlock` = 1024 and `processBlock` fills main / send buffers
  (delay, reverb, chorus, chaos) for `fx.process`, then `click`, then `masterFilter` (as KDSP-16).
- Voice: LOW CUT (`hl/hr`), GRAIN MIX (`grainG` / `dryG` equal power; dry = cubic read from POSITION to END at `grainInc`,
  looping with a 10 ms crossfade, own mip level); no grains spawned at GRAIN MIX 0.
- REC: `Engine::recording`, `recordNote` (nearest step of the track's grid, `recSkipK`, gate from the held time),
  `popRec`; plugin cmds `rec / click / gh` + `GrainDirect::popRecorded / setRecording / setClick / setChaosHold`. UI
  `setRec`, `pollRecorded` (chord detection per step of the take `recTake`), knob moves while REC + playing = locks.
- Bank `KGRN45`; `convertLock04` / `convertKnob04` / `kKnobs04` (0.4 knobs, LFO DEST values re-aimed). User presets
  `KGRN4-PRESET 4` (1-3 converted, old globals mapped where they have an equivalent).
- UI 1180 x 800 (`DISTRHO_UI_DEFAULT_HEIGHT` 800): FX tabs as KDSP (`fxParam`, `masterName`, `masterText`, pads `padSlot`:
  echo FREEZE, filter ON, CHAOS HOLD), patterns at y 386, bottom bar `drawBottom` (REC / CLICK, RESET -> `ovReset`
  `resetSound`, FILL, UNDO, PRESETS, SAVE), 8 step pages, `drawKnob(..., mod01, modCol)` + `lfoInk` + L1/L2/L3 badge.
  CLICK saved per user as the 3rd value of the view file.

## 0.6.0 (owner: "synchroniser une boucle si je choisis d'en charger une", "un choix de scale qui force le pitch des grains")

- `Kit::beats` (M line field 10, 0 = off): LOOP SYNC; engine `setupTrack`: `scanInc = scanSpeed(SCAN) * (n0 / beats) *
  bpm / 60 / sr` (not for LIVE). UI `loopBtn` (left of MODE), `guessBeats` (tempo in the file name, else the power of two
  closest to 120 BPM), `looksLikeLoop` ("loop", "break", "bpm" in the name: synced at load), `kMaterialBeats` (BELLS PLUCKS
  BEAT KEYS = 8 beats, also in `PB::mat` / `defaultKits`), SCAN text "x1.00", the display shows the loop's tempo.
- `Bank::scale` / `root` (line / command `G scale root`, copied with the kit by the plugin): `quantizeNote` in
  `setupTrack` on note + PITCH (+ LFO); `kScaleNames` / `kScaleMask` / `kRootNames` in `Params.hpp`. UI `scaleBox` /
  `rootBox` in the bottom bar, out-of-scale keys dimmed.

## 0.7.0 (owner: FILTER page, a knob for the grains' scale with random / ordered notes, wheel + modifiers on steps, RESET
TRACK, MIX 50 on resets, MIX outside the pages, sample name in MATERIAL, page tabs distinct from the buttons, red UNDO, ARP)

- `Params.hpp`: 69 knobs: SOURCE 8, GRAIN 8, NOTES 6 (`tpNSCALE` -1 = GLOBAL, `tpNROOT`, `tpNAMOUNT`, `tpNRANGE`,
  `tpNORDER`, `tpNSTEP`), AMP 8 (`tpVELSENS` new), FILTER 8 (`tpFTYPE` `kFilterTypeNames`, `tpCUTOFF` `cutoffHz`,
  `tpRESO`, `tpFLTENV`, `tpFATK` / `tpFDEC` `fEnvSec`, `tpKEYTRK`, `tpLOWCUT`), FX 4 sends, `tpGMIX`, then ARP 8 (not LFO
  targets: `kModTargets` = `tpAMODE` = 43), LFO 1-3. Pages `kPageFirst` / `kPageLen` / `kLfoPage0`. `trackDefault` MIX = 64
  (kTP default 127 for the presets). Helpers `binOf` / `binKnob`, `nScaleOf`... `arpModeOf`...
- `Bank.hpp`: `KGRN46`; old locks go through the 0.5 layout (`Knob05` enum, `kKnobs02/03/04` -> 0.5, `kKnobs05` -> now,
  `convertFilter05` = bipolar FILTER -> TYPE + CUTOFF, `convertKnob05`, `convertOldLine`, a converted line may hold several).
- Engine: filter (2 SVF stages for 24 dB, BP, notch, AD env `fe`, key track); `velG`; `scaleNote` per grain (NOTES); ARP
  (`TrackState::Arp`, `arpOn`, `arpLiveOn/Off` with LATCH, `arpDropSeq`, `playStep` feeds it with the step's notes + locks,
  `runArp` per segment on the beat grid with swing, LENGTH / REPEAT, CHORD plays all).
- UI: `pageBar` tabs, `mixZone` fader (`drawMix`, `mixAt`, `dkMix`), `trackResetBtn` -> `resetTrack`, wheel on steps
  with modifiers (onScroll), MATERIAL shows `srcName`, red UNDO (as KMSP-6). User presets `KGRN4-PRESET 5` (1-4 converted).

## 0.7.1 (owner: "tu as repris les noms et l'emplacement des paramètres mais pas les moteurs ?")

- The engines were KDSP-16's (`MasterFx.hpp` identical) but the sends were squared (`d * d`): 9-12 dB too little into the
  effects. Now `T.dSend / rSend / cSend = send / 127 * 1.4` as `Engine::controlTrack` of KDSP-16; `PB::tp` converts the
  presets' DELAY / REVERB (voiced on the squared curve: v^2 / (127 * 1.4)); `trackDefault` sends lowered. Test: models differ,
  wet level against the dry note (> -12 dB at send 90).
