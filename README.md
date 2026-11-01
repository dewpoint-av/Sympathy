# Sympathy

A physical-modeling **resonator / reverb** audio effect for Windows and macOS,
in the spirit of Ableton *Corpus* and AAS *Objeq Delay*. Feed it any audio — drums,
vocals, synths — and it excites a resonant physical object, then rings it back
with a tuned, material-like timbre.

Built with JUCE and delivered as **VST3** + **CLAP** (plus **AU** on macOS),
packaged by an Inno Setup installer on Windows and a `.pkg` installer on
macOS.

## What's inside

Audio excites a physical object, one per channel, alongside a pair of strings:

- **Seven resonators.** Beam, Marimba, String, Membrane and Plate are modal banks of
  up to 64 two-pole resonators, using each object's real partial series and mode
  shapes. Pipe and Tube are waveguides; a closed pipe has only odd harmonics.
- **Strings.** A pair of Karplus-Strong strings at the root and a chosen interval.
- **Stereo imaging.** The left and right objects can be detuned (Spread) and heard
  from different points (Pos L / Pos R), which makes real stereo from a mono input.
- **Refraction.** A unison effect of 1-8 voices running one Mode each: Unison,
  Bandpass, Comb, Bitcrusher, Distortion or Harmonizer. Amount spreads the Mode's main
  control across the voices. It can take the resonator's output or the raw input.
- **MIDI.** Notes set the tuning, with pitch bend and note-off damping.

An optional **exciter** injects noise so the object can ring with a silent input.

## Parameters

| Group      | Parameter | Range | Meaning |
|------------|-----------|-------|---------|
| Resonator  | Type | 7 types | The object being struck |
| Resonator  | Quality | Eco / High | 16 or 64 modes (modal types) |
| Resonator  | Tune | 20 Hz - 2 kHz | Fundamental |
| Resonator  | Decay | 50 ms - 20 s | Ring time at the fundamental |
| Resonator  | Material | -100 - +100 % | Lows ring longest .. highs ring longest |
| Resonator  | Bright | 0 - 100 % | Level of the high partials |
| Resonator  | Inharm | -100 - +100 % | Pulls partials down .. pushes them up |
| Resonator  | Hit | 0 - 100 % | Strike point, centre .. edge |
| Resonator  | Ratio | 20 - 100 % | Plate/Membrane proportions; 100 % is square/round |
| Resonator  | Radius | 0 - 100 % | Pipe/Tube bore: decay, highs, and pitch at the very top |
| Resonator  | Opening | 0 - 100 % | Pipe: closed at one end .. open at both |
| Resonator  | Level | 0 - 100 % | Object level |
| Stereo     | Width | 0 - 100 % | Mono .. each resonator on its own side |
| Stereo     | Spread | 0 - 100 % | Detunes L and R, up to +-50 cents |
| Stereo     | Pos L / Pos R | 0 - 100 % | Listening point on each object |
| Strings    | Level, Tune, Damp, Decay | | As before; Spread and MIDI also apply |
| Pitch LFO  | Depth | 0 - 12 st | Pitch modulation |
| Pitch LFO  | Rate, Sync, Division | 0.02 - 50 Hz, or tempo | Speed |
| Pitch LFO  | Shape | 7 shapes | Includes stepped and smooth noise |
| Pitch LFO  | Phase | 0 - 360 deg | Right channel's offset |
| MIDI       | Follow | on/off | Notes set the tuning |
| MIDI       | Priority | Last / Low | Which held note wins |
| MIDI       | Trans, Fine, Bend | +-48 st, +-100 ct, 0-24 st | Offsets and pitch-bend range |
| MIDI       | Off Decay | 0 - 100 % | Note-offs ignored .. mute at once |
| Refraction | Source | Resonator / Input | What it processes |
| Refraction | Mode | 6 modes | The effect each voice runs |
| Refraction | Voices, Amount | 1-8, 0-100 % | Unison size; detune, width and control spread |
| Refraction | Primary | by mode | Cutoff, Down Sample, Drive or Spray |
| Refraction | Secondary | by mode | Resonance, Bits, Algorithm or Chord |
| Refraction | Spread, Mix | 0 - 100 % | Stereo width; refracted level |
| Refraction | HP In, LP Out | 20 Hz - 20 kHz | Filters around the effect |
| Refraction | LFO | 6 shapes, Hz or tempo | To Amount and to the Primary control, bipolar |
| Global     | Exciter | 0 - 100 % | Self-noise drive |
| Global     | Input | 0 - 2x | Excitation drive |
| Global     | Input Filter, BW | on/off, 0.1 - 8 oct | Band-pass before the object |
| Global     | Bleed | 0 - 100 % | Dry added to the wet, to restore highs |
| Global     | Mix | 0 - 100 % | Dry/wet. Lowering it stops new input but lets tails ring |
| Global     | Output, Auto Gain | 0 - 2x, on/off | Master gain; level matching |

Sessions from 1.x load with the closest settings, and stereo stays neutral. They won't
be identical, since the old materials were one shared formula rather than real objects.

The animated interface shows the object's partials as a radial corona around its icon,
with audio-reactive particles and rings.

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

The installer `installer\output\Sympathy-Setup-1.2.3.exe` installs:

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

This produces `installer/output/Sympathy-1.2.3-mac.pkg`, which installs the
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
ready-to-use `Sympathy-1.2.3-mac.pkg`.

## Manual install (Windows)

If you don't want the installer:

- **VST3** — copy `build\dist\Sympathy.vst3` to `C:\Program Files\Common Files\VST3\`
- **CLAP** — copy `build\dist\Sympathy.clap` to `%LOCALAPPDATA%\Programs\Common\CLAP\`

Then rescan plugins in your DAW.

## Architecture

```
Source/
  PluginProcessor.h/.cpp     parameters, 1.x session migration, MIDI, audio glue
  PluginEditor.h/.cpp        GUI
  CustomLookAndFeel.h        theme
  dsp/
    ResonatorEngine.h/.cpp   top level: objects, strings, stereo, mix
    PhysicalModels.h/.cpp    ModalBank + Waveguide: the seven objects
    RefractEngine.h/.cpp     Refraction
    Lfo.h                    shared LFO
```

The DSP is independent of JUCE.

## License

MIT — see the project's license terms. JUCE and clap-juce-extensions retain their
own licenses (AGPL/commercial for JUCE, as applicable).
