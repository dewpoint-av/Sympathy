#include "RefractEngine.h"

namespace sympathy
{
    namespace
    {
        constexpr float kPi = 3.14159265358979323846f;
        constexpr int   kControlInterval = 16;   // samples between control updates

        float clampF (float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }

        // Intervals in semitones. Voices beyond the chord repeat it an octave up.
        constexpr int kChordSizes[kNumChords] = { 2, 2, 3, 4, 5, 3, 4, 4, 5, 5, 3, 3 };
        constexpr float kChords[kNumChords][5] = {
            { 0, 12 },              // Octave
            { 0, 7 },               // 5
            { 0, 3, 7 },            // Minor
            { 0, 3, 7, 10 },        // Minor 7
            { 0, 3, 7, 10, 14 },    // Minor 9
            { 0, 4, 7 },            // Major
            { 0, 4, 7, 10 },        // 7
            { 0, 4, 7, 11 },        // Major 7
            { 0, 4, 7, 11, 14 },    // Major 9
            { 0, 4, 7, 9, 14 },     // 6/9
            { 0, 2, 7 },            // Sus 2
            { 0, 5, 7 }             // Sus 4
        };

        // log(cosh(x)), safe for large |x|.
        float logCosh (float x)
        {
            const float a = std::fabs (x);
            return a > 8.0f ? a - 0.6931472f : std::log (std::cosh (a));
        }

        constexpr float kGermaniumBias = 0.25f;

        // Waveshapers and their antiderivatives. Evaluating the antiderivative's slope
        // between samples (ADAA) removes most aliasing with no oversampling or latency.
        float shape (int algo, float x)
        {
            switch (algo)
            {
                case kAlgoGermanium:  return std::tanh (x + kGermaniumBias) - std::tanh (kGermaniumBias);
                case kAlgoSoftBounce: return x / (1.0f + std::fabs (x));
                case kAlgoSineFolder: return std::sin (x);
                case kAlgoTape:
                default:              return std::tanh (x);
            }
        }

        float shapeIntegral (int algo, float x)
        {
            switch (algo)
            {
                case kAlgoGermanium:  return logCosh (x + kGermaniumBias) - x * std::tanh (kGermaniumBias);
                case kAlgoSoftBounce: return std::fabs (x) - std::log (1.0f + std::fabs (x));
                case kAlgoSineFolder: return -std::cos (x);
                case kAlgoTape:
                default:              return logCosh (x);
            }
        }
    }

    // -----------------------------------------------------------------------
    void RefractEngine::Biquad::setHighpass (float sr, float hz)
    {
        const float w0 = 2.0f * kPi * clampF (hz, 5.0f, sr * 0.45f) / sr;
        const float cw = std::cos (w0), alpha = std::sin (w0) * 0.70710678f;
        const float a0 = 1.0f + alpha;
        b0 = ((1.0f + cw) * 0.5f) / a0;
        b1 = -(1.0f + cw) / a0;
        b2 = b0;
        a1 = (-2.0f * cw) / a0;
        a2 = (1.0f - alpha) / a0;
    }

    void RefractEngine::Biquad::setLowpass (float sr, float hz)
    {
        const float w0 = 2.0f * kPi * clampF (hz, 20.0f, sr * 0.49f) / sr;
        const float cw = std::cos (w0), alpha = std::sin (w0) * 0.70710678f;
        const float a0 = 1.0f + alpha;
        b0 = ((1.0f - cw) * 0.5f) / a0;
        b1 = (1.0f - cw) / a0;
        b2 = b0;
        a1 = (-2.0f * cw) / a0;
        a2 = (1.0f - alpha) / a0;
    }

    void RefractEngine::Svf::set (float sr, float hz, float q)
    {
        g  = std::tan (kPi * clampF (hz, 15.0f, sr * 0.45f) / sr);
        k  = 1.0f / q;
        a1 = 1.0f / (1.0f + g * (g + k));
        a2 = g * a1;
        a3 = g * a2;
    }

    float RefractEngine::Svf::bandpass (int ch, float x)
    {
        const float v3 = x - ic2[ch];
        const float v1 = a1 * ic1[ch] + a2 * v3;
        const float v2 = ic2[ch] + a2 * ic1[ch] + a3 * v3;
        ic1[ch] = 2.0f * v1 - ic1[ch];
        ic2[ch] = 2.0f * v2 - ic2[ch];
        return k * v1;
    }

    // -----------------------------------------------------------------------
    void RefractEngine::prepare (double sr, int /*maxBlockSize*/)
    {
        sampleRate = sr;
        fs = static_cast<float> (sr);

        inputSize = static_cast<int> (sr * 0.05) + 8;
        for (auto& b : input) b.assign (static_cast<size_t> (inputSize), 0.0f);

        combSize = static_cast<int> (sr / 15.0) + 8;
        // Longest spray (1.5 s) plus the pitch-shift window.
        pitchWindow = 0.06f * fs;
        pitchSize = static_cast<int> (sr * 1.5) + static_cast<int> (pitchWindow) * 2 + 8;

        for (auto& v : voices)
        {
            for (auto& b : v.combBuf)  b.assign (static_cast<size_t> (combSize), 0.0f);
            for (auto& b : v.pitchBuf) b.assign (static_cast<size_t> (pitchSize), 0.0f);
        }

        reset();
        update();
    }

    void RefractEngine::reset()
    {
        for (auto& b : input) std::fill (b.begin(), b.end(), 0.0f);
        inputWrite = 0;
        hp.clear();
        lp.clear();
        lfo.reset();
        controlCounter = 0;

        for (auto& v : voices)
        {
            for (auto& b : v.combBuf)  std::fill (b.begin(), b.end(), 0.0f);
            for (auto& b : v.pitchBuf) std::fill (b.begin(), b.end(), 0.0f);
            v.combWrite = v.pitchWrite = 0;
            v.pitchPhase = 0.0f;
            for (int ch = 0; ch < 2; ++ch)
            {
                v.svf.ic1[ch] = v.svf.ic2[ch] = 0.0f;
                v.combLp[ch] = v.holdValue[ch] = v.prevX[ch] = v.prevF[ch] = v.fbState[ch] = 0.0f;
                v.holdPhase[ch] = 1.0f;
            }
        }

        meter = 0.0f;
        level.store (0.0f);
    }

    void RefractEngine::setParams (const RefractParams& np)
    {
        if (np == p)
            return;
        p = np;
        update();
    }

    void RefractEngine::update()
    {
        activeVoices = std::clamp (p.voices, 1, kMaxRefractVoices);
        const int n = activeVoices;

        hp.setHighpass (fs, p.hpHz);
        lp.setLowpass (fs, p.lpHz);

        for (int i = 0; i < n; ++i)
        {
            Voice& v = voices[static_cast<size_t> (i)];
            const float t = n > 1 ? static_cast<float> (i) / static_cast<float> (n - 1) : 0.5f;
            v.s = n > 1 ? t * 2.0f - 1.0f : 0.0f;

            // Short, unequal delays: a unison, not an audible doubling.
            v.baseDelay = (0.004f + 0.009f * std::fabs (v.s)) * fs;
            v.depth = (0.0015f + 0.0035f * t) * fs;
            v.lfoRate = (0.13f + 0.47f * t) / fs;

            // Harmonizer interval.
            const int size = kChordSizes[std::clamp (p.chord, 0, kNumChords - 1)];
            const int c = std::clamp (p.chord, 0, kNumChords - 1);
            const float semis = kChords[c][i % size] + 12.0f * static_cast<float> (i / size);
            v.pitchRatio = std::pow (2.0f, semis / 12.0f);
        }

        // Bit depth: secondary 0 = 16 bits .. 1 = 2 bits.
        crushSteps = std::pow (2.0f, 16.0f - 14.0f * clampF (p.secondary, 0.0f, 1.0f));

        updateVoiceControls();
    }

    // Runs at control rate: the LFO, then each voice's own value of the Primary control.
    void RefractEngine::updateVoiceControls()
    {
        lfo.advance (p.lfo, static_cast<double> (kControlInterval) / sampleRate, hostTime);
        const float l = lfo.value (p.lfo.shape);

        amountNow = clampF (p.amount + l * p.lfoToAmount * 0.5f, 0.0f, 1.0f);
        const float centre = clampF (p.primary + l * p.lfoToPrimary * 0.5f, 0.0f, 1.0f);

        // Amount widens the image as well as spreading the voices.
        widthNow = clampF (p.spread * (0.35f + 0.65f * amountNow), 0.0f, 1.0f);

        const float q = 0.7f + 24.0f * clampF (p.secondary, 0.0f, 1.0f) * clampF (p.secondary, 0.0f, 1.0f);
        const float fb = clampF (p.secondary, 0.0f, 1.0f);

        for (int i = 0; i < activeVoices; ++i)
        {
            Voice& v = voices[static_cast<size_t> (i)];

            const float angle = (v.s * widthNow + 1.0f) * 0.25f * kPi;
            v.panL = std::cos (angle) * 1.41421356f;
            v.panR = std::sin (angle) * 1.41421356f;

            // The core move: centre + Amount * position in the spread.
            v.primary = clampF (centre + amountNow * v.s * 0.5f, 0.0f, 1.0f);

            switch (p.mode)
            {
                case kModeBandpass:
                    v.svf.set (fs, 40.0f * std::pow (300.0f, v.primary), q);
                    break;

                case kModeComb:
                {
                    const float hz = 40.0f * std::pow (200.0f, v.primary);
                    v.combDelay = clampF (fs / hz, 2.0f, static_cast<float> (combSize - 4));
                    // Past unity at the very top: the loop's soft limit holds a steady tone.
                    v.combG = fb * 1.06f;
                    // Keeps a broadband input at about the same level at any resonance.
                    v.combScale = std::sqrt (std::max (1.0f - v.combG * v.combG, 0.02f));
                    break;
                }

                case kModeBitcrush:
                    // Primary 0 = no reduction .. 1 = 1/64 of the sample rate.
                    v.holdInc = std::pow (2.0f, -6.0f * v.primary);
                    break;

                case kModeDistortion:
                    // 0 .. 36 dB. Louder shapes are pulled back so Drive changes tone more than level.
                    v.drive = std::pow (10.0f, 1.8f * v.primary);
                    v.makeup = 1.0f / std::pow (v.drive, 0.55f);
                    break;

                case kModeHarmonizer:
                {
                    // Temporal spread up to 1.5 s. Past the midpoint, feedback brings in the shimmer.
                    const float pos = 0.5f + 0.5f * v.s;
                    v.sprayDelay = v.primary * v.primary * pos * 1.5f * fs;
                    v.sprayFb = clampF ((v.primary - 0.5f) * 1.3f, 0.0f, 0.65f);
                    break;
                }

                default:
                    break;
            }
        }
    }

    // 4-point Hermite interpolation: keeps the highs that linear interpolation dulls.
    float RefractEngine::readInput (int ch, float delay) const
    {
        const auto& b = input[static_cast<size_t> (ch)];
        float rp = static_cast<float> (inputWrite) - delay;
        while (rp < 0.0f) rp += static_cast<float> (inputSize);

        const int i1 = static_cast<int> (rp) % inputSize;
        const float t = rp - std::floor (rp);
        const int i0 = (i1 + inputSize - 1) % inputSize;
        const int i2 = (i1 + 1) % inputSize;
        const int i3 = (i1 + 2) % inputSize;

        const float y0 = b[static_cast<size_t> (i0)], y1 = b[static_cast<size_t> (i1)];
        const float y2 = b[static_cast<size_t> (i2)], y3 = b[static_cast<size_t> (i3)];
        const float c1 = 0.5f * (y2 - y0);
        const float c2 = y0 - 2.5f * y1 + 2.0f * y2 - 0.5f * y3;
        const float c3 = 0.5f * (y3 - y0) + 1.5f * (y1 - y2);
        return ((c3 * t + c2) * t + c1) * t + y1;
    }

    float RefractEngine::processComb (Voice& v, int ch, float x)
    {
        auto& b = v.combBuf[static_cast<size_t> (ch)];

        float rp = static_cast<float> (v.combWrite) - v.combDelay;
        if (rp < 0.0f) rp += static_cast<float> (combSize);
        const int i0 = static_cast<int> (rp);
        const float t = rp - static_cast<float> (i0);
        const int i1 = (i0 + 1) % combSize;
        const float delayed = b[static_cast<size_t> (i0)] * (1.0f - t) + b[static_cast<size_t> (i1)] * t;

        // A soft limit inside the loop lets it self-oscillate without running away.
        v.combLp[ch] += 0.85f * (delayed - v.combLp[ch]);
        const float fed = std::tanh (x * v.combScale + v.combG * v.combLp[ch]);
        b[static_cast<size_t> (v.combWrite)] = fed;
        return fed;
    }

    float RefractEngine::processCrush (Voice& v, int ch, float x)
    {
        v.holdPhase[ch] += v.holdInc;
        if (v.holdPhase[ch] >= 1.0f)
        {
            v.holdPhase[ch] -= std::floor (v.holdPhase[ch]);
            v.holdValue[ch] = x;
        }
        return std::round (v.holdValue[ch] * crushSteps) / crushSteps;
    }

    float RefractEngine::processDistortion (Voice& v, int ch, float x)
    {
        const float in = x * v.drive;
        const float dx = in - v.prevX[ch];
        const float integral = shapeIntegral (p.algorithm, in);
        const float y = std::fabs (dx) > 1.0e-4f
                      ? (integral - v.prevF[ch]) / dx
                      : shape (p.algorithm, 0.5f * (in + v.prevX[ch]));
        v.prevX[ch] = in;
        v.prevF[ch] = integral;
        return y * v.makeup;
    }

    // A two-head pitch shifter. Each head fades in and out on a raised-cosine window and
    // the heads are half a cycle apart, so a head always wraps while its gain is zero.
    float RefractEngine::processHarmonizer (Voice& v, int ch, float x)
    {
        auto& b = v.pitchBuf[static_cast<size_t> (ch)];
        b[static_cast<size_t> (v.pitchWrite)] = x + v.fbState[ch] * v.sprayFb;

        const auto readAt = [&] (float delay)
        {
            float rp = static_cast<float> (v.pitchWrite) - delay;
            if (rp < 0.0f) rp += static_cast<float> (pitchSize);
            const int i0 = static_cast<int> (rp);
            const float t = rp - static_cast<float> (i0);
            const int i1 = (i0 + 1) % pitchSize;
            return b[static_cast<size_t> (i0)] * (1.0f - t) + b[static_cast<size_t> (i1)] * t;
        };

        float y;
        if (v.pitchRatio == 1.0f)
        {
            y = readAt (v.sprayNow + 1.0f);
        }
        else
        {
            const float ph1 = v.pitchPhase;
            float ph2 = ph1 + 0.5f;
            if (ph2 >= 1.0f) ph2 -= 1.0f;

            const float w1 = std::sin (kPi * ph1);
            const float w2 = std::sin (kPi * ph2);

            // Raised-cosine weights (w1^2 + w2^2 = 1). Rising pitch reads with a shrinking delay.
            const float up = v.pitchRatio > 1.0f ? 1.0f - ph1 : ph1;
            const float up2 = v.pitchRatio > 1.0f ? 1.0f - ph2 : ph2;
            y = w1 * w1 * readAt (v.sprayNow + 1.0f + up * pitchWindow)
              + w2 * w2 * readAt (v.sprayNow + 1.0f + up2 * pitchWindow);
        }

        v.fbState[ch] = std::tanh (y);
        return y;
    }

    void RefractEngine::advanceHarmonizer (Voice& v)
    {
        v.pitchWrite = (v.pitchWrite + 1) % pitchSize;
        v.sprayNow += 0.0005f * (v.sprayDelay - v.sprayNow);
        v.pitchPhase += std::fabs (v.pitchRatio - 1.0f) / pitchWindow;
        if (v.pitchPhase >= 1.0f) v.pitchPhase -= 1.0f;
    }

    void RefractEngine::processSample (float inL, float inR, float& outL, float& outR)
    {
        outL = 0.0f;
        outR = 0.0f;

        if (!p.on || p.mix <= 0.0001f)
            return;

        if (--controlCounter <= 0)
        {
            controlCounter = kControlInterval;
            updateVoiceControls();
        }

        input[0][static_cast<size_t> (inputWrite)] = hp.process (0, inL);
        input[1][static_cast<size_t> (inputWrite)] = hp.process (1, inR);
        inputWrite = (inputWrite + 1) % inputSize;

        float accL = 0.0f, accR = 0.0f;

        for (int i = 0; i < activeVoices; ++i)
        {
            Voice& v = voices[static_cast<size_t> (i)];

            v.lfoPhase += v.lfoRate;
            if (v.lfoPhase >= 1.0f) v.lfoPhase -= 1.0f;

            // A lone voice is the undetuned sound, so 1 voice is a pure Mode effect.
            const float d = activeVoices == 1
                          ? v.baseDelay
                          : v.baseDelay + v.depth * amountNow * (0.5f + 0.5f * std::sin (2.0f * kPi * v.lfoPhase));

            float l = readInput (0, d);
            float r = readInput (1, d);

            switch (p.mode)
            {
                case kModeBandpass:
                    l = v.svf.bandpass (0, l);
                    r = v.svf.bandpass (1, r);
                    break;
                case kModeComb:
                    l = processComb (v, 0, l);
                    r = processComb (v, 1, r);
                    v.combWrite = (v.combWrite + 1) % combSize;
                    break;
                case kModeBitcrush:
                    l = processCrush (v, 0, l);
                    r = processCrush (v, 1, r);
                    break;
                case kModeDistortion:
                    l = processDistortion (v, 0, l);
                    r = processDistortion (v, 1, r);
                    break;
                case kModeHarmonizer:
                    l = processHarmonizer (v, 0, l);
                    r = processHarmonizer (v, 1, r);
                    advanceHarmonizer (v);
                    break;
                default:
                    break;
            }

            accL += l * v.panL;
            accR += r * v.panR;
        }

        const float norm = 1.0f / std::sqrt (static_cast<float> (activeVoices));
        outL = lp.process (0, accL * norm) * p.mix;
        outR = lp.process (1, accR * norm) * p.mix;

        meter = std::max (std::max (std::fabs (outL), std::fabs (outR)), meter * 0.90f);
        level.store (meter);
    }

} // namespace sympathy
