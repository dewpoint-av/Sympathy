#pragma once

/*
  Sympathy — resonator engine (JUCE-free).

  Incoming audio excites two objects, one per channel, and a pair of Karplus-Strong
  strings:

    in -> (optional band-pass) -> object L/R + strings L/R -> stereo width -> mix

  The left and right objects can differ in tuning (Spread) and in where they are heard
  (Pos L / Pos R), which is what produces a real stereo image from a mono input.

  Refraction (see RefractEngine) takes either the resonator's output or the raw input.
  Lowering Mix reduces what enters the resonator rather than muting its output, so
  tails already ringing keep ringing.
*/

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <vector>

#include "Lfo.h"
#include "PhysicalModels.h"
#include "RefractEngine.h"

namespace sympathy
{
    static constexpr int kNumStrings  = 2;
    static constexpr int kNumChannels = 2;
    static constexpr int kVizSize     = 4096;

    enum RefractSource : int { kSourceResonator = 0, kSourceInput = 1 };

    // -----------------------------------------------------------------------
    // Everything the audio thread needs, mirroring the plugin parameters.
    // -----------------------------------------------------------------------
    struct Params
    {
        ModelParams model;       // pos here is ignored; posL / posR apply per channel
        float posL = 0.3f, posR = 0.3f;
        float spread = 0.0f;     // 0..1  detunes L and R in opposite directions
        float width = 1.0f;      // 0 mono .. 1 each resonator on its own side

        float body        = 0.8f;
        float strings     = 0.5f;
        float stringTune  = 7.0f;   // semitones (+interval of 2nd string)
        float stringDamp  = 0.35f;
        float stringDecay = 3.0f;

        float exciter  = 0.0f;
        float input    = 1.0f;
        float bleed    = 0.0f;
        float mix      = 0.5f;
        float output   = 1.0f;
        bool  autoGain = true;

        bool  filterOn = false;
        float filterHz = 1000.0f;
        float filterBw = 2.0f;      // octaves

        int   refractSource = kSourceResonator;

        LfoSettings lfo;
        float lfoSemis = 0.0f;      // pitch depth, 0..12 semitones
        float lfoPhase = 0.0f;      // right-channel offset, 0..1 cycles

        bool  midiFrequency = false;
        float midiTranspose = 0.0f; // semitones
        float midiFine = 0.0f;      // cents
        float pbRange = 2.0f;       // semitones
        float offDecay = 0.0f;      // 0 ignore note-offs .. 1 mute at once

        bool operator== (const Params& o) const
        {
            return model == o.model && posL == o.posL && posR == o.posR && spread == o.spread && width == o.width
                && body == o.body && strings == o.strings && stringTune == o.stringTune
                && stringDamp == o.stringDamp && stringDecay == o.stringDecay
                && exciter == o.exciter && input == o.input && bleed == o.bleed && mix == o.mix
                && output == o.output && autoGain == o.autoGain
                && filterOn == o.filterOn && filterHz == o.filterHz && filterBw == o.filterBw
                && refractSource == o.refractSource && lfo == o.lfo && lfoSemis == o.lfoSemis
                && lfoPhase == o.lfoPhase && midiFrequency == o.midiFrequency
                && midiTranspose == o.midiTranspose && midiFine == o.midiFine
                && pbRange == o.pbRange && offDecay == o.offDecay;
        }
    };

    // What MIDI wants right now; the processor tracks held notes.
    struct MidiState
    {
        bool  hasPlayed = false;   // a note has arrived, so pitch can follow it
        bool  anyHeld = false;
        int   note = 60;
        float bend = 0.0f;         // -1..1
    };

    // -----------------------------------------------------------------------
    // A pair of Karplus-Strong strings.
    // -----------------------------------------------------------------------
    class StringBank
    {
    public:
        void prepare (double sampleRate, int maxBlockSize);
        void reset();
        void update (double sampleRate, const Params& p);
        void setPitch (float ratio);
        void damp (float secondsTimeConstant);   // <= 0 means no extra damping
        float process (float x);

    private:
        struct String
        {
            FractionalDelay delay;
            float feedback = 0.0f;
            float feedbackDamped = 0.0f;
            float damp = 0.0f;      // one-pole lowpass coefficient
            float lp = 0.0f;        // lowpass state
            float baseDelay = 100.0f;
            float delaySamples = 100.0f;
            float gain = 0.0f;
        };

        void updateDelays();

        std::array<String, kNumStrings> strings;
        double fs = 48000.0;
        float pitch = 1.0f;
        float dampTau = 0.0f;
    };

    // -----------------------------------------------------------------------
    // A fast xorshift noise source (the self-exciter).
    // -----------------------------------------------------------------------
    class NoiseSource
    {
    public:
        NoiseSource (uint32_t seed) : state (seed) {}

        float next()
        {
            state ^= state << 13;
            state ^= state >> 17;
            state ^= state << 5;
            return static_cast<float> (static_cast<int32_t> (state)) * (1.0f / 2147483648.0f);
        }

    private:
        uint32_t state;
    };

    // -----------------------------------------------------------------------
    // A compact lookahead peak limiter (2 ms) with a hard-clip safety net.
    // -----------------------------------------------------------------------
    class StereoLimiter
    {
    public:
        void prepare (double sr)
        {
            lookahead = static_cast<int> (sr * 0.002);
            if (lookahead < 1) lookahead = 1;
            delayL.assign (static_cast<size_t> (lookahead + 1), 0.0f);
            delayR.assign (static_cast<size_t> (lookahead + 1), 0.0f);
            threshold = 0.89f;
            releaseCoeff = static_cast<float> (1.0 - std::exp (-1.0 / (0.05 * sr)));
            reset();
        }

        void reset()
        {
            std::fill (delayL.begin(), delayL.end(), 0.0f);
            std::fill (delayR.begin(), delayR.end(), 0.0f);
            writePos = 0;
            gain = 1.0f;
            peak = 0.0f;
        }

        void process (float& l, float& r)
        {
            delayL[static_cast<size_t> (writePos)] = l;
            delayR[static_cast<size_t> (writePos)] = r;

            const float a = std::max (std::fabs (l), std::fabs (r));
            if (a > peak)      peak = a;
            else               peak += releaseCoeff * (a - peak);

            const float target = (peak > threshold) ? threshold / peak : 1.0f;
            if (target < gain) gain = target;
            else               gain += releaseCoeff * (target - gain);

            const int rd = (writePos + 1) % (lookahead + 1);
            l = std::clamp (delayL[static_cast<size_t> (rd)] * gain, -1.0f, 1.0f);
            r = std::clamp (delayR[static_cast<size_t> (rd)] * gain, -1.0f, 1.0f);
            writePos = rd;
        }

    private:
        int lookahead = 96;
        std::vector<float> delayL;
        std::vector<float> delayR;
        int writePos = 0;
        float gain = 1.0f;
        float peak = 0.0f;
        float threshold = 0.89f;
        float releaseCoeff = 0.0f;
    };

    // -----------------------------------------------------------------------
    // Optional auto makeup gain: matches the processed loudness to the dry
    // input so resonant boosts/cuts don't change the overall level.
    // -----------------------------------------------------------------------
    class GainCompensator
    {
    public:
        void prepare (double fs) { coeff = static_cast<float> (1.0 - std::exp (-1.0 / (0.15 * fs))); reset(); }
        void reset() { inLvl = outLvl = 0.0f; gain = 1.0f; }

        float process (float dryMag, float wetMag)
        {
            inLvl  += coeff * (dryMag - inLvl);
            outLvl += coeff * (wetMag - outLvl);

            float target = 1.0f;
            if (outLvl > 1.0e-4f && inLvl > 1.0e-4f)
                target = inLvl / outLvl;

            target = std::clamp (target, 0.25f, 4.0f);   // ±12 dB
            gain += coeff * (target - gain);
            return gain;
        }

    private:
        float coeff = 0.0f;
        float inLvl = 0.0f;
        float outLvl = 0.0f;
        float gain = 1.0f;
    };

    // -----------------------------------------------------------------------
    // Top-level engine.
    // -----------------------------------------------------------------------
    class ResonatorEngine
    {
    public:
        void prepare (double sampleRate, int maxBlockSize);
        void reset();
        void setParams (const Params& p);
        void setRefractParams (const RefractParams& p);
        void setHostTime (const HostTime& t);
        void setMidi (const MidiState& m) { midi = m; }
        void process (const float* inL, const float* inR,
                      float* outL, float* outR, int numSamples);

        // For the GUI: the object's current partials and a wet level.
        int   getModeCount() const { return modeCount.load(); }
        float getModeFrequency (int i) const { return modeFreqs[static_cast<size_t> (i)].load(); }
        float getLevel() const { return level.load(); }

        // Recent mono input for the header spheres (spectrogram / spectrum).
        const std::vector<float>& vizInput() const { return vizInputBuf; }
        int  vizWritePos() const                    { return vizWrite.load(); }

    private:
        void rebuildObject();
        void rebuildFilter();
        void updateControl();

        double sampleRate = 48000.0;
        Params p;
        ModelParams builtModel;
        float builtPosL = -1.0f, builtPosR = -1.0f;
        bool objectBuilt = false;

        ModalBank  modal[kNumChannels];
        Waveguide  tube[kNumChannels];
        StringBank strings[kNumChannels];
        NoiseSource noise[kNumChannels] { { 0x9E3779B9u }, { 0x85EBCA6Bu } };
        RefractEngine refract;
        StereoLimiter limiter;
        GainCompensator agc;
        Lfo lfo { 0x7FB5D329u };
        HostTime hostTime;
        MidiState midi;

        // Input band-pass.
        float bpB0 = 0.0f, bpB2 = 0.0f, bpA1 = 0.0f, bpA2 = 0.0f;
        float bpZ1[2] = { 0.0f, 0.0f }, bpZ2[2] = { 0.0f, 0.0f };

        int   controlCounter = 0;
        float mixNow = 0.5f;
        float mixCoef = 0.001f;
        float modalDamp = 1.0f;     // per control tick while note-off damping is active

        std::atomic<float> level { 0.0f };
        float meter = 0.0f;   // audio-thread-only smoothing state

        std::atomic<int> modeCount { 0 };
        std::array<std::atomic<float>, kMaxModes> modeFreqs {};

        std::vector<float> vizInputBuf;
        std::atomic<int> vizWrite { 0 };
    };

} // namespace sympathy
