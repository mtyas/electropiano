# PyPHS Electric Piano

[![Build Status](https://github.com/matthewtyas/pianoelectro/actions/workflows/build.yml/badge.svg)](https://github.com/matthewtyas/pianoelectro/actions)
[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](https://opensource.org/licenses/MIT)
[![Platforms: macOS | Windows | Linux](https://img.shields.io/badge/Platform-macOS%20%7C%20Windows%20%7C%20Linux-blue.svg)]()
[![Formats: VST3 | CLAP | AU | Standalone](https://img.shields.io/badge/Formats-VST3%20%7C%20CLAP%20%7C%20AU%20%7C%20Standalone-green.svg)]()

An open-source, multiplatform (macOS, Windows, Linux) physical modeling virtual instrument simulating **Fender Rhodes** (Mk I & Dyno-My-Piano) and **Wurlitzer 200A** electric pianos.

The acoustic and electro-mechanical engine is based on the **Port-Hamiltonian Systems (PHS)** research developed by Antoine Falaize & Thomas Hélie (*"Passive simulation of the nonlinear port-Hamiltonian modeling of a Rhodes piano"*, *Journal of Sound and Vibration*, 2017) and the [pyphs](https://github.com/pyphs/pyphs) project.

---

## 🎹 Physical & Mathematical Modeling (PHS)

Unlike traditional sample libraries that require tens of gigabytes of static velocity layers, PyPHS simulates the continuous, coupled multi-physical laws of conservation in discrete time using **structure-preserving numerical schemes**.

```
 +---------------------------------------------------------------------------------------+
 |                                  PORT-HAMILTONIAN SYSTEM                              |
 |                                                                                       |
 |  [H] Hammer Felt      [B] Cantilever Tine / Reed    [T] Tone-Bar      [P] Transducer   |
 |  Hertzian Storage     Euler-Bernoulli Modal Beam    Coupled Shimmer   Faraday / E-Stat|
 |  + Dissipative Loss   Modes: f0, 6.27*f0, 17.5*f0   Tuned Resonator   Discrete Grad   |
 |       \                      /                           /                   /        |
 |        +---- Interconnection Matrix J = -J^T  <---------+                   /         |
 |        +---- Dissipation Matrix R >= 0                                     /          |
 +---------------------------------------------------------------------------+-----------+
                                                                             |
                                                                             v
                                                                 [E] Vintage Stage FX
                                                                 - Tube Preamp Saturation
                                                                 - Passive Baxandall EQ
                                                                 - Suitcase Stereo Tremolo
                                                                 - 4-Stage Optical Phaser
                                                                 - Stereo BBD Chorus
                                                                 - Stereo Spring/Plate
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

### 3. Nonlinear Pickup Transduction via Discrete Gradient ($P$)
Faraday's law of electromagnetic induction (Rhodes coil) and electrostatic capacitance variation (Wurlitzer reed) are integrated using the Gonzalez discrete gradient:
$$\varepsilon(t) = \bar{\nabla}\Phi\left(w^k, w^{k+1}\right) \cdot \frac{w^{k+1} - w^k}{\Delta t}$$
This guarantees unconditional numerical passivity:
$$\frac{dH}{dt} + P_{\text{diss}} = P_{\text{in}} \quad \Longleftrightarrow \quad \Delta H_{\text{discrete}} \le 0 \quad (\text{post-impact})$$

---

## ✨ Features

- **Multiplatform Formats**:
  - **macOS**: VST3, CLAP, AudioUnit v2 (`.component`), and Standalone App (Universal arm64 + x86_64).
  - **Windows**: VST3, CLAP, and Standalone (`.exe`) (x64).
  - **Linux**: VST3, CLAP, and Standalone (x64).
- **Full Undo & Redo History**:
  - 64-step deep undo/redo stack tracking parameter edits and preset changes.
  - Shortcut support: `Cmd+Z` / `Ctrl+Z` (Undo), `Cmd+Shift+Z` / `Ctrl+Y` (Redo).
- **Interactive MIDI Learn**:
  - Click `⚡ LEARN` in the toolbar or **right-click** any knob, move a MIDI hardware CC controller to bind instantly.
  - Mapped knobs display glowing `CC #` badges.
  - Mappings are serialized into plugin host DAW projects.
- **Full-Featured Preset Browser**:
  - 16 curated factory presets across 4 categories: *Rhodes Vintage*, *Wurlitzer Reeds*, *Dyno & Mod 80s*, *Ambient & FX*.
  - Full Category filtering, preset descriptions, and search.
  - **User Presets**: Save and delete custom patches on disk (`.phspreset`).
- **Boutique High-Contrast GUI**:
  - Metallic 3D rotary knobs with surrounding glowing LED value arcs.
  - Double-click to reset knob to factory default; Shift-drag for fine adjustment.
  - Real-time animated **Physical Hammer $\rightarrow$ Tine $\rightarrow$ Pickup** diagram with flux linkage lines.
  - Live CRT phosphor **Stereo Oscilloscope & 4-Band Harmonic Spectrum Analyzer** ($f_0, 2f_0, 3f_0, 6.27f_0$).
  - Playable 61-key velocity-sensitive keyboard with octave markers, Pitch Bend, Mod Wheel, and Sustain Pedal toggle (`Spacebar`).

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

This repository includes a complete GitHub Actions CI/CD matrix workflow (`.github/workflows/build.yml`) that builds and packages binaries for macOS, Windows, and Linux on every commit.

### Initializing & Pushing to Your GitHub:
```bash
# 1. Initialize git and commit
git init
git add .
git commit -m "Initial commit: PyPHS Electric Piano v1.0.0"

# 2. Add your GitHub repository remote
git remote add origin https://github.com/<your-username>/pianoelectro.git

# 3. Push to main branch
git branch -M main
git push -u origin main

# 4. Trigger automated release builds
git tag v1.0.0
git push origin v1.0.0
```
When you push a tag like `v1.0.0`, GitHub Actions will automatically compile, test, and package release ZIP archives (`PyPHS-Electric-Piano-macOS-Universal.zip`, `PyPHS-Electric-Piano-Windows-x64.zip`, `PyPHS-Electric-Piano-Linux-x64.tar.gz`) directly to your GitHub Releases page!

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
