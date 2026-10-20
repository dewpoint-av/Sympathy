#include "ResonatorEngine.h"

namespace sympathy
{
    namespace
    {
        constexpr float kPi = 3.14159265358979323846f;

        float clampF (float v, float lo, float hi)
        {
            return v < lo ? lo : (v > hi ? hi : v);
        }
    }

    // -----------------------------------------------------------------------
    // ModalBank
    // -----------------------------------------------------------------------
    void ModalBank::prepare (double /*sampleRate*/)
    {
        reset();
    }

    void ModalBank::reset()
    {
        for (auto& m : modes)
        {
            m.b  = m.c1 = m.c2 = 0.0f;
            m.y1 = m.y2 = 0.0f;
            m.amp = m.freq = 0.0f;
        }
        modeFreqs.fill (0.0f);
        activeModes.store (0);
    }

    void ModalBank::update (double sampleRate, const Params& p)
    {
        const int materialIndex = std::clamp (p.material, 0, kNumMaterials - 1);
        const Material& m = kMaterials[static_cast<int> (materialIndex)];

        const float fundamental = p.tune * m.fundamentalMult;
        const float nyquist      = static_cast<float> (sampleRate) * 0.49f;

        // Mode frequencies follow f_i = fundamental * i^exponent. An exponent of
        // 1 is a harmonic string; ~2 is a stiff bar; material presets sit in
        // between, and the Inharmonicity knob stretches it further.
        const float exponent = clampF (1.0f + m.inharmBase + p.inharmonicity * 1.2f,
                                       1.0f, 2.5f);

        int numModes = static_cast<int> (std::lround (32.0f * m.density));
        numModes = std::clamp (numModes, 8, kMaxModes);

        // Higher modes lose energy faster. Brightness counteracts that tilt.
        const float highDamp  = 1.0f - p.brightness;
        const float dampScale = m.dampTilt * highDamp * 6.0f;

        int count = 0;
        for (int i = 1; i <= numModes; ++i)
        {
            const float freq = fundamental * std::pow (static_cast<float> (i), exponent);
            if (freq > nyquist)
                break;
            if (freq < 5.0f)
                continue;

            // Per-mode decay time (seconds) -> pole radius.
            float decay_i = p.decay / (1.0f + dampScale * static_cast<float> (i - 1)
                                       / static_cast<float> (numModes));
            decay_i = clampF (decay_i, 0.01f, 120.0f);

            const float r = std::min (std::exp (-1.0f / (decay_i * static_cast<float> (sampleRate))),
                                      0.9995f);
            const float theta = 2.0f * kPi * freq / static_cast<float> (sampleRate);
            const float cosT  = std::cos (theta);

            // Spectral rolloff: low modes carry more energy; brightness lifts the highs.
            const float amp = std::pow (1.0f / static_cast<float> (i),
                                        m.rolloff + (1.0f - p.brightness) * 0.5f);

            Mode& mode   = modes[static_cast<size_t> (count)];
            mode.b       = 1.0f - r;      // normalises the resonant peak to ~unity
            mode.c1      = 2.0f * r * cosT;
            mode.c2      = -r * r;
            mode.amp     = amp;
            mode.freq    = freq;
            modeFreqs[static_cast<size_t> (count)] = freq;
            ++count;
        }

        activeModes.store (count);
        normalize = 0.5f / std::sqrt (static_cast<float> (std::max (1, count)));
    }

    float ModalBank::process (float x)
    {
        float sum = 0.0f;
        const int n = activeModes.load();

        for (int i = 0; i < n; ++i)
        {
            Mode& m = modes[static_cast<size_t> (i)];
            const float y = m.b * x + m.c1 * m.y1 + m.c2 * m.y2;
            m.y2 = m.y1;
            m.y1 = y;
            sum += m.amp * y;
        }

        return sum * normalize;
    }

    // -----------------------------------------------------------------------
    // StringBank
    // -----------------------------------------------------------------------
    void StringBank::prepare (double sampleRate, int /*maxBlockSize*/)
    {
        maxDelaySamples = static_cast<float> (sampleRate / 20.0) + 2048.0f;
        for (auto& s : strings)
            s.delay.prepare (static_cast<int> (maxDelaySamples));
        reset();
    }

    void StringBank::reset()
    {
        for (auto& s : strings)
        {
            s.delay.reset();
            s.feedback = s.damp = s.lp = s.delaySamples = s.gain = 0.0f;
        }
    }

    void StringBank::update (double sampleRate, const Params& p)
    {
        const float nyquist = static_cast<float> (sampleRate) * 0.45f;

        for (int i = 0; i < kNumStrings; ++i)
        {
            const float semitones = (i == 0) ? 0.0f : p.stringTune;
            const float freq = clampF (p.tune * std::pow (2.0f, semitones / 12.0f),
                                      20.0f, nyquist);

            String& s      = strings[static_cast<size_t> (i)];
            s.delaySamples = clampF (static_cast<float> (sampleRate) / freq,
                                     2.0f, maxDelaySamples - 4.0f);
            s.feedback     = std::exp (-1.0f / (std::max (0.05f, p.stringDecay) * freq));
            s.damp         = 1.0f - 0.9f * p.stringDamp;   // 1 = bright, 0.1 = dark
            s.gain         = 0.5f;
        }
    }

    float StringBank::process (float x)
    {
        float sum = 0.0f;

        for (auto& s : strings)
        {
            const float d = s.delay.process (x + s.feedback * s.lp, s.delaySamples);
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
        for (auto& b : body)
            b.prepare (sr);
        for (auto& s : strings)
            s.prepare (sr, maxBlockSize);
        updateAll (sr);
        meter = 0.0f;
        level.store (0.0f);
    }

    void ResonatorEngine::reset()
    {
        for (auto& b : body)
            b.reset();
        for (auto& s : strings)
            s.reset();
        meter = 0.0f;
        level.store (0.0f);
    }

    void ResonatorEngine::setParams (const Params& newParams)
    {
        if (newParams == p)
            return;   // nothing changed -> skip re-computing mode/string coefficients
        p = newParams;
        updateAll (sampleRate);
    }

    void ResonatorEngine::updateAll (double sr)
    {
        for (auto& b : body)
            b.update (sr, p);
        for (auto& s : strings)
            s.update (sr, p);
    }

    void ResonatorEngine::process (const float* inL, const float* inR,
                                   float* outL, float* outR, int numSamples)
    {
        float maxWet = 0.0f;

        for (int i = 0; i < numSamples; ++i)
        {
            const float l = inL[i];
            const float r = inR[i];

            // Excitation: the driven input plus an optional self-noise "bow".
            const float excL = l * p.input + noise[0].next() * p.exciter;
            const float excR = r * p.input + noise[1].next() * p.exciter;

            const float wetL = p.body    * body[0].process (excL)
                             + p.strings * strings[0].process (excL);
            const float wetR = p.body    * body[1].process (excR)
                             + p.strings * strings[1].process (excR);

            const float absL = std::fabs (wetL);
            const float absR = std::fabs (wetR);
            maxWet = std::max (maxWet, std::max (absL, absR));

            outL[i] = ((1.0f - p.mix) * l + p.mix * wetL) * p.output;
            outR[i] = ((1.0f - p.mix) * r + p.mix * wetR) * p.output;
        }

        // Smooth peak meter (audio thread only; the GUI reads the atomic).
        meter = std::max (maxWet, meter * 0.85f);
        level.store (meter);
    }

} // namespace sympathy
