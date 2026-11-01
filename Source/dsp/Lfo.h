#pragma once

/*
  Sympathy — low-frequency oscillator shared by the resonator and Refraction.
  Runs free in Hz, or locks to the host's position when synced (tempo divisions
  assume 4/4). Advanced at control rate rather than per sample.
*/

#include <cmath>
#include <cstdint>

namespace sympathy
{
    enum class LfoShape : int { Sine = 0, Square, Triangle, SawUp, SawDown, Stepped, Smooth };

    enum LfoSyncMode : int { kSyncFree = 0, kSyncStraight, kSyncTriplet, kSyncDotted };

    static constexpr int kNumLfoDivisions = 10;
    // 16 bars ... 1/32, in quarter-note beats.
    static constexpr float kLfoDivisionBeats[kNumLfoDivisions] = {
        64.0f, 32.0f, 16.0f, 8.0f, 4.0f, 2.0f, 1.0f, 0.5f, 0.25f, 0.125f
    };

    struct HostTime
    {
        bool   playing = false;
        double ppq = 0.0;
        double bpm = 120.0;
    };

    struct LfoSettings
    {
        int   shape = 0;
        int   sync = kSyncFree;
        int   division = 6;
        float rateHz = 1.0f;

        bool operator== (const LfoSettings& o) const
        {
            return shape == o.shape && sync == o.sync && division == o.division && rateHz == o.rateHz;
        }
    };

    class Lfo
    {
    public:
        explicit Lfo (uint32_t seed = 0x51ED270Bu) : rng (seed) { reset(); }

        void reset()
        {
            phase = 0.0;
            current = random();
            next = random();
        }

        // Moves on by `seconds`. `host` is the position at the end of that span.
        void advance (const LfoSettings& s, double seconds, const HostTime& host)
        {
            const double previous = phase;

            if (s.sync == kSyncFree)
            {
                phase += seconds * static_cast<double> (s.rateHz);
            }
            else
            {
                const int d = s.division < 0 ? 0 : (s.division >= kNumLfoDivisions ? kNumLfoDivisions - 1 : s.division);
                double beats = static_cast<double> (kLfoDivisionBeats[d]);
                if (s.sync == kSyncTriplet)     beats *= 2.0 / 3.0;
                else if (s.sync == kSyncDotted) beats *= 1.5;

                if (host.playing)
                    phase = host.ppq / beats;
                else
                    phase += seconds * (host.bpm / 60.0) / beats;
            }

            if (std::floor (phase) != std::floor (previous))
            {
                current = next;
                next = random();
            }
            phase -= std::floor (phase);
        }

        // Output in -1..1. `offset` (in cycles) gives a second channel its own phase;
        // the noise shapes have no meaningful phase, so they ignore it.
        float value (int shape, double offset = 0.0) const
        {
            double t = phase + offset;
            t -= std::floor (t);
            const float x = static_cast<float> (t);

            switch (static_cast<LfoShape> (shape))
            {
                case LfoShape::Square:   return x < 0.5f ? 1.0f : -1.0f;
                case LfoShape::Triangle: return x < 0.5f ? 4.0f * x - 1.0f : 3.0f - 4.0f * x;
                case LfoShape::SawUp:    return 2.0f * x - 1.0f;
                case LfoShape::SawDown:  return 1.0f - 2.0f * x;
                case LfoShape::Stepped:  return current;
                case LfoShape::Smooth:
                {
                    const float p = static_cast<float> (phase);
                    const float w = 0.5f - 0.5f * std::cos (3.14159265358979323846f * p);
                    return current + (next - current) * w;
                }
                case LfoShape::Sine:
                default:                 return std::sin (2.0f * 3.14159265358979323846f * x);
            }
        }

    private:
        float random()
        {
            rng ^= rng << 13;
            rng ^= rng >> 17;
            rng ^= rng << 5;
            return static_cast<float> (static_cast<int32_t> (rng)) * (1.0f / 2147483648.0f);
        }

        uint32_t rng;
        double phase = 0.0;
        float current = 0.0f, next = 0.0f;
    };

} // namespace sympathy
