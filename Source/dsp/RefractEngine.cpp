#include "RefractEngine.h"

namespace sympathy
{
    namespace
    {
        constexpr float kPi = 3.14159265358979323846f;

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
            v.harmBuf[0].assign (static_cast<size_t> (maxDelay), 0.0f);
            v.harmBuf[1].assign (static_cast<size_t> (maxDelay), 0.0f);
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
            std::fill (v.harmBuf[0].begin(), v.harmBuf[0].end(), 0.0f);
            std::fill (v.harmBuf[1].begin(), v.harmBuf[1].end(), 0.0f);
            v.writePos = 0;
            v.harmWritePos = 0;
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

        chorusInc = 0.5f / fs;
        amountInc = p.lfoRate / fs;
        window    = 0.03f * fs;

        for (int i = 0; i < n; ++i)
        {
            Voice& v = voices[static_cast<size_t> (i)];
            const float t = (n > 1) ? static_cast<float> (i) / static_cast<float> (n - 1) : 0.5f;
            const float s = t * 2.0f - 1.0f;

            const float angle = (s * p.spread + 1.0f) * 0.5f * (0.5f * kPi);
            v.panL = std::cos (angle);
            v.panR = std::sin (angle);

            v.detuneDepth = (0.002f + 0.012f * (0.35f + 0.65f * t)) * fs;
            v.detuneBase  = (0.04f + 0.010f * t) * fs;
            v.lfoRateMul  = 0.6f + 0.8f * t;
            v.lfoPhase    = s * 0.5f;

            // bandpass
            const float cutoff = clampF (1000.0f * std::pow (2.0f, p.amount * s * 1.5f),
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

            // comb
            v.combG     = 0.55f + 0.35f * p.amount;
            v.combDelay = (1.0f + 2.0f * t + 3.0f * p.amount) * 0.001f * fs;

            // bitcrusher
            v.crushSteps = std::pow (2.0f, 4.0f + (1.0f - p.amount) * 10.0f);
            v.holdLen    = 1 + static_cast<int> (p.amount * 24.0f);

            // distortion
            v.drive = 1.0f + p.amount * 14.0f * (0.4f + 0.6f * t);

            // harmonizer
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

        const int i0 = static_cast<int> (readPos);
        const float frac = readPos - static_cast<float> (i0);
        const int i1 = (i0 + 1) % maxDelay;

        return buf[static_cast<size_t> (i0)] * (1.0f - frac)
             + buf[static_cast<size_t> (i1)] * frac;
    }

    float RefractEngine::readHarm (const Voice& v, int ch, float delaySamples) const
    {
        const auto& buf = v.harmBuf[static_cast<size_t> (ch)];
        delaySamples = std::clamp (delaySamples, 1.0f, static_cast<float> (maxDelay - 1));

        float readPos = static_cast<float> (v.harmWritePos) - delaySamples;
        if (readPos < 0.0f)
            readPos += static_cast<float> (maxDelay);

        const int i0 = static_cast<int> (readPos);
        const float frac = readPos - static_cast<float> (i0);
        const int i1 = (i0 + 1) % maxDelay;

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

    float RefractEngine::harmonize (Voice& v, int ch, float x)
    {
        v.harmBuf[ch][static_cast<size_t> (v.harmWritePos)] = x;

        float& ph = v.readPhase[ch];
        ph += 1.0f - v.ratio;
        if (ph >= window) ph -= window;
        if (ph < 0.0f)    ph += window;

        const float p2  = ph + window * 0.5f;
        const float p2w = (p2 >= window) ? p2 - window : p2;

        const float fade = ph / window;
        const float t1 = readHarm (v, ch, v.detuneBase + ph);
        const float t2 = readHarm (v, ch, v.detuneBase + p2w);

        v.harmWritePos = (v.harmWritePos + 1) % maxDelay;
        return t1 * (1.0f - fade) + t2 * fade;
    }

    void RefractEngine::processSample (float inL, float inR, float& outL, float& outR)
    {
        outL = 0.0f;
        outR = 0.0f;

        if (!p.on || p.mix <= 0.0001f || activeVoices < 2)
            return;

        globalPhase += amountInc;
        if (globalPhase >= 1.0f) globalPhase -= 1.0f;
        const float lfo       = p.lfoDepth * std::sin (2.0f * kPi * globalPhase);
        const float amountMod = clampF (p.amount + p.amount * lfo, 0.0f, 1.0f);

        const int n = activeVoices;

        // Gain staging: normalise by the number of active stages so stacking
        // several FX doesn't push the sum too hot.
        const int nStages = (p.unison ? 1 : 0) + (p.harmonizer ? 1 : 0)
                          + (p.bandpass ? 1 : 0) + (p.distort ? 1 : 0)
                          + (p.bitcrush ? 1 : 0) + (p.comb ? 1 : 0);
        const float stageNorm = 1.0f / std::sqrt (static_cast<float> (std::max (1, nStages)));

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
            if (p.unison)
            {
                const float mod = v.detuneBase
                                + v.detuneDepth * amountMod
                                  * (0.5f + 0.5f * std::sin (2.0f * kPi * v.lfoPhase));
                detL = read (v, 0, mod);
                detR = read (v, 1, mod);
            }
            else
            {
                detL = inL;
                detR = inR;
            }

            if (p.harmonizer) { detL = harmonize (v, 0, detL); detR = harmonize (v, 1, detR); }
            if (p.bandpass)   { detL = bandpass (v, 0, detL);  detR = bandpass (v, 1, detR); }
            if (p.distort)    { detL = std::tanh (v.drive * detL); detR = std::tanh (v.drive * detR); }
            if (p.bitcrush)   { detL = crush (v, 0, detL);     detR = crush (v, 1, detR); }
            if (p.comb)       { detL += v.combG * read (v, 0, v.combDelay); detR += v.combG * read (v, 1, v.combDelay); }

            accL += v.panL * detL;
            accR += v.panR * detR;
        }

        const float voiceNorm = 1.0f / std::sqrt (static_cast<float> (n));
        const float lfoGain   = 1.0f + 0.25f * lfo;
        outL = accL * voiceNorm * stageNorm * p.mix * lfoGain;
        outR = accR * voiceNorm * stageNorm * p.mix * lfoGain;

        meter = std::max (std::max (std::fabs (outL), std::fabs (outR)), meter * 0.90f);
        level.store (meter);
    }

} // namespace sympathy
