#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

// The seven resonator types, in the order of the Resonator Type parameter and the
// ResonatorType enum in the DSP engine.
enum class MaterialId : int
{
    Beam     = 0,
    Marimba  = 1,
    String   = 2,
    Membrane = 3,
    Plate    = 4,
    Pipe     = 5,
    Tube     = 6
};

static inline const char* materialName (MaterialId m)
{
    switch (m)
    {
        case MaterialId::Beam:     return "Beam";
        case MaterialId::Marimba:  return "Marimba";
        case MaterialId::String:   return "String";
        case MaterialId::Membrane: return "Membrane";
        case MaterialId::Plate:    return "Plate";
        case MaterialId::Pipe:     return "Pipe";
        case MaterialId::Tube:     return "Tube";
    }
    return "";
}

// A stable hue per type, so each has a visual identity while the surrounding
// energy shimmers iridescently.
static inline float materialBaseHue (MaterialId m)
{
    switch (m)
    {
        case MaterialId::Beam:     return 0.56f;   // blue
        case MaterialId::Marimba:  return 0.07f;   // orange
        case MaterialId::String:   return 0.11f;   // amber
        case MaterialId::Membrane: return 0.98f;   // red
        case MaterialId::Plate:    return 0.84f;   // magenta
        case MaterialId::Pipe:     return 0.33f;   // green
        case MaterialId::Tube:     return 0.50f;   // cyan
    }
    return 0.0f;
}

// Draws a small vector icon for a type into r, stroked with col / lw.
// Call it twice (a thick translucent pass then a thin crisp pass) for a glow.
static inline void drawMaterialIcon (juce::Graphics& g, MaterialId id,
                                     juce::Rectangle<float> r, juce::Colour col, float lw)
{
    const float x  = r.getX();
    const float y  = r.getY();
    const float w  = r.getWidth();
    const float h  = r.getHeight();
    const float cx = r.getCentreX();
    const float cy = r.getCentreY();
    constexpr float kPi = 3.14159265358979323846f;

    g.setColour (col);

    juce::Path p;

    switch (id)
    {
        case MaterialId::Beam:
        {
            // A bar resting on its two nodal supports.
            const juce::Rectangle<float> bar (x + w * 0.04f, cy - h * 0.13f, w * 0.92f, h * 0.26f);
            g.drawRoundedRectangle (bar, 2.5f, lw);
            for (float t : { 0.224f, 0.776f })
            {
                const float sx = x + w * (0.04f + 0.92f * t);
                p.startNewSubPath (sx, bar.getBottom());
                p.lineTo (sx - w * 0.06f, bar.getBottom() + h * 0.2f);
                p.lineTo (sx + w * 0.06f, bar.getBottom() + h * 0.2f);
                p.closeSubPath();
            }
            g.strokePath (p, juce::PathStrokeType (lw * 0.7f));
            break;
        }

        case MaterialId::Marimba:
        {
            // Bars shortening with pitch, above their resonators.
            constexpr int kBars = 5;
            const float gap = w * 0.05f;
            const float bw = (w - gap * (kBars - 1)) / kBars;
            for (int i = 0; i < kBars; ++i)
            {
                const float bh = h * (0.92f - 0.11f * static_cast<float> (i));
                const float bx = x + static_cast<float> (i) * (bw + gap);
                g.drawRoundedRectangle (bx, cy - bh * 0.5f, bw, bh, 2.0f, lw * 0.85f);
            }
            break;
        }

        case MaterialId::String:
        {
            // A string vibrating in its second harmonic.
            const float amp = h * 0.26f;
            p.startNewSubPath (x, cy);
            for (int i = 1; i <= 64; ++i)
            {
                const float t = static_cast<float> (i) / 64.0f;
                p.lineTo (x + t * w, cy + std::sin (t * 2.0f * kPi * 2.0f) * amp);
            }
            g.strokePath (p, juce::PathStrokeType (lw, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
            g.fillEllipse (x - 1.5f, cy - 1.5f, 3.0f, 3.0f);
            g.fillEllipse (x + w - 1.5f, cy - 1.5f, 3.0f, 3.0f);
            break;
        }

        case MaterialId::Membrane:
        {
            // A drum: shell and head.
            const juce::Rectangle<float> shell (cx - w * 0.36f, y + h * 0.30f, w * 0.72f, h * 0.52f);
            g.drawRoundedRectangle (shell, w * 0.08f, lw);
            const juce::Rectangle<float> head (cx - w * 0.36f, y + h * 0.14f, w * 0.72f, h * 0.30f);
            g.drawEllipse (head, lw);
            break;
        }

        case MaterialId::Plate:
        {
            // A plate with a nodal pattern.
            const juce::Rectangle<float> plate = r.reduced (w * 0.08f, h * 0.18f);
            g.drawRoundedRectangle (plate, 2.0f, lw);
            g.drawLine (plate.getCentreX(), plate.getY(), plate.getCentreX(), plate.getBottom(), lw * 0.5f);
            g.drawLine (plate.getX(), plate.getCentreY(), plate.getRight(), plate.getCentreY(), lw * 0.5f);
            break;
        }

        case MaterialId::Pipe:
        {
            // An upright organ pipe with its mouth.
            const juce::Rectangle<float> body (cx - w * 0.17f, y + h * 0.02f, w * 0.34f, h * 0.66f);
            g.drawRoundedRectangle (body, 1.5f, lw);
            p.startNewSubPath (body.getX(), body.getBottom());
            p.lineTo (cx, y + h * 0.96f);
            p.lineTo (body.getRight(), body.getBottom());
            g.strokePath (p, juce::PathStrokeType (lw));
            g.drawLine (body.getX(), y + h * 0.52f, body.getRight(), y + h * 0.52f, lw * 0.6f);
            break;
        }

        case MaterialId::Tube:
        {
            // A horizontal tube, open at both ends.
            const float top = cy - h * 0.2f, bottom = cy + h * 0.2f;
            g.drawLine (x + w * 0.1f, top, x + w * 0.9f, top, lw);
            g.drawLine (x + w * 0.1f, bottom, x + w * 0.9f, bottom, lw);
            g.drawEllipse (x, top, w * 0.2f, bottom - top, lw);
            g.drawEllipse (x + w * 0.8f, top, w * 0.2f, bottom - top, lw);
            break;
        }
    }
}
