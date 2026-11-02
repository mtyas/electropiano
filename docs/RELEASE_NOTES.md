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
