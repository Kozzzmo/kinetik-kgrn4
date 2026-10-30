# Changelog

## 0.7.3

- MUTE / SOLO cut the sound instantly, effect tails included: what a silenced track left in the echo, reverb and
  chorus stops at once (within about 10 ms) while the other tracks keep theirs. Unmuting brings it back.

## 0.7.2

- MUTE (and SOLO) now silence a track at once, notes already playing included (held notes, long releases, live notes,
  the arpeggiator); before, only the next steps were skipped. Unmuting brings back what still plays. 4 ms ramps, no click.

## 0.7.1

- The effects (KDSP-16's echo, reverb, chorus and CHAOS engines) were fed far too softly: the track sends were squared,
  so a send at 50 % reached the effect 12 dB lower than on KDSP-16. The sends are now linear, up to x1.4, as on KDSP-16;
  the factory presets keep their balance, new tracks start with moderate sends.
- Test: every echo model and every reverb model sounds different, and their level against the dry sound is checked.

## 0.7.0

- Knob pages: SOURCE, GRAIN, NOTES, AMP, FILTER, FX, ARP, LFO 1-3, drawn as tabs (distinct from the buttons above them).
- FILTER page: TYPE (low pass 24 / 12 dB, band pass, high pass 12 / 24 dB, notch), CUTOFF, RESO, its own envelope (ENV
  AMT, ENV ATK, ENV DEC), KEY TRK and LOW CUT.
- NOTES page: the grains play in a scale chosen per track (SCALE / ROOT, or GLOBAL = the bottom bar). NOTES sets how
  many grains take other notes of the scale, RANGE how far (1-4 octaves), ORDER how (RANDOM, AROUND, UP, DOWN,
  PING-PONG, CHORD), EVERY how many grains share a note.
- ARP page: an arpeggiator per track on held keys / MIDI and on the sequencer's chords: 11 modes (UP, DOWN, UP-DOWN,
  DOWN-UP, RANDOM, AS PLAYED, CHORD, CONVERGE, DIVERGE, PINKY, THUMB), RATE, OCTAVES, GATE, LENGTH, SWING, REPEAT, LATCH.
- MIX (sample / grains) has its own fader beside the display; RESET and the new RESET TRACK put it at 50 / 50.
- The mouse wheel on a step: note; SHIFT = length; CTRL / CMD = octave; ALT = ratchet; CTRL + SHIFT = chord; ALT + SHIFT
  = condition.
- AMP page: VELOCITY (how much the velocity changes the level). The MATERIAL button shows the sample's name. UNDO in red.
- Banks and presets of 0.5 / 0.6 are converted (the old bipolar FILTER becomes TYPE + CUTOFF).

## 0.6.0

- LOOP SYNC per track: a loaded loop plays in time at the tempo (time stretched by the grains, the pitch stays). The
  button above the waveform turns it on with the number of beats guessed (from a tempo in the file name such as
  "_174bpm", otherwise the length closest to 120 BPM); right-click or scroll changes the beats. Files named as loops
  ("loop", "break", "bpm") are synced as soon as they load, and so are the rhythmic built-in materials (BELLS, PLUCKS,
  BEAT, KEYS). SCAN then sets the speed against the tempo (x1 = in time). The display shows the loop's own tempo.
- SCALE and ROOT (bottom bar): every grain's pitch (step notes, keys, MIDI, PITCH, LFOs) moves to the nearest note of
  the scale (major, minor, dorian, phrygian, harmonic minor, pentatonics, blues); notes outside it are dimmed on the keyboard.

## 0.5.0

- The effects of KDSP-16: ECHO (8 models: digital, tape, BBD, ping-pong, lo-fi, multi-tap, reverse, diffuse, with MOD,
  R TIME, DRIVE, DUCK and FREEZE), REVERB (room, plate, hall, spring, shimmer), CHORUS (chorus, ensemble, flanger), CHAOS
  (slices of the tracks caught and repeated, HOLD pad), master FILTER and MASTER (EQ, compressor, drive, volume). Each
  track sends to them from its FX page (DELAY, REVERB, CHORUS, CHAOS).
- GRAIN MIX per track: the sample played straight (from POSITION to END, looping, at the note's pitch) on one side, the
  grains on the other.
- LOW CUT per track (a second filter), a third LFO per track, and LFO-moved knobs drawn as in the other Kinetik plug-ins
  (the arc follows the modulation in the LFO's colour, L1 / L2 / L3 badge).
- REC at the bottom left (with CLICK): while playing, notes played on the keyboard or by MIDI are written on the selected
  track's steps (with their length; notes played together become a chord when they match one), knob moves become locks.
  One undo per take.
- RESET: every knob and effect back to its default; the samples, materials, slices and patterns stay.
- 128 steps per track (8 pages). Presets, SAVE AS PRESET, FILL and UNDO in a bar at the bottom; the window is 1180 x 800.
- The text under the grain display is gone.
- Banks and presets of 0.4 are converted; host automation of older projects no longer matches the knobs.

## 0.4.1

- Polyphony you can play: MIDI channel 1 now plays the selected track with 6 voices (channels 2-5 still play tracks 1-4;
  track trigs, FILL and pattern selection moved to channel 16). The on-screen keyboard gets CHORD (a key plays a chord),
  LATCH (keys stay down) and PC KEYS (play from the computer keyboard, Z / X = octave); the keys of every sounding voice
  light up in its colour. New instances start without LEGATO.
- Every grain is drawn: its window over the stretch of material it reads, at its pitch (chords and octaves become separate
  bands), shifted by its pan, brightened by its window and the note's envelope, one colour per voice; outlines show
  original-pitch grains (PITCH MIX) and octave grains, arrows reversed grains. SIZE, DENSITY, SHAPE, SPRAY, SPREAD,
  REVERSE, OCTAVE, PITCH, PITCH MIX, JITTER, the envelopes and the LFOs can all be seen moving the grains. The counts are
  shown under the display.
- ZOOM: the display shows START to END (on by default).

## 0.4.0

- Polyphony: 6 voices per track, each with its own envelope (ATTACK DECAY SUSTAIN RELEASE) and filter moved by FLT ENV;
  the oldest note is taken when a seventh starts.
- New interface in the spirit of the other Kinetik sequencers: track tabs (SOLO / MUTE), one display per track (waveform
  with START / END flags, slices, POSITION and the play head of every voice), knob pages SOURCE / GRAIN / AMP / FX /
  LFO 1 / LFO 2, effect tabs, and a single-track sequencer with lanes TRIG / NOTE / LEN / CHORD / RATCH / COND and an
  on-screen keyboard.
- Steps play notes (C3 = the sample's own pitch), chords, note lengths (up to HOLD), ratchets and conditions (chance,
  FILL, first loop, every n loops). FILL (HOLD) button and MIDI B2.
- START / END: the part of the sample (or slice) that plays. PITCH MIX: grains at the original pitch mixed with
  transposed grains. FINE tuning, DRIVE per track.
- Two LFOs per track (6 shapes, free or synced, retrigger) on any knob.
- Effects extended: echo HIGH CUT / LOW CUT / WIDTH / LEVEL, reverb PRE-DELAY / WIDTH / LEVEL, master CHORUS, DRIVE,
  LOW CUT, HIGH CUT.
- Sound: new stereo built-in materials (PAD, BELLS, CHOIR, RAIN, PLUCKS, BEAT, DRONE, KEYS, all in C minor), pitched-up
  grains without aliasing, a smoother reverb, livelier defaults per track, LEGATO. Factory presets redone; Grain Chords added.
- MIDI channels 2-5 play the tracks polyphonically. Banks and presets of 0.2 / 0.3 are converted (pitch locks become step
  notes); host automation of older projects no longer matches the knobs.

## 0.3.0

- New grain engine, one MODE per track: CLASSIC (grains chained without gaps, clean time stretch and freeze), CLOUD
  (free grains, regular or random, 1.5 to 16 at once) and LOOP (loops of SIZE at the play head, crossfaded, 1 to 4
  detuned copies). SYNC: SIZE in note values; CLOUD's regular grains fall on the tempo grid.
- 20 knobs per track: SOURCE (POSITION SLICE SCAN SPRAY PITCH), GRAIN (SIZE DENSITY SHAPE JITTER REVERSE), COLOUR
  (OCTAVE SPREAD FILTER RESO), ENV (ATTACK DECAY), MIX (VOLUME PAN DELAY REVERB). SHAPE: square to triangle to smooth to
  percussive windows; REVERSE / OCTAVE: chance per grain; JITTER: random pitch, length, level and timing.
- No more clicks: grains never wrap inside a slice, cubic interpolation, a smooth output safety; the level stays even
  whatever the size and density; stereo samples stay stereo.
- Audio input (side-chain bus): LIVE material granulates the input as it comes (FREEZE holds it); CAPTURE turns the last
  1, 2, 4 or 8 bars into a track's sample (sliced, saved with the project and presets).
- Factory presets redone, four new ones (Shimmer Freeze, Grid Cloud, Loop Lab, Live Granulator). Projects and presets
  made with 0.2 keep their patterns and locks, but the knobs of the host automation moved.

## 0.2.0

- CHAIN: play a list of patterns in a loop, each a number of times.
- Opening a step page past the end of the selected track makes the track (and the pattern) that long.

## 0.1.0

- First version: four granular sample tracks (POSITION, SLICE, SCAN, PITCH, SIZE, DENSITY, SPRAY, SPREAD, ATTACK,
  DECAY, FILTER, RESO, VOLUME, PAN, DELAY, REVERB), transient or equal slicing, eight built-in materials, sample
  loading (WAV / AIFF, drag and drop), 64-step sequencer with per-step locks on every knob, track length and speed,
  16 patterns, echo and reverb sends, MIDI play and pattern select, 13 factory presets, user presets with samples,
  undo, four skins, updates and licence.
