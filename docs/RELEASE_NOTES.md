## electropiano v0.82 — sustain pedal fix

A bug-fix release for 0.81. **Everyone on 0.81 should update.**

### Fixed
- **Sustain pedal thump and loudness jumps.** With the pedal, the level could swell suddenly when it
  went down (up to clipping on the 200A presets), and releasing it over held or ringing notes gave a
  loud thump. Both came from the new sympathetic resonance: its resonators switched between damped
  and undamped in a single sample, and its coupling was far too strong for overtones that land
  exactly on another key's pitch. The resonators are rebuilt so the dampers glide on and off over
  about 40 ms without a click, and the resonance now sits well under the notes (about 34 dB at the
  default 20 %, 20 dB at 100 %), as sympathetic strings do. Pedal moves are now as clean as in 0.79.

Projects saved with 0.81 load unchanged; where they used **Sympathetic**, its resonance is now
quieter and cleaner.

### Downloads

| File | Contents |
|---|---|
| `electropiano-v0.82-macOS-Universal.zip` | CLAP, VST3, AU, LV2, standalone app (Apple Silicon + Intel) |
| `electropiano-v0.82-Windows-x64.zip` | CLAP, VST3, LV2 |
| `electropiano-v0.82-Linux-x64.tar.gz` | CLAP, VST3, LV2 (X11) |

Everything new in 0.81 (microtuning, pedals, LV2, the resizable editor and more) is described below.

---

## electropiano v0.81 — playing, tuning, space and formats

Physically modelled **Rhodes** and **Wurlitzer 200A** electric pianos, built from stages you can swap.
This release is about playing it like the real thing, tuning it any way you like, and running it
everywhere: CLAP, VST3 and now **LV2** on macOS, Windows and Linux; Audio Unit and a standalone app
on macOS.

![The new Play page](https://raw.githubusercontent.com/mtyas/electropiano/main/docs/images/editor_play.png)

### Playing
- **Sample-accurate** notes and automation in **VST3** and **Audio Unit** (CLAP already was): timing
  no longer depends on the buffer size, and every automation point in a block is applied.
- **Repeated notes strike the ringing tine**, as on the instrument, instead of restarting it.
- **Voice stealing fades** the stolen note out over 4 ms instead of cutting it.
- **Pedals**: continuous sustain with **half-pedalling**, **sostenuto** (CC 66), **soft pedal** (CC 67),
  and **release velocity** sets how fast the dampers land. Re-pressing the pedal lifts the dampers again.
- **Polyphony** 8 – 64 voices.

### Tuning
- **Master tune** (A4 = 415 – 466 Hz) and **transpose** (±24 semitones).
- **Microtuning**: Scala `.scl` scales with `.kbm` keyboard mappings, built-in just intonation,
  Pythagorean, meantone, Werckmeister III, 19 / 24 / 31-EDO, and **MIDI Tuning Standard** SysEx.
  The tuning is saved with the project.

### Realism and space
- **Sympathetic resonance**: with the pedal down, undamped tines ring along with what you play.
- **Condition**: per-key variation of tuning, voicing, decay and hammer wear.
- **Key spread** across the stereo field.
- New reverbs: **Studio Hall** (8-line feedback delay network, true stereo) and **Plate**, plus
  **pre-delay**; the previous hall stays as *Classic Hall*. A **tri-chorus** mode.
- Offline bounces use the 4× oversampled preamp automatically.

### Editor
- **Resizable** (75 – 200 %, or drag the window where the host allows), remembered between sessions.
- New **Play** page: tuning with a cents chart, polyphony, condition, sympathetic, key spread,
  **A/B compare** and zoom. A **CPU** and voices read-out on every page.
- Preset browser: **search**, **favourites** and preset descriptions.

### Formats and engine
- **LV2** on macOS, Windows and Linux (with the editor).
- CPU: denormals are flushed (no more spikes from decaying tails on x86), an idle instance uses
  next to no CPU, the hammer-contact solve is about twice as fast, the 200A vibrato oscillator and
  Rhodes voices are cheaper, and parameter smoothing only runs for knobs that are moving.
- Fixes: undo / redo and loading a state no longer write audio-thread data from the UI thread;
  the scope is published safely; a note-on no longer jumps every smoothed knob; the Audio Unit's
  component version now matches the release.
- CMake is now the single build on every platform (the Makefile wraps it); pull requests are
  checked for unintended sound changes.

**Projects saved with 0.79 load with the same settings and reverb**; the new controls start at
values that leave the old sound unchanged.

### Downloads

| File | Contents |
|---|---|
| `electropiano-v0.81-macOS-Universal.zip` | CLAP, VST3, AU, LV2, standalone app (Apple Silicon + Intel) |
| `electropiano-v0.81-Windows-x64.zip` | CLAP, VST3, LV2 |
| `electropiano-v0.81-Linux-x64.tar.gz` | CLAP, VST3, LV2 (X11) |

Installation and every control are explained in the
[user manual](https://github.com/mtyas/electropiano/blob/main/docs/MANUAL.md). The macOS builds are
not notarised yet: see *Installing* in the manual if macOS blocks them.

---

## electropiano v0.79 — expression, modulation, mastering

Physically modelled **Rhodes** and **Wurlitzer 200A** electric pianos, built from stages you
can swap. This release adds a production layer on top of the instruments and folds it all into one
window. CLAP and VST3 on macOS, Windows and Linux; Audio Unit and a standalone app on macOS.

![electropiano](https://raw.githubusercontent.com/mtyas/electropiano/main/docs/images/editor_sound.png)

### New in 0.79

**Expression**
- **MPE**: per-note pitch bend (on tines *and* reeds, even while they ring), pressure and slide.
  Bend range 1 – 48 semitones. CLAP note expressions and VST3 polyphonic pressure are supported.

**Modulation**
- **Modulation matrix**: two LFOs (sine, triangle, saw, square, sample-and-hold; free or
  tempo-synced) and six slots, with nine sources (LFOs, mod wheel, pressure, velocity, key, output
  envelope, random) and 39 targets. Sources and targets are chosen from **dropdown lists**.
- **CLAP host parameter modulation** on continuous controls.

**"Impossible instrument" macros**
- **Material**: steel → glass → carbon fibre.
- **Pickups / Spread**: one to four magnetic pickups per tine.
- **Gap FM**: audio-rate modulation of the pickup gap and offset, locked to each note's pitch.
- **Morph**: crossfade from tine to reed (or back) while a note rings.

**Mastering and space**
- **Four-band parametric mastering EQ** with an interactive curve (drag the points, scroll for Q)
  and a live spectrum analyser.
- **Compressor**: threshold, ratio, attack, release, knee, make-up and auto gain, sidechain
  high-pass, peak / RMS detector, parallel mix, with a live transfer curve and gain-reduction
  history. Plus an **output limiter**.
- **Nine miked cabinets** with mic position, distance and room, and a plotted response; the original
  200A speaker remains.
- **Spring reverb** alongside the hall.
- **Tempo sync** for tremolo / vibrato, phaser and echo; **2× / 4× oversampling** of the suitcase
  preamp (about 18 dB less aliasing at 2×).

**Editor**
- **One window, seven pages**: Sound, Mod, Macros, Space, EQ, Dynamics, Scope, with the header,
  keyboard and live output meters always visible.
- A **Scope** that draws the tine and hammer, the pickup field and the amp's saturation in real time.
- Knob scales and a glow on the active arc. The window is now 900 × 776.

**Under the hood**
- 101 automatable controls (up from 32). Saved projects from earlier versions still load.
- The macOS standalone's MIDI input now handles channel pressure and other short messages.
- The CLAP / VST3 state loaders accept older, shorter saves.
- New tests cover MPE bend accuracy and channel isolation, modulation routing, aliasing, tempo sync,
  macros, and that the EQ and cabinet curves drawn in the editor match the audio.

### Downloads

| File | Contents |
|---|---|
| `electropiano-v0.79-macOS-Universal.zip` | CLAP, VST3, AU, standalone app (Apple Silicon + Intel) |
| `electropiano-v0.79-Windows-x64.zip` | CLAP, VST3 |
| `electropiano-v0.79-Linux-x64.tar.gz` | CLAP, VST3 (X11) |

Installation and every control are explained in the
[user manual](https://github.com/mtyas/electropiano/blob/main/docs/MANUAL.md).

### Things to know

- The cabinets are procedural models, not recorded impulse responses, and the new effects have had
  little listening time yet: feedback on how they sound is very welcome.
- **Morph** is read when a note starts: set it above zero before you play.
- Choosing a cabinet other than *Speaker (200A)* turns the main panel's **Cabinet** knob into
  **Cab Drive**. The cabinet list changed since 0.77, so a saved cabinet choice may now point at a
  different cabinet.
- **AU** and the **standalone app** assume 120 BPM for tempo sync. **VST3** hosts rarely pass pitch
  bend and aftertouch to plugins, so MPE is best used through CLAP or AU.
- macOS builds are not notarised: see the manual if macOS blocks them.
- The Windows and Linux builds are compiled and tested by CI but have had less hands-on testing in
  hosts than the macOS build. The editor isn't scaled for high-DPI displays yet.

### Credits

Rhodes model after Antoine Falaize & Thomas Hélie (J. Sound Vib. 390, 2017) and PyPHS.
Wurlitzer 200A model from OpenWurli by hal0zer0 and contributors. Licensed GPL-3.0-or-later.

---

*v0.77 was the first public release: the Rhodes and Wurlitzer 200A models with swappable stages,
envelope filter, phaser, chorus, tape echo, reverb, 28 presets, MIDI learn and undo/redo.*
