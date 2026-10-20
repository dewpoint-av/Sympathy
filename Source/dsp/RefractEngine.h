#pragma once

/*
  Sympathy — "Refract" super-unison multi-FX engine.

  Faithful (in spirit) to Arturia's EFX REFRACT: the incoming signal is
  duplicated into up to 8 stereo voices, each one detuned/modulated against the
  others and — optionally — run through a per-voice "Mode FX" before the voices
  are recombined into a wide, dispersing stereo image.

  Modes
    Unison       pure detuned voices (chorus-style super unison)
    Bandpass     each voice filtered around its own cutoff (dispersed by Amount)
    Comb         per-voice feedforward comb / flanger
    Bitcrusher   per-voice bit-depth + sample-rate reduction
    Distortion   per-voice saturation (tanh)
    Harmonizer   per-voice pitch shift to musical intervals (delay-line shifter)

  Amount detunes the voices and disperses the mode FX; a global LFO (rate +
  depth) breathes the Amount for movement. CPU stays tiny: one delay line per
  voice per channel, no per-sample coefficient recomputation.
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

    enum RefractMode
    {
        kRefractUnison      = 0,
        kRefractBandpass    = 1,
        kRefractComb        = 2,
        kRefractBitcrusher  = 3,
        kRefractDistortion  = 4,
        kRefractHarmonizer  = 5
    };

    static constexpr int kNumRefractModes = 6;

    struct RefractParams
    {
        bool  on       = false;
        int   mode     = kRefractUnison;
        int   voices   = 4;        // 2..8
        float amount   = 0.40f;    // 0..1 (detune depth + FX dispersion)
        float mix      = 0.35f;    // 0..1 (refracted level)
        float spread   = 0.70f;    // 0..1 (stereo width)
        float lfoRate  = 0.25f;    // Hz (modulates Amount)
        float lfoDepth = 0.30f;    // 0..1

        bool operator== (const RefractParams& o) const
        {
            return on       == o.on
                && mode     == o.mode
                && voices   == o.voices
                && amount   == o.amount
                && mix      == o.mix
                && spread   == o.spread
                && lfoRate  == o.lfoRate
                && lfoDepth == o.lfoDepth;
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
        int   getActiveVoices() const { return activeVoices; }

    private:
        struct Voice
        {
            std::array<std::vector<float>, 2> buf;
            int writePos = 0;

            // chorus / unison detune
            float lfoPhase   = 0.0f;
            float lfoRateMul = 1.0f;
            float detuneDepth = 0.0f;   // samples (at amount = 1)
            float detuneBase  = 0.0f;   // samples

            // stereo pan (constant power)
            float panL = 1.0f;
            float panR = 1.0f;

            // bandpass (direct form II)
            float b0 = 0.0f, b1 = 0.0f, b2 = 0.0f, a1 = 0.0f, a2 = 0.0f;
            float z1[2] = { 0.0f, 0.0f };
            float z2[2] = { 0.0f, 0.0f };

            // comb
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
            float interval   = 0.0f;   // semitones at amount = 1
            float ratio      = 1.0f;
            float readPhase[2] = { 0.0f, 0.0f };
        };

        void  update (double sr);
        float read (const Voice& v, int ch, float delaySamples) const;
        float bandpass (Voice& v, int ch, float x);
        float crush (Voice& v, int ch, float x);
        float harmonize (Voice& v, int ch);

        double sampleRate = 48000.0;
        int    maxDelay   = 4096;
        int    activeVoices = 0;

        float chorusInc = 0.0f;
        float amountInc = 0.0f;
        float window    = 1024.0f;
        float globalPhase = 0.0f;
        float meter = 0.0f;

        RefractParams p;
        std::array<Voice, kMaxRefractVoices> voices;
        std::atomic<float> level { 0.0f };
    };

} // namespace sympathy
