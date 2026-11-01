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
    juce::String freqText (float v) { return v >= 1000.0f ? juce::String (v / 1000.0f, 1) + " kHz" : juce::String (v, 0) + " Hz"; }
    juce::String bipolarText (float v) { return (v >= 0.0f ? "+" : "") + juce::String (v * 100.0f, 0) + " %"; }
    juce::String octText  (float v) { return juce::String (v, 1) + " oct"; }
    juce::String degText  (float v) { return juce::String (v, 0) + juce::String (juce::CharPointer_UTF8 ("\xc2\xb0")); }
    juce::String centText (float v) { return (v >= 0.0f ? "+" : "") + juce::String (v, 0) + " ct"; }
    juce::String semiText (float v) { return (v >= 0.0f ? "+" : "") + juce::String (v, v == std::floor (v) ? 0 : 1) + " st"; }

    // ---- a labelled rotary knob bound to a parameter -----------------------
    class Knob : public juce::Component,
                 private juce::AudioProcessorValueTreeState::Listener,
                 private juce::AsyncUpdater
    {
    public:
        Knob (juce::AudioProcessorValueTreeState& state,
              const juce::String& paramId,
              const juce::String& name,
              std::function<juce::String (float)> textFnIn)
            : stateRef (state), param (paramId), textFn (textFnIn)
        {
            slider.setSliderStyle (juce::Slider::Rotary);
            slider.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
            slider.setRotaryParameters ({ juce::MathConstants<float>::pi * 1.25f,
                                          juce::MathConstants<float>::pi * 2.75f,
                                          true });

            addAndMakeVisible (slider);

            nameLabel.setText (name, juce::dontSendNotification);
            nameLabel.setFont (juce::FontOptions (11.0f, juce::Font::plain));
            nameLabel.setColour (juce::Label::textColourId, SympathyLookAndFeel::muted);
            nameLabel.setJustificationType (juce::Justification::centred);
            addAndMakeVisible (nameLabel);

            valueLabel.setFont (juce::FontOptions (11.0f, juce::Font::bold));
            valueLabel.setColour (juce::Label::textColourId, SympathyLookAndFeel::text);
            valueLabel.setJustificationType (juce::Justification::centred);
            addAndMakeVisible (valueLabel);

            attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
                state, paramId, slider);

            // Keep the value label in sync with the parameter at all times â€”
            // both user drags and host automation / preset loads.
            stateRef.addParameterListener (param, this);
            refreshLabel();
        }

        ~Knob() override { stateRef.removeParameterListener (param, this); }

        // parameterChanged can fire on the audio thread (host automation) or at
        // mouse-move rate during a drag â€” marshal + coalesce the label update to
        // the message thread so we never touch the Label off-thread.
        void parameterChanged (const juce::String&, float) override { triggerAsyncUpdate(); }

        void handleAsyncUpdate() override { refreshLabel(); }

        void setName (const juce::String& name) { nameLabel.setText (name, juce::dontSendNotification); }
        void refresh() { refreshLabel(); }

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
            titleLabel.setFont (juce::FontOptions (14.0f, juce::Font::bold));
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

            // Sized from the height, so wide cells don't squash the icon.
            const float iconH = iconArea.getHeight() - 8.0f;
            const auto iconBox = iconArea.withSizeKeepingCentre (juce::jmin (iconArea.getWidth() - 8.0f, iconH * 1.5f), iconH);

            // Two-pass icon for a subtle glow when selected.
            if (sel)
                drawMaterialIcon (g, id, iconBox, col.withAlpha (0.30f), 4.0f);
            drawMaterialIcon (g, id, iconBox, col, sel ? 2.2f : 1.6f);

            g.setColour (sel ? SympathyLookAndFeel::text : SympathyLookAndFeel::muted);
            g.setFont (juce::FontOptions (10.5f, sel ? juce::Font::bold : juce::Font::plain));
            g.drawText (materialName (id), labelArea, juce::Justification::centred, false);
        }

        void mouseDown (const juce::MouseEvent&) override
        {
            state.getParameter ("resType")->setValueNotifyingHost (
                static_cast<float> (static_cast<int> (id)) / 6.0f);
        }

        bool selected() const
        {
            const int idx = juce::roundToInt (state.getRawParameterValue ("resType")->load());
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

            g.setFont (juce::FontOptions (10.0f, sel ? juce::Font::bold : juce::Font::plain));
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

    // ---- one option of a Choice parameter, as a button ------------------------
    class ChoiceButton : public juce::Component
    {
    public:
        ChoiceButton (const juce::String& paramId, int optionIndex, const juce::String& label,
                      juce::AudioProcessorValueTreeState& st)
            : param (paramId), option (optionIndex), name (label), state (st)
        {
        }

        void paint (juce::Graphics& g) override
        {
            const bool sel = selected();
            auto r = getLocalBounds().toFloat().reduced (1.5f);
            const juce::Colour col = sel ? SympathyLookAndFeel::accent : SympathyLookAndFeel::muted;

            g.setColour (sel ? col.withAlpha (0.15f) : SympathyLookAndFeel::panel);
            g.fillRoundedRectangle (r, 7.0f);
            g.setColour (sel ? col.withAlpha (0.65f) : SympathyLookAndFeel::border);
            g.drawRoundedRectangle (r.reduced (0.5f), 7.0f, sel ? 1.5f : 1.0f);

            g.setFont (juce::FontOptions (10.0f, sel ? juce::Font::bold : juce::Font::plain));
            g.setColour (sel ? SympathyLookAndFeel::text : SympathyLookAndFeel::muted);
            g.drawText (name, r, juce::Justification::centred, false);
        }

        void mouseDown (const juce::MouseEvent&) override
        {
            if (auto* p = dynamic_cast<juce::RangedAudioParameter*> (state.getParameter (param)))
                p->setValueNotifyingHost (p->convertTo0to1 (static_cast<float> (option)));
        }

        bool selected() const
        {
            return juce::roundToInt (state.getRawParameterValue (param)->load()) == option;
        }

    private:
        juce::String param;
        int option;
        juce::String name;
        juce::AudioProcessorValueTreeState& state;
    };

    // ---- a Choice parameter as a menu, with a small caption --------------------
    class ChoiceMenu : public juce::Component
    {
    public:
        ChoiceMenu (juce::AudioProcessorValueTreeState& state, const juce::String& paramId, const juce::String& caption)
        {
            if (auto* p = dynamic_cast<juce::AudioParameterChoice*> (state.getParameter (paramId)))
                box.addItemList (p->choices, 1);
            box.setJustificationType (juce::Justification::centred);
            addAndMakeVisible (box);

            label.setText (caption, juce::dontSendNotification);
            label.setFont (juce::FontOptions (10.0f, juce::Font::plain));
            label.setColour (juce::Label::textColourId, SympathyLookAndFeel::muted);
            label.setJustificationType (juce::Justification::centredLeft);
            addAndMakeVisible (label);

            attachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (state, paramId, box);
        }

        void resized() override
        {
            auto b = getLocalBounds();
            label.setBounds (b.removeFromTop (14));
            box.setBounds (b.withSizeKeepingCentre (b.getWidth(), juce::jmin (b.getHeight(), 24)));
        }

    private:
        juce::ComboBox box;
        juce::Label label;
        std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> attachment;
    };

    // ---- animated dispersion orb -------------------------------------------
    class RefractVisualizer : public juce::Component,
                              private juce::Timer
    {
    public:
        explicit RefractVisualizer (juce::AudioProcessorValueTreeState& st) : state (st)
        {
            startTimerHz (12);
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
        for (const auto* id : watchedParams())
            processor.getParameters().addParameterListener (id, this);
        refreshDynamic();
    }

    ~Impl() override
    {
        for (const auto* id : watchedParams())
            processor.getParameters().removeParameterListener (id, this);
    }

    static std::vector<const char*> watchedParams()
    {
        return { "resType", "quality", "refractOn", "refractMode", "refractSource", "refractLfoSync",
                 "resLfoSync", "autoGain", "filterOn", "midiFreq" };
    }

    // Marshalled to the message thread (parameterChanged may arrive on the audio
    // thread during host automation).
    void parameterChanged (const juce::String&, float) override { triggerAsyncUpdate(); }

    void handleAsyncUpdate() override { refreshDynamic(); }

    SympathyAudioProcessor& processor;

    std::unique_ptr<HoloTitle> title;
    juce::Label subtitle;
    juce::Label brand;
    juce::Label footer;

    std::unique_ptr<ResonanceVisualizer> visualizer;
    std::unique_ptr<SpectrogramSphere> leftSphere;
    std::unique_ptr<SpectrumSphere> rightSphere;

    std::vector<std::unique_ptr<MaterialIconButton>> materialButtons;

    std::unique_ptr<Panel> bodyPanel, stereoPanel, stringsPanel, lfoPanel, midiPanel, globalPanel, refractPanel;

    std::vector<std::unique_ptr<Knob>> bodyKnobs, stereoKnobs, stringKnobs, lfoKnobs, midiKnobs, globalKnobs, refractKnobs;
    std::unique_ptr<Knob> ratioKnob, radiusKnob, openingKnob;
    Knob* materialKnob = nullptr;
    Knob* brightKnob = nullptr;
    Knob* inharmKnob = nullptr;
    Knob* hitKnob = nullptr;
    Knob* posLKnob = nullptr;
    Knob* posRKnob = nullptr;
    Knob* bleedKnob = nullptr;
    Knob* primaryKnob = nullptr;
    Knob* secondaryKnob = nullptr;
    Knob* refractRateKnob = nullptr;
    Knob* lfoRateKnob = nullptr;
    Knob* lfoToPrimaryKnob = nullptr;

    std::unique_ptr<ChoiceButton> qualityEco, qualityHigh, sourceResonator, sourceInput;
    std::unique_ptr<RefractPowerButton> refractPower;
    std::unique_ptr<RefractVisualizer> refractVisual;
    std::vector<std::unique_ptr<ChoiceButton>> refractModeButtons;
    std::unique_ptr<RefractFxButton> autoGainToggle, filterToggle, midiToggle;
    std::unique_ptr<ChoiceMenu> algoMenu, chordMenu, refractShapeMenu, refractSyncMenu, refractDivMenu;
    std::unique_ptr<ChoiceMenu> lfoShapeMenu, lfoSyncMenu, lfoDivMenu, priorityMenu;
    juce::Rectangle<int> secondarySlot;

    float param (const char* id) const { return processor.getParameters().getRawParameterValue (id)->load(); }

    Knob* addKnob (std::vector<std::unique_ptr<Knob>>& list, const char* id, const char* name,
                   std::function<juce::String (float)> text)
    {
        list.push_back (std::make_unique<Knob> (processor.getParameters(), id, name, std::move (text)));
        return list.back().get();
    }

    // The Primary knob's reading depends on the mode.
    juce::String primaryText (float v) const
    {
        switch (juce::roundToInt (param ("refractMode")))
        {
            case sympathy::kModeBandpass:   return freqText (40.0f * std::pow (300.0f, v));
            case sympathy::kModeComb:       return freqText (40.0f * std::pow (200.0f, v));
            case sympathy::kModeBitcrush:   return juce::String (1.0f / std::pow (2.0f, -6.0f * v), 1) + ":1";
            case sympathy::kModeDistortion: return "+" + juce::String (36.0f * v, 0) + " dB";
            case sympathy::kModeHarmonizer: return juce::String (v * v * 1500.0f, 0) + " ms";
            default:                        return "-";
        }
    }

    juce::String secondaryText (float v) const
    {
        if (juce::roundToInt (param ("refractMode")) == sympathy::kModeBitcrush)
            return juce::String (16.0f - 14.0f * v, 0) + " bit";
        return pctText (v);
    }

    void build()
    {
        auto& state = processor.getParameters();

        // --- header --------------------------------------------------------
        title = std::make_unique<HoloTitle> ("SYMPATHY", 42.0f, true);

        subtitle.setText ("PHYSICAL MODELING RESONATOR", juce::dontSendNotification);
        subtitle.setFont (juce::FontOptions (12.0f, juce::Font::bold));
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

        // --- resonator type selector ---------------------------------------
        for (int i = 0; i < 7; ++i)
            materialButtons.push_back (std::make_unique<MaterialIconButton> (static_cast<MaterialId> (i), state));

        visualizer = std::make_unique<ResonanceVisualizer> (processor);

        leftSphere = std::make_unique<SpectrogramSphere> (
            &processor.getEngine().vizInput(),
            [this] { return processor.getEngine().vizWritePos(); });
        rightSphere = std::make_unique<SpectrumSphere> (
            &processor.getEngine().vizInput(),
            [this] { return processor.getEngine().vizWritePos(); });

        // --- the object -----------------------------------------------------
        bodyPanel = std::make_unique<Panel> ("RESONATOR");
        addKnob (bodyKnobs, "tune", "TUNE", hzText);
        addKnob (bodyKnobs, "decay", "DECAY", secText);
        materialKnob = addKnob (bodyKnobs, "materialTone", "MATERIAL", bipolarText);
        brightKnob   = addKnob (bodyKnobs, "brightness", "BRIGHT", pctText);
        inharmKnob   = addKnob (bodyKnobs, "inharmonicity", "INHARM", bipolarText);
        hitKnob      = addKnob (bodyKnobs, "hit", "HIT", pctText);
        ratioKnob    = std::make_unique<Knob> (state, "ratio", "RATIO", pctText);
        radiusKnob   = std::make_unique<Knob> (state, "radius", "RADIUS", pctText);
        openingKnob  = std::make_unique<Knob> (state, "opening", "OPENING", pctText);
        addKnob (bodyKnobs, "body", "LEVEL", pctText);
        qualityEco  = std::make_unique<ChoiceButton> ("quality", 0, "ECO", state);
        qualityHigh = std::make_unique<ChoiceButton> ("quality", 1, "HIGH", state);

        // --- stereo ---------------------------------------------------------
        stereoPanel = std::make_unique<Panel> ("STEREO");
        addKnob (stereoKnobs, "width", "WIDTH", pctText);
        addKnob (stereoKnobs, "spread", "SPREAD", pctText);
        posLKnob = addKnob (stereoKnobs, "posL", "POS L", pctText);
        posRKnob = addKnob (stereoKnobs, "posR", "POS R", pctText);

        // --- strings --------------------------------------------------------
        stringsPanel = std::make_unique<Panel> ("STRINGS");
        addKnob (stringKnobs, "strings", "LEVEL", pctText);
        addKnob (stringKnobs, "stringTune", "TUNE", stText);
        addKnob (stringKnobs, "stringDamp", "DAMP", pctText);
        addKnob (stringKnobs, "stringDecay", "DECAY", secText);

        // --- pitch LFO ------------------------------------------------------
        lfoPanel = std::make_unique<Panel> ("PITCH LFO");
        addKnob (lfoKnobs, "resLfoAmount", "DEPTH", semiText);
        lfoRateKnob = addKnob (lfoKnobs, "resLfoRate", "RATE", lfoText);
        addKnob (lfoKnobs, "resLfoPhase", "PHASE", degText);
        lfoShapeMenu = std::make_unique<ChoiceMenu> (state, "resLfoShape", "SHAPE");
        lfoSyncMenu  = std::make_unique<ChoiceMenu> (state, "resLfoSync", "SYNC");
        lfoDivMenu   = std::make_unique<ChoiceMenu> (state, "resLfoDiv", "DIVISION");

        // --- MIDI -----------------------------------------------------------
        midiPanel = std::make_unique<Panel> ("MIDI");
        midiToggle = std::make_unique<RefractFxButton> ("midiFreq", "FOLLOW", state);
        priorityMenu = std::make_unique<ChoiceMenu> (state, "midiPriority", "PRIORITY");
        addKnob (midiKnobs, "midiTranspose", "TRANS", semiText);
        addKnob (midiKnobs, "midiFine", "FINE", centText);
        addKnob (midiKnobs, "pbRange", "BEND", stText);
        addKnob (midiKnobs, "offDecay", "OFF DEC", pctText);

        // --- global ---------------------------------------------------------
        globalPanel = std::make_unique<Panel> ("GLOBAL");
        addKnob (globalKnobs, "exciter", "EXCITER", pctText);
        addKnob (globalKnobs, "input", "INPUT", gainText);
        addKnob (globalKnobs, "filterFreq", "FILTER", freqText);
        addKnob (globalKnobs, "filterBw", "BW", octText);
        bleedKnob = addKnob (globalKnobs, "bleed", "BLEED", pctText);
        addKnob (globalKnobs, "mix", "MIX", pctText);
        addKnob (globalKnobs, "output", "OUTPUT", gainText);
        autoGainToggle = std::make_unique<RefractFxButton> ("autoGain", "AUTO GAIN", state);
        filterToggle   = std::make_unique<RefractFxButton> ("filterOn", "INPUT FILTER", state);

        // --- refraction -----------------------------------------------------
        refractPanel = std::make_unique<Panel> ("REFRACTION");
        refractPower = std::make_unique<RefractPowerButton> (state);
        refractVisual = std::make_unique<RefractVisualizer> (state);
        sourceResonator = std::make_unique<ChoiceButton> ("refractSource", 0, "RESONATOR", state);
        sourceInput     = std::make_unique<ChoiceButton> ("refractSource", 1, "INPUT", state);

        const char* modeNames[sympathy::kNumRefractModes] = { "UNISON", "BANDPASS", "COMB", "BITCRUSHER", "DISTORTION", "HARMONIZER" };
        for (int i = 0; i < sympathy::kNumRefractModes; ++i)
            refractModeButtons.push_back (std::make_unique<ChoiceButton> ("refractMode", i, modeNames[i], state));

        addKnob (refractKnobs, "refractVoices", "VOICES", voiceText);
        addKnob (refractKnobs, "refractAmount", "AMOUNT", pctText);
        primaryKnob   = addKnob (refractKnobs, "refractPrimary", "CUTOFF", [this] (float v) { return primaryText (v); });
        secondaryKnob = addKnob (refractKnobs, "refractSecondary", "RESONANCE", [this] (float v) { return secondaryText (v); });
        addKnob (refractKnobs, "refractSpread", "SPREAD", pctText);
        addKnob (refractKnobs, "refractMix", "MIX", pctText);
        addKnob (refractKnobs, "refractHp", "HP IN", freqText);
        addKnob (refractKnobs, "refractLp", "LP OUT", freqText);
        refractRateKnob  = addKnob (refractKnobs, "refractLfoRate", "LFO RATE", lfoText);
        addKnob (refractKnobs, "refractLfoDepth", "LFO>AMT", bipolarText);
        lfoToPrimaryKnob = addKnob (refractKnobs, "refractLfoPrimary", "LFO>CTRL", bipolarText);

        algoMenu  = std::make_unique<ChoiceMenu> (state, "refractAlgo", "ALGORITHM");
        chordMenu = std::make_unique<ChoiceMenu> (state, "refractChord", "CHORD");
        refractShapeMenu = std::make_unique<ChoiceMenu> (state, "refractLfoShape", "LFO SHAPE");
        refractSyncMenu  = std::make_unique<ChoiceMenu> (state, "refractLfoSync", "LFO SYNC");
        refractDivMenu   = std::make_unique<ChoiceMenu> (state, "refractLfoDiv", "LFO DIVISION");
    }

    // Controls follow the selected type and mode: irrelevant ones dim or swap out.
    void refreshDynamic()
    {
        const auto dim = [] (juce::Component* c, bool active)
        {
            c->setEnabled (active);
            c->setAlpha (active ? 1.0f : 0.32f);
        };

        const int type = juce::roundToInt (param ("resType"));
        const bool waveguide = sympathy::isWaveguide (type);
        for (Knob* k : { materialKnob, brightKnob, inharmKnob, hitKnob, posLKnob, posRKnob, bleedKnob })
            dim (k, ! waveguide);
        dim (qualityEco.get(), ! waveguide);
        dim (qualityHigh.get(), ! waveguide);

        ratioKnob->setVisible (! waveguide);
        dim (ratioKnob.get(), type == sympathy::kMembrane || type == sympathy::kPlate);
        radiusKnob->setVisible (waveguide);
        dim (openingKnob.get(), type == sympathy::kPipe);

        const int mode = juce::roundToInt (param ("refractMode"));
        const char* primaryName = "CONTROL";
        const char* secondaryName = "";
        switch (mode)
        {
            case sympathy::kModeBandpass:   primaryName = "CUTOFF";  secondaryName = "RESONANCE"; break;
            case sympathy::kModeComb:       primaryName = "CUTOFF";  secondaryName = "RESONANCE"; break;
            case sympathy::kModeBitcrush:   primaryName = "DOWNSAMP"; secondaryName = "BITS";     break;
            case sympathy::kModeDistortion: primaryName = "DRIVE";   break;
            case sympathy::kModeHarmonizer: primaryName = "SPRAY";   break;
            default: break;
        }
        primaryKnob->setName (primaryName);
        primaryKnob->refresh();
        secondaryKnob->setName (secondaryName);
        secondaryKnob->refresh();
        dim (primaryKnob, mode != sympathy::kModeUnison);
        dim (lfoToPrimaryKnob, mode != sympathy::kModeUnison);

        const bool usesSecondary = mode == sympathy::kModeBandpass || mode == sympathy::kModeComb || mode == sympathy::kModeBitcrush;
        secondaryKnob->setVisible (usesSecondary);
        algoMenu->setVisible (mode == sympathy::kModeDistortion);
        chordMenu->setVisible (mode == sympathy::kModeHarmonizer);

        const bool refractSynced = juce::roundToInt (param ("refractLfoSync")) != 0;
        dim (refractRateKnob, ! refractSynced);
        dim (refractDivMenu.get(), refractSynced);

        const bool lfoSynced = juce::roundToInt (param ("resLfoSync")) != 0;
        dim (lfoRateKnob, ! lfoSynced);
        dim (lfoDivMenu.get(), lfoSynced);

        for (auto& b : materialButtons) b->repaint();
        for (auto& b : refractModeButtons) b->repaint();
        qualityEco->repaint();
        qualityHigh->repaint();
        sourceResonator->repaint();
        sourceInput->repaint();
        refractPower->repaint();
        autoGainToggle->repaint();
        filterToggle->repaint();
        midiToggle->repaint();
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

        const auto panelWith = [&] (Panel& panel, std::vector<std::unique_ptr<Knob>>& knobs)
        {
            root.addAndMakeVisible (panel);
            for (auto& k : knobs)
                panel.addAndMakeVisible (*k);
        };

        panelWith (*bodyPanel, bodyKnobs);
        bodyPanel->addChildComponent (*ratioKnob);
        bodyPanel->addChildComponent (*radiusKnob);
        bodyPanel->addAndMakeVisible (*openingKnob);
        bodyPanel->addAndMakeVisible (*qualityEco);
        bodyPanel->addAndMakeVisible (*qualityHigh);

        panelWith (*stereoPanel, stereoKnobs);
        panelWith (*stringsPanel, stringKnobs);

        panelWith (*lfoPanel, lfoKnobs);
        lfoPanel->addAndMakeVisible (*lfoShapeMenu);
        lfoPanel->addAndMakeVisible (*lfoSyncMenu);
        lfoPanel->addAndMakeVisible (*lfoDivMenu);

        panelWith (*midiPanel, midiKnobs);
        midiPanel->addAndMakeVisible (*midiToggle);
        midiPanel->addAndMakeVisible (*priorityMenu);

        panelWith (*globalPanel, globalKnobs);
        globalPanel->addAndMakeVisible (*autoGainToggle);
        globalPanel->addAndMakeVisible (*filterToggle);

        panelWith (*refractPanel, refractKnobs);
        refractPanel->addAndMakeVisible (*refractPower);
        refractPanel->addAndMakeVisible (*refractVisual);
        refractPanel->addAndMakeVisible (*sourceResonator);
        refractPanel->addAndMakeVisible (*sourceInput);
        for (auto& b : refractModeButtons)
            refractPanel->addAndMakeVisible (*b);
        refractPanel->addChildComponent (*algoMenu);
        refractPanel->addChildComponent (*chordMenu);
        refractPanel->addAndMakeVisible (*refractShapeMenu);
        refractPanel->addAndMakeVisible (*refractSyncMenu);
        refractPanel->addAndMakeVisible (*refractDivMenu);

        refreshDynamic();
    }

    void resized (juce::Rectangle<int> area)
    {
        auto b = area;

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

        b.removeFromTop (4);

        // Resonator type row
        auto matRow = b.removeFromTop (62);
        const int cellW = matRow.getWidth() / 7;
        for (auto& mb : materialButtons)
            mb->setBounds (matRow.removeFromLeft (cellW));

        b.removeFromTop (6);

        const int gap = 10;
        const int rowH = 146;
        const int refractH = 186;

        visualizer->setBounds (b.removeFromTop (juce::jmax (60, b.getHeight() - refractH - 2 * rowH - 3 * gap)));
        b.removeFromTop (gap);

        // Resonator + stereo
        auto top = b.removeFromTop (rowH);
        stereoPanel->setBounds (top.removeFromRight (static_cast<int> (top.getWidth() * 0.29f)));
        top.removeFromRight (gap);
        bodyPanel->setBounds (top);
        b.removeFromTop (gap);

        auto bb = bodyPanel->getLocalBounds().reduced (10);
        bb.removeFromTop (22);
        auto qualityColumn = bb.removeFromRight (56);
        const int qh = juce::jmin (32, qualityColumn.getHeight() / 2);
        qualityHigh->setBounds (qualityColumn.removeFromTop (qh));
        qualityEco->setBounds (qualityColumn.removeFromTop (qh));
        bb.removeFromRight (6);

        // Tune, Decay, Material, Bright, Inharm, Hit, Ratio or Radius, Opening, Level.
        constexpr int kBodySlots = 9;
        const int bw = bb.getWidth() / kBodySlots;
        const auto slot = [&] (int i) { return juce::Rectangle<int> (bb.getX() + i * bw, bb.getY(), bw, bb.getHeight()); };
        for (int i = 0; i < 6; ++i)
            bodyKnobs[static_cast<size_t> (i)]->setBounds (slot (i));
        ratioKnob->setBounds (slot (6));
        radiusKnob->setBounds (slot (6));
        openingKnob->setBounds (slot (7));
        bodyKnobs[6]->setBounds (slot (8));

        auto sb = stereoPanel->getLocalBounds().reduced (10);
        sb.removeFromTop (22);
        layoutKnobs (stereoKnobs, 4, sb, 4, 0);

        // Refraction
        refractPanel->setBounds (b.removeFromTop (refractH));
        b.removeFromTop (gap);

        auto rb = refractPanel->getLocalBounds().reduced (10);
        rb.removeFromTop (22);
        auto left = rb.removeFromLeft (112);
        refractPower->setBounds (left.removeFromTop (44));
        left.removeFromTop (4);
        auto sourceRow = left.removeFromTop (46);
        sourceResonator->setBounds (sourceRow.removeFromTop (23));
        sourceInput->setBounds (sourceRow);
        refractVisual->setBounds (left.reduced (2, 2));
        rb.removeFromLeft (10);

        auto menus = rb.removeFromRight (118);
        const int mh = menus.getHeight() / 3;
        refractShapeMenu->setBounds (menus.removeFromTop (mh).reduced (0, 1));
        refractSyncMenu->setBounds (menus.removeFromTop (mh).reduced (0, 1));
        refractDivMenu->setBounds (menus.reduced (0, 1));
        rb.removeFromRight (8);

        auto modeRow = rb.removeFromTop (34);
        const int mgap = 5;
        const int numModes = static_cast<int> (refractModeButtons.size());
        const int mcell = (modeRow.getWidth() - mgap * (numModes - 1)) / numModes;
        for (auto& mb : refractModeButtons)
        {
            mb->setBounds (modeRow.removeFromLeft (mcell));
            modeRow.removeFromLeft (mgap);
        }
        rb.removeFromTop (2);

        layoutKnobs (refractKnobs, 11, rb, 2, 0);
        secondarySlot = secondaryKnob->getBounds();
        algoMenu->setBounds (secondarySlot.withSizeKeepingCentre (secondarySlot.getWidth() - 2, 44));
        chordMenu->setBounds (algoMenu->getBounds());

        // Bottom row: strings, pitch LFO, MIDI, global.
        const int stringsW = b.getWidth() * 20 / 100;
        const int lfoW     = b.getWidth() * 20 / 100;
        const int midiW    = b.getWidth() * 24 / 100;

        stringsPanel->setBounds (b.removeFromLeft (stringsW));
        b.removeFromLeft (gap);
        lfoPanel->setBounds (b.removeFromLeft (lfoW));
        b.removeFromLeft (gap);
        midiPanel->setBounds (b.removeFromLeft (midiW));
        b.removeFromLeft (gap);
        globalPanel->setBounds (b);

        auto stringArea = stringsPanel->getLocalBounds().reduced (10);
        stringArea.removeFromTop (22);
        layoutKnobs (stringKnobs, 4, stringArea, 2, 0);

        auto lb = lfoPanel->getLocalBounds().reduced (10);
        lb.removeFromTop (22);
        auto lfoMenus = lb.removeFromRight (lb.getWidth() * 40 / 100);
        const int lm = lfoMenus.getHeight() / 3;
        lfoShapeMenu->setBounds (lfoMenus.removeFromTop (lm));
        lfoSyncMenu->setBounds (lfoMenus.removeFromTop (lm));
        lfoDivMenu->setBounds (lfoMenus);
        lb.removeFromRight (4);
        layoutKnobs (lfoKnobs, 3, lb, 0, 0);

        auto mb = midiPanel->getLocalBounds().reduced (10);
        mb.removeFromTop (22);
        auto midiTop = mb.removeFromTop (34);
        midiToggle->setBounds (midiTop.removeFromLeft (midiTop.getWidth() * 55 / 100).withSizeKeepingCentre (midiTop.getWidth() * 55 / 100 - 4, 26));
        priorityMenu->setBounds (midiTop);
        layoutKnobs (midiKnobs, 4, mb, 2, 0);

        auto gb = globalPanel->getLocalBounds().reduced (10);
        gb.removeFromTop (22);
        auto toggleRow = gb.removeFromTop (28);
        autoGainToggle->setBounds (toggleRow.removeFromLeft (toggleRow.getWidth() / 2).reduced (2, 1));
        filterToggle->setBounds (toggleRow.reduced (2, 1));
        layoutKnobs (globalKnobs, 7, gb, 1, 0);
    }
};

// ===========================================================================
SympathyAudioProcessorEditor::SympathyAudioProcessorEditor (SympathyAudioProcessor& p)
    : AudioProcessorEditor (&p)
{
    setLookAndFeel (&lookAndFeel);

    impl = std::make_unique<Impl> (p);
    impl->attachTo (*this);

    setResizable (true, true);
    setResizeLimits (1040, 800, 1700, 1250);
    setSize (1180, 900);
}

SympathyAudioProcessorEditor::~SympathyAudioProcessorEditor()
{
    setLookAndFeel (nullptr);
}

void SympathyAudioProcessorEditor::paint (juce::Graphics& g)
{
    // Flat fill: a full-window radial gradient is re-rasterised on every
    // repaint and is a notable GPU cost on Windows (Direct2D).
    g.fillAll (SympathyLookAndFeel::background);
}

void SympathyAudioProcessorEditor::resized()
{
    impl->resized (getLocalBounds().reduced (16));
}
