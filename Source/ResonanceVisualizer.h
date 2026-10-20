#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "PluginProcessor.h"
#include "MaterialIcons.h"

#include <vector>

// The animated holographic heart of the interface. It renders the resonant
// body as a radial "corona" of modal spikes around the selected material icon,
// with audio-reactive particles, expanding energy rings and a pair of animated
// Karplus-Strong strings. Every parameter changes what it looks like.
class ResonanceVisualizer : public juce::Component,
                            private juce::Timer
{
public:
    explicit ResonanceVisualizer (SympathyAudioProcessor& p);
    ~ResonanceVisualizer() override;

    void paint (juce::Graphics&) override;
    void timerCallback() override;

private:
    struct Particle
    {
        float x, y, vx, vy;
        float life, maxLife, size;
        float hue;
    };

    struct Ring
    {
        float radius = 0.0f;
        float alpha = 0.0f;
        float speed = 0.0f;
    };

    void update();
    void spawnParticle();
    void spawnRing();
    float nextRandom();           // [0, 1)

    SympathyAudioProcessor& proc;

    std::vector<Particle> particles;
    std::vector<Ring> rings;

    uint32_t rngState = 0x12345678u;

    float time = 0.0f;
    float level = 0.0f;
    float ringAccumulator = 0.0f;

    // Cached parameter snapshot (refreshed every tick).
    int   material = 0;
    float decay = 0.5f;
    float brightness = 0.65f;
    float inharmonicity = 0.35f;
    float bodyLevel = 0.8f;
    float stringsLevel = 0.5f;
    float exciter = 0.0f;
    float mix = 0.5f;

    static constexpr int kMaxParticles = 220;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ResonanceVisualizer)
};
