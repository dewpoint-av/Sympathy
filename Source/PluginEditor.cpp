#include "PluginEditor.h"

#include "ResonanceVisualizer.h"
#include "HoloLogo.h"

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

    // ---- a labelled rotary knob bound to a parameter -----------------------
    class Knob : public juce::Component
    {
    public:
        Knob (juce::AudioProcessorValueTreeState& state,
              const juce::String& paramId,
              const juce::String& name,
              std::function<juce::String (float)> textFn)
        {
            slider.setSliderStyle (juce::Slider::Rotary);
            slider.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);

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

            slider.onValueChange = [this, textFn]
            {
                if (textFn)
                    valueLabel.setText (textFn (static_cast<float> (slider.getValue())),
                                        juce::dontSendNotification);
            };
            slider.onValueChange();
        }

        void resized() override
        {
            auto b = getLocalBounds();
            nameLabel.setBounds (b.removeFromTop (18));
            valueLabel.setBounds (b.removeFromBottom (18));
            slider.setBounds (b);
        }

    private:
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
class SympathyAudioProcessorEditor::Impl : private juce::AudioProcessorValueTreeState::Listener
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
    }

    void parameterChanged (const juce::String& paramID, float) override
    {
        if (paramID == "material")
            for (auto& b : materialButtons)
                b->repaint();
    }

    SympathyAudioProcessor& processor;

    std::unique_ptr<HoloTitle> title;
    juce::Label subtitle;
    juce::Label brand;
    juce::Label footer;

    std::unique_ptr<ResonanceVisualizer> visualizer;

    std::vector<std::unique_ptr<MaterialIconButton>> materialButtons;

    std::unique_ptr<Panel> bodyPanel;
    std::unique_ptr<Panel> stringsPanel;
    std::unique_ptr<Panel> globalPanel;

    std::vector<std::unique_ptr<Knob>> bodyKnobs;
    std::vector<std::unique_ptr<Knob>> stringKnobs;
    std::vector<std::unique_ptr<Knob>> globalKnobs;

    void build()
    {
        // --- header --------------------------------------------------------
        title = std::make_unique<HoloTitle> ("SYMPATHY", 32.0f, true);

        subtitle.setText ("PHYSICAL MODELING RESONATOR", juce::dontSendNotification);
        subtitle.setFont (juce::Font (12.0f, juce::Font::bold));
        subtitle.setColour (juce::Label::textColourId, SympathyLookAndFeel::muted);
        subtitle.setJustificationType (juce::Justification::centredLeft);

        brand.setText ("DEWPOINT AV", juce::dontSendNotification);
        brand.setFont (monumentFont (13.0f, false));
        brand.setColour (juce::Label::textColourId, SympathyLookAndFeel::accent);
        brand.setJustificationType (juce::Justification::centredRight);

        footer.setText ("DEWPOINT AV", juce::dontSendNotification);
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
    }

    void attachTo (juce::Component& root)
    {
        root.addAndMakeVisible (*title);
        root.addAndMakeVisible (subtitle);
        root.addAndMakeVisible (brand);
        root.addAndMakeVisible (footer);

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
    }

    void resized (juce::Rectangle<int> area)
    {
        auto b = area;

        // Footer strip.
        footer.setBounds (b.removeFromBottom (18));
        b.removeFromBottom (2);

        // Header
        auto header = b.removeFromTop (46);
        title->setBounds (header.removeFromLeft (340));
        brand.setBounds (header.removeFromRight (220));
        subtitle.setBounds (header);

        b.removeFromTop (6);

        // Material icon selector row
        auto matRow = b.removeFromTop (66);
        const int cellW = matRow.getWidth() / 7;
        for (auto& mb : materialButtons)
            mb->setBounds (matRow.removeFromLeft (cellW));

        b.removeFromTop (8);

        // Visualizer fills the middle; panels sit at the bottom.
        const int panelH = 168;
        const int gap = 12;
        visualizer->setBounds (b.removeFromTop (juce::jmax (60, b.getHeight() - panelH - gap)));
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

        // Strings / global: 2x2 grids.
        auto sb = stringsPanel->getLocalBounds().reduced (12);
        sb.removeFromTop (30);
        layoutKnobs (stringKnobs, 2, sb, 8, 8);

        auto gb = globalPanel->getLocalBounds().reduced (12);
        gb.removeFromTop (30);
        layoutKnobs (globalKnobs, 2, gb, 8, 8);
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
    setResizeLimits (680, 480, 1500, 1000);
    setSize (940, 680);
}

SympathyAudioProcessorEditor::~SympathyAudioProcessorEditor()
{
    setLookAndFeel (nullptr);
}

void SympathyAudioProcessorEditor::paint (juce::Graphics& g)
{
    g.fillAll (SympathyLookAndFeel::background);
}

void SympathyAudioProcessorEditor::resized()
{
    impl->resized (getLocalBounds().reduced (16));
}
