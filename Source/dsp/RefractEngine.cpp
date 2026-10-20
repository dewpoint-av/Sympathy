#include "RefractEngine.h"

namespace sympathy
{
    namespace
    {
        constexpr float kPi = 3.14159265358979323846f;

        // Musical intervals used by the Harmonizer (one per voice, up to 8).
        constexpr float kHarmonizerIntervals[kMaxRefractVoices] = {
            0.0f, 7.0f, 12.0f, 19.0f, 24.0f, -5.0f, -12.0f, 3.0f
        };

        float clampF (float v, float lo, float hi)
        {
            return v < lo ? lo : (v > hi ? hi : v);
        }
    }

    // -----------------------------------------------------------------------
    void RefractEngine::prepare (double sr, int /*maxBlockSize*/)
    {
        sampleRate = sr;
        maxDelay   = static_cast<int> (sr * 0.12) + 256;
        window     = 0.03f * static_cast<float> (sr);

        for (auto& v : voices)
        {
            v.buf[0].assign (static_cast<size_t> (maxDelay), 0.0f);
            v.buf[1].assign (static_cast<size_t> (maxDelay), 0.0f);
        }

        reset();
        update (sr);
    }

    void RefractEngine::reset()
    {
        for (auto& v : voices)
        {
            std::fill (v.buf[0].begin(), v.buf[0].end(), 0.0f);
            std::fill (v.buf[1].begin(), v.buf[1].end(), 0.0f);
            v.writePos = 0;
            v.lfoPhase = 0.0f;
            v.z1[0] = v.z1[1] = v.z2[0] = v.z2[1] = 0.0f;
            v.hold[0] = v.hold[1] = 0.0f;
            v.holdCount[0] = v.holdCount[1] = 0;
            v.readPhase[0] = v.readPhase[1] = 0.0f;
        }

        globalPhase = 0.0f;
        meter = 0.0f;
        level.store (0.0f);
    }

    void RefractEngine::setParams (const RefractParams& np)
    {
        if (np == p)
            return;
        p = np;
        update (sampleRate);
    }

    void RefractEngine::update (double sr)
    {
        const float fs = static_cast<float> (sr);
        const int n = std::clamp (p.voices, 2, kMaxRefractVoices);

        chorusInc = 0.5f / fs;                 // per-voice drift ~0.5 Hz
        amountInc = p.lfoRate / fs;            // global amount LFO
        window    = 0.03f * fs;

        for (int i = 0; i < n; ++i)
        {
            Voice& v = voices[static_cast<size_t> (i)];
            const float t = (n > 1) ? static_cast<float> (i) / static_cast<float> (n - 1)
                                    : 0.5f;
            const float s = t * 2.0f - 1.0f;   // -1..1

            // Constant-power pan spread across the stereo field.
            const float angle = (s * p.spread + 1.0f) * 0.5f * (0.5f * kPi);
            v.panL = std::cos (angle);
            v.panR = std::sin (angle);

            // Unison detune: 2..14 ms of modulated delay (amount = 1), wider
            // and deeper toward the outer voices.
            v.detuneDepth = (0.002f + 0.012f * (0.35f + 0.65f * t)) * fs;
            v.detuneBase  = (0.04f + 0.010f * t) * fs;

            v.lfoRateMul = 0.6f + 0.8f * t;
            v.lfoPhase   = s * 0.5f;

            // --- Bandpass: per-voice cutoff dispersed around a 1 kHz centre. ---
            const float center = 1000.0f;
            const float cutoff = clampF (center * std::pow (2.0f, p.amount * s * 1.5f),
                                         40.0f, fs * 0.45f);
            const float q     = 0.6f + 2.0f * p.amount;
            const float w0    = 2.0f * kPi * cutoff / fs;
            const float alpha = std::sin (w0) / (2.0f * q);
            const float a0    = 1.0f + alpha;
            v.b0 =  alpha / a0;
            v.b1 =  0.0f;
            v.b2 = -alpha / a0;
            v.a1 = -2.0f * std::cos (w0) / a0;
            v.a2 =  (1.0f - alpha) / a0;

            // --- Comb (feedforward) ---
            v.combG     = 0.55f + 0.35f * p.amount;
            v.combDelay = (1.0f + 2.0f * t + 3.0f * p.amount) * 0.001f * fs;

            // --- Bitcrusher ---
            v.crushSteps = std::pow (2.0f, 4.0f + (1.0f - p.amount) * 10.0f);
            v.holdLen    = 1 + static_cast<int> (p.amount * 24.0f);

            // --- Distortion ---
            v.drive = 1.0f + p.amount * 14.0f * (0.4f + 0.6f * t);

            // --- Harmonizer ---
            v.interval = kHarmonizerIntervals[static_cast<size_t> (i)];
            v.ratio    = std::pow (2.0f, p.amount * v.interval / 12.0f);
        }

        activeVoices = n;
    }

    float RefractEngine::read (const Voice& v, int ch, float delaySamples) const
    {
        const auto& buf = v.buf[static_cast<size_t> (ch)];
        delaySamples = std::clamp (delaySamples, 1.0f, static_cast<float> (maxDelay - 1));

        float readPos = static_cast<float> (v.writePos) - delaySamples;
        if (readPos < 0.0f)
            readPos += static_cast<float> (maxDelay);

        const int i0    = static_cast<int> (readPos);
        const float frac = readPos - static_cast<float> (i0);
        const int i1    = (i0 + 1) % maxDelay;

        return buf[static_cast<size_t> (i0)] * (1.0f - frac)
             + buf[static_cast<size_t> (i1)] * frac;
    }

    float RefractEngine::bandpass (Voice& v, int ch, float x)
    {
        const float w = x - v.a1 * v.z1[ch] - v.a2 * v.z2[ch];
        const float y = v.b0 * w + v.b1 * v.z1[ch] + v.b2 * v.z2[ch];
        v.z2[ch] = v.z1[ch];
        v.z1[ch] = w;
        return y;
    }

    float RefractEngine::crush (Voice& v, int ch, float x)
    {
        if (++v.holdCount[ch] >= v.holdLen)
        {
            v.holdCount[ch] = 0;
            v.hold[ch] = x;
        }

        return std::round (v.hold[ch] * v.crushSteps) / v.crushSteps;
    }

    float RefractEngine::harmonize (Voice& v, int ch)
    {
        // Two crossfaded read heads through the voice's delay line give a
        // granular, Doppler-style pitch shift. Cheap, and the mild artefacts
        // are exactly the texture we want for a "dispersion" effect.
        float& ph = v.readPhase[ch];
        ph += 1.0f - v.ratio;                 // advance relative to write head
        if (ph >= window) ph -= window;
        if (ph < 0.0f)    ph += window;

        const float p2 = ph + window * 0.5f;
        const float p2w = (p2 >= window) ? p2 - window : p2;

        const float fade = ph / window;
        const float w1   = 1.0f - fade;
        const float w2   = fade;

        const float t1 = read (v, ch, v.detuneBase + ph);
        const float t2 = read (v, ch, v.detuneBase + p2w);
        return t1 * w1 + t2 * w2;
    }

    void RefractEngine::processSample (float inL, float inR, float& outL, float& outR)
    {
        outL = 0.0f;
        outR = 0.0f;

        if (!p.on || p.mix <= 0.0001f || activeVoices < 2)
            return;

        // Global LFO breathes the refraction amount.
        globalPhase += amountInc;
        if (globalPhase >= 1.0f) globalPhase -= 1.0f;
        const float lfo       = p.lfoDepth * std::sin (2.0f * kPi * globalPhase);
        const float amountMod = clampF (p.amount + p.amount * lfo, 0.0f, 1.0f);

        const int n = activeVoices;
        float accL = 0.0f;
        float accR = 0.0f;

        for (int i = 0; i < n; ++i)
        {
            Voice& v = voices[static_cast<size_t> (i)];

            v.lfoPhase += chorusInc * v.lfoRateMul;
            if (v.lfoPhase >= 1.0f) v.lfoPhase -= 1.0f;

            v.buf[0][static_cast<size_t> (v.writePos)] = inL;
            v.buf[1][static_cast<size_t> (v.writePos)] = inR;
            v.writePos = (v.writePos + 1) % maxDelay;

            float detL, detR;
            if (p.mode == kRefractHarmonizer)
            {
                detL = harmonize (v, 0);
                detR = harmonize (v, 1);
            }
            else
            {
                const float mod = v.detuneBase
                                + v.detuneDepth * amountMod
                                  * (0.5f + 0.5f * std::sin (2.0f * kPi * v.lfoPhase));
                detL = read (v, 0, mod);
                detR = read (v, 1, mod);
            }

            switch (p.mode)
            {
                case kRefractBandpass:
                    detL = bandpass (v, 0, detL);
                    detR = bandpass (v, 1, detR);
                    break;

                case kRefractComb:
                    detL += v.combG * read (v, 0, v.combDelay);
                    detR += v.combG * read (v, 1, v.combDelay);
                    break;

                case kRefractBitcrusher:
                    detL = crush (v, 0, detL);
                    detR = crush (v, 1, detR);
                    break;

                case kRefractDistortion:
                    detL = std::tanh (v.drive * detL);
                    detR = std::tanh (v.drive * detR);
                    break;

                default:
                    break;
            }

            accL += v.panL * detL;
            accR += v.panR * detR;
        }

        const float norm    = 1.0f / std::sqrt (static_cast<float> (n));
        const float lfoGain = 1.0f + 0.25f * lfo;
        outL = accL * norm * p.mix * lfoGain;
        outR = accR * norm * p.mix * lfoGain;

        meter = std::max (std::max (std::fabs (outL), std::fabs (outR)), meter * 0.90f);
        level.store (meter);
    }

} // namespace sympathy
