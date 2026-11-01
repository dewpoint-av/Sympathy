#include "PhysicalModels.h"

namespace sympathy
{
    namespace
    {
        constexpr float kPi = 3.14159265358979323846f;

        float clampF (float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }

        // Roots of the free-free bar's frequency equation; (n + 1/2) pi is accurate beyond these.
        float barBeta (int n)   // n from 1
        {
            static constexpr float exact[4] = { 4.73004f, 7.85320f, 10.99561f, 14.13717f };
            return n <= 4 ? exact[n - 1] : (static_cast<float> (n) + 0.5f) * kPi;
        }

        // Bar mode shape, 1-based, at x in 0..1. Odd modes are symmetric about the centre.
        float barShape (int n, float x)
        {
            const float u = barBeta (n) * (x - 0.5f);
            return (n % 2 == 1) ? std::cos (u) : std::sin (u);
        }

        // Bessel function by its integral form (std::cyl_bessel_j isn't in libc++).
        float besselJ (int m, float x)
        {
            constexpr int kSteps = 32;
            float sum = 0.0f;
            for (int i = 0; i < kSteps; ++i)
            {
                const float t = (static_cast<float> (i) + 0.5f) * kPi / static_cast<float> (kSteps);
                sum += std::cos (static_cast<float> (m) * t - x * std::sin (t));
            }
            return sum / static_cast<float> (kSteps);
        }

        struct MembraneMode { float zero; int m; };

        // Membrane modes in rising order. Each Bessel zero starts from McMahon's approximation
        // and is refined by Newton's method, since the approximation alone is off by up to 1%.
        const std::array<MembraneMode, 96>& membraneModes()
        {
            static const std::array<MembraneMode, 96> table = []
            {
                const auto J = [] (int m, double x)
                {
                    constexpr int kSteps = 256;
                    double sum = 0.0;
                    for (int i = 0; i < kSteps; ++i)
                    {
                        const double t = (i + 0.5) * 3.14159265358979323846 / kSteps;
                        sum += std::cos (m * t - x * std::sin (t));
                    }
                    return sum / kSteps;
                };

                std::array<MembraneMode, 96> t {};
                int k = 0;
                for (int m = 0; m < 12; ++m)
                    for (int n = 1; n <= 8; ++n, ++k)
                    {
                        const double beta = (n + 0.5 * m - 0.25) * 3.14159265358979323846;
                        double z = beta - (4.0 * m * m - 1.0) / (8.0 * beta);
                        for (int it = 0; it < 6; ++it)
                        {
                            // J_m' = (J_{m-1} - J_{m+1}) / 2, with J_{-1} = -J_1.
                            const double below = m == 0 ? -J (1, z) : J (m - 1, z);
                            const double slope = 0.5 * (below - J (m + 1, z));
                            if (std::fabs (slope) < 1.0e-9) break;
                            z -= J (m, z) / slope;
                        }
                        t[static_cast<size_t> (k)] = { static_cast<float> (z), m };
                    }
                std::sort (t.begin(), t.end(), [] (const MembraneMode& a, const MembraneMode& b) { return a.zero < b.zero; });
                return t;
            }();
            return table;
        }

        // Strike and listening point as a coordinate along the object.
        float along (float h) { return 0.5f * (1.0f - clampF (h, 0.0f, 0.96f)); }
    }

    // -----------------------------------------------------------------------
    void ModalBank::reset()
    {
        y1.fill (0.0f);
        y2.fill (0.0f);
    }

    void ModalBank::rebuild (double sampleRate, const ModelParams& m)
    {
        const float fs = static_cast<float> (sampleRate);
        const int limit = m.highQuality ? kMaxModes : kEcoModes;

        std::array<float, kMaxModes> ratio {};
        std::array<float, kMaxModes> shape {};
        int found = 0;

        const float hx = along (m.hit), px = along (m.pos);

        if (m.type == kBeam || m.type == kMarimba)
        {
            const float b1 = barBeta (1);
            for (int n = 1; n <= limit; ++n, ++found)
            {
                const float r = barBeta (n) / b1;
                float f = r * r;
                if (m.type == kMarimba)
                {
                    // Undercutting tunes the first overtones to 4x and about 10x.
                    if (n == 2)      f = 4.0f;
                    else if (n >= 3)
                    {
                        const float r3 = barBeta (n) / barBeta (3);
                        f = 10.0f * r3 * r3;
                    }
                }
                ratio[static_cast<size_t> (found)] = f;
                shape[static_cast<size_t> (found)] = barShape (n, hx) * barShape (n, px);
            }
        }
        else if (m.type == kString)
        {
            for (int n = 1; n <= limit; ++n, ++found)
            {
                const float k = static_cast<float> (n) * kPi;
                ratio[static_cast<size_t> (found)] = static_cast<float> (n);
                shape[static_cast<size_t> (found)] = std::sin (k * hx) * std::sin (k * px);
            }
        }
        else if (m.type == kMembrane)
        {
            const auto& modes = membraneModes();
            const float z0 = modes[0].zero;
            const float rh = clampF (m.hit, 0.0f, 0.98f), rp = clampF (m.pos, 0.0f, 0.98f);
            const float stretch = 0.3f * (1.0f - clampF (m.ratio, 0.0f, 1.0f));

            for (int k = 0; k < limit; ++k, ++found)
            {
                const auto& mm = modes[static_cast<size_t> (k)];
                // An out-of-round head splits each pair of angular modes; this keeps one
                // of each and moves it by the same amount, so a round head (ratio 1) is exact.
                const float split = mm.m == 0 ? 0.0f : ((mm.m % 2 == 0) ? stretch : -stretch);
                ratio[static_cast<size_t> (found)] = mm.zero / z0 * (1.0f + split);
                shape[static_cast<size_t> (found)] = besselJ (mm.m, mm.zero * rh) * besselJ (mm.m, mm.zero * rp);
            }
        }
        else   // plate
        {
            const float r2 = clampF (m.ratio, 0.2f, 1.0f) * clampF (m.ratio, 0.2f, 1.0f);
            const float base = 1.0f + r2;

            struct PlateMode { float f; int a, c; };
            std::array<PlateMode, 196> all {};
            int k = 0;
            for (int a = 1; a <= 14; ++a)
                for (int c = 1; c <= 14; ++c, ++k)
                    all[static_cast<size_t> (k)] = { (static_cast<float> (a * a) + r2 * static_cast<float> (c * c)) / base, a, c };
            std::sort (all.begin(), all.end(), [] (const PlateMode& x, const PlateMode& y) { return x.f < y.f; });

            // Off the diagonal, so moving the point doesn't cancel every symmetric mode.
            const float hy = along (m.hit * 0.6f), py = along (m.pos * 0.6f);
            for (int i = 0; i < limit; ++i, ++found)
            {
                const auto& pm = all[static_cast<size_t> (i)];
                const float a = static_cast<float> (pm.a) * kPi, c = static_cast<float> (pm.c) * kPi;
                ratio[static_cast<size_t> (found)] = pm.f;
                shape[static_cast<size_t> (found)] = std::sin (a * hx) * std::sin (c * hy)
                                                   * std::sin (a * px) * std::sin (c * py);
            }
        }

        // Material: how decay changes with frequency. Lows-long is the stronger direction,
        // as damping in wood or nylon rises quickly with frequency.
        const float decayExp = m.material >= 0.0f ? m.material * 0.4f : m.material * 1.2f;
        const float stretchExp = 1.0f + 0.25f * clampF (m.inharm, -1.0f, 1.0f);
        const float brightExp = (clampF (m.brightness, 0.0f, 1.0f) - 1.0f) * 1.5f;
        const float nyquist = fs * 0.49f;

        float energy = 0.0f;
        int count = 0;
        for (int i = 0; i < found; ++i)
        {
            const float rr = ratio[static_cast<size_t> (i)];
            const float freq = m.tune * std::pow (rr, stretchExp);
            if (freq >= nyquist) continue;

            const float t = clampF (m.decay * std::pow (rr, decayExp), 0.01f, 120.0f);
            const float pole = std::min (std::exp (-6.9078f / (t * fs)), 0.9999999f);   // -60 dB in t
            const float a = shape[static_cast<size_t> (i)] * std::pow (rr, brightExp);

            const auto c = static_cast<size_t> (count);
            baseFreq[c] = freq;
            theta[c]  = 2.0f * kPi * freq / fs;
            radius[c] = pole;
            // Unit impulse-response energy, so Decay doesn't change the level on broadband input.
            drive[c]  = std::sqrt (2.0f * (1.0f - pole * pole));
            amp[c]    = a;
            energy += a * a;
            ++count;
        }

        const float norm = energy > 1.0e-9f ? 1.0f / std::sqrt (energy) : 0.0f;
        for (int i = 0; i < count; ++i)
            amp[static_cast<size_t> (i)] *= norm;

        // Modes no longer used must stop ringing.
        for (int i = count; i < kMaxModes; ++i)
        {
            const auto c = static_cast<size_t> (i);
            b[c] = c1[c] = c2[c] = y1[c] = y2[c] = amp[c] = 0.0f;
        }
        numModes = count;
        applyPitch();
    }

    void ModalBank::applyPitch()
    {
        for (int i = 0; i < numModes; ++i)
        {
            const auto c = static_cast<size_t> (i);
            const float th = theta[c] * pitch;
            if (th >= kPi * 0.97f)
            {
                b[c] = 0.0f;
                c1[c] = 0.0f;
                c2[c] = 0.0f;
                continue;
            }
            const float r = radius[c];
            b[c]  = drive[c] * std::sin (th);
            c1[c] = 2.0f * r * std::cos (th);
            c2[c] = -r * r;
        }
    }

    void ModalBank::setPitch (float ratio)
    {
        if (ratio == pitch) return;
        pitch = ratio;
        applyPitch();
    }

    void ModalBank::damp (float factor)
    {
        for (int i = 0; i < numModes; ++i)
        {
            y1[static_cast<size_t> (i)] *= factor;
            y2[static_cast<size_t> (i)] *= factor;
        }
    }

    float ModalBank::process (float x)
    {
        const int n = numModes;
        float* py1 = y1.data();
        float* py2 = y2.data();
        const float* pb = b.data();
        const float* pc1 = c1.data();
        const float* pc2 = c2.data();
        const float* pamp = amp.data();

        float sum = 0.0f;
        for (int i = 0; i < n; ++i)
        {
            const float y = pb[i] * x + pc1[i] * py1[i] + pc2[i] * py2[i];
            py2[i] = py1[i];
            py1[i] = y;
            sum += pamp[i] * y;
        }
        return sum;
    }

    // -----------------------------------------------------------------------
    void Waveguide::prepare (double sampleRate)
    {
        fs = sampleRate;
        // 10 Hz is the lowest fundamental after MIDI, LFO and end correction.
        const int size = static_cast<int> (sampleRate / 10.0) + 16;
        openLoop.delay.prepare (size);
        closedLoop.delay.prepare (size);
        openLoop.sign = 1.0f;
        closedLoop.sign = -1.0f;
    }

    void Waveguide::reset()
    {
        openLoop.delay.reset();
        closedLoop.delay.reset();
        openLoop.lp = closedLoop.lp = 0.0f;
    }

    void Waveguide::rebuild (const ModelParams& m)
    {
        const float rad = clampF (m.radius, 0.0f, 1.0f);

        // A wider bore loses less at the walls: longer decay and more highs.
        const float decay = m.decay * (0.4f + 1.6f * rad);
        // A tube's narrow, resistive walls are darker than an open pipe's.
        lpCoef = m.type == kTube ? 0.12f + 0.6f * rad : 0.25f + 0.73f * rad;
        mixOpen = m.type == kTube ? 1.0f : clampF (m.opening, 0.0f, 1.0f);

        // End correction: a very wide bore is acoustically longer, so the pitch falls.
        // Only the top of the range, up to about a semitone, so Radius stays tuned otherwise.
        const float over = std::max (rad - 0.7f, 0.0f) / 0.3f;
        const float endCorrection = 1.0f + 0.06f * over * over;
        fundamental = m.tune;

        const float perPass = 1.0f / (clampF (decay, 0.01f, 120.0f) * std::max (m.tune, 1.0f));
        const float g = std::pow (10.0f, -3.0f * perPass);

        const float cycle = static_cast<float> (fs) / std::max (m.tune, 1.0f) * endCorrection;
        // Taken out of the delay line: the damping filter's (1 - c) / c samples at low
        // frequency, plus one sample because feedback arrives on the next sample.
        const float filterDelay = (1.0f - lpCoef) / lpCoef + 1.0f;

        openLoop.baseDelay   = cycle;              // resonates at f, 2f, 3f ...
        closedLoop.baseDelay = cycle * 0.5f;       // inverted, half length: f, 3f, 5f ...
        openLoop.g = closedLoop.g = g;
        closedLoop.g = std::pow (g, 0.5f);         // twice the passes per cycle
        openLoop.scale = std::sqrt (1.0f - g * g);
        closedLoop.scale = std::sqrt (1.0f - closedLoop.g * closedLoop.g);

        compensation = filterDelay;
        updateLoops();
    }

    void Waveguide::updateLoops()
    {
        const float limit = static_cast<float> (openLoop.delay.capacity() - 4);
        for (Loop* l : { &openLoop, &closedLoop })
        {
            l->delaySamples = clampF (l->baseDelay / pitch - compensation, 1.0f, limit);
            l->gDamped = l->g;
            if (dampTau > 0.0f)
                l->gDamped *= std::exp (-l->delaySamples / (dampTau * static_cast<float> (fs)));
        }
    }

    void Waveguide::setPitch (float ratio)
    {
        if (ratio == pitch) return;
        pitch = ratio;
        updateLoops();
    }

    void Waveguide::damp (float secondsTimeConstant)
    {
        if (secondsTimeConstant == dampTau) return;
        dampTau = secondsTimeConstant;
        updateLoops();
    }

    float Waveguide::process (float x)
    {
        float out = 0.0f;

        if (mixOpen > 0.0f)
        {
            const float y = openLoop.delay.process (x * openLoop.scale + openLoop.gDamped * openLoop.lp, openLoop.delaySamples);
            openLoop.lp += lpCoef * (y - openLoop.lp);
            out += mixOpen * y;
        }
        if (mixOpen < 1.0f)
        {
            const float y = closedLoop.delay.process (x * closedLoop.scale - closedLoop.gDamped * closedLoop.lp, closedLoop.delaySamples);
            closedLoop.lp += lpCoef * (y - closedLoop.lp);
            out += (1.0f - mixOpen) * y;
        }
        return out;
    }

} // namespace sympathy
