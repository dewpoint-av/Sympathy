# Sympathy 2.0 — Overhaul Plan

**Status: stages 0-5 and the UI are built (v2.0.0). Factory presets are not.**
Decisions made: one Mode only, Resonator/Input switch, keep strings, migrate old sessions.
Left out so far: Corpus's LFO Spin and tempo Offset. Section 7's second-Mode idea was declined.

Goal: bring Sympathy's resonator up to the feature set of Ableton **Corpus**, and rebuild
Refraction so it sounds as good as Arturia **Efx REFRACT**. This is a plan only; nothing
here is built yet. Current version: 1.3.0.

Sources: [Ableton Live audio effect reference](https://www.ableton.com/en/live-manual/12/live-audio-effect-reference/),
[Efx REFRACT manual](https://dl.arturia.net/products/efx-refract/manual/efx-refract_Manual_1_0_0_EN.pdf),
[DATABROTH review](https://www.databroth.com/blog/free-efx-refract-review).
"Reference" below means these documents. Neither vendor's internal DSP is published, so
models here come from standard acoustics, and matching their sound is judged by ear.

## 1. Why Refraction sounds weak today

Found by reading `dsp/RefractEngine.cpp`; items 1 and 2 are the biggest.

| # | Current behaviour | What Efx REFRACT does | Effect on the sound |
|---|---|---|---|
| 1 | **Amount has no centre to spread around.** Bandpass is fixed at 1 kHz; each mode has one baked-in curve | Amount disperses each mode's *primary control* across voices, centred on a value you set. The manual calls this its most unique feature | This *is* the refraction sound. Without it, voices barely differ |
| 2 | **Harmonizer clicks.** The crossfade is `fade = ph / window`, a sawtooth, while the read heads wrap (lines 186-194). Weight jumps at each wrap | Clean pitched voices, plus chord selection | Rough, glitchy pitch shifting |
| 3 | Harmonizer ratio is `2^(amount * interval / 12)`, so an interval is only in tune at Amount 100%. Intervals are hardcoded | 12 chords and intervals, always in tune; Amount controls Spray instead | Out-of-tune smears at most settings |
| 4 | No Spray: no time dispersion or feedback | Spray: up to a 1500 ms tail, wider image, shimmer past the midpoint | Missing the signature lush mode |
| 5 | Comb is feedforward, with no resonance control | Feedback comb with Cutoff + Resonance, up to self-oscillation | Thin notches rather than tuned, metallic tones |
| 6 | Bandpass Q is tied to Amount; no Cutoff control | Separate Cutoff and Resonance | Can't be played or swept |
| 7 | Distortion is a single `tanh`, unoversampled | Tape, Germanium, Soft Bounce, Sine Folder | One flat colour; folding would alias badly without oversampling |
| 8 | Bitcrush: every voice has the same rate and depth | Downsample dispersed per voice | Sounds like one crusher instead of a stereo cloud |
| 9 | LFO is sine only, free-running, targets Amount, and adds 25% tremolo to the level | 6 shapes, Hz or tempo sync (straight/triplet/dotted), separate bipolar depth to Refraction and to the mode control | Little movement, plus an unwanted level wobble |
| 10 | 40-50 ms base delay with linear interpolation | Reviewer hears unison detune, not chorus | Audible doubling and dulled highs |
| 11 | No input high-pass or output low-pass | HP In (default 150 Hz), LP Out | Muddy low end when voices stack |
| 12 | Input is only the resonator's wet signal | Input is the audio itself | Refraction can't be used without the resonator colouring it |

## 2. Corpus features: what's missing

| Corpus (per the reference) | Sympathy today | Plan |
|---|---|---|
| 7 resonance types: Beam, Marimba, String, Membrane, Plate, Pipe, Tube | 7 materials, all one formula (`f * i^exponent`) | Real models (section 3). Keep today's materials as presets |
| Quality (Eco / High) | Mode count fixed per material | Add, with Eco as the low-CPU option |
| Decay, Bright, Tune | Have them | Keep |
| Material: lows vs highs ring longer | Fixed per material; Brightness moves it one way | Add as a bipolar control |
| Inharm: lower or upper partials | Upward only | Make bipolar |
| Radius (Pipe/Tube), Opening (Pipe), Ratio (Membrane/Plate) | None | Add per model; hide when not relevant |
| Hit (strike position) | None | Add |
| Pos. L / Pos. R (listening point per channel) | None | Add |
| Spread: detune the two resonators; Width | **None.** I measured a mono input giving identical L and R (−234 dB difference) | Add. Currently the resonator has no stereo at all |
| LFO on frequency: 7 shapes, Hz/sync, Phase or Spin | None | Add; shared LFO code with Refraction |
| Band-pass filter on the input | None | Add |
| Bleed: mixes dry back to restore highs | None | Add |
| Gain with limiter | Have the limiter | Keep |
| Dry/Wet doesn't cut ringing tails | Mix scales the wet output, so it cuts them | Change to match |
| MIDI sets the tuning (Last/Low priority), Transpose, Fine, PB Range | **No MIDI input** (`NEEDS_MIDI_INPUT FALSE`) | Add. This makes it playable, and is the biggest workflow gain |
| Off Decay: Note Off damps the ring | None | Add |

**Keeping beyond Corpus:** the Karplus-Strong strings, noise exciter, auto gain and the
visualisers.

## 3. Resonator models

| Type | Model | Partials |
|---|---|---|
| Beam | Modal | Free-free bar: 1, 2.756, 5.404, 8.933 ... |
| Marimba | Modal | Bar with overtones tuned near 1 : 4 : 10 |
| String | Modal | Harmonic, with stiffness via Inharm |
| Membrane | Modal | Circular membrane Bessel zeros: 1, 1.594, 2.136, 2.296 ... |
| Plate | Modal | `m² + r·n²` from Ratio |
| Pipe | Waveguide | Opening crossfades closed (odd harmonics) to open (all) |
| Tube | Waveguide | Open tube; Radius sets damping and shifts pitch at extremes |

Hit and Pos weight each mode by its mode shape at that point, so moving them changes the
tone the way moving a striker does. Each channel gets its own bank, for Spread and Pos.

**CPU:** today's `update()` recomputes every coefficient on any change. With MIDI and LFO
pitch that becomes constant, so frequency needs a cheap control-rate path (every ~32 samples).

## 4. New Refraction

Signal flow: `In level → HP In → Refractor (1-8 voices) → one Mode per voice → sum →
LP Out → Out level`.

- **Spread rule:** for voice `i` of `n`, with `s` from −1 to +1, the mode's primary control
  is `centre + Amount × s × range`. Used by every mode.
- **Refractor:** short delays with better interpolation, per-voice detune, and Amount
  driving both detune and stereo width. Remove the tremolo.
- **Bandpass / Comb:** Cutoff + Resonance; comb gets feedback to self-oscillation.
- **Bitcrusher:** Down Sample + Bit Depth, with downsample dispersed per voice.
- **Distortion:** Drive + 4 algorithms, oversampled, with In level pushing it harder.
- **Harmonizer:** rewritten pitch shifter with proper overlapping windows; 12 chords and
  intervals assigned to voices (a chord needs enough voices, as in Refract); Spray with a
  feedback path for the tail and shimmer.
- **LFO:** Sine, Triangle, Saw, Square, Sample & Hold, Sample & Glide; 0.025-50 Hz or synced;
  bipolar depth to Refraction and to the mode control.

## 5. Build order

Each stage ships on its own. "Done" checks use the same style of standalone test as the
recent Impact and Subside fixes, then your ears in a DAW.

| Stage | Work | Done when |
|---|---|---|
| 0 | Test harness; versioned state + old-session migration; `NEEDS_MIDI_INPUT` | A 1.3.0 session loads and sounds the same |
| 1 | New Refraction engine (section 4) | No clicks on sustained tones with the harmonizer (measured); chords in tune to within a few cents; comb self-oscillates at the set pitch; Amount audibly spreads every mode |
| 2 | Resonator models + Quality | Measured partials match section 3; Eco is at least 2x cheaper than High |
| 3 | Stereo: Spread, Width, Hit, Pos L/R | L and R differ when Spread/Pos are set, and match at 0 |
| 4 | MIDI tuning, Last/Low, PB, Off Decay | Resonator pitch follows notes within a few cents; Off Decay damps as set |
| 5 | LFOs, input filter, Bleed, Dry/Wet tail behaviour | Turning Dry/Wet down leaves the tail ringing |
| 6 | UI + factory presets (original, not copied) | Full reload test, on Windows and Mac |

Refraction comes first because it's the most audible gain and is independent of the rest.

## 6. Risks

- **CPU:** 8 voices × oversampled distortion × 2 channels, on top of two 64-mode banks.
  Measure at stage 2 and cap voices or quality if needed.
- **Old sessions:** parameters change meaning. Stage 0 migrates them; `refract*` toggles
  need a defined mapping if stacking goes away (decision 1).
- **Matching by ear:** the vendors' DSP isn't public, so "as cool as Refract" is your call
  at stage 1, not something a test can confirm.
- **Trademarks:** "Refraction" is already in use here and is a common word. Avoid using
  Arturia's or Ableton's names, UI or presets in product copy.

## 7. Decisions needed

1. **One Mode or a stack?** Refract allows one at a time; Sympathy stacks six toggles today.
   *Recommendation: one Mode plus an optional second in series.* It keeps Refract's clarity
   and most of what stacking gives you.
2. **What feeds Refraction?** *Recommendation: a switch for Resonator (today's behaviour,
   the default) or Input,* so it can also work as a Refract-style effect on its own.
3. **Keep the strings?** Corpus has no equivalent. *Recommendation: keep them,* as Sympathy's
   own character.
4. **Old sessions:** *Recommendation: migrate them to sound as close as possible,* rather
   than shipping 2.0 as a separate plugin.
