# Kinetik KGRN-4 — project memory for Claude

Kinetik KGRN-4 is a granular sampler plug-in (VST3 / AU / CLAP, built with DPF and NanoVG): four sample tracks cut
into slices and played as grains by a step sequencer with per-step locks (owner's brief: "granulaire, quatre pistes
d'échantillons découpés en grains et séquencés", reuse KDSP-16 sample loading and the J-Slice slicing, workflow in the
spirit of a well-known granular groovebox — **no other maker's names or trade dress anywhere**). All DSP is original
code. Family: KDSP-16, KMSP-6, KFX-8, KBAS-1, KJNG-2, KDYN-10.
**Repository not created on GitHub yet** (owner: "construis en local d'abord"); built in `/home/claude/kinetik-kgrn4`
with `dpf` symlinked to the KDSP-16 checkout. The update URL already points to `Kozzzmo/kinetik-kgrn4`.

## Source map (`src/`)

| File | Role |
|---|---|
| `Params.hpp` | 4 tracks, 64 steps, 16 patterns, 16 knobs per track (`TrackParam`, `kTP` names / defaults / help; groups MATERIAL GRAIN SHAPE MIX), host params `tpIndex(t,k)` = t*16+k, globals after `kGlobalBase` = 64: REV_SIZE REV_TONE DLY_TIME DLY_FB OUTPUT TEMPO PLAY PATTERN, outputs OUT_STEP (1/24 16ths) OUT_ACTIVE OUT_PATTERN OUT_POS1-4 (play head, fraction of the sample, -1 silent); mappings (`scanSpeed`, `grainSec`, `grainRate`, `spraySec`, `attackSec`, `decaySec` + HOLD, `filterHz` bipolar LP/HP, `sliceOf` / `sliceKnob`), `trackValueText` |
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
- CI workflow adapted (auval `aumu KGR4 Kntk`); it runs once the repository exists with the family's secrets.
