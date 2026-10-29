# Changelog

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
