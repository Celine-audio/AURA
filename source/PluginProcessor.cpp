#include "PluginProcessor.h"
#include "PluginEditor.h"

#include <cstring>

namespace
{
    // A sidechain quieter than this is not something anyone is trying to match to.
    // Deliberately well above the noise floor: an enabled-but-unrouted bus can carry
    // dither, denormals or whatever was last in that memory, and -60 dBFS is far
    // enough above all of it to not be fooled, while still catching a quiet take.
    constexpr float sidechainSilenceThreshold = 1.0e-3f;

    /** True when two buses hold the same samples. Some hosts answer "no sidechain" by
        echoing the main input into it, which is signal, but not a sidechain. */
    bool buffersMatch (const juce::AudioBuffer<float>& a, const juce::AudioBuffer<float>& b,
                       int numSamples) noexcept
    {
        const auto channels = juce::jmin (a.getNumChannels(), b.getNumChannels());

        if (channels <= 0)
            return false;

        // Bit patterns rather than values: the question is whether the host copied the
        // buffer, not whether the two are numerically close.
        for (int ch = 0; ch < channels; ++ch)
            if (std::memcmp (a.getReadPointer (ch), b.getReadPointer (ch),
                             (size_t) numSamples * sizeof (float)) != 0)
                return false;

        return true;
    }

    // Per-channel read pointer that falls back to channel 0 on a mono bus, so a mono
    // source still fills both capture channels (with identical data).
    const float* channelPointer (const juce::AudioBuffer<float>& buffer, int channel) noexcept
    {
        if (buffer.getNumChannels() <= 0)
            return nullptr;

        return buffer.getReadPointer (juce::jmin (channel, buffer.getNumChannels() - 1));
    }
}

//==============================================================================
PluginProcessor::PluginProcessor()
     : AudioProcessor (BusesProperties()
                       .withInput  ("Input",     juce::AudioChannelSet::stereo(), true)
                       .withOutput ("Output",    juce::AudioChannelSet::stereo(), true)
                       .withInput  ("Sidechain", juce::AudioChannelSet::stereo(), false)),
       apvts (*this, nullptr, "PARAMETERS", Parameters::createLayout())
{
    // The live analyzers always run (while a UI is open) using a decaying average.
    for (auto* analyzer : { &liveOutput, &liveReference })
    {
        analyzer->setExponentialMode (true);
        analyzer->setCapturing (true);
    }

    engine.onLatencyChanged = [this] (int samples) { setLatencySamples (samples); };
    engine.setSettings (currentSettings());

    for (auto* id : ParamID::filterShaping)
        apvts.addParameterListener (id, this);
}

PluginProcessor::~PluginProcessor()
{
    for (auto* id : ParamID::filterShaping)
        apvts.removeParameterListener (id, this);
}

//==============================================================================
MatchEngine::Settings PluginProcessor::currentSettings() const
{
    const auto value = [this] (const char* id) { return apvts.getRawParameterValue (id)->load(); };

    MatchEngine::Settings settings;
    settings.design.amount = value (ParamID::amount);
    settings.design.smoothingOctaves = value (ParamID::smoothing);
    settings.design.lowFreqHz = value (ParamID::lowFreq);
    settings.design.highFreqHz = value (ParamID::highFreq);
    settings.link = value (ParamID::link);

    // The choice parameter's raw value is its index: 0 is Linear.
    settings.linearPhase = value (ParamID::phase) < 0.5f;

    return settings;
}

void PluginProcessor::parameterChanged (const juce::String&, float)
{
    // Called on whatever thread moved the parameter; the engine defers the heavy
    // rebuild to the message thread itself.
    engine.setSettings (currentSettings());
}

void PluginProcessor::setUiActive (bool active)
{
    const auto wasActive = uiActive.exchange (active);

    // Nothing has been feeding the live analyzers since the last editor closed, so
    // their decaying averages are frozen on whatever was playing back then. Start the
    // new editor from silence rather than from a stale trace.
    if (active && ! wasActive)
    {
        liveOutput.reset();
        liveReference.reset();
    }
}

//==============================================================================
void PluginProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    engine.prepare (sampleRate, samplesPerBlock, juce::jmax (1, getMainBusNumOutputChannels()));

    // Purely for the moving display, so they always start clean.
    liveOutput.reset();
    liveReference.reset();

    // Re-decide where the reference comes from: a layout change is exactly how a
    // sidechain gets connected or removed.
    sidechainCarriesSignal.store (false);

    // Generous on purpose. A host is supposed to keep to the block size it declares
    // here, but the response to one that does not must never be to allocate on the
    // audio thread -- see processBlock, which trims instead.
    monoScratch.assign ((size_t) juce::jmax (1, samplesPerBlock) * 2, 0.0f);

    const juce::dsp::ProcessSpec spec { sampleRate,
                                        (juce::uint32) samplesPerBlock,
                                        (juce::uint32) juce::jmax (1, getMainBusNumOutputChannels()) };
    outputGain.prepare (spec);
    outputGain.setRampDurationSeconds (0.05);
    outputGain.reset();
}

void PluginProcessor::releaseResources()
{
    engine.release();
}

bool PluginProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto mainIn  = layouts.getMainInputChannelSet();
    const auto mainOut = layouts.getMainOutputChannelSet();

    if (mainOut != juce::AudioChannelSet::mono() && mainOut != juce::AudioChannelSet::stereo())
        return false;

    if (mainIn != mainOut)
        return false;

    // The sidechain is optional; when present it may be mono or stereo.
    const auto side = layouts.getChannelSet (true, 1);

    return side.isDisabled()
        || side == juce::AudioChannelSet::mono()
        || side == juce::AudioChannelSet::stereo();
}

//==============================================================================
const float* PluginProcessor::mixToMono (const juce::AudioBuffer<float>& bus, int numSamples) noexcept
{
    const auto numChannels = bus.getNumChannels();

    if (numChannels <= 0 || numSamples <= 0)
        return nullptr;

    auto* dest = monoScratch.data();

    juce::FloatVectorOperations::copy (dest, bus.getReadPointer (0), numSamples);

    for (int ch = 1; ch < numChannels; ++ch)
        juce::FloatVectorOperations::add (dest, bus.getReadPointer (ch), numSamples);

    if (numChannels > 1)
        juce::FloatVectorOperations::multiply (dest, 1.0f / (float) numChannels, numSamples);

    return dest;
}

juce::AudioBuffer<float> PluginProcessor::chooseReferenceBus (juce::AudioBuffer<float>& buffer,
                                                              const juce::AudioBuffer<float>& mainInput,
                                                              int numSamples) noexcept
{
    // By value, and that is not a copy of the audio: both candidates are buffers that
    // refer to channels they do not own, and AudioBuffer's copy constructor copies
    // samples only when it owns them. For a referring buffer it copies the channel
    // pointers, which for anything up to 32 channels live in an array inside the
    // object. So this allocates nothing, which is the only reason it can be here.

    // Hosts differ on what "no sidechain" looks like, and an enabled bus is not
    // evidence of a connection: Logic leaves the sidechain bus enabled whatever the
    // Side Chain menu says. So the bus has to prove itself — it must carry real
    // signal, and that signal must not simply be the main input handed back. Once it
    // has proved itself the answer latches, so a quiet passage in the reference track
    // cannot hand the reference back to the main input halfway through a Learn.
    const auto* bus = getBus (true, 1);

    if (bus == nullptr || ! bus->isEnabled())
    {
        referenceUsingSidechain.store (false);
        return mainInput;
    }

    auto sidechain = getBusBuffer (buffer, true, 1);

    // getBusBuffer indexes into the shared block by channel offset, so a host that
    // enabled the bus without widening the buffer would have us reading whatever
    // follows the main input in memory.
    const auto needed = bus->getNumberOfChannels();
    const bool addressable = sidechain.getNumChannels() >= needed
                          && buffer.getNumChannels() >= getMainBusNumInputChannels() + needed;

    if (addressable && ! sidechainCarriesSignal.load()
        && sidechain.getMagnitude (0, numSamples) > sidechainSilenceThreshold
        && ! buffersMatch (sidechain, mainInput, numSamples))
        sidechainCarriesSignal.store (true);

    const bool active = addressable && sidechainCarriesSignal.load();
    referenceUsingSidechain.store (active);

    return active ? sidechain : mainInput;
}

void PluginProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages)
{
    juce::ignoreUnused (midiMessages);

    juce::ScopedNoDenormals noDenormals;

    auto mainInput = getBusBuffer (buffer, true, 0);
    auto mainOutput = getBusBuffer (buffer, false, 0);
    const auto numSamples = buffer.getNumSamples();

    // Clear any output channels that have no matching input.
    for (auto i = getTotalNumInputChannels(); i < getTotalNumOutputChannels(); ++i)
        buffer.clear (i, 0, numSamples);

    const auto referenceInput = chooseReferenceBus (buffer, mainInput, numSamples);

    // The live traces are the only thing that needs the mono scratch, and they are a
    // picture rather than audio. If a host overruns the block size it promised, show
    // it as much as fits rather than growing a buffer inside the callback: an
    // allocation here can block, and a slightly short display frame cannot.
    const bool live = uiActive.load();
    const auto liveSamples = juce::jmin (numSamples, (int) monoScratch.size());

    // --- Capture (pre-EQ), one analyzer per channel ---------------------------
    for (int ch = 0; ch < 2; ++ch)
    {
        engine.pushSource (ch, channelPointer (mainInput, ch), numSamples);
        engine.pushReference (ch, channelPointer (referenceInput, ch), numSamples);
    }

    // --- Live reference, sampled pre-EQ ---------------------------------------
    if (live)
        if (const auto* mono = mixToMono (referenceInput, liveSamples))
            liveReference.pushBlock (mono, liveSamples);

    // --- Apply the matched EQ -------------------------------------------------
    const bool bypassed = apvts.getRawParameterValue (ParamID::bypass)->load() > 0.5f;

    if (! bypassed)
        engine.process (mainOutput);

    juce::dsp::AudioBlock<float> block (mainOutput);
    juce::dsp::ProcessContextReplacing<float> context (block);

    // Bypass takes the trim out with the EQ, so A/B-ing compares like with like.
    outputGain.setGainDecibels (bypassed ? 0.0f : apvts.getRawParameterValue (ParamID::outputGain)->load());
    outputGain.process (context);

    // --- Live "current" output, sampled post-EQ -------------------------------
    if (live)
        if (const auto* mono = mixToMono (mainOutput, liveSamples))
            liveOutput.pushBlock (mono, liveSamples);
}

//==============================================================================
juce::Result PluginProcessor::exportImpulseResponse (const juce::File& file, const IrExport::Options& options)
{
    const auto& curves = engine.getCorrectionCurves();

    if (! curves.isValid())
        return juce::Result::fail ("Capture a source and a reference first: there is no curve to export.");

    // Rendered at the length the plugin's own filter runs at, whatever the panel
    // asked for, so the file is the filter rather than an approximation of it.
    auto sized = options;
    sized.length = MatchEngine::irLength;

    const auto buffer = IrExport::buildBuffer (curves.leftDb, curves.rightDb, sized);
    return IrExport::writeWav (file, buffer, engine.getSampleRate());
}

juce::AudioProcessorEditor* PluginProcessor::createEditor()
{
    return new PluginEditor (*this);
}

//==============================================================================
void PluginProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    const auto state = apvts.copyState();

    if (! state.isValid())
        return;

    const auto xml = state.createXml();
    engine.saveTo (*xml->createNewChildElement ("MatchState"));
    copyXmlToBinary (*xml, destData);
}

void PluginProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    auto xml = getXmlFromBinary (data, sizeInBytes);

    if (xml == nullptr || ! xml->hasTagName (apvts.state.getType()))
        return;

    // Lifted out before replaceState, so the match doesn't end up as a stray child of
    // the parameter tree — and restored after, because the curve has to be derived
    // with the settings from this session, and replaceState fires no listeners.
    std::unique_ptr<juce::XmlElement> match;

    if (auto* stored = xml->getChildByName ("MatchState"))
    {
        xml->removeChildElement (stored, false);
        match.reset (stored);
    }

    apvts.replaceState (juce::ValueTree::fromXml (*xml));
    engine.setSettings (currentSettings());

    if (match != nullptr)
        engine.restoreFrom (*match);
}

//==============================================================================
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new PluginProcessor();
}
