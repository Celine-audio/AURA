#pragma once

#include <juce_dsp/juce_dsp.h>
#include <juce_events/juce_events.h>

#include "FilterDesigner.h"
#include "PartitionedConvolver.h"
#include "SpectrumAnalyzer.h"

#include <array>
#include <atomic>
#include <functional>

/**
    Everything the plugin does that is actually about matching: it holds the two
    captures, derives the correction curve from them, turns that into a linear-phase
    impulse response, and applies it.

    It knows nothing about JUCE's parameter machinery or about hosts — the settings it
    works from are pushed in via setSettings(), which is what keeps it testable and
    keeps PluginProcessor down to wiring.

    Threading: pushSource/pushReference/process are the audio thread and never
    allocate or block. Everything else is the message thread.
*/
class MatchEngine : private juce::Timer
{
public:
    /** The final correction being applied, in dB per FFT bin, with the L/R link
        already blended in. Empty until something has been captured. */
    struct CorrectionCurves
    {
        std::vector<float> leftDb, rightDb;

        bool isValid() const noexcept { return leftDb.size() > 1 && leftDb.size() == rightDb.size(); }
    };

    /** What the curve is derived with, and how it is realised. Pushed in whenever a
        parameter moves. */
    struct Settings
    {
        FilterDesigner::Params design;
        float link = 1.0f;

        // Linear phase: symmetric response, flat phase, half its length in latency.
        // Minimum phase: same magnitude, energy pulled to the front, no latency, and
        // the default — see the phase parameter for why.
        bool linearPhase = false;
    };

    /** Which of the two captures an operation is about. */
    enum class Side { source, reference };

    MatchEngine();
    ~MatchEngine() override;

    //==============================================================================
    // Setup.

    void prepare (double sampleRate, int maxBlockSize, int numChannels);
    void release();

    /** Called whenever the applied filter changes, with the latency it now imposes.
        The processor forwards this to setLatencySamples(). */
    std::function<void (int latencySamples)> onLatencyChanged;

    //==============================================================================
    // Audio thread.

    /** Feeds one channel of the material being captured. Only accumulates while that
        side is capturing, so calling it unconditionally is free. */
    void pushSource (int channel, const float* data, int numSamples) noexcept;
    void pushReference (int channel, const float* data, int numSamples) noexcept;

    /** Applies the matched EQ in place. A no-op until something has been matched. */
    void process (juce::AudioBuffer<float>& buffer) noexcept;

    //==============================================================================
    // Message thread.

    /** Starts or stops a capture. Starting one always begins a fresh take: the
        previous contents of that analyzer are discarded first. */
    void setCapturing (Side, bool shouldCapture);
    bool isCapturing (Side) const noexcept;
    std::int64_t getFrameCount (Side) const noexcept;

    /** The learned spectrum for one side, summed to a single curve for display.
        Prefers whatever the capture analyzers hold and falls back to the snapshot the
        committed match was taken from, which is all a reloaded session has. Returns
        false when that side has never been learned. */
    bool getLearnedMagnitudes (Side, std::vector<float>& dest) const;

    /** True once a match curve has been computed and loaded. */
    bool isMatched() const noexcept { return matched.load(); }

    /** Snapshots both captures and builds the filter from them. Returns false if
        either side is empty. */
    bool performMatch();

    /** The correction curves being applied, recomputed only when something has
        actually changed. This is the same data the convolution is built from, so the
        display tracks the knobs in lock-step with what you hear. */
    const CorrectionCurves& getCorrectionCurves();

    /** Flags the cached curves as out of date. Callers use this while a capture is
        accumulating; setSettings and performMatch mark it internally. */
    void markCorrectionDirty() noexcept { correctionDirty.store (true); }

    /** New curve-shaping settings. Marks the cache dirty and schedules a throttled
        rebuild of the impulse response.

        **Message thread only**, and that is the whole of `settings`' thread safety:
        one owner, so there is nothing to synchronise. It used to accept a call from
        whichever thread moved the parameter -- which for host automation is the audio
        thread -- and that was wrong twice over. Writing the struct there raced the
        rebuild reading its fields, which could compute a curve from half the old
        settings and half the new; and the async update it posted takes a lock and can
        allocate, on the one thread that must do neither. The processor now polls a
        flag and calls this from its own timer. */
    void setSettings (const Settings&) noexcept;

    int getFftSize() const noexcept { return 1 << spectrumFftOrder; }

    /** The length of the filter, in taps. Twice the analysis FFT, so a linear-phase
        response is symmetric about tap 4096 and the correction gets that much
        resolution to work with down low. */
    static constexpr int irLength = (1 << spectrumFftOrder) * 2;

    /** What the filter costs. The convolver adds nothing of its own, so this is the
        response's own group delay: half its length in linear phase, and none at all
        in minimum phase, which is the entire point of the mode. */
    int getLatencySamples() const noexcept { return settings.linearPhase ? irLength / 2 : 0; }
    double getSampleRate() const noexcept { return sampleRate; }

    /** How many times the impulse response has been rebuilt. Exposed so tests can pin
        the drag throttle, which is what keeps knob moves from crackling. */
    int getRebuildCount() const noexcept { return rebuildCount; }

    //==============================================================================
    // State. The captures are persisted so a match survives a session reload.

    void saveTo (juce::XmlElement& element) const;
    void restoreFrom (const juce::XmlElement& element);

private:
    using Spectra = std::array<std::vector<float>, 2>;

    // Per-channel captures, so the correction can be derived independently for L and
    // R; the Link setting decides how much the two are averaged together.
    struct Capture
    {
        std::array<SpectrumAnalyzer, 2> analyzer;
        Spectra snapshot; // taken when Match was pressed
    };

    /** Re-maps a snapshot taken at one sample rate onto the bin grid of another.

        A snapshot is magnitudes per FFT bin, and bin k means k * sampleRate / fftSize
        hertz -- so the same numbers describe a different curve at a different rate.
        Without this, a match captured at 44.1k and played at 48k applied every feature
        of it 8.8% too high: a correction aimed at 1 kHz landing nearer 1.09 kHz, an
        error of a good fraction of a semitone, silently. That happens on a plain
        session reload between two projects, and also when the interface's rate is
        changed with the plugin loaded. */
    static void rebaseSnapshot (Spectra&, double fromSampleRate, double toSampleRate);

    Capture& captureFor (Side) noexcept;
    const Capture& captureFor (Side) const noexcept;

    // The spectra the correction should be derived from: the snapshot taken at match
    // time if there is one, otherwise whatever is in the analyzers (so the display can
    // preview before you commit).
    bool collectCaptures (Spectra& source, Spectra& reference) const;

    void updateCorrectionCurves();

    // Rebuilds the impulse response and loads it into the convolution. No-op if there
    // is nothing to match.
    void rebuildMatch();

    // Coalesces rebuild requests so that dragging a control reloads the convolution a
    // handful of times a second rather than on every frame.
    void requestRebuild();

    void timerCallback() override;

    Capture source, reference;

    PartitionedConvolver convolution;

    CorrectionCurves correctionCache;

    /** Message thread only -- see setSettings. */
    Settings settings;

    std::atomic<bool> correctionDirty { true };
    std::atomic<bool> matched { false };

    double sampleRate = 44100.0;

    // The rate the snapshots were captured at, which is not always the rate we are
    // playing at now -- see rebaseSnapshot. Zero until something has been captured.
    double captureSampleRate = 0.0;
    int numOutputChannels = 2;

    // When the convolution last took a new impulse response, for the rebuild throttle.
    juce::uint32 lastRebuildMs = 0;
    int rebuildCount = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MatchEngine)
};
