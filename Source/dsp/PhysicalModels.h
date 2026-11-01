#pragma once

/*
  Sympathy — the resonating objects.

  Modal types are banks of two-pole resonators whose frequencies and amplitudes come
  from the physics of each object:

    Beam      free-free bar           partials (beta_n / beta_1)^2: 1, 2.756, 5.404 ...
    Marimba   undercut, tuned bar     1 : 4 : 10, then bar-like partials above
    String    ideal string            1, 2, 3 ...
    Membrane  circular drum head      Bessel zeros j_mn / j_01: 1, 1.594, 2.136 ...
    Plate     simply supported plate  m^2 + (r n)^2, normalised to the (1,1) mode

  Each mode's amplitude is its mode shape at the strike point times its shape at the
  listening point, so Hit and Pos behave as moving a mallet and a pickup do. Bar shapes
  leave out the hyperbolic edge terms, which matter only very close to the ends.

  Pipe and Tube are waveguides: delay loops with a damping filter. A closed pipe's loop
  inverts, leaving odd harmonics; an open one keeps all harmonics. Opening crossfades
  between two such loops, both tuned so the fundamental stays put.
*/

#include <algorithm>
#include <array>
#include <cmath>
#include <vector>

namespace sympathy
{
    enum ResonatorType : int { kBeam = 0, kMarimba, kString, kMembrane, kPlate, kPipe, kTube, kNumResonatorTypes };

    static constexpr int kMaxModes = 64;
    static constexpr int kEcoModes = 16;

    inline bool isWaveguide (int type) { return type == kPipe || type == kTube; }

    struct ModelParams
    {
        int   type = kBeam;
        bool  highQuality = true;
        float tune = 220.0f;     // Hz
        float decay = 2.5f;      // seconds at the fundamental
        float material = 0.0f;   // -1 lows ring longest .. +1 highs ring longest
        float brightness = 0.65f;
        float inharm = 0.0f;     // -1 pulls partials down .. +1 pushes them up
        float hit = 0.3f;        // 0 centre .. 1 edge
        float pos = 0.3f;        // listening point, same scale
        float ratio = 0.7f;      // plate/membrane proportions, 1 = square/round
        float radius = 0.5f;     // pipe/tube
        float opening = 1.0f;    // pipe: 0 closed at one end .. 1 open at both

        bool operator== (const ModelParams& o) const
        {
            return type == o.type && highQuality == o.highQuality && tune == o.tune && decay == o.decay
                && material == o.material && brightness == o.brightness && inharm == o.inharm
                && hit == o.hit && pos == o.pos && ratio == o.ratio && radius == o.radius && opening == o.opening;
        }
    };

    // A fractional delay using first-order allpass interpolation, which is flat in
    // magnitude, so unlike linear interpolation it doesn't dull a feedback loop.
    class FractionalDelay
    {
    public:
        void prepare (int maxSamples)
        {
            buffer.assign (static_cast<size_t> (maxSamples), 0.0f);
            maxLength = maxSamples;
            writePos = 0;
            z = 0.0f;
        }

        void reset()
        {
            std::fill (buffer.begin(), buffer.end(), 0.0f);
            writePos = 0;
            z = 0.0f;
        }

        int capacity() const { return maxLength; }

        // Output is x delayed by delaySamples, for delaySamples >= 1.
        float process (float x, float delaySamples)
        {
            buffer[static_cast<size_t> (writePos)] = x;

            // Keeps the allpass fraction in 0.1 .. 1.1, where its delay is most accurate.
            const float d = std::max (delaySamples - 0.1f, 0.0f);
            const int   whole = static_cast<int> (d);
            const float frac = (d - static_cast<float> (whole)) + 0.1f;

            int i0 = writePos - whole;
            if (i0 < 0) i0 += maxLength;
            int i1 = i0 - 1;
            if (i1 < 0) i1 += maxLength;

            writePos = (writePos + 1) % maxLength;

            const float a = (1.0f - frac) / (1.0f + frac);
            const float y = a * buffer[static_cast<size_t> (i0)] + buffer[static_cast<size_t> (i1)] - a * z;
            z = y;
            return y;
        }

    private:
        std::vector<float> buffer;
        int maxLength = 1;
        int writePos = 0;
        float z = 0.0f;
    };

    // Modal bank, stored so the per-sample loop can vectorise.
    class ModalBank
    {
    public:
        void reset();
        void rebuild (double sampleRate, const ModelParams& m);
        void setPitch (float ratio);          // retunes without touching decay or amplitude
        void damp (float factor);             // scales the ringing state
        float process (float x);

        int   count() const { return numModes; }
        float frequency (int i) const { return baseFreq[static_cast<size_t> (i)]; }

    private:
        void applyPitch();

        alignas (32) std::array<float, kMaxModes> b {};
        alignas (32) std::array<float, kMaxModes> c1 {};
        alignas (32) std::array<float, kMaxModes> c2 {};
        alignas (32) std::array<float, kMaxModes> y1 {};
        alignas (32) std::array<float, kMaxModes> y2 {};
        alignas (32) std::array<float, kMaxModes> amp {};
        std::array<float, kMaxModes> theta {};
        std::array<float, kMaxModes> radius {};
        std::array<float, kMaxModes> drive {};
        std::array<float, kMaxModes> baseFreq {};
        int numModes = 0;
        float pitch = 1.0f;
    };

    // Pipe / Tube.
    class Waveguide
    {
    public:
        void prepare (double sampleRate);
        void reset();
        void rebuild (const ModelParams& m);
        void setPitch (float ratio);
        void damp (float secondsTimeConstant);   // <= 0 means no extra damping
        float process (float x);

        int   count() const { return kReportedPartials; }
        float frequency (int i) const { return fundamental * static_cast<float> (i + 1); }

    private:
        static constexpr int kReportedPartials = 16;

        struct Loop
        {
            FractionalDelay delay;
            float sign = 1.0f;
            float delaySamples = 100.0f;
            float baseDelay = 100.0f;
            float g = 0.0f;
            float gDamped = 0.0f;
            float scale = 0.0f;
            float lp = 0.0f;
        };

        void updateLoops();

        double fs = 48000.0;
        Loop openLoop, closedLoop;
        float lpCoef = 0.5f;
        float compensation = 0.0f;   // samples of delay added by the damping filter
        float mixOpen = 1.0f;
        float fundamental = 220.0f;
        float pitch = 1.0f;
        float dampTau = 0.0f;
    };

} // namespace sympathy
