#include "PluginProcessor.h"
#include "PluginEditor.h"

namespace
{
    juce::StringArray materialNames()
    {
        juce::StringArray names;
        for (int i = 0; i < sympathy::kNumMaterials; ++i)
            names.add (sympathy::kMaterials[i].name);
        return names;
    }

}

SympathyAudioProcessor::SympathyAudioProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true))
    , parameters (*this, nullptr, "Parameters",
        {
            std::make_unique<juce::AudioParameterChoice> ("material", "Material",
                materialNames(), 1),
            std::make_unique<juce::AudioParameterFloat> ("tune", "Tune",
                juce::NormalisableRange<float> (20.0f, 2000.0f, 0.0f, 0.35f), 220.0f),
            std::make_unique<juce::AudioParameterFloat> ("inharmonicity", "Inharmonicity",
                0.0f, 1.0f, 0.35f),
            std::make_unique<juce::AudioParameterFloat> ("decay", "Decay",
                juce::NormalisableRange<float> (0.05f, 20.0f, 0.0f, 0.45f), 2.5f),
            std::make_unique<juce::AudioParameterFloat> ("brightness", "Brightness",
                0.0f, 1.0f, 0.65f),
            std::make_unique<juce::AudioParameterFloat> ("body", "Body",
                0.0f, 1.0f, 0.8f),
            std::make_unique<juce::AudioParameterFloat> ("strings", "Strings",
                0.0f, 1.0f, 0.5f),
            std::make_unique<juce::AudioParameterFloat> ("stringTune", "String Tune",
                -12.0f, 24.0f, 7.0f),
            std::make_unique<juce::AudioParameterFloat> ("stringDamp", "String Damping",
                0.0f, 1.0f, 0.35f),
            std::make_unique<juce::AudioParameterFloat> ("stringDecay", "String Decay",
                juce::NormalisableRange<float> (0.05f, 20.0f, 0.0f, 0.45f), 3.0f),
            std::make_unique<juce::AudioParameterFloat> ("exciter", "Exciter",
                0.0f, 1.0f, 0.0f),
            std::make_unique<juce::AudioParameterFloat> ("input", "Input",
                0.0f, 2.0f, 1.0f),
            std::make_unique<juce::AudioParameterFloat> ("mix", "Mix",
                0.0f, 1.0f, 0.5f),
            std::make_unique<juce::AudioParameterFloat> ("output", "Output",
                0.0f, 2.0f, 1.0f),
            std::make_unique<juce::AudioParameterBool> ("autoGain", "Auto Gain",
                true),
            std::make_unique<juce::AudioParameterBool> ("refractOn", "Refraction",
                false),
            std::make_unique<juce::AudioParameterBool> ("refractUnison", "Refraction Unison",
                true),
            std::make_unique<juce::AudioParameterBool> ("refractHarmonizer", "Refraction Harmonizer",
                false),
            std::make_unique<juce::AudioParameterBool> ("refractBandpass", "Refraction Bandpass",
                false),
            std::make_unique<juce::AudioParameterBool> ("refractDistort", "Refraction Distortion",
                false),
            std::make_unique<juce::AudioParameterBool> ("refractBitcrush", "Refraction Bitcrusher",
                false),
            std::make_unique<juce::AudioParameterBool> ("refractComb", "Refraction Comb",
                false),
            std::make_unique<juce::AudioParameterInt> ("refractVoices", "Refraction Voices",
                2, 8, 4),
            std::make_unique<juce::AudioParameterFloat> ("refractAmount", "Refraction Amount",
                0.0f, 1.0f, 0.40f),
            std::make_unique<juce::AudioParameterFloat> ("refractMix", "Refraction Mix",
                0.0f, 1.0f, 0.35f),
            std::make_unique<juce::AudioParameterFloat> ("refractSpread", "Refraction Spread",
                0.0f, 1.0f, 0.70f),
            std::make_unique<juce::AudioParameterFloat> ("refractLfoRate", "Refraction LFO Rate",
                0.02f, 8.0f, 0.25f),
            std::make_unique<juce::AudioParameterFloat> ("refractLfoDepth", "Refraction LFO Depth",
                0.0f, 1.0f, 0.30f)
        })
{
}

SympathyAudioProcessor::~SympathyAudioProcessor() = default;

const juce::String SympathyAudioProcessor::getName() const
{
    return JucePlugin_Name;
}

bool SympathyAudioProcessor::acceptsMidi() const           { return false; }
bool SympathyAudioProcessor::producesMidi() const          { return false; }
bool SympathyAudioProcessor::isMidiEffect() const          { return false; }

double SympathyAudioProcessor::getTailLengthSeconds() const
{
    return 25.0;
}

int SympathyAudioProcessor::getNumPrograms()               { return 1; }
int SympathyAudioProcessor::getCurrentProgram()            { return 0; }

void SympathyAudioProcessor::setCurrentProgram (int index) { juce::ignoreUnused (index); }

const juce::String SympathyAudioProcessor::getProgramName (int index)
{
    juce::ignoreUnused (index);
    return {};
}

void SympathyAudioProcessor::changeProgramName (int index, const juce::String& newName)
{
    juce::ignoreUnused (index, newName);
}

void SympathyAudioProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    engine.prepare (sampleRate, samplesPerBlock);
}

void SympathyAudioProcessor::releaseResources()
{
    engine.reset();
}

bool SympathyAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto mainIn  = layouts.getMainInputChannelSet();
    const auto mainOut = layouts.getMainOutputChannelSet();

    if (mainIn != juce::AudioChannelSet::mono()
        && mainIn != juce::AudioChannelSet::stereo())
        return false;

    if (mainOut != juce::AudioChannelSet::mono()
        && mainOut != juce::AudioChannelSet::stereo())
        return false;

    return mainIn == mainOut;
}

void SympathyAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer,
                                           juce::MidiBuffer& midiMessages)
{
    juce::ignoreUnused (midiMessages);
    juce::ScopedNoDenormals noDenormals;

    const int totalIn  = getTotalNumInputChannels();
    const int totalOut = getTotalNumOutputChannels();

    for (int i = totalIn; i < totalOut; ++i)
        buffer.clear (i, 0, buffer.getNumSamples());

    if (totalIn == 0 || totalOut == 0)
        return;

    // Pull the current parameter values into the engine. In JUCE 8,
    // getRawParameterValue() returns std::atomic<float>*, hence ->load().
    const auto param = [&] (const char* id)
    {
        return parameters.getRawParameterValue (id)->load();
    };

    sympathy::Params p;
    p.material      = juce::roundToInt (param ("material"));
    p.tune          = param ("tune");
    p.inharmonicity = param ("inharmonicity");
    p.decay         = param ("decay");
    p.brightness    = param ("brightness");
    p.body          = param ("body");
    p.strings       = param ("strings");
    p.stringTune    = param ("stringTune");
    p.stringDamp    = param ("stringDamp");
    p.stringDecay   = param ("stringDecay");
    p.exciter       = param ("exciter");
    p.input         = param ("input");
    p.mix           = param ("mix");
    p.output        = param ("output");
    p.autoGain      = param ("autoGain") > 0.5f;

    engine.setParams (p);

    sympathy::RefractParams rp;
    rp.on       = param ("refractOn") > 0.5f;
    rp.unison     = param ("refractUnison") > 0.5f;
    rp.harmonizer = param ("refractHarmonizer") > 0.5f;
    rp.bandpass   = param ("refractBandpass") > 0.5f;
    rp.distort    = param ("refractDistort") > 0.5f;
    rp.bitcrush   = param ("refractBitcrush") > 0.5f;
    rp.comb       = param ("refractComb") > 0.5f;
    rp.voices   = juce::roundToInt (param ("refractVoices"));
    rp.amount   = param ("refractAmount");
    rp.mix      = param ("refractMix");
    rp.spread   = param ("refractSpread");
    rp.lfoRate  = param ("refractLfoRate");
    rp.lfoDepth = param ("refractLfoDepth");
    engine.setRefractParams (rp);

    const int numSamples = buffer.getNumSamples();

    const float* inL = buffer.getReadPointer (0);
    const float* inR = (totalIn > 1) ? buffer.getReadPointer (1) : inL;

    float* outL = buffer.getWritePointer (0);
    float* outR = (totalOut > 1) ? buffer.getWritePointer (1) : outL;

    engine.process (inL, inR, outL, outR, numSamples);
}

bool SympathyAudioProcessor::hasEditor() const
{
    return true;
}

juce::AudioProcessorEditor* SympathyAudioProcessor::createEditor()
{
    return new SympathyAudioProcessorEditor (*this);
}

void SympathyAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto state = parameters.copyState();
    std::unique_ptr<juce::XmlElement> xml (state.createXml());
    copyXmlToBinary (*xml, destData);
}

void SympathyAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    std::unique_ptr<juce::XmlElement> xmlState (getXmlFromBinary (data, sizeInBytes));
    if (xmlState != nullptr)
        if (xmlState->hasTagName (parameters.state.getType()))
            parameters.replaceState (juce::ValueTree::fromXml (*xmlState));
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new SympathyAudioProcessor();
}
