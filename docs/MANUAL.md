# electropiano — User Manual

**Version 0.77** · by Matthew Tyas (mtyas) · GPL-3.0

![electropiano with a Rhodes Mk I preset and a chord held](images/editor_rhodes.png)

electropiano is a physically modelled electric piano. Nothing in it is sampled: every note
is computed from the mechanics and electronics of the instruments, so it responds to your
playing the way the real thing does, and you can reach inside and change how it is built.

It models two classic instruments:

- the **Fender Rhodes** (Mk I and Dyno-My-Piano voicings) — a felt hammer striking a steel
  tine, read by a magnetic pickup; and
- the **Wurlitzer 200A** — a hammer striking a steel reed, read by an electrostatic pickup and
  amplified by the 200A's own circuit.

What makes electropiano different is that each instrument is built from **stages you can
swap**: put a Rhodes tine in front of the Wurlitzer's pickup and amplifier, or a Wurlitzer
reed on a Rhodes pickup, and every combination in between.

---

## Contents

1. [Installing](#1-installing)
2. [A quick tour](#2-a-quick-tour)
3. [The signal chain](#3-the-signal-chain)
4. [Resonator controls](#4-resonator-controls)
5. [Pickup and tone controls](#5-pickup-and-tone-controls)
6. [Amp](#6-amp)
7. [Effects](#7-effects)
8. [Presets](#8-presets)
9. [Playing, MIDI and automation](#9-playing-midi-and-automation)
10. [Tips for sound design](#10-tips-for-sound-design)
11. [Troubleshooting](#11-troubleshooting)
12. [How it works](#12-how-it-works)
13. [Credits and licence](#13-credits-and-licence)

---

## 1. Installing

Download the archive for your system from the
[Releases page](https://github.com/mtyas/electropiano/releases) and unzip it.

| System | Formats | Where to put them |
|---|---|---|
| **macOS** (Apple Silicon and Intel) | `electropiano.clap`, `electropiano.vst3`, `electropiano.component` (Audio Unit), `electropiano.app` (standalone) | `~/Library/Audio/Plug-Ins/CLAP/`, `~/Library/Audio/Plug-Ins/VST3/`, `~/Library/Audio/Plug-Ins/Components/`, and the app anywhere you like |
| **Windows** (64-bit) | `electropiano.clap`, `electropiano.vst3` | `C:\Program Files\Common Files\CLAP\` and `C:\Program Files\Common Files\VST3\` |
| **Linux** (64-bit, X11) | `electropiano.clap`, `electropiano.vst3` | `~/.clap/` and `~/.vst3/` |

Then rescan plugins in your DAW.

**macOS note.** The builds are not notarised by Apple. If macOS refuses to load them, remove
the download quarantine flag once, in Terminal:

```bash
xattr -dr com.apple.quarantine ~/Library/Audio/Plug-Ins/CLAP/electropiano.clap ~/Library/Audio/Plug-Ins/VST3/electropiano.vst3 ~/Library/Audio/Plug-Ins/Components/electropiano.component
```

The window is 900 × 740 pixels and looks the same on all three systems:

![The same editor drawn by the Windows / Linux renderer](images/editor_windows_linux.png)

---

## 2. A quick tour

### Header

![Header](images/section_header.png)

From left to right:

- **Instrument** — `Mk I`, `Dyno`, `200A`, `Hybrid`. Each button sets a whole instrument: its
  voicing *and* its three stages (see below). Choose one as a starting point.
- **Preset** — `‹` and `›` step through presets; click the name to open the preset browser.
- **Undo / Redo** — every knob move, preset change and stage change can be undone.
- **MIDI** — MIDI-learn mode (see [section 9](#9-playing-midi-and-automation)).
- **i** — about electropiano and the work it builds on.

### Signal chain

![Signal chain strip](images/section_signal_chain.png)

Three switches, read left to right: what vibrates (**Resonator**), what turns the vibration
into a voltage (**Pickup**) and what amplifies it (**Amp**). The panels below change their
names and controls to match what you select.

### Knobs

- **Drag** up and down to turn a knob. Hold **Shift** for fine control.
- **Double-click** a knob to return it to its default.
- **Scroll** over a knob to nudge it.
- **Right-click** a knob for *MIDI Learn*, *Forget MIDI CC* and *Reset to Default*.
- The orange arc shows the value; Bass and Treble arcs grow from the centre (cut ↔ boost).
- A small blue tag such as `CC74` means the knob is mapped to a MIDI controller.

### Keyboard strip

![Keyboard strip](images/section_keyboard.png)

A playable 61-key keyboard (keys light up for notes being played, from the mouse or MIDI),
a **Sustain** switch and the pitch / modulation wheel displays. The modulation wheel adds
tremolo / vibrato depth.

---

## 3. The signal chain

| Stage | Choices | |
|---|---|---|
| **Resonator** | **Rhodes Tine** | A felt hammer strikes a cantilevered steel tine coupled to a tone bar. Simulated as a port-Hamiltonian system (Falaize & Hélie / PyPHS): energy is conserved exactly by the numerical scheme, so the hammer, tine and tone bar interact like real mechanics. |
| | **Wurli Reed** | A hammer strikes a steel reed with a solder tip mass. OpenWurli's modal reed model, calibrated per note across the 200A's 64 reeds. |
| **Pickup** | **Magnetic** | The Rhodes pickup: the tine tip moves through a magnet's field. Uses Falaize & Hélie's flux law, so the tone depends on where the tip sits relative to the pickup. Followed by the coil's resonance. |
| | **Electrostatic** | The Wurlitzer pickup: the reed swings into a charged comb. Uses OpenWurli's field-solved capacitance law, the main source of the Wurlitzer's characteristic harmonics. |
| **Amp** | **Suitcase** | A Rhodes-style preamp with smooth tube/JFET saturation and the Suitcase's stereo auto-pan tremolo. |
| | **200A Circuit** | The Wurlitzer 200A's amplifier, simulated component by component (OpenWurli): two-transistor preamp, vibrato made by an LED-and-photoresistor inside the preamp's feedback loop (so it changes tone, not only volume), volume network and power amp. |

The instrument buttons set these for you:

| Instrument | Resonator | Pickup | Amp |
|---|---|---|---|
| Mk I | Rhodes Tine | Magnetic | Suitcase |
| Dyno | Rhodes Tine (Dyno voicing) | Magnetic (closer, stronger pickups) | Suitcase |
| 200A | Wurli Reed | Electrostatic | 200A Circuit |
| Hybrid | Rhodes Tine | Electrostatic | 200A Circuit |

After choosing one, change any single stage in the strip:

![A hybrid: Rhodes tine through the Wurlitzer pickup and amp](images/section_signal_chain_hybrid.png)

---

## 4. Resonator controls

![Resonator and pickup panels, Rhodes](images/section_resonator_pickup.png)

The eight knobs in the left panel shape the struck object. Their names change with the
resonator.

| Knob (Tine / Reed) | Rhodes tine | Wurlitzer reed |
|---|---|---|
| **Hardness** | Felt stiffness and its nonlinearity: harder felt gives a shorter contact and a brighter, more percussive attack. | Hammer contact as if struck harder: brighter onset. |
| **Cushion / Damper** | Felt damping: low = elastic snap, high = soft, absorbent felt. | Damper felt strength on release (centre = OpenWurli). |
| **Strike** | Where the hammer hits the tine (shown as a fraction of its length). Near the clamp excites more overtones. | — (not used by the reed) |
| **Decay** | Sustain time (shown as the T60 at middle C). | Sustain multiplier relative to the real 200A (×1.00 = calibrated). |
| **Chime / Overtones** | Level and length of the tine's bell partial (≈6.3 × the fundamental) and its high "ping". | Level of the reed's upper modes. |
| **Tone Bar / Tip Mass** | Coupling to the tone bar: sustain bloom and a slow shimmer (0.15–1 Hz beating). | Extra solder mass on the reed tip: moves the overtone frequencies. |
| **Thump / Attack** | Key-bed thud and felt click on the attack, plus the damper tap on release. | Level of the hammer's attack noise. |
| **Touch** | Velocity response: left = heavy (soft playing stays gentle), right = light (full tone even when played softly). | Same; centre = OpenWurli's response. |

With the reed selected:

![Reed and electrostatic panels](images/section_reed_electrostatic.png)

**Every reed and electrostatic control at its default position reproduces OpenWurli's
calibrated Wurlitzer 200A exactly.** Turn them to depart from it.

---

## 5. Pickup and tone controls

| Knob | Magnetic pickup | Electrostatic pickup |
|---|---|---|
| **Air Gap / Swing** | Horizontal distance from tine tip to pickup (mm). Closer = louder and more bark when you dig in. | How far the reed swings into the comb (×1.00 = factory). More swing = more harmonics. |
| **Voicing / Rest Gap** | Vertical offset of the tine tip from the pickup centre (mm). **Small values approach the pure octave "bell"**; larger values give a rounder fundamental. This is the real Rhodes voicing adjustment. | How far outside the finger band the reed rests (0.085 mm on a real 200A). Changes the balance of even harmonics. |
| **Coil Q / Plate C** | The coil's resonance: low = warm and dark, high = a singing presence peak. | Capacitance at the pickup plate (240 pF on a real 200A). Lower = higher corner = brighter. |
| **Bass, Treble** | ±16 dB shelving tone controls (140 Hz / 2.8 kHz), after the amp. | Same. |

---

## 6. Amp

![200A amp panel](images/section_200a_amp.png)

| Knob | Suitcase | 200A Circuit |
|---|---|---|
| **Drive / Volume** | Preamp saturation: clean to gritty. | The 200A's volume pot feeding the power amp. Push it to drive the amp into its rails (default 80 %). |
| **Tremolo / Vibrato** | Stereo auto-pan depth. | Vibrato depth: the photoresistor network inside the preamp's feedback loop. |
| **Speed** | Pan rate. | Vibrato rate. The 200A's own oscillator runs at about 5.6 Hz; the knob speeds it up or slows it down. |
| **Cabinet** | Speaker cabinet character: 0 = direct (flat), up = small-speaker roll-off, cone saturation and thermal compression. | Same. |

The **mod wheel** adds tremolo / vibrato depth on top of the knob.

---

## 7. Effects

![Effects panels](images/section_effects.png)

The effects follow the amp, in this order:

| Panel | Controls |
|---|---|
| **Env Filter** | An envelope-following resonant filter (auto-wah). **Amount** sets depth and mix; **Sens** sets how strongly your playing opens it. It sits in front of the amp, like a pedal. |
| **Phaser** | 4-stage optical phaser: **Mix**, **Rate**. |
| **Chorus** | Stereo BBD chorus: **Mix**. |
| **Tape Echo** | **Mix**, **Time** (40 ms – 1 s), **Repeats**. Repeats darken and saturate like tape; changing the time glides the pitch, like a tape-speed change. |
| **Reverb** | **Mix**, **Decay**. |
| **Output** | Master **Volume**. |

---

## 8. Presets

Click the preset name to open the browser:

![Preset browser](images/dialog_presets.png)

- Filter by category with the tabs; **scroll** for more; click a preset to load it.
- **Save Preset…** stores the current sound as a user preset. Type a name and press
  **Enter** (or **Save**); **Esc** cancels.

![Saving a preset](images/dialog_save.png)

- To delete a user preset, select it and click **Delete**. Factory presets can't be deleted.
- User presets are files in:
  - macOS: `~/Library/Application Support/mtyas/electropiano/Presets`
  - Windows: `%APPDATA%\mtyas\electropiano\Presets`
  - Linux: `~/.local/share/mtyas/electropiano/Presets`

### Factory presets

| Category | Presets |
|---|---|
| **Rhodes** | 1973 Suitcase Mk I · 1977 Stage Mk I (DI) · Silver Top '71 Sparkle · Mellow Ballad Mk I · Barking Mk I Overdrive · Bell Octave Tine · Herbie Funk Wah-Pan · Phase-90 Stage Funk |
| **Wurlitzer** | 200A Factory (OpenWurli) · 200A Supertramp Stage · 200A Dry Session · 200A Cranked Bark · 200A Vibrato Lounge · 200A Tape Echo · 200A Psychedelic Phase |
| **Dyno / Bells** | Dyno-My-Piano LA Bell · 80s Ballad Chorus EP · Dyno Tine Shimmer · Glass Tine |
| **Hybrid / FX** | Hybrid: Tine into 200A · Hybrid: Reed into Suitcase · Reed on Magnetic, 200A Amp · Tine on Comb, Suitcase · Mu-Tron Funk Machine · Dub Echo Chamber · Ambient Tine Pad · Sub-Bark Bass EP · Lo-Fi Cassette Keys |

**200A Factory (OpenWurli)** is OpenWurli's calibrated Wurlitzer with every control at its
reference position — a good place to start for an authentic 200A.

---

## 9. Playing, MIDI and automation

- **Velocity** drives the hammer physically: playing harder strikes harder, which changes the
  tone, not just the level.
- **Sustain pedal** (CC 64) and the on-screen **Sustain** switch hold notes; the dampers land
  when you release.
- **Mod wheel** (CC 1) adds tremolo / vibrato depth.
- **All 32 controls are automatable** from your DAW, including the three stage switches.

### MIDI Learn

![Knob context menu](images/menu_knob.png)

1. Right-click a knob and choose **MIDI Learn** (or click **MIDI** in the header, then click
   a knob). The knob shows `LEARNING`.
2. Move a controller on your MIDI device. The knob is now mapped and shows its CC number.
3. To remove a mapping, right-click the knob and choose **Forget MIDI CC**.

Factory mappings: CC 71 Drive, CC 74 Air Gap, CC 91 Tremolo, CC 92 Reverb, CC 93 Chorus.
Mappings are saved with your project.

---

## 10. Tips for sound design

- **Bark comes from the pickup, not the amp.** For a growling Rhodes, move **Air Gap** up
  (closer) and play hard; **Voicing** decides whether the extra harmonics lean to the octave
  or the fifth.
- **The octave bell.** Turn **Voicing** fully down on a magnetic pickup: with the tine centred
  on the pickup, the fundamental cancels and the octave rings out (preset *Bell Octave Tine*).
- **A Wurlitzer's edge comes from its comb.** On the electrostatic pickup, more **Swing** or a
  smaller **Rest Gap** pushes the reed deeper into the fingers.
- **Real 200A vibrato is timbral.** It modulates the preamp's feedback, so the tone moves as
  well as the level. Try it on a Rhodes tine (Hybrid).
- **Drive the 200A.** Its **Volume** knob above about 70 % pushes the power amp into its rails,
  as cranking a real 200A does.
- **Hybrids worth trying:** Rhodes tine → electrostatic → 200A (glassy, vocal); Wurli reed →
  magnetic → Suitcase (a reedy Rhodes with stereo pan).
- **Tone bar for pads.** High **Tone Bar** and long **Decay** give a blooming, shimmering
  sustain (preset *Ambient Tine Pad*).

---

## 11. Troubleshooting

| Problem | Try |
|---|---|
| macOS says the plugin is damaged or can't be opened | Remove the quarantine flag (see [Installing](#1-installing)). |
| The DAW doesn't list electropiano | Check the folder for the format and rescan. On Windows, make sure you copied the whole `electropiano.vst3` folder. |
| No sound from the 200A at first | The 200A circuit settles for a moment when the plugin loads; play again after a second. |
| Too quiet / too loud | Use **Output → Volume**. Presets are levelled to sit around −20 dBFS RMS. |
| Changing Resonator or Pickup doesn't affect ringing notes | Those two stages are chosen when a note starts: the next note uses the new stage. Amp changes are immediate. |
| Typing doesn't reach the save dialog | Some hosts keep keyboard input for themselves: click inside the editor first, or save the preset from the standalone app (macOS). |

---

## 12. How it works

**Rhodes tine.** Following Falaize & Hélie's port-Hamiltonian Rhodes model (and the PyPHS
components it is built from), the hammer is a mass with a nonlinear felt
($H = \frac{k}{\alpha+1}\xi^{\alpha+1}$, damping $\propto \xi^{\alpha-1}\dot\xi$), the tine is a
clamped–free Euler–Bernoulli beam (modes at 1, 6.27 and 17.55 × f₀) coupled to a tone bar,
and contact is solved with a discrete-gradient scheme. The discrete energy balance
$\Delta H + \Delta t\,P_{\text{diss}} = 0$ holds on every sample (verified to about 10⁻¹⁵ of
the stored energy, through hammer contact). The magnetic pickup applies their flux law
(eq. 25), with the horizontal and vertical tip offsets as controls.

**Wurlitzer 200A.** A C++ port of OpenWurli: a 7-mode reed with per-note tip mass, dwell and
damping; the electrostatic pickup's field-solved capacitance law; the 9-node DK/MNA preamp
(2N5089 transistors); the Twin-T oscillator driving the vibrato LED; the photoresistor in the
preamp's feedback loop; the volume network; the power amp; and the speaker. The port is
checked against OpenWurli's SPICE-derived targets (preamp DC point within 0.1 mV, 15.56 dB at
1 kHz, 880.5 Hz pickup corner, 0.37–2.28 mA LED current at 5.56 Hz).

**Editor.** One editor shared by all platforms. macOS draws it with CoreGraphics; Windows and
Linux draw it with a built-in anti-aliased renderer using the Inter typeface, so it looks the
same everywhere.

---

## 13. Credits and licence

- **electropiano** — Matthew Tyas (mtyas) · [mtyas.com](https://mtyas.com) ·
  [Support on Ko-fi](https://ko-fi.com/mtyas)
- **Rhodes model** — after Antoine Falaize and Thomas Hélie, *"Passive simulation of the
  nonlinear port-Hamiltonian modeling of a Rhodes piano"*, Journal of Sound and Vibration 390
  (2017), and the [PyPHS](https://github.com/pyphs/pyphs) project (GPL-3.0). No PyPHS code is
  included; the published equations are implemented independently.
- **Wurlitzer 200A model** — [OpenWurli](https://github.com/hal0zer0/openwurli) by hal0zer0
  and contributors (GPL-3.0), ported to C++.
- **Inter** typeface (Windows / Linux editor) — The Inter Project Authors, SIL Open Font
  License 1.1.

electropiano is free software under the **GNU General Public License v3.0 or later**. See
[LICENSE](../LICENSE).
