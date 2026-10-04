#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_dsp/juce_dsp.h>

#include "Parameters.h"
#include "dsp/AudioFileSpectrum.h"
#include "dsp/IrExport.h"
#include "dsp/MatchEngine.h"
#include "dsp/SpectrumAnalyzer.h"

#if (MSVC)
#include "ipps.h"
#endif

/**
    The host-facing shell: buses, parameters, and routing audio to the pieces that do
    the work. The matching itself lives in MatchEngine, and the moving display's
    spectra come from two analyzers that run only while an editor is open.
*/
class PluginProcessor : public juce::AudioProcessor,
                        private juce::AudioProcessorValueTreeState::Listener,
                        private juce::Timer
{
public:
    using CorrectionCurves = MatchEngine::CorrectionCurves;
    using Side = MatchEngine::Side;

    PluginProcessor();
    ~PluginProcessor() override;

    //==============================================================================
    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;

    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;

    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return JucePlugin_Name; }

    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    // Hosts cope badly with a plugin reporting no programs, so report one we ignore.
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    //==============================================================================
    // AURA control surface (called from the editor / message thread).

    /** Starts or stops a capture. Starting one always begins a fresh take: the
        previous contents of that analyzer are discarded first -- and so is a file
        being read for that side, which the Learn has overtaken. */
    void setCapturing (Side, bool shouldCapture);
    void setSourceCapturing (bool shouldCapture)    { setCapturing (Side::source, shouldCapture); }
    void setReferenceCapturing (bool shouldCapture) { setCapturing (Side::reference, shouldCapture); }

    /** Learns a side's take from an audio file rather than from what is playing.

        The file is read on a worker thread -- a song takes a moment to decode -- and
        lands as that side's take on the message thread, after which `onFinished` is
        called with how it went. It is not called at all if the read is overtaken: by a
        Learn started on that side, by another file, or by the plugin closing. */
    void importTakeFromFile (Side, const juce::File&, std::function<void (const juce::Result&)> onFinished);

    /** True from importTakeFromFile() until the file has landed or been given up. */
    bool isImportingTake (Side side) const noexcept { return importingSide == side; }

    /** The file this side's take came from, or empty when it was learned. */
    juce::String getImportedTakeName (Side side) const { return engine.getImportedTakeName (side); }

    bool isSourceCapturing() const noexcept    { return engine.isCapturing (MatchEngine::Side::source); }
    bool isReferenceCapturing() const noexcept { return engine.isCapturing (MatchEngine::Side::reference); }

    std::int64_t getSourceFrameCount() const noexcept    { return engine.getFrameCount (MatchEngine::Side::source); }
    std::int64_t getReferenceFrameCount() const noexcept { return engine.getFrameCount (MatchEngine::Side::reference); }

    /** True while the reference analyzer is fed by a connected sidechain bus. */
    bool isReferenceUsingSidechain() const noexcept { return referenceUsingSidechain.load(); }

    /** True once a match curve has been computed and loaded. */
    bool isMatched() const noexcept { return engine.isMatched(); }

    /** True when what has been learned has moved on from the match in force, which is
        the cue for the interface to ask for another one. */
    bool isMatchStale() const noexcept { return engine.isMatchStale(); }

    /** Whether pressing Match would do anything. */
    bool canMatch() const { return engine.canMatch(); }

    /** Builds the correction curve from the two captures and loads it into the
        convolution engine. Returns false if either capture is empty. */
    bool performMatch() { return engine.performMatch(); }

    int getFftSize() const noexcept { return engine.getFftSize(); }

    /** What the matched filter costs, in samples, as reported to the host. */
    int getMatchLatencySamples() const noexcept { return engine.getLatencySamples(); }

    /** Taps in the filter the plugin is running, and therefore in an exported IR. */
    int getFilterLength() const noexcept { return MatchEngine::irLength; }

    /** Real-time (exponentially-averaged) spectra for the moving display. "Output" is
        the post-EQ signal you hear now; "reference" is the live reference/sidechain. */
    bool getLiveOutputMagnitudes (std::vector<float>& dest) const { return liveOutput.getAveragedMagnitudes (dest); }
    bool getLiveReferenceMagnitudes (std::vector<float>& dest) const { return liveReference.getAveragedMagnitudes (dest); }

    /** How many frames each live analyzer has produced. The editor watches these to
        tell "quiet" from "not running": a spectrum that has stopped being updated
        should fade out rather than stand still looking like live audio. */
    std::int64_t getLiveOutputFrameCount() const noexcept { return liveOutput.getFrameCount(); }
    std::int64_t getLiveReferenceFrameCount() const noexcept { return liveReference.getFrameCount(); }

    /** The long-term averages a Learn pass built up, summed to one curve per side. */
    bool getLearnedSourceMagnitudes (std::vector<float>& dest) const    { return engine.getLearnedMagnitudes (MatchEngine::Side::source, dest); }
    bool getLearnedReferenceMagnitudes (std::vector<float>& dest) const { return engine.getLearnedMagnitudes (MatchEngine::Side::reference, dest); }

    /** The editor calls this so live analysis only runs while a UI is open. Turning it
        on clears the live analyzers: their averages went stale the moment the last
        editor closed and stopped feeding them. */
    void setUiActive (bool active);

    /** The correction curves the plugin is applying, recomputed only when something
        has actually changed. This is the same data the convolution is built from, so
        the display tracks the knobs in lock-step with what you hear. */
    const CorrectionCurves& getCorrectionCurves() { return engine.getCorrectionCurves(); }

    /** What pressing Match would build now, when that differs from what is applied:
        before the first match, and once the match is out of date. Empty otherwise. */
    const CorrectionCurves& getPreviewCurves() { return engine.getPreviewCurves(); }

    /** Flags the cached curves as out of date. The editor calls this while a capture
        is accumulating; parameter and match changes mark it internally. */
    void markCorrectionDirty() noexcept { engine.markCorrectionDirty(); }

    /** Message thread: renders the current correction to a WAV file. */
    juce::Result exportImpulseResponse (const juce::File& file, const IrExport::Options& options);

    juce::AudioProcessorValueTreeState& getAPVTS() noexcept { return apvts; }

    /** How many times the impulse response has been rebuilt. Exposed so tests can
        pin the drag throttle, which is what keeps knob moves from crackling. */
    int getRebuildCountForTests() const noexcept { return engine.getRebuildCount(); }

private:
    void parameterChanged (const juce::String& parameterID, float newValue) override;
    void timerCallback() override;

    /** Set when a parameter moved on a thread that may not push the settings across
        itself -- which is the audio thread, under host automation. The timer below is
        what turns it back into a call on the message thread, and it is why
        MatchEngine::settings has exactly one writer. */
    std::atomic<bool> settingsDirty { false };

    /** How often that flag is looked at. Automation is the only thing that goes through
        it -- a control moved in the window is pushed across on the spot -- so this only
        has to be fast against the engine's own 120 ms rebuild throttle, and it costs
        nothing while the flag is clear, which is nearly always. */
    static constexpr int settingsPollMs = 30;

    // Reads the curve-shaping parameters into the form the engine works from.
    MatchEngine::Settings currentSettings() const;

    // Sums all channels of a bus into monoScratch and returns it (or nullptr if empty).
    const float* mixToMono (const juce::AudioBuffer<float>& bus, int numSamples) noexcept;

    // The bus the reference is taken from: the sidechain when something is feeding it,
    // otherwise the main input. Latches, so a quiet passage cannot switch it midway.
    juce::AudioBuffer<float> chooseReferenceBus (juce::AudioBuffer<float>& buffer,
                                                 const juce::AudioBuffer<float>& mainInput,
                                                 int numSamples) noexcept;

    juce::AudioProcessorValueTreeState apvts;
    MatchEngine engine;

    // Always-on (while a UI is open), exponentially-averaged analyzers driving the
    // moving traces. Mono-summed: they never feed the correction.
    SpectrumAnalyzer liveOutput, liveReference;

    // Post-EQ trim. Ramped rather than applied per block, so turning it while audio is
    // running stays free of zipper noise.
    juce::dsp::Gain<float> outputGain;

    std::vector<float> monoScratch;

    std::atomic<bool> referenceUsingSidechain { false };
    std::atomic<bool> uiActive { false };

    // Latched once the sidechain bus delivers a non-silent block; cleared on prepare.
    std::atomic<bool> sidechainCarriesSignal { false };

    // A file being read for a side. Message thread only. The ticket is what tells a
    // read that finishes from one that was overtaken while it ran: each new read and
    // each Learn takes the next number, and a result carrying an old one is dropped.
    void finishImport (Side, std::uint32_t ticket, const juce::String& name,
                       AudioFileSpectrum::Result, const std::function<void (const juce::Result&)>& onFinished);

    std::optional<Side> importingSide;
    std::array<std::uint32_t, 2> importTickets {};

    // Set as the plugin closes, so a read in progress gives its thread back at once
    // rather than decoding the rest of a song nobody is waiting for.
    std::atomic<bool> stopImporting { false };

    // Declared last so it is destroyed first: the pool waits for a running read, and
    // that read is still using stopImporting above.
    juce::ThreadPool importPool { juce::ThreadPoolOptions{}.withThreadName ("AURA file import")
                                                          .withNumberOfThreads (1) };

    // A read finishes on a worker thread and comes back by message; the plugin may have
    // been closed in between.
    JUCE_DECLARE_WEAK_REFERENCEABLE (PluginProcessor)
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PluginProcessor)
};
