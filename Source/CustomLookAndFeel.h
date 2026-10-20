#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

// A holographic dark theme: near-black background, iridescent cyan accent,
// soft glowing knob arcs and bordered panels. The animated iridescence lives in
// the ResonanceVisualizer; this look-and-feel provides the static chrome.
class SympathyLookAndFeel : public juce::LookAndFeel_V4
{
public:
    SympathyLookAndFeel()
    {
        setColour (juce::ResizableWindow::backgroundColourId, background);
        setColour (juce::Slider::rotarySliderFillColourId,   accent);
        setColour (juce::Slider::rotarySliderOutlineColourId, track);
        setColour (juce::Slider::thumbColourId,              accent);
        setColour (juce::Slider::textBoxTextColourId,        text);
        setColour (juce::Slider::textBoxOutlineColourId,     juce::Colours::transparentBlack);

        setColour (juce::ComboBox::backgroundColourId, panel);
        setColour (juce::ComboBox::outlineColourId,     border);
        setColour (juce::ComboBox::textColourId,        text);
        setColour (juce::ComboBox::arrowColourId,       accent);
        setColour (juce::ComboBox::buttonColourId,      panel);
        setColour (juce::ComboBox::focusedOutlineColourId, accent);

        setColour (juce::Label::textColourId, text);
        setColour (juce::Label::textWhenEditingColourId, text);

        setColour (juce::PopupMenu::backgroundColourId,        panel);
        setColour (juce::PopupMenu::textColourId,              text);
        setColour (juce::PopupMenu::highlightedBackgroundColourId, accent.withAlpha (0.35f));
        setColour (juce::PopupMenu::highlightedTextColourId,   juce::Colours::white);
    }

    // --- palette -----------------------------------------------------------
    static inline const juce::Colour background { 0xff07090d };
    static inline const juce::Colour panel      { 0xff10141c };
    static inline const juce::Colour border     { 0xff1c2331 };
    static inline const juce::Colour track      { 0xff232b3a };
    static inline const juce::Colour accent     { 0xff6ea8fe };   // holographic blue
    static inline const juce::Colour accent2    { 0xff7a5cff };   // iridescent violet
    static inline const juce::Colour text       { 0xffe6eaf2 };
    static inline const juce::Colour muted      { 0xff7d8496 };

    // --- rotary knobs (neon arc + glow) ------------------------------------
    void drawRotarySlider (juce::Graphics& g, int x, int y, int width, int height,
                           float sliderPos, float rotaryStartAngle, float rotaryEndAngle,
                           juce::Slider& slider) override
    {
        const float cx = static_cast<float> (x) + width  * 0.5f;
        const float cy = static_cast<float> (y) + height * 0.5f;
        const float radius = juce::jmin (static_cast<float> (width), static_cast<float> (height)) * 0.5f - 3.0f;
        const float valueAngle = rotaryStartAngle + sliderPos * (rotaryEndAngle - rotaryStartAngle);

        // Track ring
        g.setColour (track);
        g.drawEllipse (cx - radius, cy - radius, radius * 2.0f, radius * 2.0f, 2.5f);

        // Neon glow + core arc
        juce::Path arc;
        arc.addCentredArc (cx, cy, radius, radius, 0.0f, rotaryStartAngle, valueAngle, true);
        g.setColour (accent.withAlpha (0.28f));
        g.strokePath (arc, juce::PathStrokeType (7.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        g.setColour (slider.isMouseOverOrDragging() ? juce::Colours::white : accent);
        g.strokePath (arc, juce::PathStrokeType (2.5f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

        // Pointer
        g.setColour (text);
        juce::Line<float> pointer (cx, cy,
                                   cx + radius * 0.58f * std::cos (valueAngle),
                                   cy + radius * 0.58f * std::sin (valueAngle));
        g.drawLine (pointer, 2.0f);

        // Centre dot
        g.setColour (accent);
        g.fillEllipse (cx - 2.0f, cy - 2.0f, 4.0f, 4.0f);
    }

    // --- combo box ---------------------------------------------------------
    void drawComboBox (juce::Graphics& g, int width, int height, bool /*isButtonDown*/,
                       int /*buttonX*/, int /*buttonY*/, int /*buttonW*/, int /*buttonH*/,
                       juce::ComboBox& box) override
    {
        const juce::Rectangle<int> r (0, 0, width, height);
        g.setColour (panel);
        g.fillRoundedRectangle (r.toFloat(), 6.0f);
        g.setColour (box.hasKeyboardFocus (true) ? accent : border);
        g.drawRoundedRectangle (r.toFloat().reduced (0.5f), 6.0f, 1.5f);

        const float cx = static_cast<float> (width)  - 16.0f;
        const float cy = static_cast<float> (height) * 0.5f;
        juce::Path arrow;
        arrow.addTriangle (cx - 4.0f, cy - 3.0f, cx + 4.0f, cy - 3.0f, cx, cy + 3.0f);
        g.setColour (accent);
        g.fillPath (arrow);
    }

    void positionComboBoxText (juce::ComboBox& box, juce::Label& label) override
    {
        label.setBounds (6, 1, box.getWidth() - 26, box.getHeight() - 2);
        label.setFont (getComboBoxFont (box));
    }

    juce::Font getComboBoxFont (juce::ComboBox&) override
    {
        return juce::Font (14.0f, juce::Font::bold);
    }

    juce::Font getLabelFont (juce::Label&) override
    {
        return juce::Font (11.0f, juce::Font::plain);
    }

    static void drawPanelBackground (juce::Graphics& g, juce::Rectangle<int> bounds)
    {
        g.setColour (panel);
        g.fillRoundedRectangle (bounds.toFloat(), 10.0f);
        g.setColour (border);
        g.drawRoundedRectangle (bounds.toFloat().reduced (0.5f), 10.0f, 1.0f);
    }
};
