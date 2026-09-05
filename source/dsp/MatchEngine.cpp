#include "MatchEngine.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace
{
    // Deriving the curve and transforming the impulse response costs real work on the
    // message thread, so a drag that fires a parameter callback per mouse move would
    // spend most of a frame on it. The convolver walks between filters rather than
    // switching, and finishes that walk well inside this interval, so the only thing
    // this rate has to satisfy now is the CPU budget.
    constexpr juce::uint32 rebuildIntervalMs = 120;

    juce::String encodeFloats (const std::vector<float>& values)
    {
        if (values.empty())
            return {};

        const juce::MemoryBlock block (values.data(), values.size() * sizeof (float));
        return block.toBase64Encoding();
    }

    std::vector<float> decodeFloats (const juce::String& base64)
    {
        juce::MemoryBlock block;

        if (base64.isEmpty() || ! block.fromBase64Encoding (base64))
            return {};

        std::vector<float> values (block.getSize() / sizeof (float));

        if (! values.empty())
            std::memcpy (values.data(), block.getData(), values.size() * sizeof (float));

        return values;
    }
}

//==============================================================================
void MatchEngine::rebaseSnapshot (Spectra& spectra, double fromSampleRate, double toSampleRate)
{
    // A tolerance rather than ==, because two rates that differ by less than this are
    // the same rate reported twice, and exact float comparison is a warning here.
    if (fromSampleRate <= 0.0 || toSampleRate <= 0.0
        || std::abs (fromSampleRate - toSampleRate) < 1.0e-6)
        return;

    // Bin k of the new grid stands at k * toSampleRate / fftSize hertz, which sits at
    // k * toSampleRate / fromSampleRate on the old one. Linear between neighbours: the
    // ratio is never far from 1 and these are smoothed averages, so nothing finer
    // would be describing anything real.
    const auto ratio = toSampleRate / fromSampleRate;

    for (auto& magnitudes : spectra)
    {
        if (magnitudes.size() < 2)
            continue;

        const auto lastIndex = (double) (magnitudes.size() - 1);
        std::vector<float> rebased (magnitudes.size());

        for (size_t k = 0; k < magnitudes.size(); ++k)
        {
            const auto position = std::clamp ((double) k * ratio, 0.0, lastIndex);
            const auto lower = (size_t) position;
            const auto upper = std::min (lower + 1, magnitudes.size() - 1);
            const auto fraction = (float) (position - (double) lower);

            rebased[k] = magnitudes[lower] + fraction * (magnitudes[upper] - magnitudes[lower]);
        }

        magnitudes = std::move (rebased);
    }
}

MatchEngine::MatchEngine() = default;

MatchEngine::~MatchEngine()
{
    stopTimer();
}

MatchEngine::Capture& MatchEngine::captureFor (Side side) noexcept
{
    return side == Side::source ? source : reference;
}

const MatchEngine::Capture& MatchEngine::captureFor (Side side) const noexcept
{
    return side == Side::source ? source : reference;
}

//==============================================================================
void MatchEngine::prepare (double newSampleRate, int maxBlockSize, int numChannels)
{
    // Before sampleRate is updated: the snapshots are still on the old grid, and this
    // is the only place that knows both rates.
    if (! source.snapshot[0].empty() || ! reference.snapshot[0].empty())
    {
        rebaseSnapshot (source.snapshot, captureSampleRate, newSampleRate);
        rebaseSnapshot (reference.snapshot, captureSampleRate, newSampleRate);
        captureSampleRate = newSampleRate;
    }

    sampleRate = newSampleRate;
    numOutputChannels = juce::jmax (1, numChannels);

    // Only wipe an analyzer that is mid-capture, so each take starts clean. Completed
    // captures are kept and survive transport restarts, just like Logic.
    for (auto* capture : { &source, &reference })
        for (auto& analyzer : capture->analyzer)
            if (analyzer.isCapturing())
                analyzer.reset();

    juce::ignoreUnused (maxBlockSize);

    convolution.prepare (numOutputChannels, irLength);
    convolution.reset();

    // The curves depend on the sample rate, so they have to be recomputed here, and
    // the convolution drops its impulse response on prepare().
    correctionDirty.store (true);

    if (matched.load())
    {
        lastRebuildMs = juce::Time::getMillisecondCounter();
        rebuildMatch();
    }
    else if (onLatencyChanged != nullptr)
    {
        onLatencyChanged (0);
    }
}

void MatchEngine::release()
{
    convolution.reset();
}

//==============================================================================
void MatchEngine::pushSource (int channel, const float* data, int numSamples) noexcept
{
    if (data != nullptr && juce::isPositiveAndBelow (channel, (int) source.analyzer.size()))
        source.analyzer[(size_t) channel].pushBlock (data, numSamples);
}

void MatchEngine::pushReference (int channel, const float* data, int numSamples) noexcept
{
    if (data != nullptr && juce::isPositiveAndBelow (channel, (int) reference.analyzer.size()))
        reference.analyzer[(size_t) channel].pushBlock (data, numSamples);
}

void MatchEngine::process (juce::AudioBuffer<float>& buffer) noexcept
{
    if (matched.load())
        convolution.process (buffer);
}

//==============================================================================
void MatchEngine::setCapturing (Side side, bool shouldCapture)
{
    auto& capture = captureFor (side);

    // Starting a capture wipes the analyzers first, so a new take never gets averaged
    // into the previous one. Safe here because capturing is still off at this point,
    // which is the only state in which reset() may be called.
    if (shouldCapture)
        for (auto& analyzer : capture.analyzer)
            analyzer.reset();

    for (auto& analyzer : capture.analyzer)
        analyzer.setCapturing (shouldCapture);

    correctionDirty.store (true);
}

bool MatchEngine::isCapturing (Side side) const noexcept
{
    return captureFor (side).analyzer[0].isCapturing();
}

std::int64_t MatchEngine::getFrameCount (Side side) const noexcept
{
    return captureFor (side).analyzer[0].getFrameCount();
}

bool MatchEngine::getLearnedMagnitudes (Side side, std::vector<float>& dest) const
{
    const auto& capture = captureFor (side);

    Spectra mags;
    const bool live = capture.analyzer[0].getAveragedMagnitudes (mags[0])
                   && capture.analyzer[1].getAveragedMagnitudes (mags[1]);

    if (! live)
    {
        if (capture.snapshot[0].empty() || capture.snapshot[1].empty())
            return false;

        mags = capture.snapshot;
    }

    const auto numBins = std::min (mags[0].size(), mags[1].size());

    if (numBins < 2)
        return false;

    // Power average, not magnitude average: two channels holding the same energy in
    // opposite polarity still have to read as that much energy.
    dest.resize (numBins);

    for (size_t k = 0; k < numBins; ++k)
        dest[k] = std::sqrt (0.5f * (mags[0][k] * mags[0][k] + mags[1][k] * mags[1][k]));

    return true;
}

//==============================================================================
bool MatchEngine::collectCaptures (Spectra& sourceMags, Spectra& referenceMags) const
{
    // A committed match reads from the snapshot, so tweaking smoothing/link/bounds
    // re-derives the filter without needing a re-capture.
    if (! source.snapshot[0].empty() && ! reference.snapshot[0].empty())
    {
        sourceMags = source.snapshot;
        referenceMags = reference.snapshot;
        return true;
    }

    // Otherwise preview from whatever the analyzers hold right now.
    for (size_t ch = 0; ch < sourceMags.size(); ++ch)
        if (! source.analyzer[ch].getAveragedMagnitudes (sourceMags[ch])
            || ! reference.analyzer[ch].getAveragedMagnitudes (referenceMags[ch]))
            return false;

    return true;
}

void MatchEngine::updateCorrectionCurves()
{
    correctionCache = {};
    correctionDirty.store (false);

    Spectra sourceMags, referenceMags;

    if (! collectCaptures (sourceMags, referenceMags))
        return;

    auto leftDb  = FilterDesigner::computeCorrectionDb (sourceMags[0], referenceMags[0], sampleRate, settings.design);
    auto rightDb = FilterDesigner::computeCorrectionDb (sourceMags[1], referenceMags[1], sampleRate, settings.design);

    // Link pulls the two channels towards their common average: 1 makes them
    // identical (one shared curve), 0 leaves each channel to its own correction.
    FilterDesigner::applyLink (leftDb, rightDb, settings.link);

    correctionCache.leftDb = std::move (leftDb);
    correctionCache.rightDb = std::move (rightDb);
}

const MatchEngine::CorrectionCurves& MatchEngine::getCorrectionCurves()
{
    if (correctionDirty.load())
        updateCorrectionCurves();

    return correctionCache;
}

void MatchEngine::setSettings (const Settings& newSettings) noexcept
{
    settings = newSettings;
    correctionDirty.store (true);

    // Straight to the throttle, because this is already the message thread -- see the
    // declaration for why it has to be.
    requestRebuild();
}

//==============================================================================
void MatchEngine::rebuildMatch()
{
    if (source.snapshot[0].empty() || reference.snapshot[0].empty())
        return;

    // getCorrectionCurves() recomputes only when something actually changed, so a
    // curve the display already refreshed this frame is reused as-is.
    if (! getCorrectionCurves().isValid())
        return;

    const bool stereo = numOutputChannels >= 2;

    // A mono output gets the average of the two curves — there is no second channel to
    // keep separate.
    const auto channelDb = [&] (int channel) -> std::vector<float>
    {
        if (! stereo)
            return FilterDesigner::averageDb (correctionCache.leftDb, correctionCache.rightDb);

        return channel == 0 ? correctionCache.leftDb : correctionCache.rightDb;
    };

    juce::AudioBuffer<float> ir (stereo ? 2 : 1, irLength);

    for (int channel = 0; channel < ir.getNumChannels(); ++channel)
    {
        const auto magnitudes = FilterDesigner::dbToMagnitudes (channelDb (channel));

        const auto taps = settings.linearPhase
                              ? FilterDesigner::buildLinearPhaseIR (magnitudes, irLength)
                              : FilterDesigner::buildMinimumPhaseIR (magnitudes, irLength);

        juce::FloatVectorOperations::copy (ir.getWritePointer (channel), taps.data(), irLength);
    }

    // Only the filter changes hands here; the convolver keeps the input it has
    // already heard, which is what stops a knob move from clicking.
    convolution.setImpulseResponse (ir);

    ++rebuildCount;
    matched.store (true);

    if (onLatencyChanged != nullptr)
        onLatencyChanged (getLatencySamples());
}

void MatchEngine::requestRebuild()
{
    if (! matched.load())
        return;

    const auto now = juce::Time::getMillisecondCounter();
    const auto elapsed = now - lastRebuildMs;

    if (elapsed < rebuildIntervalMs)
    {
        // Too soon. Come back when the quiet period is up; a drag that keeps moving
        // just keeps pushing this out, and the last position always gets built.
        startTimer ((int) (rebuildIntervalMs - elapsed));
        return;
    }

    stopTimer();
    lastRebuildMs = now;
    rebuildMatch();
}

void MatchEngine::timerCallback()
{
    stopTimer();
    requestRebuild();
}

//==============================================================================
bool MatchEngine::performMatch()
{
    Spectra sourceMags, referenceMags;

    for (size_t ch = 0; ch < sourceMags.size(); ++ch)
        if (! source.analyzer[ch].getAveragedMagnitudes (sourceMags[ch])
            || ! reference.analyzer[ch].getAveragedMagnitudes (referenceMags[ch]))
            return false;

    // Snapshot the captures so the match survives transport restarts and can be
    // re-tweaked (smoothing / link / bounds) without re-capturing.
    source.snapshot = std::move (sourceMags);
    reference.snapshot = std::move (referenceMags);
    captureSampleRate = sampleRate;

    correctionDirty.store (true);
    rebuildMatch();
    return true;
}

//==============================================================================
void MatchEngine::saveTo (juce::XmlElement& element) const
{
    element.setAttribute ("matched", matched.load() ? 1 : 0);
    element.setAttribute ("fftOrder", spectrumFftOrder);
    element.setAttribute ("captureSampleRate", captureSampleRate);
    element.setAttribute ("sourceL", encodeFloats (source.snapshot[0]));
    element.setAttribute ("sourceR", encodeFloats (source.snapshot[1]));
    element.setAttribute ("referenceL", encodeFloats (reference.snapshot[0]));
    element.setAttribute ("referenceR", encodeFloats (reference.snapshot[1]));
}

void MatchEngine::restoreFrom (const juce::XmlElement& element)
{
    // The FFT order has to match for the stored bins to line up with ours.
    if (element.getIntAttribute ("fftOrder", spectrumFftOrder) == spectrumFftOrder)
    {
        // Sessions saved before the captures went stereo hold a single mono spectrum;
        // load it into both channels so they restore fully linked.
        const auto legacySource = decodeFloats (element.getStringAttribute ("source"));
        const auto legacyReference = decodeFloats (element.getStringAttribute ("reference"));

        source.snapshot[0] = legacySource.empty() ? decodeFloats (element.getStringAttribute ("sourceL")) : legacySource;
        source.snapshot[1] = legacySource.empty() ? decodeFloats (element.getStringAttribute ("sourceR")) : legacySource;
        reference.snapshot[0] = legacyReference.empty() ? decodeFloats (element.getStringAttribute ("referenceL")) : legacyReference;
        reference.snapshot[1] = legacyReference.empty() ? decodeFloats (element.getStringAttribute ("referenceR")) : legacyReference;
    }

    // Sessions saved before this was recorded have no way of saying, so assume they
    // were captured at whatever we are running at now: that is what the old code did
    // implicitly, and guessing otherwise would move curves that are currently right.
    captureSampleRate = element.getDoubleAttribute ("captureSampleRate", sampleRate);

    rebaseSnapshot (source.snapshot, captureSampleRate, sampleRate);
    rebaseSnapshot (reference.snapshot, captureSampleRate, sampleRate);
    captureSampleRate = sampleRate;

    correctionDirty.store (true);

    // Rebuild from the restored captures. prepare() reloads it too, but the host may
    // ask for the latency before playback ever starts.
    if (! source.snapshot[0].empty() && ! reference.snapshot[0].empty())
        rebuildMatch();
    else
        matched.store (false);
}
