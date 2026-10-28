# Changelog

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
