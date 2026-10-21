#include "PluginEditor.h"

#include "ResonanceVisualizer.h"
#include "HoloLogo.h"
#include "AudioSpheres.h"

#include <cmath>
#include <functional>

namespace
{
    // ---- value formatting --------------------------------------------------
    juce::String hzText   (float v) { return juce::String (v, 0)  + " Hz"; }
    juce::String stText   (float v) { return (v >= 0.0f ? "+" : "") + juce::String (v, 0) + " st"; }
    juce::String secText  (float v) { return juce::String (v, 2)  + " s"; }
    juce::String pctText  (float v) { return juce::String (v * 100.0f, 0) + " %"; }
    juce::String gainText (float v) { return juce::String (v, 2)  + "x"; }
    juce::String lfoText  (float v) { return juce::String (v, 2)  + " Hz"; }
    juce::String voiceText(float v) { return juce::String (v, 0)  + "x"; }

    // ---- a labelled rotary knob bound to a parameter -----------------------
    class Knob : public juce::Component,
                 private juce::AudioProcessorValueTreeState::Listener,
                 private juce::AsyncUpdater
    {
    public:
        Knob (juce::AudioProcessorValueTreeState& state,
              const juce::String& paramId,
              const juce::String& name,
              std::function<juce::String (float)> textFn)
            : stateRef (state), param (paramId), textFn (textFn)
        {
            slider.setSliderStyle (juce::Slider::Rotary);
            slider.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
            slider.setRotaryParameters ({ juce::MathConstants<float>::pi * 1.25f,
                                          juce::MathConstants<float>::pi * 2.75f,
                                          true });

            addAndMakeVisible (slider);

            nameLabel.setText (name, juce::dontSendNotification);
            nameLabel.setFont (juce::Font (11.0f, juce::Font::plain));
            nameLabel.setColour (juce::Label::textColourId, SympathyLookAndFeel::muted);
            nameLabel.setJustificationType (juce::Justification::centred);
            addAndMakeVisible (nameLabel);

            valueLabel.setFont (juce::Font (11.0f, juce::Font::bold));
            valueLabel.setColour (juce::Label::textColourId, SympathyLookAndFeel::text);
            valueLabel.setJustificationType (juce::Justification::centred);
            addAndMakeVisible (valueLabel);

            attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
                state, paramId, slider);

            // Keep the value label in sync with the parameter at all times —
            // both user drags and host automation / preset loads.
            stateRef.addParameterListener (param, this);
            refreshLabel();
        }

        ~Knob() override { stateRef.removeParameterListener (param, this); }

        // parameterChanged can fire on the audio thread (host automation) or at
        // mouse-move rate during a drag — marshal + coalesce the label update to
        // the message thread so we never touch the Label off-thread.
        void parameterChanged (const juce::String&, float) override { triggerAsyncUpdate(); }

        void handleAsyncUpdate() override { refreshLabel(); }

        void resized() override
        {
            auto b = getLocalBounds();
            nameLabel.setBounds (b.removeFromTop (18));
            valueLabel.setBounds (b.removeFromBottom (18));
            slider.setBounds (b);
        }

    private:
        void refreshLabel()
        {
            if (auto* p = dynamic_cast<juce::RangedAudioParameter*> (stateRef.getParameter (param)))
                if (textFn)
                    valueLabel.setText (textFn (p->getNormalisableRange().convertFrom0to1 (p->getValue())),
                                        juce::dontSendNotification);
        }

        juce::AudioProcessorValueTreeState& stateRef;
        juce::String param;
        std::function<juce::String (float)> textFn;
        juce::Slider slider;
        juce::Label nameLabel;
        juce::Label valueLabel;
        std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment;
    };

    // ---- a titled panel that draws its own background ----------------------
    class Panel : public juce::Component
    {
    public:
        explicit Panel (const juce::String& title)
        {
            titleLabel.setText (title, juce::dontSendNotification);
            titleLabel.setFont (juce::Font (14.0f, juce::Font::bold));
            titleLabel.setColour (juce::Label::textColourId, SympathyLookAndFeel::accent);
            titleLabel.setJustificationType (juce::Justification::centred);
            addAndMakeVisible (titleLabel);
        }

        void paint (juce::Graphics& g) override
        {
            SympathyLookAndFeel::drawPanelBackground (g, getLocalBounds());
        }

        void resized() override
        {
            titleLabel.setBounds (0, 8, getWidth(), 18);
        }

        juce::Label titleLabel;
    };

    // ---- a material icon selector button -----------------------------------
    class MaterialIconButton : public juce::Component
    {
    public:
        MaterialIconButton (MaterialId m, juce::AudioProcessorValueTreeState& st)
            : id (m), state (st)
        {
        }

        void paint (juce::Graphics& g) override
        {
            const bool sel = selected();
            auto r = getLocalBounds().toFloat();
            auto iconArea = r.removeFromTop (r.getHeight() * 0.70f);
            auto labelArea = r;

            const juce::Colour col = sel
                ? juce::Colour::fromHSV (materialBaseHue (id), 0.72f, 0.95f, 1.0f)
                : SympathyLookAndFeel::muted;

            if (sel)
            {
                g.setColour (col.withAlpha (0.14f));
                g.fillRoundedRectangle (iconArea.reduced (2.0f), 9.0f);
                g.setColour (col.withAlpha (0.6f));
                g.drawRoundedRectangle (iconArea.reduced (2.5f), 9.0f, 1.0f);
            }

            // Two-pass icon for a subtle glow when selected.
            if (sel)
                drawMaterialIcon (g, id, iconArea.reduced (iconArea.getWidth() * 0.16f),
                                  col.withAlpha (0.30f), 4.0f);
            drawMaterialIcon (g, id, iconArea.reduced (iconArea.getWidth() * 0.16f),
                              col, sel ? 2.2f : 1.6f);

            g.setColour (sel ? SympathyLookAndFeel::text : SympathyLookAndFeel::muted);
            g.setFont (juce::Font (10.5f, sel ? juce::Font::bold : juce::Font::plain));
            g.drawText (materialName (id), labelArea, juce::Justification::centred, false);
        }

        void mouseDown (const juce::MouseEvent&) override
        {
            state.getParameter ("material")->setValueNotifyingHost (
                static_cast<float> (static_cast<int> (id)) / 6.0f);
        }

        bool selected() const
        {
            const int idx = juce::roundToInt (state.getRawParameterValue ("material")->load());
            return idx == static_cast<int> (id);
        }

    private:
        MaterialId id;
        juce::AudioProcessorValueTreeState& state;
    };

    // ---- the Refract power trigger -----------------------------------------
    class RefractPowerButton : public juce::Component
    {
    public:
        explicit RefractPowerButton (juce::AudioProcessorValueTreeState& st) : state (st) {}

        void paint (juce::Graphics& g) override
        {
            const bool on = isOn();
            auto r = getLocalBounds().toFloat().reduced (2.0f);
            const juce::Colour col = on ? SympathyLookAndFeel::accent : SympathyLookAndFeel::muted;

            if (on)
            {
                g.setColour (col.withAlpha (0.18f));
                g.fillRoundedRectangle (r, 10.0f);
                g.setColour (col.withAlpha (0.7f));
                g.drawRoundedRectangle (r.reduced (1.0f), 10.0f, 1.5f);
            }
            else
            {
                g.setColour (SympathyLookAndFeel::panel);
                g.fillRoundedRectangle (r, 10.0f);
                g.setColour (SympathyLookAndFeel::border);
                g.drawRoundedRectangle (r.reduced (0.5f), 10.0f, 1.0f);
            }

            const float cx = r.getCentreX();
            const float cy = r.getCentreY() - 8.0f;
            g.setColour (col);
            g.drawEllipse (cx - 11.0f, cy - 11.0f, 22.0f, 22.0f, 2.0f);
            g.drawLine (cx, cy - 7.0f, cx, cy + 7.0f, 2.0f);

            g.setFont (monumentFont (10.0f, false));
            g.setColour (on ? SympathyLookAndFeel::text : SympathyLookAndFeel::muted);
            g.drawText (on ? "ACTIVE" : "OFF",
                        juce::Rectangle<float> (r.getX(), cy + 13.0f, r.getWidth(), r.getBottom() - cy - 13.0f),
                        juce::Justification::centredTop, false);
        }

        void mouseDown (const juce::MouseEvent&) override
        {
            if (auto* p = state.getParameter ("refractOn"))
                p->setValueNotifyingHost (isOn() ? 0.0f : 1.0f);
        }

        bool isOn() const
        {
            return state.getRawParameterValue ("refractOn")->load() > 0.5f;
        }

    private:
        juce::AudioProcessorValueTreeState& state;
    };

    // ---- a Refraction stage toggle button ----------------------------------
    class RefractFxButton : public juce::Component
    {
    public:
        RefractFxButton (const juce::String& paramId, const juce::String& label,
                         juce::AudioProcessorValueTreeState& st)
            : param (paramId), name (label), state (st)
        {
        }

        void paint (juce::Graphics& g) override
        {
            const bool sel = selected();
            auto r = getLocalBounds().toFloat().reduced (1.5f);
            const juce::Colour col = sel ? SympathyLookAndFeel::accent : SympathyLookAndFeel::muted;

            if (sel)
            {
                g.setColour (col.withAlpha (0.15f));
                g.fillRoundedRectangle (r, 7.0f);
                g.setColour (col.withAlpha (0.65f));
                g.drawRoundedRectangle (r.reduced (0.5f), 7.0f, 1.5f);
            }
            else
            {
                g.setColour (SympathyLookAndFeel::panel);
                g.fillRoundedRectangle (r, 7.0f);
                g.setColour (SympathyLookAndFeel::border);
                g.drawRoundedRectangle (r.reduced (0.5f), 7.0f, 1.0f);
            }

            g.setFont (juce::Font (10.0f, sel ? juce::Font::bold : juce::Font::plain));
            g.setColour (sel ? SympathyLookAndFeel::text : SympathyLookAndFeel::muted);
            g.drawText (name, r, juce::Justification::centred, false);
        }

        void mouseDown (const juce::MouseEvent&) override
        {
            if (auto* p = state.getParameter (param))
                p->setValueNotifyingHost (selected() ? 0.0f : 1.0f);
        }

        bool selected() const
        {
            return state.getRawParameterValue (param)->load() > 0.5f;
        }

    private:
        juce::String param;
        juce::String name;
        juce::AudioProcessorValueTreeState& state;
    };

    // ---- animated dispersion orb -------------------------------------------
    class RefractVisualizer : public juce::Component,
                              private juce::Timer
    {
    public:
        explicit RefractVisualizer (juce::AudioProcessorValueTreeState& st) : state (st)
        {
            startTimerHz (30);
        }

        ~RefractVisualizer() override { stopTimer(); }

        void paint (juce::Graphics& g) override
        {
            const bool  on     = state.getRawParameterValue ("refractOn")->load() > 0.5f;
            const float amount = state.getRawParameterValue ("refractAmount")->load();
            const float spread = state.getRawParameterValue ("refractSpread")->load();
            const int   voices = juce::roundToInt (state.getRawParameterValue ("refractVoices")->load());

            auto r = getLocalBounds().toFloat().reduced (3.0f);
            const float cx = r.getCentreX();
            const float cy = r.getCentreY();
            const float baseR = juce::jmin (r.getWidth(), r.getHeight()) * 0.5f - 5.0f;
            const float alpha = on ? 0.95f : 0.22f;

            g.setColour (SympathyLookAndFeel::border.withAlpha (alpha));
            g.drawEllipse (cx - baseR, cy - baseR, baseR * 2.0f, baseR * 2.0f, 1.0f);

            const int n = juce::jmax (2, voices);
            std::vector<juce::Point<float>> pts;
            pts.reserve (static_cast<size_t> (n));

            const float pulse = 1.0f + 0.14f * std::sin (phase);
            const float arcStart = 3.14159265358979323846f * 0.30f;
            const float arcEnd   = 3.14159265358979323846f * 0.70f;

            for (int i = 0; i < n; ++i)
            {
                const float t = (n > 1) ? static_cast<float> (i) / static_cast<float> (n - 1) : 0.5f;
                const float ang = arcStart + (arcEnd - arcStart) * t
                                + (t - 0.5f) * spread * 2.2f;
                const float rr = baseR * (0.5f + 0.5f * amount) * pulse;
                pts.push_back ({ cx + rr * std::cos (ang), cy + rr * std::sin (ang) });
            }

            g.setColour (SympathyLookAndFeel::accent.withAlpha (0.30f * alpha));
            for (int i = 0; i + 1 < n; ++i)
                g.drawLine (pts[static_cast<size_t> (i)].x, pts[static_cast<size_t> (i)].y,
                            pts[static_cast<size_t> (i + 1)].x, pts[static_cast<size_t> (i + 1)].y, 1.0f);

            for (int i = 0; i < n; ++i)
            {
                const bool edge = (i == 0 || i == n - 1);
                const juce::Colour c = edge ? SympathyLookAndFeel::accent2 : SympathyLookAndFeel::accent;
                const float sz = edge ? 4.5f : 3.5f;
                const auto& p = pts[static_cast<size_t> (i)];
                g.setColour (c.withAlpha (0.35f * alpha));
                g.fillEllipse (p.x - sz - 2.0f, p.y - sz - 2.0f, (sz + 2.0f) * 2.0f, (sz + 2.0f) * 2.0f);
                g.setColour (c.withAlpha (alpha));
                g.fillEllipse (p.x - sz, p.y - sz, sz * 2.0f, sz * 2.0f);
            }
        }

        void timerCallback() override
        {
            const float rate = state.getRawParameterValue ("refractLfoRate")->load();
            phase += (2.0f * 3.14159265358979323846f * rate) / 30.0f;
            if (phase > 2.0f * 3.14159265358979323846f)
                phase -= 2.0f * 3.14159265358979323846f;
            repaint();
        }

    private:
        juce::AudioProcessorValueTreeState& state;
        float phase = 0.0f;
    };

    // ---- layout helper -----------------------------------------------------
    void layoutKnobs (std::vector<std::unique_ptr<Knob>>& knobs, int cols,
                      juce::Rectangle<int> area, int gapX, int gapY)
    {
        if (knobs.empty())
            return;

        const int rows = static_cast<int> (std::ceil (knobs.size() / static_cast<double> (cols)));
        const int cw = (area.getWidth()  - gapX * (cols - 1)) / cols;
        const int ch = (area.getHeight() - gapY * (rows - 1)) / rows;
        const int knobH = juce::jmin (ch, 116);

        int idx = 0;
        for (int r = 0; r < rows; ++r)
        {
            for (int c = 0; c < cols && idx < static_cast<int> (knobs.size()); ++c, ++idx)
            {
                const int x = area.getX() + c * (cw + gapX);
                const int y = area.getY() + r * (ch + gapY);
                knobs[static_cast<size_t> (idx)]->setBounds (x, y + (ch - knobH) / 2, cw, knobH);
            }
        }
    }
}

// ===========================================================================
class SympathyAudioProcessorEditor::Impl : private juce::AudioProcessorValueTreeState::Listener,
                                           private juce::AsyncUpdater
{
public:
    explicit Impl (SympathyAudioProcessor& proc)
        : processor (proc)
    {
        build();
    }

    ~Impl() override
    {
        processor.getParameters().removeParameterListener ("material", this);
        processor.getParameters().removeParameterListener ("refractOn", this);
        for (const auto* id : { "refractUnison", "refractHarmonizer", "refractBandpass",
                                "refractDistort", "refractBitcrush", "refractComb" })
            processor.getParameters().removeParameterListener (id, this);
    }

    // Repaint is marshalled to the message thread (parameterChanged may arrive
    // on the audio thread during host automation).
    void parameterChanged (const juce::String&, float) override { triggerAsyncUpdate(); }

    void handleAsyncUpdate() override
    {
        for (auto& b : materialButtons) b->repaint();
        if (refractPower != nullptr)  refractPower->repaint();
        for (auto& b : refractFxButtons) b->repaint();
    }

    SympathyAudioProcessor& processor;

    std::unique_ptr<HoloTitle> title;
    juce::Label subtitle;
    juce::Label brand;
    juce::Label footer;

    std::unique_ptr<ResonanceVisualizer> visualizer;
    std::unique_ptr<SpectrogramSphere> leftSphere;
    std::unique_ptr<SpectrumSphere> rightSphere;

    std::vector<std::unique_ptr<MaterialIconButton>> materialButtons;

    std::unique_ptr<Panel> bodyPanel;
    std::unique_ptr<Panel> stringsPanel;
    std::unique_ptr<Panel> globalPanel;
    std::unique_ptr<Panel> refractPanel;

    std::vector<std::unique_ptr<Knob>> bodyKnobs;
    std::vector<std::unique_ptr<Knob>> stringKnobs;
    std::vector<std::unique_ptr<Knob>> globalKnobs;
    std::vector<std::unique_ptr<Knob>> refractKnobs;

    std::unique_ptr<RefractPowerButton> refractPower;
    std::unique_ptr<RefractVisualizer> refractVisual;
    std::vector<std::unique_ptr<RefractFxButton>> refractFxButtons;

    void build()
    {
        // --- header --------------------------------------------------------
        title = std::make_unique<HoloTitle> ("SYMPATHY", 42.0f, true);

        subtitle.setText ("PHYSICAL MODELING RESONATOR", juce::dontSendNotification);
        subtitle.setFont (juce::Font (12.0f, juce::Font::bold));
        subtitle.setColour (juce::Label::textColourId, SympathyLookAndFeel::muted);
        subtitle.setJustificationType (juce::Justification::centred);

        brand.setText ("DEWPOINT AV", juce::dontSendNotification);
        brand.setFont (monumentFont (13.0f, false));
        brand.setColour (juce::Label::textColourId, SympathyLookAndFeel::accent);
        brand.setJustificationType (juce::Justification::centred);

        footer.setText (juce::String ("DEWPOINT AV  -  v") + juce::String (JucePlugin_VersionString), juce::dontSendNotification);
        footer.setFont (monumentFont (11.0f, false));
        footer.setColour (juce::Label::textColourId, SympathyLookAndFeel::muted);
        footer.setJustificationType (juce::Justification::centred);

        // --- material icon selector ----------------------------------------
        for (int i = 0; i < 7; ++i)
            materialButtons.push_back (std::make_unique<MaterialIconButton> (
                static_cast<MaterialId> (i), processor.getParameters()));

        // Repaint the selector whenever the material changes (clicks, host
        // automation or presets), so the highlighted button always follows.
        processor.getParameters().addParameterListener ("material", this);

        // --- visualizer -----------------------------------------------------
        visualizer = std::make_unique<ResonanceVisualizer> (processor);

        leftSphere = std::make_unique<SpectrogramSphere> (
            &processor.getEngine().vizInput(),
            [this] { return processor.getEngine().vizWritePos(); });
        rightSphere = std::make_unique<SpectrumSphere> (
            &processor.getEngine().vizInput(),
            [this] { return processor.getEngine().vizWritePos(); });

        // --- panels ---------------------------------------------------------
        bodyPanel = std::make_unique<Panel> ("RESONANT BODY");
        bodyKnobs.push_back (std::make_unique<Knob> (processor.getParameters(), "tune", "TUNE", hzText));
        bodyKnobs.push_back (std::make_unique<Knob> (processor.getParameters(), "inharmonicity", "INHARM", pctText));
        bodyKnobs.push_back (std::make_unique<Knob> (processor.getParameters(), "decay", "DECAY", secText));
        bodyKnobs.push_back (std::make_unique<Knob> (processor.getParameters(), "brightness", "BRIGHT", pctText));
        bodyKnobs.push_back (std::make_unique<Knob> (processor.getParameters(), "body", "LEVEL", pctText));

        stringsPanel = std::make_unique<Panel> ("STRINGS");
        stringKnobs.push_back (std::make_unique<Knob> (processor.getParameters(), "strings", "LEVEL", pctText));
        stringKnobs.push_back (std::make_unique<Knob> (processor.getParameters(), "stringTune", "TUNE", stText));
        stringKnobs.push_back (std::make_unique<Knob> (processor.getParameters(), "stringDamp", "DAMP", pctText));
        stringKnobs.push_back (std::make_unique<Knob> (processor.getParameters(), "stringDecay", "DECAY", secText));

        globalPanel = std::make_unique<Panel> ("GLOBAL");
        globalKnobs.push_back (std::make_unique<Knob> (processor.getParameters(), "exciter", "EXCITER", pctText));
        globalKnobs.push_back (std::make_unique<Knob> (processor.getParameters(), "input", "INPUT", gainText));
        globalKnobs.push_back (std::make_unique<Knob> (processor.getParameters(), "mix", "MIX", pctText));
        globalKnobs.push_back (std::make_unique<Knob> (processor.getParameters(), "output", "OUTPUT", gainText));

        // --- refract (super unison multi-FX) --------------------------------
        refractPanel = std::make_unique<Panel> ("REFRACTION - SUPER UNISON");
        refractPower = std::make_unique<RefractPowerButton> (processor.getParameters());
        refractVisual = std::make_unique<RefractVisualizer> (processor.getParameters());

        refractFxButtons.push_back (std::make_unique<RefractFxButton> (
            "refractUnison", "UNISON", processor.getParameters()));
        refractFxButtons.push_back (std::make_unique<RefractFxButton> (
            "refractHarmonizer", "HARMONIZER", processor.getParameters()));
        refractFxButtons.push_back (std::make_unique<RefractFxButton> (
            "refractBandpass", "BANDPASS", processor.getParameters()));
        refractFxButtons.push_back (std::make_unique<RefractFxButton> (
            "refractDistort", "DISTORT", processor.getParameters()));
        refractFxButtons.push_back (std::make_unique<RefractFxButton> (
            "refractBitcrush", "BITCRUSH", processor.getParameters()));
        refractFxButtons.push_back (std::make_unique<RefractFxButton> (
            "refractComb", "COMB", processor.getParameters()));

        refractKnobs.push_back (std::make_unique<Knob> (processor.getParameters(), "refractVoices", "VOICES", voiceText));
        refractKnobs.push_back (std::make_unique<Knob> (processor.getParameters(), "refractAmount", "AMOUNT", pctText));
        refractKnobs.push_back (std::make_unique<Knob> (processor.getParameters(), "refractMix", "MIX", pctText));
        refractKnobs.push_back (std::make_unique<Knob> (processor.getParameters(), "refractSpread", "SPREAD", pctText));
        refractKnobs.push_back (std::make_unique<Knob> (processor.getParameters(), "refractLfoRate", "LFO RATE", lfoText));
        refractKnobs.push_back (std::make_unique<Knob> (processor.getParameters(), "refractLfoDepth", "LFO DEPTH", pctText));

        processor.getParameters().addParameterListener ("refractOn", this);
        for (const auto* id : { "refractUnison", "refractHarmonizer", "refractBandpass",
                                "refractDistort", "refractBitcrush", "refractComb" })
            processor.getParameters().addParameterListener (id, this);
    }

    void attachTo (juce::Component& root)
    {
        root.addAndMakeVisible (*title);
        root.addAndMakeVisible (subtitle);
        root.addAndMakeVisible (brand);
        root.addAndMakeVisible (footer);
        root.addAndMakeVisible (*leftSphere);
        root.addAndMakeVisible (*rightSphere);

        for (auto& b : materialButtons)
            root.addAndMakeVisible (*b);

        root.addAndMakeVisible (*visualizer);

        root.addAndMakeVisible (*bodyPanel);
        for (auto& k : bodyKnobs)
            bodyPanel->addAndMakeVisible (*k);

        root.addAndMakeVisible (*stringsPanel);
        for (auto& k : stringKnobs)
            stringsPanel->addAndMakeVisible (*k);

        root.addAndMakeVisible (*globalPanel);
        for (auto& k : globalKnobs)
            globalPanel->addAndMakeVisible (*k);

        root.addAndMakeVisible (*refractPanel);
        refractPanel->addAndMakeVisible (*refractPower);
        refractPanel->addAndMakeVisible (*refractVisual);
        for (auto& b : refractFxButtons)
            refractPanel->addAndMakeVisible (*b);
        for (auto& k : refractKnobs)
            refractPanel->addAndMakeVisible (*k);
    }

    void resized (juce::Rectangle<int> area)
    {
        auto b = area;

        // Footer strip.
        footer.setBounds (b.removeFromBottom (18));
        b.removeFromBottom (2);

        // Header
        auto header = b.removeFromTop (92);
        const int sphere = 76;
        const int pad = 28;
        const int sphereY = header.getY() + (header.getHeight() - sphere) / 2;
        leftSphere->setBounds  (header.getX() + pad, sphereY, sphere, sphere);
        rightSphere->setBounds (header.getRight() - pad - sphere, sphereY, sphere, sphere);

        header = header.withTrimmedLeft (sphere + pad).withTrimmedRight (sphere + pad);
        brand.setBounds (header.removeFromTop (20));
        title->setBounds (header.removeFromTop (54));
        subtitle.setBounds (header);

        b.removeFromTop (6);

        // Material icon selector row
        auto matRow = b.removeFromTop (66);
        const int cellW = matRow.getWidth() / 7;
        for (auto& mb : materialButtons)
            mb->setBounds (matRow.removeFromLeft (cellW));

        b.removeFromTop (8);

        // Visualizer fills the middle; refract + panels sit at the bottom.
        const int panelH   = 168;
        const int refractH = 186;
        const int gap      = 12;

        visualizer->setBounds (b.removeFromTop (
            juce::jmax (60, b.getHeight() - refractH - panelH - 2 * gap)));
        b.removeFromTop (gap);

        refractPanel->setBounds (b.removeFromTop (refractH));
        b.removeFromTop (gap);

        const int bodyW = juce::jmax (280, static_cast<int> (b.getWidth() * 0.40f));
        const int sideW = (b.getWidth() - bodyW - 2 * gap) / 2;

        bodyPanel->setBounds (b.removeFromLeft (bodyW));
        b.removeFromLeft (gap);
        stringsPanel->setBounds (b.removeFromLeft (sideW));
        b.removeFromLeft (gap);
        globalPanel->setBounds (b);

        // Body panel: 5 knobs in a single row.
        auto bb = bodyPanel->getLocalBounds().reduced (12);
        bb.removeFromTop (30);
        layoutKnobs (bodyKnobs, 5, bb, 6, 0);

        // Strings / global: single row (bigger, more legible knobs).
        auto sb = stringsPanel->getLocalBounds().reduced (12);
        sb.removeFromTop (30);
        layoutKnobs (stringKnobs, 4, sb, 8, 0);

        auto gb = globalPanel->getLocalBounds().reduced (12);
        gb.removeFromTop (30);
        layoutKnobs (globalKnobs, 4, gb, 8, 0);

        // Refract panel: power + orb on the left, modes + knobs on the right.
        auto rb = refractPanel->getLocalBounds().reduced (12);
        rb.removeFromTop (30);
        auto left = rb.removeFromLeft (118);
        refractPower->setBounds (left.removeFromTop (44));
        refractVisual->setBounds (left.reduced (6, 2));

        auto modeRow = rb.removeFromTop (40);
        const int mgap = 6;
        const int mcell = (modeRow.getWidth() - mgap * (static_cast<int> (refractFxButtons.size()) - 1))
                        / static_cast<int> (refractFxButtons.size());
        for (auto& mb : refractFxButtons)
        {
            mb->setBounds (modeRow.removeFromLeft (mcell));
            modeRow.removeFromLeft (mgap);
        }

        layoutKnobs (refractKnobs, 6, rb, 6, 0);
    }
};

// ===========================================================================
SympathyAudioProcessorEditor::SympathyAudioProcessorEditor (SympathyAudioProcessor& p)
    : AudioProcessorEditor (&p)
    , processor (p)
{
    setLookAndFeel (&lookAndFeel);

    impl = std::make_unique<Impl> (processor);
    impl->attachTo (*this);

    setResizable (true, true);
    setResizeLimits (680, 680, 1500, 1000);
    setSize (960, 840);
}

SympathyAudioProcessorEditor::~SympathyAudioProcessorEditor()
{
    setLookAndFeel (nullptr);
}

void SympathyAudioProcessorEditor::paint (juce::Graphics& g)
{
    const auto b = getLocalBounds().toFloat();
    g.setGradientFill (juce::ColourGradient (
        SympathyLookAndFeel::background.brighter (0.05f), b.getCentreX(), b.getCentreY(),
        SympathyLookAndFeel::background, b.getWidth() * 0.65f, b.getHeight() * 0.65f, true));
    g.fillAll();
}

void SympathyAudioProcessorEditor::resized()
{
    impl->resized (getLocalBounds().reduced (16));
}
