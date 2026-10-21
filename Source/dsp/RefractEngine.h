#pragma once

/*
  Sympathy — "Refraction" super-unison multi-FX engine.

  The incoming signal is duplicated into up to 8 stereo voices. Every stage is
  independently toggleable and runs in a fixed chain order:

      Unison (chorus detune) -> Harmonizer (pitch shift) -> Bandpass
        -> Distortion -> Bitcrusher -> Comb

  so multiple textures can be stacked on the same voice stack. Gain is staged
  (normalised by the number of active stages) and the main output limiter
  catches any residual peaks, keeping stacked modes from clipping too hard.
*/

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstddef>
#include <vector>

namespace sympathy
{
    static constexpr int kMaxRefractVoices = 8;

    struct RefractParams
    {
        bool  on       = false;
        int   voices   = 4;        // 2..8
        float amount   = 0.40f;    // 0..1 (detune depth + FX dispersion)
        float mix      = 0.35f;    // 0..1 (refracted level)
        float spread   = 0.70f;    // 0..1 (stereo width)
        float lfoRate  = 0.25f;    // Hz (modulates Amount)
        float lfoDepth = 0.30f;    // 0..1

        bool unison     = true;
        bool harmonizer = false;
        bool bandpass   = false;
        bool distort    = false;
        bool bitcrush   = false;
        bool comb       = false;

        bool operator== (const RefractParams& o) const
        {
            return on         == o.on
                && voices     == o.voices
                && amount     == o.amount
                && mix        == o.mix
                && spread     == o.spread
                && lfoRate    == o.lfoRate
                && lfoDepth   == o.lfoDepth
                && unison     == o.unison
                && harmonizer == o.harmonizer
                && bandpass   == o.bandpass
                && distort    == o.distort
                && bitcrush   == o.bitcrush
                && comb       == o.comb;
        }
    };

    class RefractEngine
    {
    public:
        void prepare (double sampleRate, int maxBlockSize);
        void reset();
        void setParams (const RefractParams& p);
        void processSample (float inL, float inR, float& outL, float& outR);

        float getLevel() const { return level.load(); }

    private:
        struct Voice
        {
            std::array<std::vector<float>, 2> buf;      // raw input history
            std::array<std::vector<float>, 2> harmBuf;  // harmonizer pitch-shift buffer
            int writePos     = 0;
            int harmWritePos = 0;

            float lfoPhase    = 0.0f;
            float lfoRateMul  = 1.0f;
            float detuneDepth = 0.0f;   // samples (at amount = 1)
            float detuneBase  = 0.0f;   // samples

            float panL = 1.0f;
            float panR = 1.0f;

            // bandpass (direct form II)
            float b0 = 0.0f, b1 = 0.0f, b2 = 0.0f, a1 = 0.0f, a2 = 0.0f;
            float z1[2] = { 0.0f, 0.0f };
            float z2[2] = { 0.0f, 0.0f };

            // comb (feedforward)
            float combG     = 0.0f;
            float combDelay = 0.0f;

            // bitcrusher
            float hold[2]      = { 0.0f, 0.0f };
            int   holdCount[2] = { 0, 0 };
            int   holdLen      = 1;
            float crushSteps   = 0.0f;

            // distortion
            float drive = 1.0f;

            // harmonizer
            float interval   = 0.0f;
            float ratio      = 1.0f;
            float readPhase[2] = { 0.0f, 0.0f };
        };

        void  update (double sr);
        float read (const Voice& v, int ch, float delaySamples) const;
        float readHarm (const Voice& v, int ch, float delaySamples) const;
        float bandpass (Voice& v, int ch, float x);
        float crush (Voice& v, int ch, float x);
        float harmonize (Voice& v, int ch, float x);

        double sampleRate = 48000.0;
        int    maxDelay   = 4096;
        int    activeVoices = 0;

        float chorusInc   = 0.0f;
        float amountInc   = 0.0f;
        float window      = 1024.0f;
        float globalPhase = 0.0f;
        float meter = 0.0f;

        RefractParams p;
        std::array<Voice, kMaxRefractVoices> voices;
        std::atomic<float> level { 0.0f };
    };

} // namespace sympathy
