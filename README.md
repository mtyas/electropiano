# electropiano by mtyas

[![Build Status](https://github.com/matthewtyas/pianoelectro/actions/workflows/build.yml/badge.svg)](https://github.com/matthewtyas/pianoelectro/actions)
[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](https://opensource.org/licenses/MIT)
[![Platforms: macOS | Windows | Linux](https://img.shields.io/badge/Platform-macOS%20%7C%20Windows%20%7C%20Linux-blue.svg)]()
[![Formats: VST3 | CLAP | AU | Standalone](https://img.shields.io/badge/Formats-VST3%20%7C%20CLAP%20%7C%20AU%20%7C%20Standalone-green.svg)]()
[![Website: mtyas.com](https://img.shields.io/badge/Website-mtyas.com-orange.svg)](https://mtyas.com)
[![Support on Ko-fi](https://img.shields.io/badge/Donate-Ko--fi-ff5e5b.svg)](https://ko-fi.com/mtyas)

An open-source, multiplatform (macOS, Windows, Linux) physical modeling virtual instrument simulating **Fender Rhodes** (Mk I & Dyno-My-Piano) and **Wurlitzer 200A** electric pianos.

Created and developed by **Matthew Tyas (mtyas)**:
- 🌐 **Website**: [mtyas.com](https://mtyas.com)
- ☕ **Support / Donate**: [ko-fi.com/mtyas](https://ko-fi.com/mtyas)

The acoustic and electro-mechanical engine is based on the **Port-Hamiltonian Systems (PHS)** research developed by Antoine Falaize & Thomas Hélie (*"Passive simulation of the nonlinear port-Hamiltonian modeling of a Rhodes piano"*, *Journal of Sound and Vibration*, 2017) and the [pyphs](https://github.com/pyphs/pyphs) research project.

---

## 🎹 Physical & Mathematical Modeling (PHS)

Unlike traditional sample libraries that require tens of gigabytes of static velocity layers, **electropiano** simulates the continuous, coupled multi-physical laws of conservation in discrete time using **structure-preserving symplectic numerical schemes**.

```
 +---------------------------------------------------------------------------------------+
 |                                  PORT-HAMILTONIAN SYSTEM                              |
 |                                                                                       |
 |  [H] Hammer Felt      [B] Cantilever Tine / Reed    [T] Tone-Bar      [P] Transducer   |
 |  Hertzian Storage     Euler-Bernoulli Modal Beam    Coupled Shimmer   Faraday / E-Stat|
 |  + Dissipative Loss   Modes: f0, 6.27*f0, 17.5*f0   Tuned Resonator   C-Inf Analytic  |
 |       \                      /                           /                   /        |
 |        +---- Interconnection Matrix J = -J^T  <---------+                   /         |
 |        +---- Dissipation Matrix R >= 0                                     /          |
 +---------------------------------------------------------------------------+-----------+
                                                                             |
                                                                             v
                                                                 [E] Vintage Stage FX
                                                                 - Continuous JFET Preamp
                                                                 - Passive Baxandall EQ
                                                                 - Suitcase Stereo Tremolo
                                                                 - 4-Stage Optical Phaser
                                                                 - Stereo BBD Chorus
                                                                 - Stereo Spring & Hall Reverb
```

### 1. Hammer-Tine Impact ($H \leftrightarrow B$)
The hammer felt is modeled as a nonlinear Hertzian contact spring with Hunt-Crossley hysteresis:
$$F_h(\xi, \dot{\xi}) = \left[ k_h \xi^{\alpha_h} + \lambda_h \xi^{\alpha_h} \dot{\xi} \right]^+$$
Contact duration $\tau_c \approx 0.5\text{--}1.5\text{ ms}$ is physically calibrated across the 88-key keyboard to ensure zero sub-bass thumps and 100% natural impulse transfer into the cantilever beam.

### 2. Cantilever Beam Resonator ($B$)
The tine (Rhodes) and reed (Wurlitzer) are modeled using exact modal decompositions of the clamped-free Euler-Bernoulli beam equation:
- **Mode 0 ($1.000 f_0$)**: Fundamental transverse bending mode providing warm singing sustain.
- **Mode 1 ($\sim 6.267 f_0$)**: Signature crystalline cantilever chime ("Tine Bell").
- **Mode 2 ($\sim 17.55 f_0$)**: Initial metallic strike ping.
- **Modes 3 & 4 ($2 f_0, 3 f_0$)**: Structural bridge warmth and harmonic body.

All modal frequencies use exact continuous-to-discrete bilinear/midpoint pre-warping:
$$\omega_{m,\text{warped}} = 2 f_s \tan\left(\frac{\pi f_m}{f_s}\right)$$

### 3. Nonlinear Pickup Transduction ($P$)
Faraday's law of electromagnetic induction (Rhodes coil) and electrostatic capacitance variation (Wurlitzer reed) are integrated using $C^\infty$ analytic sensitivity functions that guarantee unconditional numerical passivity:
$$\frac{dH}{dt} + P_{\text{diss}} = P_{\text{in}} \quad \Longleftrightarrow \quad \Delta H_{\text{discrete}} \le 0 \quad (\text{post-impact})$$

---

## ✨ Features

- **Multiplatform Formats**:
  - **macOS**: VST3, CLAP, AudioUnit v2 (`.component`), and Standalone App (Universal arm64 + x86_64).
  - **Windows**: VST3, CLAP, and Standalone (`.exe`) (x64).
  - **Linux**: VST3, CLAP, and Standalone (x64).
- **Streamlined Compact GUI ($880 \times 580$ px)**:
  - Milled dark brushed graphite chassis with solid American walnut end-cheeks.
  - Short, punchy knob labels (`Hardness`, `Cushion`, `Strike`, `Decay`, `Chime`, `Tonebar`, `Thump`, `Touch`, `Air Gap`, `Voicing`, `Coil Q`, `Bass`, `Treble`, `Drive`, `Speed`, `Depth`, `Rate`, `Mix`, `Chorus`, `Volume`).
  - Raytraced 3D rotary knobs modeled in Blender with 64-frame animation and glowing LED value arcs.
  - **About Window**: Built-in dialog with plugin information, project credits, link to [mtyas.com](https://mtyas.com), and [Ko-fi donation link](https://ko-fi.com/mtyas).
- **Full Undo & Redo History**:
  - 64-step deep undo/redo stack tracking parameter edits and preset changes.
- **Interactive MIDI Learn**:
  - Click `MIDI` in the toolbar or **right-click** any knob, move a MIDI hardware CC controller to bind instantly.
  - Mapped knobs display glowing `CC #` badges.
- **Full-Featured Preset Browser**:
  - 16 curated factory presets across 4 categories: *Rhodes Vintage*, *Wurlitzer Reeds*, *Dyno & Mod 80s*, *Ambient & FX*.
  - Category filtering, search, and user preset saving to disk (`.phspreset`).
- **Playable Keyboard & Controls**:
  - Playable 61-key velocity-sensitive keyboard with active key lighting.
  - Pitch Bend and Modulation Wheels with 3D slider caps.
  - Authentic Sustain Pedal rocker switch with green jewel indicator.

---

## 🛠️ Building from Source

### macOS (Apple Clang / Make)
No external libraries are required. Simply run:
```bash
# Build all formats (CLAP, VST3, AU, App) and run tests
make all
make test

# Install to system plugin folders (~/Library/Audio/Plug-Ins)
make install
```

### Cross-Platform Build (CMake)
Works on macOS, Windows (Visual Studio 2022), and Linux (GCC / Clang):
```bash
# Configure
cmake -B build -DCMAKE_BUILD_TYPE=Release

# Build plugins and test runner
cmake --build build --config Release

# Run automated tests
ctest --test-dir build --output-on-failure
```

---

## 🚀 Hosting on GitHub & Automated Builds

This repository includes a complete GitHub Actions CI/CD matrix workflow (`.github/workflows/build.yml`) that builds and packages binaries for macOS, Windows, and Linux on every commit or tag push.

```bash
git push origin main
git tag v1.2.0
git push origin v1.2.0
```

When you push a tag like `v1.2.0`, GitHub Actions will automatically compile, test, and package release ZIP archives (`electropiano-macOS-Universal.zip`, `electropiano-Windows-x64.zip`, `electropiano-Linux-x64.tar.gz`) directly to your GitHub Releases page!

---

## 📄 License & Citations

Licensed under the **MIT License**. See [LICENSE](LICENSE) for details.

If you use this project in an academic or musical context, please cite:
```bibtex
@article{falaize2017passive,
  title={Passive simulation of the nonlinear port-Hamiltonian modeling of a Rhodes piano},
  author={Falaize, Antoine and H{\'e}lie, Thomas},
  journal={Journal of Sound and Vibration},
  volume={390},
  pages={289--309},
  year={2017},
  publisher={Elsevier}
}
```
