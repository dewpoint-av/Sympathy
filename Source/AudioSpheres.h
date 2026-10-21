#pragma once

#include <juce_dsp/juce_dsp.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include <array>
#include <atomic>
#include <cmath>
#include <functional>
#include <vector>

// Circular FFT visuals shared by the three plugins. Each sphere takes a stable
// pointer to the engine's recent-audio ring buffer and a getter for its write
// position, then runs its own FFT on the GUI thread (30 Hz).

inline juce::Colour sphereColour (float m)
{
    const juce::Colour blue   (0xff6ea8fe);
    const juce::Colour purple (0xff8b5cf6);
    const juce::Colour red    (0xffff2a2a);
    m = juce::jlimit (0.0f, 1.0f, m);
    m = std::pow (m, 0.4f);   // contrast curve so quiet content isn't all blue
    return (m < 0.5f) ? blue.interpolatedWith  (purple, m * 2.0f)
                      : purple.interpolatedWith (red,    (m - 0.5f) * 2.0f);
}

namespace
{
    constexpr float kSpherePi = 3.14159265358979323846f;

    // Fills out[] with numBins magnitude values (0..1), linearly mapped to FFT
    // bins 1, 3, 5, ... (i.e. up to ~numBins*2 bins).
    inline void computeSpectrum (const std::vector<float>& audio, int writePos,
                                 juce::dsp::FFT& fft, const std::vector<float>& window,
                                 std::vector<float>& fftBuf, float* out, int numBins)
    {
        const int n = static_cast<int> (audio.size());
        const int N = static_cast<int> (window.size());

        std::fill (fftBuf.begin(), fftBuf.end(), 0.0f);
        for (int i = 0; i < N && i < n; ++i)
        {
            int idx = writePos - N + i;
            idx = ((idx % n) + n) % n;
            fftBuf[static_cast<size_t> (i)] = audio[static_cast<size_t> (idx)] * window[static_cast<size_t> (i)];
        }

        fft.performRealOnlyForwardTransform (fftBuf.data(), true);

        const float norm = 1.0f / (static_cast<float> (N) * 0.25f);
        for (int b = 0; b < numBins; ++b)
        {
            const int bin = 1 + b * 2;
            if (bin * 2 + 1 >= N)
            {
                out[b] = 0.0f;
                continue;
            }
            const float re = fftBuf[static_cast<size_t> (bin * 2)];
            const float im = fftBuf[static_cast<size_t> (bin * 2 + 1)];
            out[b] = juce::jlimit (0.0f, 1.0f, std::sqrt (re * re + im * im) * norm);
        }
    }
}

// Live circular spectrum analyzer: radial bars, angle = frequency.
class SpectrumSphere : public juce::Component,
                       private juce::Timer
{
public:
    SpectrumSphere (const std::vector<float>* bufPtr, std::function<int()> getWp)
        : buf (bufPtr), wp (std::move (getWp))
    {
        fft.reset (new juce::dsp::FFT (kOrder));
        window.assign (fftSize, 0.0f);
        for (int i = 0; i < fftSize; ++i)
            window[static_cast<size_t> (i)] = 0.5f * (1.0f - std::cos (2.0f * kSpherePi * i / (fftSize - 1)));
        fftBuf.assign (fftSize * 2, 0.0f);
        bins.fill (0.0f);
        startTimerHz (30);
    }

    ~SpectrumSphere() override { stopTimer(); }

    void timerCallback() override { update(); repaint(); }

    void update()
    {
        if (buf != nullptr)
            computeSpectrum (*buf, wp(), *fft, window, fftBuf, bins.data(), kNumBins);
    }

    void paint (juce::Graphics& g) override
    {
        const auto r = getLocalBounds().toFloat();
        const float cx = r.getCentreX();
        const float cy = r.getCentreY();
        const float R  = juce::jmin (r.getWidth(), r.getHeight()) * 0.5f - 2.0f;
        const float r0 = R * 0.26f;

        for (int b = 0; b < kNumBins; ++b)
        {
            const float ang = static_cast<float> (b) / kNumBins * 2.0f * kSpherePi - 0.5f * kSpherePi;
            const float mag = bins[static_cast<size_t> (b)];
            const float r1  = r0 + (R - r0) * (0.15f + 0.85f * mag);

            g.setColour (sphereColour (mag).withAlpha (0.85f));
            g.drawLine (cx + r0 * std::cos (ang), cy + r0 * std::sin (ang),
                        cx + r1 * std::cos (ang), cy + r1 * std::sin (ang), 2.0f);
        }

        g.setColour (juce::Colour (0xff6ea8fe).withAlpha (0.55f));
        g.drawEllipse (cx - R, cy - R, R * 2.0f, R * 2.0f, 1.5f);
        g.setColour (juce::Colour (0xff6ea8fe).withAlpha (0.25f));
        g.drawEllipse (cx - r0, cy - r0, r0 * 2.0f, r0 * 2.0f, 1.0f);
    }

private:
    const std::vector<float>* buf;
    std::function<int()> wp;
    static constexpr int kOrder   = 9;
    static constexpr int fftSize  = 1 << kOrder;   // 512
    static constexpr int kNumBins = 72;
    std::unique_ptr<juce::dsp::FFT> fft;
    std::vector<float> window;
    std::vector<float> fftBuf;
    std::array<float, kNumBins> bins {};
};

// Circular spectrogram: a rolling magnitude image clipped to a circle.
class SpectrogramSphere : public juce::Component,
                          private juce::Timer
{
public:
    SpectrogramSphere (const std::vector<float>* bufPtr, std::function<int()> getWp)
        : buf (bufPtr), wp (std::move (getWp))
    {
        fft.reset (new juce::dsp::FFT (kOrder));
        window.assign (fftSize, 0.0f);
        for (int i = 0; i < fftSize; ++i)
            window[static_cast<size_t> (i)] = 0.5f * (1.0f - std::cos (2.0f * kSpherePi * i / (fftSize - 1)));
        fftBuf.assign (fftSize * 2, 0.0f);

        image = juce::Image (juce::Image::RGB, kNumBins, kHistory, true);
        startTimerHz (30);
    }

    ~SpectrogramSphere() override { stopTimer(); }

    void timerCallback() override
    {
        if (++frameCount >= kFramesPerSlice)
        {
            frameCount = 0;
            pushSlice();
        }
        repaint();
    }

    void pushSlice()
    {
        if (buf == nullptr)
            return;
        computeSpectrum (*buf, wp(), *fft, window, fftBuf, slice.data(), kNumBins);

        image.moveImageSection (0, 1, 0, 0, kNumBins, kHistory - 1);   // scroll down
        for (int x = 0; x < kNumBins; ++x)
            image.setPixelAt (x, 0, sphereColour (slice[static_cast<size_t> (x)]));
    }

    void paint (juce::Graphics& g) override
    {
        const auto r = getLocalBounds().toFloat();
        const float cx = r.getCentreX();
        const float cy = r.getCentreY();
        const float R  = juce::jmin (r.getWidth(), r.getHeight()) * 0.5f - 2.0f;

        juce::Path clip;
        clip.addEllipse (juce::Rectangle<float> (cx - R, cy - R, R * 2.0f, R * 2.0f));
        g.saveState();
        g.reduceClipRegion (clip);
        g.drawImage (image, juce::Rectangle<float> (cx - R, cy - R, R * 2.0f, R * 2.0f),
                     juce::RectanglePlacement::stretchToFit);
        g.restoreState();

        g.setColour (juce::Colour (0xff6ea8fe).withAlpha (0.55f));
        g.drawEllipse (cx - R, cy - R, R * 2.0f, R * 2.0f, 1.5f);
    }

private:
    const std::vector<float>* buf;
    std::function<int()> wp;
    static constexpr int kOrder   = 9;
    static constexpr int fftSize  = 1 << kOrder;
    static constexpr int kNumBins = 64;
    static constexpr int kHistory = 48;
    static constexpr int kFramesPerSlice = 3;
    std::unique_ptr<juce::dsp::FFT> fft;
    std::vector<float> window;
    std::vector<float> fftBuf;
    std::array<float, kNumBins> slice {};
    juce::Image image;
    int frameCount = 0;
};
