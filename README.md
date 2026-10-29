# Kinetik KGRN-4

Polyphonic granular sampler plug-in (VST3 / AU / CLAP, Windows and macOS): four sample tracks (or the live input), each cut into slices and
played as grains by a step sequencer with per-step locks on every knob.

- Load a WAV / AIFF file on each track (LOAD or drop it on the window), or use one of the eight built-in materials
  (PAD, BELLS, CHOIR, RAIN, PLUCKS, BEAT, DRONE, KEYS) synthesized in code, stereo, in C minor.
- Slicing: at the transients (sensitivity) or into 1-32 equal slices; click the waveform to choose a slice and the
  start inside it, drag the S / E flags to set START and END.
- Three grain engines per track: CLASSIC (smooth time stretch and freeze), CLOUD (regular or random clouds) and LOOP
  (crossfaded loops, stacked copies); SYNC sets the grain size in note values.
- 6 voices per track. Knob pages: SOURCE (START, END, POSITION, SLICE, SCAN, SPRAY, PITCH, PITCH MIX), GRAIN (SIZE,
  DENSITY, SHAPE, SPREAD, JITTER, REVERSE, OCTAVE, FINE), AMP (ATTACK, DECAY, SUSTAIN, RELEASE, VOLUME, PAN), FX (FILTER,
  RESO, FLT ENV, DRIVE, DELAY, REVERB), LFO 1 and LFO 2 (WAVE, RATE, SYNC, DEPTH, DEST, TRIG). SCAN at the centre
  freezes the sound, left plays it backwards; PITCH does not change the speed; PITCH MIX blends original and transposed grains.
- Audio input (side-chain): LIVE tracks granulate the input as it comes (FREEZE), CAPTURE keeps the last 1-8 bars as a
  track's sample.
- Sequencer: 4 tracks of up to 64 steps with their own length and speed, edited one at a time with lanes TRIG, NOTE,
  LEN, CHORD, RATCH and COND and an on-screen keyboard; right-click a step to lock any knob on it. 16 patterns, CHAIN,
  swing, FILL, copy / paste, rotate.
- Tempo-synced ping-pong echo (high / low cut, width) and a reverb (pre-delay, width) on sends; master chorus, drive,
  low / high cut and output.
- MIDI: channel 1 C3-D#3 play tracks 1-4, B2 = FILL, C4-D#5 choose patterns; channels 2-5 play tracks 1-4 polyphonically.
- Follows the host transport (or plays on its own); 19 factory presets and your own (with your samples); undo; four
  skins; resizable window.

Build: `make setup` once (fetches DPF), then `make` (or `make universal` on macOS, or
`make WINDOWS=true CC=x86_64-w64-mingw32-gcc CXX=x86_64-w64-mingw32-g++` from Linux).

Proprietary software — see `LICENSE` and `EULA.txt`. Third-party code: `THIRD_PARTY_NOTICES.txt`.
