## electropiano v0.83

A quick update to v0.82.

### Fixes
- **Mastering EQ**: dragging the EQ curve no longer makes a zipping noise. The right channel's
  filters were losing their state on every parameter change.

### Changes
- **Material** macro has a much stronger effect: longer ring, clearly stretched, inharmonic bell
  overtones and a louder chime towards glass and carbon fibre, even with the Chime knob low.
  At Steel it sounds as before.
- **Gap FM Ratio Mode**: a new *Exact* setting snaps the FM ratio to 1:4, 1:2, 1:1, 2:1 … 8:1 for
  harmonic, tuned FM tones. *Free* (the default) works as before, so existing presets and
  projects are unchanged.
- The **Scope** page has been removed; the editor now has seven pages.

### Downloads

| File | Contents |
|---|---|
| `electropiano-v0.83-macOS-Universal.zip` | CLAP, VST3, AU, LV2, standalone app (Apple Silicon + Intel) |
| `electropiano-v0.83-Windows-x64.zip` | CLAP, VST3, LV2 |
| `electropiano-v0.83-Linux-x64.tar.gz` | CLAP, VST3, LV2 (X11) |

Installation and every control are explained in the
[user manual](https://github.com/mtyas/electropiano/blob/main/docs/MANUAL.md). The macOS builds are
not notarised yet: see *Installing* in the manual if macOS blocks them.
