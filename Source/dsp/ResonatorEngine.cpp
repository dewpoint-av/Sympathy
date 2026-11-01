#include "ResonatorEngine.h"

namespace sympathy
{
    namespace
    {
        constexpr float kPi = 3.14159265358979323846f;
        constexpr int   kControlInterval = 32;    // samples between pitch / LFO updates
        constexpr float kMaxSpreadSemis = 0.5f;   // each side, at Spread 100%

        float clampF (float v, float lo, float hi)
        {
            return v < lo ? lo : (v > hi ? hi : v);
        }
    }

    // -----------------------------------------------------------------------
    // StringBank
    // -----------------------------------------------------------------------
    void StringBank::prepare (double sampleRate, int /*maxBlockSize*/)
    {
        fs = sampleRate;
        // 10 Hz is the lowest pitch after MIDI and LFO.
        const int size = static_cast<int> (sampleRate / 10.0) + 16;
        for (auto& s : strings)
            s.delay.prepare (size);
        reset();
    }

    void StringBank::reset()
    {
        for (auto& s : strings)
        {
            s.delay.reset();
            s.lp = 0.0f;
        }
    }

    void StringBank::update (double sampleRate, const Params& p)
    {
        const float sr = static_cast<float> (sampleRate);

        for (int i = 0; i < kNumStrings; ++i)
        {
            const float semitones = (i == 0) ? 0.0f : p.stringTune;
            const float freq = clampF (p.model.tune * std::pow (2.0f, semitones / 12.0f), 20.0f, sr * 0.45f);

            String& s  = strings[static_cast<size_t> (i)];
            s.damp     = 1.0f - 0.9f * p.stringDamp;   // 1 = bright, 0.1 = dark
            // The damping filter adds (1 - c) / c samples, which would flatten the pitch.
            s.baseDelay = sr / freq;
            s.feedback  = std::pow (10.0f, -3.0f / (std::max (0.05f, p.stringDecay) * freq));
            s.gain      = 0.5f;
        }
        updateDelays();
    }

    void StringBank::updateDelays()
    {
        for (auto& s : strings)
        {
            const float filter = (1.0f - s.damp) / s.damp + 1.0f;   // +1: feedback is a sample late
            s.delaySamples = clampF (s.baseDelay / pitch - filter, 1.0f,
                                     static_cast<float> (s.delay.capacity() - 4));
            s.feedbackDamped = s.feedback;
            if (dampTau > 0.0f)
                s.feedbackDamped *= std::exp (-s.delaySamples / (dampTau * static_cast<float> (fs)));
        }
    }

    void StringBank::setPitch (float ratio)
    {
        if (ratio == pitch) return;
        pitch = ratio;
        updateDelays();
    }

    void StringBank::damp (float secondsTimeConstant)
    {
        if (secondsTimeConstant == dampTau) return;
        dampTau = secondsTimeConstant;
        updateDelays();
    }

    float StringBank::process (float x)
    {
        float sum = 0.0f;

        for (auto& s : strings)
        {
            const float d = s.delay.process (x + s.feedbackDamped * s.lp, s.delaySamples);
            s.lp += s.damp * (d - s.lp);
            sum  += s.gain * d;
        }

        return sum;
    }

    // -----------------------------------------------------------------------
    // ResonatorEngine
    // -----------------------------------------------------------------------
    void ResonatorEngine::prepare (double sr, int maxBlockSize)
    {
        sampleRate = sr;
        for (auto& t : tube)
            t.prepare (sr);
        for (auto& s : strings)
            s.prepare (sr, maxBlockSize);
        refract.prepare (sr, maxBlockSize);
        limiter.prepare (sr);
        agc.prepare (sr);
        vizInputBuf.assign (kVizSize, 0.0f);
        vizWrite.store (0);

        mixCoef = 1.0f - std::exp (-1.0f / (0.01f * static_cast<float> (sr)));
        mixNow = p.mix;

        objectBuilt = false;
        rebuildObject();
        rebuildFilter();
        for (auto& s : strings)
            s.update (sr, p);

        meter = 0.0f;
        level.store (0.0f);
    }

    void ResonatorEngine::reset()
    {
        for (auto& m : modal)
            m.reset();
        for (auto& t : tube)
            t.reset();
        for (auto& s : strings)
            s.reset();
        refract.reset();
        limiter.reset();
        agc.reset();
        lfo.reset();
        controlCounter = 0;
        bpZ1[0] = bpZ1[1] = bpZ2[0] = bpZ2[1] = 0.0f;
        vizWrite.store (0);
        meter = 0.0f;
        level.store (0.0f);
    }

    void ResonatorEngine::setParams (const Params& newParams)
    {
        if (newParams == p)
            return;   // nothing changed -> skip re-computing coefficients

        const bool stringsChanged = newParams.model.tune != p.model.tune
                                 || newParams.stringTune != p.stringTune
                                 || newParams.stringDamp != p.stringDamp
                                 || newParams.stringDecay != p.stringDecay;
        const bool filterChanged = newParams.filterHz != p.filterHz || newParams.filterBw != p.filterBw;
        p = newParams;

        rebuildObject();
        if (filterChanged)
            rebuildFilter();
        if (stringsChanged)
            for (auto& s : strings)
                s.update (sampleRate, p);
    }

    void ResonatorEngine::setRefractParams (const RefractParams& newParams)
    {
        refract.setParams (newParams);
    }

    void ResonatorEngine::setHostTime (const HostTime& t)
    {
        hostTime = t;
        refract.setHostTime (t);
    }

    void ResonatorEngine::rebuildObject()
    {
        if (objectBuilt && p.model == builtModel && p.posL == builtPosL && p.posR == builtPosR)
            return;

        const bool waveguide = isWaveguide (p.model.type);
        if (objectBuilt && waveguide != isWaveguide (builtModel.type))
        {
            // Switching engine family: the other one must not ring on unheard, then resume.
            for (auto& m : modal) m.reset();
            for (auto& t : tube)  t.reset();
        }

        builtModel = p.model;
        builtPosL = p.posL;
        builtPosR = p.posR;
        objectBuilt = true;

        for (int ch = 0; ch < kNumChannels; ++ch)
        {
            ModelParams m = p.model;
            m.pos = ch == 0 ? p.posL : p.posR;
            if (waveguide)
                tube[ch].rebuild (m);
            else
                modal[ch].rebuild (sampleRate, m);
        }

        const int n = waveguide ? tube[0].count() : modal[0].count();
        for (int i = 0; i < n; ++i)
            modeFreqs[static_cast<size_t> (i)].store (waveguide ? tube[0].frequency (i) : modal[0].frequency (i));
        modeCount.store (n);
    }

    void ResonatorEngine::rebuildFilter()
    {
        const float fs = static_cast<float> (sampleRate);
        const float f = clampF (p.filterHz, 20.0f, fs * 0.45f);
        const float w0 = 2.0f * kPi * f / fs;
        // RBJ constant 0 dB peak band-pass, bandwidth in octaves.
        const float alpha = std::sin (w0) * std::sinh (0.34657359f * clampF (p.filterBw, 0.1f, 8.0f) * w0 / std::sin (w0));
        const float a0 = 1.0f + alpha;
        bpB0 = alpha / a0;
        bpB2 = -alpha / a0;
        bpA1 = -2.0f * std::cos (w0) / a0;
        bpA2 = (1.0f - alpha) / a0;
    }

    // Pitch, LFO and note-off damping, updated every kControlInterval samples.
    void ResonatorEngine::updateControl()
    {
        const double seconds = static_cast<double> (kControlInterval) / sampleRate;
        if (p.lfoSemis > 0.0f)
            lfo.advance (p.lfo, seconds, hostTime);

        float baseSemis = 0.0f;
        if (p.midiFrequency && midi.hasPlayed)
        {
            const float target = 440.0f * std::pow (2.0f, (static_cast<float> (midi.note) - 69.0f + p.midiTranspose
                                                           + p.midiFine * 0.01f + midi.bend * p.pbRange) / 12.0f);
            baseSemis = 12.0f * std::log2 (target / std::max (p.model.tune, 1.0f));
        }

        const bool waveguide = isWaveguide (p.model.type);
        const float spreadSemis = p.spread * kMaxSpreadSemis;

        for (int ch = 0; ch < kNumChannels; ++ch)
        {
            const float mod = p.lfoSemis > 0.0f
                            ? p.lfoSemis * lfo.value (p.lfo.shape, ch == 0 ? 0.0 : static_cast<double> (p.lfoPhase))
                            : 0.0f;
            const float side = ch == 0 ? spreadSemis : -spreadSemis;
            const float ratio = std::pow (2.0f, (baseSemis + mod + side) / 12.0f);

            if (waveguide)
                tube[ch].setPitch (ratio);
            else
                modal[ch].setPitch (ratio);
            strings[ch].setPitch (ratio);
        }

        // Off Decay: released notes damp the object. 1% is slow, 100% is about 5 ms.
        const bool damping = p.midiFrequency && p.offDecay > 0.0f && midi.hasPlayed && !midi.anyHeld;
        float tau = 0.0f;
        if (damping)
            tau = 0.005f * std::pow (400.0f, 1.0f - p.offDecay);

        for (int ch = 0; ch < kNumChannels; ++ch)
        {
            if (waveguide)
                tube[ch].damp (tau);
            else if (damping)
                modal[ch].damp (std::exp (-static_cast<float> (seconds) / tau));
            strings[ch].damp (tau);
        }
    }

    void ResonatorEngine::process (const float* inL, const float* inR,
                                   float* outL, float* outR, int numSamples)
    {
        const bool waveguide = isWaveguide (p.model.type);
        const float bleedAmt = waveguide ? 0.0f : p.bleed;

        float maxWet = 0.0f;

        for (int i = 0; i < numSamples; ++i)
        {
            if (--controlCounter <= 0)
            {
                controlCounter = kControlInterval;
                updateControl();
            }

            const float l = inL[i];
            const float r = inR[i];

            int w = vizWrite.load();
            vizInputBuf[static_cast<size_t> (w)] = 0.5f * (l + r);
            vizWrite.store ((w + 1) % kVizSize);

            mixNow += mixCoef * (p.mix - mixNow);

            float wetCh[kNumChannels];
            for (int ch = 0; ch < kNumChannels; ++ch)
            {
                // Mix scales what enters the object, so existing tails ring on.
                float exc = ((ch == 0 ? l : r) * p.input + noise[ch].next() * p.exciter) * mixNow;

                if (p.filterOn)
                {
                    const float y = bpB0 * exc + bpZ1[ch];
                    bpZ1[ch] = -bpA1 * y + bpZ2[ch];
                    bpZ2[ch] = bpB2 * exc - bpA2 * y;
                    exc = y;
                }

                const float object = waveguide ? tube[ch].process (exc) : modal[ch].process (exc);
                wetCh[ch] = p.body * object + p.strings * strings[ch].process (exc) + bleedAmt * exc;
            }

            // Stereo width, as mid/side.
            const float mid  = 0.5f * (wetCh[0] + wetCh[1]);
            const float side = 0.5f * (wetCh[0] - wetCh[1]) * p.width;
            const float wetL = mid + side;
            const float wetR = mid - side;

            // Refraction takes the resonator (the default) or the untouched input.
            float refL, refR;
            if (p.refractSource == kSourceInput)
                refract.processSample (l, r, refL, refR);
            else
                refract.processSample (wetL, wetR, refL, refR);

            maxWet = std::max (maxWet, std::max (std::fabs (wetL), std::fabs (wetR)));

            // Auto gain: match the processed loudness to the dry input. The wet level
            // already includes Mix, so it's judged as if Mix were fully up.
            float makeup = 1.0f;
            if (p.autoGain)
            {
                const float dryMag = std::max (std::fabs (l), std::fabs (r));
                const float feed = std::max (mixNow, 0.05f);
                const float procMag = std::max (std::fabs (wetL / feed + refL), std::fabs (wetR / feed + refR));
                makeup = agc.process (dryMag, procMag);
            }

            const float wetMixL = (wetL + refL) * makeup;
            const float wetMixR = (wetR + refR) * makeup;
            float lo = ((1.0f - mixNow) * l + wetMixL) * p.output;
            float ro = ((1.0f - mixNow) * r + wetMixR) * p.output;
            limiter.process (lo, ro);
            outL[i] = lo;
            outR[i] = ro;
        }

        // Smooth peak meter (audio thread only; the GUI reads the atomic).
        meter = std::max (maxWet, meter * 0.85f);
        level.store (meter);
    }

} // namespace sympathy
