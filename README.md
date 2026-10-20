# Sympathy

A physical-modeling **resonator / reverb** audio effect for Windows and macOS,
in the spirit of Ableton *Corpus* and AAS *Objeq Delay*. Feed it any audio — drums,
vocals, synths — and it excites a resonant physical object, then rings it back
with a tuned, material-like timbre.

Built with JUCE and delivered as **VST3** + **CLAP** (plus **AU** on macOS),
packaged by an Inno Setup installer on Windows and a `.pkg` installer on
macOS.

## What's inside

Sympathy blends two physical models in parallel:

- **Resonant body** — a bank of up to 64 two-pole *modal resonators*. Their
  frequencies follow a material-dependent inharmonic distribution (a harmonic
  string, a stiff metal bar, a dense plate, …). Each mode has its own decay, so
  low partials ring longer than high ones, exactly like a struck object.
- **Strings** — a pair of *Karplus-Strong* waveguide strings (fractional delay +
  damping + feedback), tuned to the root pitch and a musical interval above it.

An optional **exciter** injects filtered noise into the object so it can ring on
its own — bowed pads, sympathetic drones, ambient beds — even with a silent
input.

## Parameters

| Group   | Parameter      | Range            | Meaning |
|---------|----------------|------------------|---------|
| Body    | Material       | String/Wood/Metal/Glass/Plate/Body/Synth | Mode distribution & damping profile |
| Body    | Tune           | 20 Hz – 2 kHz    | Root pitch of the object |
| Body    | Inharmonicity  | 0 – 100 %        | Harmonic → stiff/bar-like spacing |
| Body    | Decay          | 50 ms – 20 s     | Ring time |
| Body    | Brightness     | 0 – 100 %        | High-partial energy |
| Body    | Level          | 0 – 100 %        | Body wet amount |
| Strings | Level          | 0 – 100 %        | String wet amount |
| Strings | Tune           | −12 – +24 st     | Interval of the second string |
| Strings | Damping        | 0 – 100 %        | String brightness |
| Strings | Decay          | 50 ms – 20 s     | String ring time |
| Global  | Exciter        | 0 – 100 %        | Self-noise drive |
| Global  | Input          | 0 – 2×           | Excitation drive |
| Global  | Mix            | 0 – 100 %        | Dry / wet |
| Global  | Output         | 0 – 2×           | Master gain |

The interface is holographic and animated. A central material icon sits inside a
radial **modal resonance corona** (one glowing spike per body mode), surrounded
by audio-reactive particles, expanding energy rings and two vibrating
Karplus-Strong strings. A row of vector **mode icons** (String, Wood, Metal,
Glass, Plate, Body, Synth) selects the material, and every control reshapes the
visual — decay lengthens the particle trails, brightness intensifies the glow,
inharmonicity scatters the spikes, the body/string levels feed their elements,
and the exciter drives emission even with a silent input.

## Requirements

- **Windows 10/11, x64**
- **macOS 10.15+** (Apple Silicon or Intel; universal 2 binary)
- Any **VST3** host (Ableton Live, FL Studio, Cubase, Studio One, …), **AU** host
  (Logic, GarageBand, Ableton Live), and/or **CLAP** host (Bitwig, Reaper, FL Studio, …).

## Building from source

Everything is vendored in-tree (JUCE 8.0.8 and clap-juce-extensions + CLAP SDK),
so no network access is required at build time. You need:

- **CMake** 3.21+
- **Visual Studio 2022** (Build Tools is fine) with the C++ workload

```
> build.bat
```

This configures with CMake and builds Release, producing:

```
build\dist\Sympathy.clap          <- CLAP
build\dist\Sympathy.vst3\         <- VST3 bundle
```

## Creating the installer

With [Inno Setup 6](https://jrsoftware.org/isinfo.php) installed:

```
> installer\build_installer.bat
```

The installer `installer\output\Sympathy-Setup-1.1.0.exe` installs:

- `Sympathy.vst3` → `C:\Program Files\Common Files\VST3\`
- `Sympathy.clap`  → `C:\Program Files\Common Files\CLAP\` and the per-user CLAP folder

and registers a standard uninstaller.

## Building on macOS

Build the universal 2 (Apple Silicon + Intel) VST3 + AU + CLAP bundles:

```
$ ./build_mac.sh
```

This needs macOS with Xcode (or the Command Line Tools) and CMake, and stages the
bundles into `dist/`:

```
dist/Sympathy.vst3          <- VST3 bundle
dist/Sympathy.component     <- Audio Unit
dist/Sympathy.clap          <- CLAP bundle
```

### Creating the installer package (.pkg)

```
$ ./make_pkg.sh
```

This produces `installer/output/Sympathy-1.1.0-mac.pkg`, which installs the
three bundles to `/Library/Audio/Plug-Ins/{VST3, Components, CLAP}` via the
macOS Installer (with an admin prompt).

> **Code signing:** for building/testing on your own Mac the bundles load as-is.
> To run the AU in Logic you may need an ad-hoc signature first:
> `codesign --force --deep -s - dist/*.component dist/*.vst3 dist/*.clap`.
> For distributing to other Macs, sign with your Developer ID and notarize, or
> Gatekeeper will block the downloads.

## Continuous integration (GitHub Actions)

The workflow `.github/workflows/build-macos.yml` builds the macOS **universal2**
VST3 + AU + CLAP bundles on every push to `main`, every `v*` tag, and on demand.
Each run uploads a `Sympathy-macOS` artifact containing the three bundles and a
ready-to-use `Sympathy-1.1.0-mac.pkg`.

## Manual install (Windows)

If you don't want the installer:

- **VST3** — copy `build\dist\Sympathy.vst3` to `C:\Program Files\Common Files\VST3\`
- **CLAP** — copy `build\dist\Sympathy.clap` to `%LOCALAPPDATA%\Programs\Common\CLAP\`

Then rescan plugins in your DAW.

## Architecture

```
Source/
  PluginProcessor.h/.cpp     JUCE processor, parameter tree, audio glue
  PluginEditor.h/.cpp        GUI (panels, knobs, material selector, spectrum)
  CustomLookAndFeel.h        dark "acoustic" theme
  dsp/
    ResonatorEngine.h/.cpp   the DSP engine (JUCE-free, self-contained)
```

The engine is deliberately independent of JUCE: `ModalBank` (parallel two-pole
resonators), `StringBank` (Karplus-Strong), and a `NoiseSource` exciter, summed
per channel for natural stereo.

## License

MIT — see the project's license terms. JUCE and clap-juce-extensions retain their
own licenses (AGPL/commercial for JUCE, as applicable).
