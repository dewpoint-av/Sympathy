#pragma once

/*
  Sympathy — physical-modeling resonator engine.

  The engine is deliberately JUCE-free so it is self-contained and easy to
  reason about. It models two coupled physical objects driven by the incoming
  audio signal:

    1. A "body": a bank of parallel two-pole modal resonators (the same family
       of models used by Ableton Corpus and modal reverbs). Each mode has a
       frequency, a per-mode amplitude (spectral rolloff) and a pole radius
       (decay). The mode frequencies follow a material-dependent inharmonic
       distribution (harmonic string -> stiff bar -> dense plate).

    2. A pair of "strings": Karplus-Strong waveguide strings (fractional delay
       + one-pole damping + feedback) tuned to the root pitch and an interval
       above it — the same family of models as AAS Objeq Delay.

  An optional noise "exciter" lets the object ring on its own (bowed/pad-like)
  even when the input is silent.
*/

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <vector>

#include "RefractEngine.h"

namespace sympathy
{
    static constexpr int kMaxModes      = 64;
    static constexpr int kNumStrings    = 2;
    static constexpr int kNumChannels   = 2;
    static constexpr int kVizSize       = 4096;

    // -----------------------------------------------------------------------
    // Material presets for the modal body.
    // -----------------------------------------------------------------------
    struct Material
    {
        const char* name;
        float fundamentalMult;   // multiplies the root pitch
        float inharmBase;        // exponent added to 1.0 for mode spacing
        float dampTilt;          // how strongly high modes lose energy (0 = flat)
        float density;           // relative number of modes
        float rolloff;           // amplitude rolloff exponent (1 / i^rolloff)
    };

    static constexpr Material kMaterials[] = {
        { "String", 1.0f, 0.00f, 0.40f, 0.8f, 0.7f },
        { "Wood",   1.0f, 0.25f, 1.00f, 1.0f, 0.9f },
        { "Metal",  1.4f, 0.70f, 0.20f, 1.0f, 0.5f },
        { "Glass",  1.6f, 0.90f, 0.10f, 0.9f, 0.4f },
        { "Plate",  0.9f, 0.50f, 0.60f, 1.3f, 0.8f },
        { "Body",   1.1f, 0.30f, 1.40f, 1.5f, 1.0f },
        { "Synth",  1.0f, 0.00f, 0.00f, 1.0f, 0.5f }
    };
    static constexpr int kNumMaterials = 7;

    // -----------------------------------------------------------------------
    // Everything the audio thread needs, mirroring the plugin parameters.
    // -----------------------------------------------------------------------
    struct Params
    {
        int   material       = 0;
        float tune           = 220.0f;   // Hz
        float inharmonicity  = 0.35f;    // 0..1
        float decay          = 2.5f;     // seconds
        float brightness     = 0.65f;    // 0..1
        float body           = 0.8f;     // 0..1
        float strings        = 0.5f;     // 0..1
        float stringTune     = 7.0f;     // semitones (+interval of 2nd string)
        float stringDamp     = 0.35f;    // 0..1
        float stringDecay    = 3.0f;     // seconds
        float exciter        = 0.0f;     // 0..1 (self-noise drive)
        float input          = 1.0f;     // 0..2 (excitation drive)
        float mix            = 0.5f;     // 0..1 dry/wet
        float output         = 1.0f;     // 0..2
        bool  autoGain       = true;     // compensate wet loudness to match dry

        bool operator== (const Params& o) const
        {
            return material      == o.material
                && tune          == o.tune
                && inharmonicity == o.inharmonicity
                && decay         == o.decay
                && brightness    == o.brightness
                && body          == o.body
                && strings       == o.strings
                && stringTune    == o.stringTune
                && stringDamp    == o.stringDamp
                && stringDecay   == o.stringDecay
                && exciter       == o.exciter
                && input         == o.input
                && mix           == o.mix
                && output        == o.output
                && autoGain      == o.autoGain;
        }
    };

    // -----------------------------------------------------------------------
    // A circular fractional delay using first-order allpass interpolation.
    // The allpass is flat in magnitude, so unlike linear interpolation it does
    // not dull the strings (the intentional `damp` lowpass is the only HF loss),
    // while its DC group delay matches `frac`, keeping the fundamental in tune.
    // -----------------------------------------------------------------------
    class FractionalDelay
    {
    public:
        void prepare (int maxSamples)
        {
            buffer.assign (static_cast<size_t> (maxSamples), 0.0f);
            maxLength = maxSamples;
            writePos  = 0;
            z         = 0.0f;
        }

        void reset()
        {
            std::fill (buffer.begin(), buffer.end(), 0.0f);
            writePos = 0;
            z        = 0.0f;
        }

        float process (float x, float delaySamples)
        {
            buffer[static_cast<size_t> (writePos)] = x;
            writePos = (writePos + 1) % maxLength;

            float readPos = static_cast<float> (writePos) - delaySamples;
            if (readPos < 0.0f)
                readPos += static_cast<float> (maxLength);

            const int i0     = static_cast<int> (readPos);
            const float frac = readPos - static_cast<float> (i0);
            const int i1     = (i0 + 1) % maxLength;

            // First-order allpass H(z) = (a + z^-1) / (1 + a z^-1) with
            // a = (1-frac)/(1+frac): group delay `frac` samples at DC, |a|<=1.
            const float a  = (1.0f - frac) / (1.0f + frac);
            const float x0 = buffer[static_cast<size_t> (i0)];   // integer tap
            const float x1 = buffer[static_cast<size_t> (i1)];   // one sample older
            const float y  = a * x0 + x1 - a * z;
            z = y;
            return y;
        }

    private:
        std::vector<float> buffer;
        int maxLength = 1;
        int writePos  = 0;
        float z = 0.0f;
    };

    // -----------------------------------------------------------------------
    // The modal body: a bank of parallel two-pole resonators.
    // -----------------------------------------------------------------------
    class ModalBank
    {
    public:
        void prepare (double sampleRate);
        void reset();
        void update (double sampleRate, const Params& p);
        float process (float x);

        int getModeCount() const              { return activeModes.load(); }
        float getModeFrequency (int i) const  { return modeFreqs[static_cast<size_t> (i)]; }

    private:
        struct Mode
        {
            float b  = 0.0f;   // input gain  (1 - r)  -> unity peak
            float c1 = 0.0f;   // 2 r cos(theta)
            float c2 = 0.0f;   // -r^2
            float y1 = 0.0f;
            float y2 = 0.0f;
            float amp = 0.0f;
            float freq = 0.0f;
        };

        std::array<Mode, kMaxModes> modes;
        std::array<float, kMaxModes> modeFreqs {};
        std::atomic<int> activeModes { 0 };
        float normalize = 1.0f;
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
        float process (float x);

    private:
        struct String
        {
            FractionalDelay delay;
            float feedback = 0.0f;
            float damp = 0.0f;      // one-pole lowpass coefficient
            float lp = 0.0f;        // lowpass state
            float delaySamples = 0.0f;
            float gain = 0.0f;
        };

        std::array<String, kNumStrings> strings;
        float maxDelaySamples = 4800.0f;
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
        void process (const float* inL, const float* inR,
                      float* outL, float* outR, int numSamples);

        // For the GUI: the body's current modal frequencies and a wet level.
        int  getModeCount() const { return body[0].getModeCount(); }
        float getModeFrequency (int i) const { return body[0].getModeFrequency (i); }
        float getLevel() const { return level.load(); }

        // Recent mono input for the header spheres (spectrogram / spectrum).
        const std::vector<float>& vizInput() const { return vizInputBuf; }
        int  vizWritePos() const                    { return vizWrite.load(); }

    private:
        void updateAll (double sampleRate);

        double sampleRate = 48000.0;
        Params p;

        ModalBank  body[kNumChannels];
        StringBank strings[kNumChannels];
        NoiseSource noise[kNumChannels] { { 0x9E3779B9u }, { 0x85EBCA6Bu } };
        RefractEngine refract;
        StereoLimiter limiter;
        GainCompensator agc;

        std::atomic<float> level { 0.0f };
        float meter = 0.0f;   // audio-thread-only smoothing state

        std::vector<float> vizInputBuf;
        std::atomic<int> vizWrite { 0 };
    };

} // namespace sympathy
