## electropiano v0.84

A big update: a much faster editor, switches on every effect, deeper effects, one-knob easy
controls and your own tunings.

### Faster editor
- **Windows and Linux**: the editor draws about 6x faster, and meters, keys and the analyser now
  repaint only themselves. The stutter when playing, turning knobs or watching the EQ analyser is gone.
- **macOS**: about 5x faster too. Everything animates at 60 fps.

### Effects
- **On / off switches** on the envelope filter, phaser, tremolo, chorus, echo, reverb, EQ and
  compressor. Switching off keeps the settings.
- New **FX** page with the effects in depth:
  - **Envelope filter**: Freq, Resonance, Release, Mode (Classic, Low-pass, Band-pass, High-pass)
    and Direction (opens or closes as you play harder).
  - **Phaser**: Depth, Center, Feedback (−95 to +95 %), Stereo, 2 – 12 Stages and Tempo Sync.
  - **Chorus**: Rate, Depth, Delay (flanger to doubling) and Width.
  - At their defaults they sound as before.

### Easy controls
- **Decay**, **Tone**, **Drive** and **Mod** beside the keyboard, on every page. Each moves every
  setting behind one idea at once: Tone, for instance, moves bass and treble, chime, hammer
  hardness, the EQ shelves and the cabinet mic. Mod scales the whole modulation matrix and the
  modulation effects (0 = none, up to double).
- Knobs moved by an easy control show a blue ring and the value you hear.
- The keyboard strip shows 49 keys to make room.

### Modulation
- LFOs now run up to **400 Hz** (the first three quarters of the knob are unchanged).
- Each LFO shows its shape with a dot running at its speed, and each matrix slot shows what it is
  doing to its target right now.

### Tuning
- 17 more built-in scales: Kirnberger III, Vallotti, Young, sixth- and third-comma meantone,
  7-limit just intonation, the harmonic series, Maqam Rast and Bayati, Slendro, Pelog, 17-, 22- and
  53-EDO, Bohlen-Pierce and Carlos Alpha.
- **Custom scale**: drag a note's bar on the Play page to retune it in every octave.

### Realism
- **Condition** and **Sympathetic** are much stronger towards the top of their knobs (the presets'
  20 % sounds as before). Sympathetic resonance now also rings lightly without the pedal.

### Audio Unit
- The Audio Unit now passes Apple's validation (`auval`). Its manufacturer code changed, so
  projects that used the old AU need it inserted again (CLAP and VST3 are unaffected).

### Downloads

| File | Contents |
|---|---|
| `electropiano-v0.84-macOS-Universal.zip` | CLAP, VST3, AU, LV2, standalone app (Apple Silicon + Intel) |
| `electropiano-v0.84-Windows-x64.zip` | CLAP, VST3, LV2 |
| `electropiano-v0.84-Linux-x64.tar.gz` | CLAP, VST3, LV2 (X11) |

Installation and every control are explained in the
[user manual](https://github.com/mtyas/electropiano/blob/main/docs/MANUAL.md). The macOS builds are
not notarised yet: see *Installing* in the manual if macOS blocks them.
