#pragma once

/*
  Sympathy — Refraction: a unison multi-effect.

  The input is split into 1-8 stereo voices, each slightly detuned, and each voice runs
  its own copy of one Mode effect. Amount does the signature move: it spreads the Mode's
  main control across the voices, centred on the value set by the Primary knob, so one
  sound fans out into many.

    In -> high-pass -> detuned voices -> one Mode per voice -> pan -> low-pass -> out

  Modes and their (Primary / Secondary) controls:
    Unison      (none)
    Bandpass    Cutoff / Resonance
    Comb        Cutoff / Resonance (feedback, up to self-oscillation)
    Bitcrusher  Down Sample / Bit Depth
    Distortion  Drive, with 4 algorithms
    Harmonizer  Spray, with 12 chords
*/

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstddef>
#include <vector>

#include "Lfo.h"

namespace sympathy
{
    static constexpr int kMaxRefractVoices = 8;

    enum RefractMode : int { kModeUnison = 0, kModeBandpass, kModeComb, kModeBitcrush, kModeDistortion, kModeHarmonizer, kNumRefractModes };
    enum DistortionAlgo : int { kAlgoTape = 0, kAlgoGermanium, kAlgoSoftBounce, kAlgoSineFolder, kNumDistortionAlgos };
    static constexpr int kNumChords = 12;

    struct RefractParams
    {
        bool  on        = false;
        int   mode      = kModeUnison;
        int   voices    = 4;       // 1..8
        float amount    = 0.40f;   // 0..1  detune, width and spread of the Primary control
        float primary   = 0.50f;   // 0..1  cutoff / down sample / drive / spray
        float secondary = 0.50f;   // 0..1  resonance / bit depth
        int   algorithm = kAlgoTape;
        int   chord     = 0;
        float mix       = 0.35f;   // 0..1  refracted level
        float spread    = 0.70f;   // 0..1  stereo width
        float hpHz      = 20.0f;
        float lpHz      = 20000.0f;
        LfoSettings lfo;
        float lfoToAmount  = 0.30f;   // -1..1
        float lfoToPrimary = 0.0f;    // -1..1

        bool operator== (const RefractParams& o) const
        {
            return on == o.on && mode == o.mode && voices == o.voices && amount == o.amount
                && primary == o.primary && secondary == o.secondary && algorithm == o.algorithm
                && chord == o.chord && mix == o.mix && spread == o.spread && hpHz == o.hpHz
                && lpHz == o.lpHz && lfo == o.lfo && lfoToAmount == o.lfoToAmount
                && lfoToPrimary == o.lfoToPrimary;
        }
    };

    class RefractEngine
    {
    public:
        void prepare (double sampleRate, int maxBlockSize);
        void reset();
        void setParams (const RefractParams& p);
        void setHostTime (const HostTime& t) { hostTime = t; }
        void processSample (float inL, float inR, float& outL, float& outR);

        float getLevel() const { return level.load(); }

    private:
        // Transposed direct form II biquad.
        struct Biquad
        {
            float b0 = 1.0f, b1 = 0.0f, b2 = 0.0f, a1 = 0.0f, a2 = 0.0f;
            float z1[2] = { 0.0f, 0.0f };
            float z2[2] = { 0.0f, 0.0f };

            void setHighpass (float fs, float hz);
            void setLowpass (float fs, float hz);
            void clear() { z1[0] = z1[1] = z2[0] = z2[1] = 0.0f; }
            float process (int ch, float x)
            {
                const float y = b0 * x + z1[ch];
                z1[ch] = b1 * x - a1 * y + z2[ch];
                z2[ch] = b2 * x - a2 * y;
                return y;
            }
        };

        // Topology-preserving state-variable filter: stays stable while being swept.
        struct Svf
        {
            float g = 0.1f, k = 1.0f, a1 = 0.0f, a2 = 0.0f, a3 = 0.0f;
            float ic1[2] = { 0.0f, 0.0f };
            float ic2[2] = { 0.0f, 0.0f };

            void set (float fs, float hz, float q);
            float bandpass (int ch, float x);   // unity gain at the centre frequency
        };

        struct Voice
        {
            float lfoPhase = 0.0f, lfoRate = 0.0f;
            float baseDelay = 0.0f, depth = 0.0f;
            float panL = 1.0f, panR = 1.0f;
            float s = 0.0f;                // position in the spread, -1..1
            float primary = 0.5f;          // this voice's control value

            Svf svf;

            std::array<std::vector<float>, 2> combBuf;
            int   combWrite = 0;
            float combDelay = 100.0f;
            float combG = 0.0f;
            float combScale = 1.0f;
            float combLp[2] = { 0.0f, 0.0f };

            float holdValue[2] = { 0.0f, 0.0f };
            float holdPhase[2] = { 1.0f, 1.0f };
            float holdInc = 1.0f;

            float drive = 1.0f;
            float makeup = 1.0f;
            float prevX[2] = { 0.0f, 0.0f };
            float prevF[2] = { 0.0f, 0.0f };

            std::array<std::vector<float>, 2> pitchBuf;
            int   pitchWrite = 0;
            float pitchPhase = 0.0f;
            float pitchRatio = 1.0f;
            float sprayDelay = 0.0f;       // target
            float sprayNow = 0.0f;         // slewed, so modulating Spray doesn't click
            float sprayFb = 0.0f;
            float fbState[2] = { 0.0f, 0.0f };
        };

        void  update();
        void  updateVoiceControls();
        float readInput (int ch, float delay) const;
        float processComb (Voice& v, int ch, float x);
        float processCrush (Voice& v, int ch, float x);
        float processDistortion (Voice& v, int ch, float x);
        float processHarmonizer (Voice& v, int ch, float x);
        void  advanceHarmonizer (Voice& v);

        double sampleRate = 48000.0;
        float  fs = 48000.0f;

        std::array<std::vector<float>, 2> input;
        int inputWrite = 0;
        int inputSize = 1;
        int combSize = 1;
        int pitchSize = 1;
        float pitchWindow = 2048.0f;

        int activeVoices = 1;
        int controlCounter = 0;
        float amountNow = 0.0f;
        float widthNow = 1.0f;
        float crushSteps = 256.0f;
        float meter = 0.0f;

        RefractParams p;
        HostTime hostTime;
        Lfo lfo { 0x2545F491u };
        Biquad hp, lp;
        std::array<Voice, kMaxRefractVoices> voices;
        std::atomic<float> level { 0.0f };
    };

} // namespace sympathy
