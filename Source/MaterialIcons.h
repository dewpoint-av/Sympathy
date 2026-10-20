#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

// The seven resonant "materials", mirroring the order of the Material parameter
// in the processor and the kMaterials table in the DSP engine.
enum class MaterialId : int
{
    String = 0,
    Wood   = 1,
    Metal  = 2,
    Glass  = 3,
    Plate  = 4,
    Body   = 5,
    Synth  = 6
};

static inline const char* materialName (MaterialId m)
{
    switch (m)
    {
        case MaterialId::String: return "String";
        case MaterialId::Wood:   return "Wood";
        case MaterialId::Metal:  return "Metal";
        case MaterialId::Glass:  return "Glass";
        case MaterialId::Plate:  return "Plate";
        case MaterialId::Body:   return "Body";
        case MaterialId::Synth:  return "Synth";
    }
    return "";
}

// A stable hue per material, so each mode has a visual identity while the
// surrounding energy shimmers iridescently.
static inline float materialBaseHue (MaterialId m)
{
    switch (m)
    {
        case MaterialId::String: return 0.09f;   // amber
        case MaterialId::Wood:   return 0.07f;   // orange
        case MaterialId::Metal:  return 0.56f;   // blue
        case MaterialId::Glass:  return 0.50f;   // cyan
        case MaterialId::Plate:  return 0.84f;   // magenta
        case MaterialId::Body:   return 0.98f;   // red
        case MaterialId::Synth:  return 0.33f;   // green
    }
    return 0.0f;
}

// Draws a small vector icon for a material into r, stroked with col / lw.
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
        case MaterialId::String:
        {
            // A string vibrating in its second harmonic.
            const float amp = h * 0.26f;
            p.startNewSubPath (x, cy);
            for (int i = 1; i <= 64; ++i)
            {
                const float t  = static_cast<float> (i) / 64.0f;
                p.lineTo (x + t * w, cy + std::sin (t * 2.0f * kPi * 2.0f) * amp);
            }
            g.strokePath (p, juce::PathStrokeType (lw, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
            g.fillEllipse (x - 1.5f, cy - 1.5f, 3.0f, 3.0f);
            g.fillEllipse (x + w - 1.5f, cy - 1.5f, 3.0f, 3.0f);
            break;
        }

        case MaterialId::Wood:
        {
            // A wooden plank with grain.
            const juce::Rectangle<float> plank = r.reduced (w * 0.08f, h * 0.24f);
            g.drawRoundedRectangle (plank, 4.0f, lw);
            for (float t = 0.28f; t < 0.78f; t += 0.22f)
            {
                juce::Path grain;
                grain.startNewSubPath (plank.getX(), plank.getY() + plank.getHeight() * t);
                grain.quadraticTo (plank.getCentreX(), plank.getY() + plank.getHeight() * (t + 0.14f),
                                   plank.getRight(), plank.getY() + plank.getHeight() * t);
                g.strokePath (grain, juce::PathStrokeType (lw * 0.5f));
            }
            break;
        }

        case MaterialId::Metal:
        {
            // A bell with a clapper.
            p.startNewSubPath (cx - w * 0.32f, y + h * 0.72f);
            p.quadraticTo (cx - w * 0.30f, y + h * 0.16f, cx, y + h * 0.14f);
            p.quadraticTo (cx + w * 0.30f, y + h * 0.16f, cx + w * 0.32f, y + h * 0.72f);
            p.lineTo (cx + w * 0.26f, y + h * 0.82f);
            p.lineTo (cx - w * 0.26f, y + h * 0.82f);
            p.closeSubPath();
            g.strokePath (p, juce::PathStrokeType (lw));
            g.fillEllipse (cx - w * 0.05f, cy + h * 0.20f, w * 0.10f, w * 0.10f);
            break;
        }

        case MaterialId::Glass:
        {
            // A faceted crystal shard.
            p.startNewSubPath (cx, y + h * 0.06f);
            p.lineTo (x + w * 0.80f, y + h * 0.42f);
            p.lineTo (cx, y + h * 0.94f);
            p.lineTo (x + w * 0.20f, y + h * 0.42f);
            p.closeSubPath();
            g.strokePath (p, juce::PathStrokeType (lw, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
            g.drawLine (cx, y + h * 0.06f, cx, y + h * 0.94f, lw * 0.5f);
            g.drawLine (x + w * 0.20f, y + h * 0.42f, x + w * 0.80f, y + h * 0.42f, lw * 0.5f);
            break;
        }

        case MaterialId::Plate:
        {
            // A cymbal / plate with concentric rings.
            const juce::Rectangle<float> plate = r.reduced (w * 0.12f, h * 0.20f);
            g.drawEllipse (plate, lw);
            g.drawEllipse (plate.reduced (plate.getWidth() * 0.22f, plate.getHeight() * 0.22f), lw * 0.6f);
            g.fillEllipse (cx - w * 0.035f, cy - w * 0.035f, w * 0.07f, w * 0.07f);
            break;
        }

        case MaterialId::Body:
        {
            // A drum shell with a membrane head.
            const juce::Rectangle<float> shell (cx - w * 0.28f, y + h * 0.30f, w * 0.56f, h * 0.52f);
            g.drawRoundedRectangle (shell, w * 0.10f, lw);
            const juce::Rectangle<float> head (cx - w * 0.28f, y + h * 0.16f, w * 0.56f, h * 0.26f);
            g.drawEllipse (head, lw);
            g.drawLine (cx - w * 0.42f, cy, cx - w * 0.32f, cy, lw * 0.5f);
            g.drawLine (cx + w * 0.32f, cy, cx + w * 0.42f, cy, lw * 0.5f);
            break;
        }

        case MaterialId::Synth:
        {
            // A square waveform.
            const float yTop = y + h * 0.22f;
            const float yBot = y + h * 0.78f;
            p.startNewSubPath (x, yTop);
            p.lineTo (x + w * 0.22f, yTop);
            p.lineTo (x + w * 0.22f, yBot);
            p.lineTo (x + w * 0.47f, yBot);
            p.lineTo (x + w * 0.47f, yTop);
            p.lineTo (x + w * 0.72f, yTop);
            p.lineTo (x + w * 0.72f, yBot);
            p.lineTo (x + w, yBot);
            g.strokePath (p, juce::PathStrokeType (lw, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
            break;
        }
    }
}
