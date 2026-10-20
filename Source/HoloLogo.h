#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <cmath>

#include "BinaryData.h"

// Monument Extended typefaces, embedded via juce_add_binary_data.
inline juce::Typeface::Ptr monumentTypeface (bool bold)
{
    static juce::Typeface::Ptr regular = juce::Typeface::createSystemTypefaceFor (
        BinaryData::MonumentExtendedRegular_otf, BinaryData::MonumentExtendedRegular_otfSize);
    static juce::Typeface::Ptr ultrabold = juce::Typeface::createSystemTypefaceFor (
        BinaryData::MonumentExtendedUltrabold_otf, BinaryData::MonumentExtendedUltrabold_otfSize);
    return bold ? ultrabold : regular;
}

inline juce::Font monumentFont (float height, bool bold = true)
{
    return juce::Font (monumentTypeface (bold)).withHeight (height);
}

// An animated, holographic title rendered in Monument Extended. The text is
// filled with a slowly shifting iridescent gradient plus a soft glow.
class HoloTitle : public juce::Component,
                  private juce::Timer
{
public:
    HoloTitle (const juce::String& titleText, float height, bool useBold = true)
        : text (titleText), fontHeight (height), bold (useBold)
    {
        startTimerHz (30);
    }

    ~HoloTitle() override { stopTimer(); }

    void timerCallback() override
    {
        phase += 0.006f;
        if (phase > 1.0f)
            phase -= 1.0f;
        repaint();
    }

    void paint (juce::Graphics& g) override
    {
        const auto b = getLocalBounds().toFloat();

        juce::GlyphArrangement glyphs;
        glyphs.addFittedText (monumentFont (fontHeight, bold), text,
                              b.getX(), b.getY(), b.getWidth(), b.getHeight(),
                              juce::Justification::centredLeft, 1, 0.9f);

        juce::Path p;
        glyphs.createPath (p);

        const float ph = phase;
        const juce::Colour a = juce::Colour::fromHSV (std::fmod (ph, 1.0f), 0.72f, 1.0f, 1.0f);
        const juce::Colour c = juce::Colour::fromHSV (std::fmod (ph + 0.45f, 1.0f), 0.70f, 1.0f, 1.0f);

        // Soft glow.
        g.setColour (a.withAlpha (0.30f));
        g.strokePath (p, juce::PathStrokeType (7.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

        // Iridescent gradient fill.
        g.setGradientFill (juce::ColourGradient (a, b.getX(), b.getY(), c, b.getRight(), b.getBottom(), false));
        g.fillPath (p);
    }

private:
    juce::String text;
    float fontHeight;
    bool bold;
    float phase = 0.0f;
};
