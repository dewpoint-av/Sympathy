#include "PluginProcessor.h"
#include "PluginEditor.h"

namespace
{
    constexpr int kStateVersion = 2;
    const char* const kVersionProperty = "sympathyStateVersion";

    using FloatP  = juce::AudioParameterFloat;
    using BoolP   = juce::AudioParameterBool;
    using ChoiceP = juce::AudioParameterChoice;
    using IntP    = juce::AudioParameterInt;
    using Range   = juce::NormalisableRange<float>;

    juce::StringArray lfoSyncNames() { return { "Hz", "Straight", "Triplet", "Dotted" }; }
    juce::StringArray lfoDivisionNames()
    {
        return { "16 bars", "8 bars", "4 bars", "2 bars", "1 bar", "1/2", "1/4", "1/8", "1/16", "1/32" };
    }

    // Refraction's LFO menu order, mapped to the shared LFO's shapes.
    constexpr int kRefractLfoShapes[6] = {
        static_cast<int> (sympathy::LfoShape::Sine),
        static_cast<int> (sympathy::LfoShape::Triangle),
        static_cast<int> (sympathy::LfoShape::SawUp),
        static_cast<int> (sympathy::LfoShape::Square),
        static_cast<int> (sympathy::LfoShape::Stepped),
        static_cast<int> (sympathy::LfoShape::Smooth)
    };

    juce::AudioProcessorValueTreeState::ParameterLayout createLayout()
    {
        juce::AudioProcessorValueTreeState::ParameterLayout layout;
        const auto addFloat = [&] (const char* id, const char* name, Range range, float def)
        {
            layout.add (std::make_unique<FloatP> (id, name, range, def));
        };
        const auto addBool = [&] (const char* id, const char* name, bool def)
        {
            layout.add (std::make_unique<BoolP> (id, name, def));
        };
        const auto addChoice = [&] (const char* id, const char* name, juce::StringArray items, int def)
        {
            layout.add (std::make_unique<ChoiceP> (id, name, items, def));
        };

        // ---- the object ----------------------------------------------------
        addChoice ("resType", "Resonator Type",
                   { "Beam", "Marimba", "String", "Membrane", "Plate", "Pipe", "Tube" }, 0);
        addChoice ("quality", "Quality", { "Eco", "High" }, 1);
        addFloat ("tune", "Tune", Range (20.0f, 2000.0f, 0.0f, 0.35f), 220.0f);
        addFloat ("decay", "Decay", Range (0.05f, 20.0f, 0.0f, 0.45f), 2.5f);
        addFloat ("materialTone", "Material", Range (-1.0f, 1.0f), -0.4f);
        addFloat ("brightness", "Brightness", Range (0.0f, 1.0f), 0.65f);
        addFloat ("inharmonicity", "Inharmonics", Range (-1.0f, 1.0f), 0.0f);
        addFloat ("hit", "Hit", Range (0.0f, 1.0f), 0.3f);
        addFloat ("ratio", "Ratio", Range (0.2f, 1.0f), 0.7f);
        addFloat ("radius", "Radius", Range (0.0f, 1.0f), 0.5f);
        addFloat ("opening", "Opening", Range (0.0f, 1.0f), 1.0f);
        addFloat ("body", "Body", Range (0.0f, 1.0f), 0.8f);

        // ---- stereo --------------------------------------------------------
        addFloat ("width", "Width", Range (0.0f, 1.0f), 1.0f);
        addFloat ("spread", "Spread", Range (0.0f, 1.0f), 0.1f);
        addFloat ("posL", "Pos L", Range (0.0f, 1.0f), 0.3f);
        addFloat ("posR", "Pos R", Range (0.0f, 1.0f), 0.6f);

        // ---- strings -------------------------------------------------------
        addFloat ("strings", "Strings", Range (0.0f, 1.0f), 0.5f);
        addFloat ("stringTune", "String Tune", Range (-12.0f, 24.0f), 7.0f);
        addFloat ("stringDamp", "String Damping", Range (0.0f, 1.0f), 0.35f);
        addFloat ("stringDecay", "String Decay", Range (0.05f, 20.0f, 0.0f, 0.45f), 3.0f);

        // ---- pitch LFO -----------------------------------------------------
        addFloat ("resLfoAmount", "Pitch LFO Amount", Range (0.0f, 12.0f, 0.0f, 0.5f), 0.0f);
        addFloat ("resLfoRate", "Pitch LFO Rate", Range (0.02f, 50.0f, 0.0f, 0.3f), 1.0f);
        addChoice ("resLfoShape", "Pitch LFO Shape",
                   { "Sine", "Square", "Triangle", "Saw Up", "Saw Down", "Stepped", "Smooth" }, 0);
        addChoice ("resLfoSync", "Pitch LFO Sync", lfoSyncNames(), 0);
        addChoice ("resLfoDiv", "Pitch LFO Division", lfoDivisionNames(), 6);
        addFloat ("resLfoPhase", "Pitch LFO Phase", Range (0.0f, 360.0f), 0.0f);

        // ---- MIDI ----------------------------------------------------------
        addBool ("midiFreq", "MIDI Frequency", false);
        addChoice ("midiPriority", "MIDI Priority", { "Last", "Low" }, 0);
        addFloat ("midiTranspose", "MIDI Transpose", Range (-48.0f, 48.0f, 1.0f), 0.0f);
        addFloat ("midiFine", "MIDI Fine", Range (-100.0f, 100.0f), 0.0f);
        addFloat ("pbRange", "Pitch Bend Range", Range (0.0f, 24.0f, 1.0f), 2.0f);
        addFloat ("offDecay", "Off Decay", Range (0.0f, 1.0f), 0.0f);

        // ---- global --------------------------------------------------------
        addFloat ("exciter", "Exciter", Range (0.0f, 1.0f), 0.0f);
        addFloat ("input", "Input", Range (0.0f, 2.0f), 1.0f);
        addFloat ("bleed", "Bleed", Range (0.0f, 1.0f), 0.0f);
        addFloat ("mix", "Mix", Range (0.0f, 1.0f), 0.5f);
        addFloat ("output", "Output", Range (0.0f, 2.0f), 1.0f);
        addBool ("autoGain", "Auto Gain", true);
        addBool ("filterOn", "Input Filter", false);
        addFloat ("filterFreq", "Filter Freq", Range (20.0f, 20000.0f, 0.0f, 0.3f), 1000.0f);
        addFloat ("filterBw", "Filter Bandwidth", Range (0.1f, 8.0f, 0.0f, 0.5f), 2.0f);

        // ---- refraction ----------------------------------------------------
        addBool ("refractOn", "Refraction", false);
        addChoice ("refractMode", "Refraction Mode",
                   { "Unison", "Bandpass", "Comb", "Bitcrusher", "Distortion", "Harmonizer" }, 0);
        addChoice ("refractSource", "Refraction Source", { "Resonator", "Input" }, 0);
        layout.add (std::make_unique<IntP> ("refractVoices", "Refraction Voices", 1, 8, 4));
        addFloat ("refractAmount", "Refraction Amount", Range (0.0f, 1.0f), 0.40f);
        addFloat ("refractPrimary", "Refraction Primary", Range (0.0f, 1.0f), 0.5f);
        addFloat ("refractSecondary", "Refraction Secondary", Range (0.0f, 1.0f), 0.5f);
        addChoice ("refractAlgo", "Distortion Algorithm",
                   { "Tape", "Germanium", "Soft Bounce", "Sine Folder" }, 0);
        addChoice ("refractChord", "Harmonizer Chord",
                   { "Octave", "5", "Minor", "Minor 7", "Minor 9", "Major", "7", "Major 7",
                     "Major 9", "6/9", "Sus 2", "Sus 4" }, 0);
        addFloat ("refractMix", "Refraction Mix", Range (0.0f, 1.0f), 0.35f);
        addFloat ("refractSpread", "Refraction Spread", Range (0.0f, 1.0f), 0.70f);
        addFloat ("refractHp", "Refraction HP", Range (20.0f, 20000.0f, 0.0f, 0.3f), 150.0f);
        addFloat ("refractLp", "Refraction LP", Range (20.0f, 20000.0f, 0.0f, 0.3f), 20000.0f);
        addChoice ("refractLfoShape", "Refraction LFO Shape",
                   { "Sine", "Triangle", "Saw", "Square", "Sample & Hold", "Sample & Glide" }, 0);
        addChoice ("refractLfoSync", "Refraction LFO Sync", lfoSyncNames(), 0);
        addChoice ("refractLfoDiv", "Refraction LFO Division", lfoDivisionNames(), 6);
        addFloat ("refractLfoRate", "Refraction LFO Rate", Range (0.025f, 50.0f, 0.0f, 0.3f), 0.25f);
        addFloat ("refractLfoDepth", "Refraction LFO Amount", Range (-1.0f, 1.0f), 0.30f);
        addFloat ("refractLfoPrimary", "Refraction LFO Primary", Range (-1.0f, 1.0f), 0.0f);

        return layout;
    }

    // ---- state from before 2.0 ---------------------------------------------
    juce::XmlElement* findParam (juce::XmlElement& root, const juce::String& id)
    {
        for (auto* e : root.getChildWithTagNameIterator ("PARAM"))
            if (e->getStringAttribute ("id") == id)
                return e;
        return nullptr;
    }

    void setParam (juce::XmlElement& root, const juce::String& id, double value)
    {
        auto* e = findParam (root, id);
        if (e == nullptr)
        {
            e = root.createNewChildElement ("PARAM");
            e->setAttribute ("id", id);
        }
        e->setAttribute ("value", value);
    }

    double getParam (juce::XmlElement& root, const juce::String& id, double fallback)
    {
        auto* e = findParam (root, id);
        return e != nullptr ? e->getDoubleAttribute ("value", fallback) : fallback;
    }

    void removeParam (juce::XmlElement& root, const juce::String& id)
    {
        if (auto* e = findParam (root, id))
            root.removeChildElement (e, true);
    }

    // Maps a 1.x session onto the closest 2.0 settings. The old materials were all one
    // formula, so this picks the nearest real object rather than an identical sound.
    void migrateFromV1 (juce::XmlElement& root)
    {
        struct Mapping { int type; float tone; };
        static constexpr Mapping materials[7] = {
            { sympathy::kString,  -0.3f },   // String
            { sympathy::kBeam,    -0.8f },   // Wood
            { sympathy::kBeam,     0.5f },   // Metal
            { sympathy::kMarimba,  0.8f },   // Glass
            { sympathy::kPlate,    0.1f },   // Plate
            { sympathy::kMembrane,-0.9f },   // Body
            { sympathy::kString,   0.0f }    // Synth
        };

        const int old = juce::jlimit (0, 6, juce::roundToInt (getParam (root, "material", 1.0)));
        setParam (root, "resType", materials[old].type);
        setParam (root, "materialTone", materials[old].tone);
        removeParam (root, "material");

        // 1.x was a single, centred image, so nothing new may widen it.
        setParam (root, "spread", 0.0);
        setParam (root, "width", 1.0);
        setParam (root, "posL", 0.3);
        setParam (root, "posR", 0.3);

        // The old stack becomes its most characterful active stage.
        const auto on = [&] (const char* id) { return getParam (root, id, 0.0) > 0.5; };
        int mode = sympathy::kModeUnison;
        if      (on ("refractHarmonizer")) mode = sympathy::kModeHarmonizer;
        else if (on ("refractComb"))       mode = sympathy::kModeComb;
        else if (on ("refractBandpass"))   mode = sympathy::kModeBandpass;
        else if (on ("refractDistort"))    mode = sympathy::kModeDistortion;
        else if (on ("refractBitcrush"))   mode = sympathy::kModeBitcrush;
        setParam (root, "refractMode", mode);
        for (const auto* id : { "refractUnison", "refractHarmonizer", "refractBandpass",
                                "refractDistort", "refractBitcrush", "refractComb" })
            removeParam (root, id);

        // 1.x fed Refraction from the resonator with no input filter.
        setParam (root, "refractSource", sympathy::kSourceResonator);
        setParam (root, "refractHp", 20.0);
    }
}

SympathyAudioProcessor::SympathyAudioProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true))
    , parameters (*this, nullptr, "Parameters", createLayout())
{
    parameters.state.setProperty (kVersionProperty, kStateVersion, nullptr);
}

SympathyAudioProcessor::~SympathyAudioProcessor() = default;

const juce::String SympathyAudioProcessor::getName() const
{
    return JucePlugin_Name;
}

bool SympathyAudioProcessor::acceptsMidi() const           { return true; }
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

void SympathyAudioProcessor::updateMidi (const juce::MidiBuffer& midi)
{
    for (const auto meta : midi)
    {
        const auto msg = meta.getMessage();

        if (msg.isNoteOn())
        {
            const int note = msg.getNoteNumber();
            // A repeat moves to the top; a full list drops its oldest note.
            int keep = 0;
            for (int i = 0; i < numHeld; ++i)
                if (heldNotes[static_cast<size_t> (i)] != note)
                    heldNotes[static_cast<size_t> (keep++)] = heldNotes[static_cast<size_t> (i)];
            numHeld = keep;
            if (numHeld == static_cast<int> (heldNotes.size()))
            {
                for (int i = 1; i < numHeld; ++i)
                    heldNotes[static_cast<size_t> (i - 1)] = heldNotes[static_cast<size_t> (i)];
                --numHeld;
            }
            heldNotes[static_cast<size_t> (numHeld++)] = note;
            midiState.hasPlayed = true;
        }
        else if (msg.isNoteOff())
        {
            const int note = msg.getNoteNumber();
            int keep = 0;
            for (int i = 0; i < numHeld; ++i)
                if (heldNotes[static_cast<size_t> (i)] != note)
                    heldNotes[static_cast<size_t> (keep++)] = heldNotes[static_cast<size_t> (i)];
            numHeld = keep;
        }
        else if (msg.isAllNotesOff() || msg.isAllSoundOff())
        {
            numHeld = 0;
        }
        else if (msg.isPitchWheel())
        {
            midiState.bend = juce::jlimit (-1.0f, 1.0f, static_cast<float> (msg.getPitchWheelValue() - 8192) / 8192.0f);
        }
    }

    midiState.anyHeld = numHeld > 0;
    if (numHeld > 0)
    {
        // With no notes held, pitch stays on the last one so the tail doesn't jump.
        const bool lowPriority = parameters.getRawParameterValue ("midiPriority")->load() > 0.5f;
        int chosen = heldNotes[static_cast<size_t> (numHeld - 1)];
        if (lowPriority)
            for (int i = 0; i < numHeld; ++i)
                chosen = juce::jmin (chosen, heldNotes[static_cast<size_t> (i)]);
        midiState.note = chosen;
    }
    engine.setMidi (midiState);
}

void SympathyAudioProcessor::updateHostTime()
{
    sympathy::HostTime t;
    if (auto* ph = getPlayHead())
    {
        if (const auto pos = ph->getPosition())
        {
            t.playing = pos->getIsPlaying();
            if (const auto bpm = pos->getBpm())          t.bpm = *bpm;
            if (const auto ppq = pos->getPpqPosition())  t.ppq = *ppq;
            else                                         t.playing = false;
        }
    }
    engine.setHostTime (t);
}

void SympathyAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer,
                                           juce::MidiBuffer& midiMessages)
{
    juce::ScopedNoDenormals noDenormals;

    const int totalIn  = getTotalNumInputChannels();
    const int totalOut = getTotalNumOutputChannels();

    for (int i = totalIn; i < totalOut; ++i)
        buffer.clear (i, 0, buffer.getNumSamples());

    if (totalIn == 0 || totalOut == 0)
        return;

    updateMidi (midiMessages);
    updateHostTime();

    // Pull the current parameter values into the engine. In JUCE 8,
    // getRawParameterValue() returns std::atomic<float>*, hence ->load().
    const auto param = [&] (const char* id)
    {
        return parameters.getRawParameterValue (id)->load();
    };
    const auto index = [&] (const char* id) { return juce::roundToInt (param (id)); };

    sympathy::Params p;
    p.model.type        = index ("resType");
    p.model.highQuality = index ("quality") == 1;
    p.model.tune        = param ("tune");
    p.model.decay       = param ("decay");
    p.model.material    = param ("materialTone");
    p.model.brightness  = param ("brightness");
    p.model.inharm      = param ("inharmonicity");
    p.model.hit         = param ("hit");
    p.model.ratio       = param ("ratio");
    p.model.radius      = param ("radius");
    p.model.opening     = param ("opening");
    p.posL   = param ("posL");
    p.posR   = param ("posR");
    p.spread = param ("spread");
    p.width  = param ("width");

    p.body        = param ("body");
    p.strings     = param ("strings");
    p.stringTune  = param ("stringTune");
    p.stringDamp  = param ("stringDamp");
    p.stringDecay = param ("stringDecay");
    p.exciter     = param ("exciter");
    p.input       = param ("input");
    p.bleed       = param ("bleed");
    p.mix         = param ("mix");
    p.output      = param ("output");
    p.autoGain    = param ("autoGain") > 0.5f;
    p.filterOn    = param ("filterOn") > 0.5f;
    p.filterHz    = param ("filterFreq");
    p.filterBw    = param ("filterBw");
    p.refractSource = index ("refractSource");

    p.lfo.shape    = index ("resLfoShape");
    p.lfo.sync     = index ("resLfoSync");
    p.lfo.division = index ("resLfoDiv");
    p.lfo.rateHz   = param ("resLfoRate");
    p.lfoSemis     = param ("resLfoAmount");
    p.lfoPhase     = param ("resLfoPhase") / 360.0f;

    p.midiFrequency = param ("midiFreq") > 0.5f;
    p.midiTranspose = param ("midiTranspose");
    p.midiFine      = param ("midiFine");
    p.pbRange       = param ("pbRange");
    p.offDecay      = param ("offDecay");

    engine.setParams (p);

    sympathy::RefractParams rp;
    rp.on        = param ("refractOn") > 0.5f;
    rp.mode      = index ("refractMode");
    rp.voices    = index ("refractVoices");
    rp.amount    = param ("refractAmount");
    rp.primary   = param ("refractPrimary");
    rp.secondary = param ("refractSecondary");
    rp.algorithm = index ("refractAlgo");
    rp.chord     = index ("refractChord");
    rp.mix       = param ("refractMix");
    rp.spread    = param ("refractSpread");
    rp.hpHz      = param ("refractHp");
    rp.lpHz      = param ("refractLp");
    rp.lfo.shape    = kRefractLfoShapes[juce::jlimit (0, 5, index ("refractLfoShape"))];
    rp.lfo.sync     = index ("refractLfoSync");
    rp.lfo.division = index ("refractLfoDiv");
    rp.lfo.rateHz   = param ("refractLfoRate");
    rp.lfoToAmount  = param ("refractLfoDepth");
    rp.lfoToPrimary = param ("refractLfoPrimary");
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
    if (xmlState == nullptr || ! xmlState->hasTagName (parameters.state.getType()))
        return;

    if (xmlState->getIntAttribute (kVersionProperty, 1) < 2)
        migrateFromV1 (*xmlState);

    parameters.replaceState (juce::ValueTree::fromXml (*xmlState));
    parameters.state.setProperty (kVersionProperty, kStateVersion, nullptr);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new SympathyAudioProcessor();
}
