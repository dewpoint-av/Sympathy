#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include "PluginProcessor.h"
#include "CustomLookAndFeel.h"

class SympathyAudioProcessorEditor : public juce::AudioProcessorEditor
{
public:
    explicit SympathyAudioProcessorEditor (SympathyAudioProcessor&);
    ~SympathyAudioProcessorEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    SympathyLookAndFeel lookAndFeel;

    class Impl;
    std::unique_ptr<Impl> impl;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SympathyAudioProcessorEditor)
};
