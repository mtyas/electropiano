## electropiano v0.82

Physically modelled **Rhodes** and **Wurlitzer 200A** electric pianos, built from stages you can swap:
a felt hammer, a steel tine or reed, a magnetic or electrostatic pickup and an amplifier, each
simulated part by part. CLAP, VST3 and LV2 on macOS, Windows and Linux; Audio Unit and a standalone
app on macOS.

![The Play page](https://raw.githubusercontent.com/mtyas/electropiano/main/docs/images/editor_play.png)

### The instruments
- **Fender Rhodes** (Mk I and Dyno voicings): a port-Hamiltonian hammer, tine and tone bar after
  Falaize & Hélie, solved with an energy-conserving scheme, through their magnetic pickup law.
- **Wurlitzer 200A**: a C++ port of OpenWurli — reed, electrostatic pickup and the 200A's amplifier,
  component by component.
- **Swappable stages**: any resonator through any pickup into either amp.

### Playing
- Notes and automation are **sample-accurate** in every format.
- **Repeated notes strike the ringing tine**, as on the instrument; stolen voices fade out cleanly.
- **Pedals**: continuous sustain with **half-pedalling**, **sostenuto** (CC 66), **soft pedal** (CC 67);
  **release velocity** sets how fast the dampers land.
- **MPE** and per-note expression; polyphony 8 – 64 voices.

### Tuning
- **Master tune** (A4 = 415 – 466 Hz) and **transpose** (±24 semitones).
- **Microtuning**: Scala `.scl` scales with `.kbm` keyboard mappings, built-in just intonation,
  Pythagorean, meantone, Werckmeister III, 19 / 24 / 31-EDO, and **MIDI Tuning Standard** SysEx.
  The tuning is saved with the project.

### Realism and space
- **Sympathetic resonance**: with the pedal down, undamped tines ring softly along with what you play.
- **Condition**: per-key variation of tuning, voicing, decay and hammer wear.
- **Key spread** across the stereo field.
- Reverbs: **Studio Hall** (8-line feedback delay network, true stereo), **Plate**, **Spring** and
  **Classic Hall**, with pre-delay; **tri-chorus**, tape echo, phaser, envelope filter.
- **Mastering**: four-band parametric EQ with a live spectrum, compressor, limiter, nine miked cabinets.
- **Macros**: tine material, up to four pickups, audio-rate gap FM, tine ⇄ reed morph.

### Editor
- One **resizable** editor on every platform (75 – 200 %), eight pages, a CPU and voices read-out.
- **Play** page: tuning with a cents chart, polyphony, condition, sympathetic, key spread,
  **A/B compare** and zoom.
- Preset browser with **search**, **favourites** and descriptions; MIDI learn; undo / redo.

### Downloads

| File | Contents |
|---|---|
| `electropiano-v0.82-macOS-Universal.zip` | CLAP, VST3, AU, LV2, standalone app (Apple Silicon + Intel) |
| `electropiano-v0.82-Windows-x64.zip` | CLAP, VST3, LV2 |
| `electropiano-v0.82-Linux-x64.tar.gz` | CLAP, VST3, LV2 (X11) |

Installation and every control are explained in the
[user manual](https://github.com/mtyas/electropiano/blob/main/docs/MANUAL.md). The macOS builds are
not notarised yet: see *Installing* in the manual if macOS blocks them.
