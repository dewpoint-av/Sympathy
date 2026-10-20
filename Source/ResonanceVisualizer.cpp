#include "ResonanceVisualizer.h"

#include <cmath>

namespace
{
    constexpr float kPi = 3.14159265358979323846f;
}

ResonanceVisualizer::ResonanceVisualizer (SympathyAudioProcessor& p)
    : proc (p)
{
    startTimerHz (60);
}

ResonanceVisualizer::~ResonanceVisualizer()
{
    stopTimer();
}

float ResonanceVisualizer::nextRandom()
{
    rngState ^= rngState << 13;
    rngState ^= rngState >> 17;
    rngState ^= rngState << 5;
    return static_cast<float> (rngState >> 8) * (1.0f / 16777216.0f);
}

void ResonanceVisualizer::timerCallback()
{
    update();
    repaint();
}

void ResonanceVisualizer::update()
{
    time += 1.0f / 60.0f;
    level = proc.getEngine().getLevel();

    auto& state = proc.getParameters();
    material      = juce::roundToInt (state.getRawParameterValue ("material")->load());
    decay         = state.getRawParameterValue ("decay")->load();
    brightness    = state.getRawParameterValue ("brightness")->load();
    inharmonicity = state.getRawParameterValue ("inharmonicity")->load();
    bodyLevel     = state.getRawParameterValue ("body")->load();
    stringsLevel  = state.getRawParameterValue ("strings")->load();
    exciter       = state.getRawParameterValue ("exciter")->load();
    mix           = state.getRawParameterValue ("mix")->load();

    // Activity drives the particle/ring emission. There is always a faint
    // ambient trickle so the visual never looks dead; the Exciter raises it
    // even with a silent input.
    const float activity = juce::jmax (level * 2.0f, exciter * 0.55f, 0.04f);

    // --- particles ---------------------------------------------------------
    const int target = juce::roundToInt (kMaxParticles * activity);
    while (static_cast<int> (particles.size()) < target)
        spawnParticle();

    for (auto& pt : particles)
    {
        pt.life -= 1.0f / 60.0f;
        const float drag = 1.0f - 0.035f * (1.0f - brightness);   // bright = less drag
        pt.vx *= drag;
        pt.vy *= drag;
        pt.x += pt.vx;
        pt.y += pt.vy;
    }

    particles.erase (std::remove_if (particles.begin(), particles.end(),
                                     [] (const Particle& p) { return p.life <= 0.0f; }),
                     particles.end());

    // --- expanding energy rings --------------------------------------------
    ringAccumulator += activity / 60.0f;
    while (ringAccumulator > 0.35f)
    {
        ringAccumulator -= 0.35f;
        spawnRing();
    }

    for (auto& ring : rings)
    {
        ring.radius += ring.speed;
        ring.alpha -= (1.0f / 60.0f) / juce::jmax (0.25f, decay);   // longer decay = slower fade
    }

    rings.erase (std::remove_if (rings.begin(), rings.end(),
                                 [] (const Ring& r) { return r.alpha <= 0.0f; }),
                 rings.end());
}

void ResonanceVisualizer::spawnParticle()
{
    const auto b = getLocalBounds().toFloat();
    const float cx = b.getCentreX();
    const float cy = b.getCentreY();
    const float m  = juce::jmin (b.getWidth(), b.getHeight());

    // Particles radiate outward from the core; inharmonicity scatters them.
    const float angle = nextRandom() * 2.0f * kPi;
    const float spread = 0.6f + inharmonicity * 1.2f;
    const float speed = (0.6f + 1.6f * nextRandom() * (0.5f + brightness)) * m * 0.045f;

    Particle pt;
    pt.x = cx + std::cos (angle) * m * 0.10f;
    pt.y = cy + std::sin (angle) * m * 0.10f;
    pt.vx = std::cos (angle) * speed * spread;
    pt.vy = std::sin (angle) * speed * spread;
    pt.life = (0.4f + 0.6f * nextRandom()) * juce::jmax (0.4f, decay * 0.45f);
    pt.maxLife = pt.life;
    pt.size = 1.5f + 3.5f * nextRandom() * (0.5f + brightness);
    pt.hue = std::fmod (materialBaseHue (static_cast<MaterialId> (juce::jlimit (0, 6, material)))
                        + time * 0.03f + nextRandom() * 0.08f, 1.0f);

    particles.push_back (pt);
}

void ResonanceVisualizer::spawnRing()
{
    const auto b = getLocalBounds().toFloat();
    const float m = juce::jmin (b.getWidth(), b.getHeight());

    Ring ring;
    ring.radius = m * 0.20f;
    ring.alpha  = 0.5f;
    ring.speed  = m * (0.010f + 0.02f * level + 0.01f * (1.0f - brightness));
    rings.push_back (ring);
}

void ResonanceVisualizer::paint (juce::Graphics& g)
{
    const auto b = getLocalBounds().toFloat();
    const float cx = b.getCentreX();
    const float cy = b.getCentreY();
    const float m  = juce::jmin (b.getWidth(), b.getHeight());

    const MaterialId id = static_cast<MaterialId> (juce::jlimit (0, 6, material));
    const float baseHue = materialBaseHue (id);

    // Iridescent accent: the material hue with a slow holographic shimmer.
    const juce::Colour accent = juce::Colour::fromHSV (
        baseHue + 0.04f * std::sin (time * 0.5f), 0.72f, 0.95f, 1.0f);

    const float iconR   = m * 0.15f;
    const float maxSpike = m * 0.36f;
    const float spikeStart = iconR * 1.7f;

    // --- radial resonance corona (one spike per body mode) -----------------
    auto& engine = proc.getEngine();
    const int numModes = engine.getModeCount();
    if (numModes > 0)
    {
        const float fmin = std::log10 (30.0f);
        const float fmax = std::log10 (20000.0f);
        const float exciteGlow = 0.35f + 0.65f * mix;

        for (int i = 0; i < numModes; ++i)
        {
            const float f = engine.getModeFrequency (i);
            const float t = (std::log10 (f) - fmin) / (fmax - fmin);
            const float angle = t * 2.0f * kPi + inharmonicity * 0.35f * std::sin (i * 2.399f);

            const float amp = 1.0f - 0.7f * static_cast<float> (i) / numModes;
            const float len = maxSpike * amp
                            * (0.30f + 0.70f * bodyLevel)
                            * (0.35f + 0.65f * brightness)
                            * (0.30f + 0.90f * level)
                            * exciteGlow;

            const float x0 = cx + std::cos (angle) * spikeStart;
            const float y0 = cy + std::sin (angle) * spikeStart;
            const float x1 = cx + std::cos (angle) * (spikeStart + len);
            const float y1 = cy + std::sin (angle) * (spikeStart + len);

            const float a = juce::jlimit (0.06f, 0.95f, 0.10f + level * 1.6f) * (0.4f + 0.6f * brightness);

            g.setColour (accent.withAlpha (a * 0.35f));
            g.drawLine (x0, y0, x1, y1, 3.5f);
            g.setColour (accent.withAlpha (a));
            g.drawLine (x0, y0, x1, y1, 1.4f);
            g.fillEllipse (x1 - 2.0f, y1 - 2.0f, 4.0f, 4.0f);
        }
    }

    // --- expanding energy rings --------------------------------------------
    for (const auto& ring : rings)
    {
        const float a = juce::jlimit (0.0f, 0.45f, ring.alpha * (0.4f + 0.6f * level));
        g.setColour (accent.withAlpha (a));
        g.drawEllipse (cx - ring.radius, cy - ring.radius, ring.radius * 2.0f, ring.radius * 2.0f, 1.5f);
    }

    // --- two vibrating Karplus-Strong strings ------------------------------
    if (stringsLevel > 0.001f)
    {
        const float amp = m * 0.015f + level * m * 0.05f * stringsLevel
                        + 0.006f * m * std::sin (time * 7.0f) * stringsLevel;

        for (int s = 0; s < 2; ++s)
        {
            const float sy = (s == 0) ? b.getY() + b.getHeight() * 0.20f
                                      : b.getY() + b.getHeight() * 0.80f;
            juce::Path wave;
            wave.startNewSubPath (b.getX(), sy);
            for (int i = 1; i <= 48; ++i)
            {
                const float t = static_cast<float> (i) / 48.0f;
                wave.lineTo (b.getX() + t * b.getWidth(),
                             sy + std::sin (t * 2.0f * kPi * 3.0f + time * 6.0f + s * 1.5f) * amp);
            }
            g.setColour (accent.withAlpha (0.18f + 0.5f * stringsLevel));
            g.strokePath (wave, juce::PathStrokeType (3.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
            g.setColour (accent.withAlpha (0.25f + 0.65f * stringsLevel));
            g.strokePath (wave, juce::PathStrokeType (1.1f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        }
    }

    // --- central material icon with halo ------------------------------------
    const juce::Rectangle<float> iconRect (cx - iconR, cy - iconR, iconR * 2.0f, iconR * 2.0f);

    // Halo glow, brighter as energy rises.
    g.setGradientFill (juce::ColourGradient (accent.withAlpha (0.30f + 0.45f * level),
                                             cx, cy,
                                             juce::Colours::transparentBlack,
                                             cx + iconR * 1.7f, cy + iconR * 1.7f,
                                             true));
    g.fillEllipse (iconRect.expanded (iconR * 1.1f));

    g.setColour (accent.withAlpha (0.9f));
    g.drawEllipse (iconRect, 1.5f);

    // Two-pass icon for a neon glow.
    drawMaterialIcon (g, id, iconRect.reduced (iconR * 0.30f), accent.withAlpha (0.28f), 5.0f);
    drawMaterialIcon (g, id, iconRect.reduced (iconR * 0.30f), accent.withAlpha (0.95f), 2.0f);

    // --- particles (on top) --------------------------------------------------
    // Constellations: join nearby particles with faint blue links.
    const float linkDist  = 55.0f;
    const float linkDist2 = linkDist * linkDist;
    for (size_t i = 0; i < particles.size(); ++i)
    {
        for (size_t j = i + 1; j < particles.size(); ++j)
        {
            const float dx = particles[i].x - particles[j].x;
            const float dy = particles[i].y - particles[j].y;
            const float d2 = dx * dx + dy * dy;
            if (d2 < linkDist2)
            {
                const float a = (1.0f - std::sqrt (d2) / linkDist) * 0.30f;
                g.setColour (juce::Colour (0xff6ea8fe).withAlpha (a));
                g.drawLine (particles[i].x, particles[i].y, particles[j].x, particles[j].y, 0.8f);
            }
        }
    }

    // Particle dots + motion trails (website palette: crimson -> blue -> violet).
    const juce::Colour crimson (0xffff2a2a);
    const juce::Colour blue    (0xff6ea8fe);
    const juce::Colour violet  (0xff7a5cff);
    for (const auto& pt : particles)
    {
        const float a = juce::jlimit (0.0f, 1.0f, pt.life / pt.maxLife);
        const juce::Colour c = pt.hue < 0.5f
            ? crimson.interpolatedWith (blue,   pt.hue * 2.0f).withAlpha (a)
            : blue.interpolatedWith    (violet, (pt.hue - 0.5f) * 2.0f).withAlpha (a);

        g.setColour (c.withAlpha (a * 0.45f));
        const float spd = std::sqrt (pt.vx * pt.vx + pt.vy * pt.vy);
        if (spd > 0.01f)
            g.drawLine (pt.x, pt.y,
                        pt.x - pt.vx / spd * pt.size * 2.5f,
                        pt.y - pt.vy / spd * pt.size * 2.5f, 1.0f);

        g.setColour (c);
        g.fillEllipse (pt.x - pt.size * 0.5f, pt.y - pt.size * 0.5f, pt.size, pt.size);
    }
}
