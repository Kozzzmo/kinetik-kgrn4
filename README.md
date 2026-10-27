# Kinetik KGRN-4

Granular sampler plug-in (VST3 / AU / CLAP, Windows and macOS): four sample tracks, each cut into slices and played
as clouds of grains by a step sequencer with per-step locks on every knob.

- Load a WAV / AIFF file on each track (LOAD or drop it on the window), or use one of the eight built-in materials
  (PAD, BELLS, VOICE, WIND, PLUCKS, BEAT, DRONE, GLASS) synthesized in code.
- Slicing: at the transients (sensitivity) or into 1-32 equal slices; click the waveform to choose a slice and the
  start inside it.
- 16 knobs per track: MATERIAL (POSITION, SLICE, SCAN, PITCH), GRAIN (SIZE, DENSITY, SPRAY, SPREAD),
  SHAPE (ATTACK, DECAY, FILTER, RESO), MIX (VOLUME, PAN, DELAY, REVERB). SCAN 0 freezes the sound, negative plays it
  backwards; PITCH does not change the speed.
- Sequencer: 4 tracks of up to 64 steps with their own length and speed, right-click a step to lock any knob on it
  (another slice, another pitch...). 16 patterns, swing, copy / paste, rotate.
- Tempo-synced ping-pong echo and a reverb on sends; output level.
- MIDI: channel 1 C3-D#3 play tracks 1-4, C4-D#5 choose patterns; channels 2-5 play tracks 1-4 chromatically.
- Follows the host transport (or plays on its own); 13 factory presets and your own (with your samples); undo; four
  skins; resizable window.

Build: `make setup` once (fetches DPF), then `make` (or `make universal` on macOS, or
`make WINDOWS=true CC=x86_64-w64-mingw32-gcc CXX=x86_64-w64-mingw32-g++` from Linux).

Proprietary software — see `LICENSE` and `EULA.txt`. Third-party code: `THIRD_PARTY_NOTICES.txt`.
