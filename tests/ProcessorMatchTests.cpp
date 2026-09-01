#include "helpers/test_helpers.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <juce_audio_formats/juce_audio_formats.h>

#include <algorithm>
#include <cmath>

namespace
{
    constexpr double sampleRate = 48000.0;
    constexpr int blockSize = 512;

    // Feeds noise through the plugin. dullness applies a one-pole lowpass to the
    // right channel only, so the two channels end up with different spectra.
    void pushNoise (PluginProcessor& plugin, int numBlocks, float rightDullness, int seed)
    {
        juce::AudioBuffer<float> buffer (2, blockSize);
        juce::MidiBuffer midi;
        juce::Random random { seed };
        float lowpassState = 0.0f;

        for (int b = 0; b < numBlocks; ++b)
        {
            for (int i = 0; i < blockSize; ++i)
            {
                const auto white = random.nextFloat() * 2.0f - 1.0f;
                lowpassState = rightDullness * lowpassState + (1.0f - rightDullness) * white;

                buffer.setSample (0, i, white * 0.25f);
                buffer.setSample (1, i, (rightDullness > 0.0f ? lowpassState * 0.8f : white) * 0.25f);
            }

            plugin.processBlock (buffer, midi);
        }
    }

    // Captures a reference with matching channels, then a source whose right
    // channel is much duller, so L and R need visibly different corrections.
    void captureAsymmetricMatch (PluginProcessor& plugin)
    {
        plugin.prepareToPlay (sampleRate, blockSize);

        plugin.setReferenceCapturing (true);
        pushNoise (plugin, 80, 0.0f, 1);
        plugin.setReferenceCapturing (false);

        plugin.setSourceCapturing (true);
        pushNoise (plugin, 80, 0.9f, 2);
        plugin.setSourceCapturing (false);

        REQUIRE (plugin.performMatch());
    }

    float maxChannelDifferenceDb (const PluginProcessor::CorrectionCurves& curves)
    {
        float worst = 0.0f;
        for (size_t k = 0; k < curves.leftDb.size(); ++k)
            worst = std::max (worst, std::abs (curves.leftDb[k] - curves.rightDb[k]));
        return worst;
    }

    void setParameter (PluginProcessor& plugin, const juce::String& id, float normalised)
    {
        auto* parameter = plugin.getAPVTS().getParameter (id);
        REQUIRE (parameter != nullptr);
        parameter->setValueNotifyingHost (normalised);

        // Parameter listeners may be flushed asynchronously; the curves are cached
        // behind a dirty flag, so ask for them to be re-derived explicitly.
        plugin.markCorrectionDirty();
    }
}

TEST_CASE ("Matching produces per-channel curves", "[processor]")
{
    PluginProcessor plugin;
    captureAsymmetricMatch (plugin);

    CHECK (plugin.isMatched());

    SECTION ("unlinked, each channel keeps its own correction")
    {
        setParameter (plugin, "link", 0.0f);
        const auto& curves = plugin.getCorrectionCurves();

        REQUIRE (curves.isValid());
        CHECK (maxChannelDifferenceDb (curves) > 3.0f);
    }

    SECTION ("fully linked, both channels share the averaged correction")
    {
        setParameter (plugin, "link", 1.0f);
        const auto& curves = plugin.getCorrectionCurves();

        REQUIRE (curves.isValid());
        CHECK_THAT (maxChannelDifferenceDb (curves), Catch::Matchers::WithinAbs (0.0f, 1.0e-4f));
    }

    SECTION ("half linked lands between the two")
    {
        setParameter (plugin, "link", 0.0f);
        const auto unlinkedSpread = maxChannelDifferenceDb (plugin.getCorrectionCurves());

        setParameter (plugin, "link", 0.5f);
        const auto halfSpread = maxChannelDifferenceDb (plugin.getCorrectionCurves());

        CHECK (halfSpread < unlinkedSpread);
        CHECK (halfSpread > 0.0f);
        CHECK_THAT (halfSpread, Catch::Matchers::WithinRel (unlinkedSpread * 0.5f, 0.05f));
    }
}

TEST_CASE ("Smoothing flattens the correction curve", "[processor]")
{
    PluginProcessor plugin;
    captureAsymmetricMatch (plugin);

    auto curveRoughness = [&plugin] (float normalisedSmoothing)
    {
        setParameter (plugin, "smoothing", normalisedSmoothing);
        const auto& db = plugin.getCorrectionCurves().leftDb;

        // Total absolute bin-to-bin change: a smoother curve wanders less.
        double roughness = 0.0;
        for (size_t k = 1; k < db.size(); ++k)
            roughness += std::abs ((double) db[k] - (double) db[k - 1]);
        return roughness;
    };

    const auto sharp = curveRoughness (0.05f);
    const auto smooth = curveRoughness (0.9f);

    CHECK (smooth < sharp);
}

TEST_CASE ("Exporting writes an IR matching the applied correction", "[processor]")
{
    PluginProcessor plugin;
    captureAsymmetricMatch (plugin);
    setParameter (plugin, "link", 0.0f);

    const auto file = juce::File::createTempFile (".wav");

    IrExport::Options options;
    options.layout = IrExport::Options::Layout::stereo;
    options.channel = IrExport::Options::Channel::mid;

    const auto result = plugin.exportImpulseResponse (file, options);
    REQUIRE (result.wasOk());

    juce::WavAudioFormat wav;
    std::unique_ptr<juce::AudioFormatReader> reader (
        wav.createReaderFor (new juce::FileInputStream (file), true));

    REQUIRE (reader != nullptr);
    CHECK (reader->numChannels == 2);
    CHECK_THAT (reader->sampleRate, Catch::Matchers::WithinAbs (sampleRate, 1.0e-6));
    // The filter's length, not the analysis FFT's: an export renders what the plugin
    // is running rather than a fresh approximation of it at some other resolution.
    CHECK (reader->lengthInSamples == plugin.getFilterLength());

    juce::AudioBuffer<float> ir (2, (int) reader->lengthInSamples);
    REQUIRE (reader->read (&ir, 0, (int) reader->lengthInSamples, 0, true, true));

    // Unlinked, the two channels were corrected differently, so the IR channels
    // must not be identical.
    bool channelsDiffer = false;
    for (int i = 0; i < ir.getNumSamples() && ! channelsDiffer; ++i)
        channelsDiffer = std::abs (ir.getSample (0, i) - ir.getSample (1, i)) > 1.0e-6f;

    CHECK (channelsDiffer);

    file.deleteFile();
}

TEST_CASE ("Exporting without a capture fails cleanly", "[processor]")
{
    PluginProcessor plugin;
    plugin.prepareToPlay (sampleRate, blockSize);

    const auto file = juce::File::createTempFile (".wav");
    const auto result = plugin.exportImpulseResponse (file, {});

    CHECK (result.failed());
    CHECK (result.getErrorMessage().isNotEmpty());
    CHECK_FALSE (file.existsAsFile());

    file.deleteFile();
}

TEST_CASE ("A saved match survives a state round trip", "[processor]")
{
    PluginProcessor source;
    captureAsymmetricMatch (source);
    setParameter (source, "link", 0.0f);

    juce::MemoryBlock state;
    source.getStateInformation (state);

    PluginProcessor restored;
    restored.prepareToPlay (sampleRate, blockSize);
    restored.setStateInformation (state.getData(), (int) state.getSize());

    CHECK (restored.isMatched());

    const auto& before = source.getCorrectionCurves();
    const auto& after = restored.getCorrectionCurves();

    REQUIRE (after.isValid());
    REQUIRE (after.leftDb.size() == before.leftDb.size());

    for (size_t k = 0; k < before.leftDb.size(); ++k)
    {
        REQUIRE_THAT (after.leftDb[k], Catch::Matchers::WithinAbs (before.leftDb[k], 1.0e-3f));
        REQUIRE_THAT (after.rightDb[k], Catch::Matchers::WithinAbs (before.rightDb[k], 1.0e-3f));
    }
}

TEST_CASE ("Starting a capture begins a fresh take", "[processor]")
{
    PluginProcessor plugin;
    plugin.prepareToPlay (sampleRate, blockSize);

    plugin.setSourceCapturing (true);
    pushNoise (plugin, 80, 0.0f, 1);
    plugin.setSourceCapturing (false);

    const auto firstTake = plugin.getSourceFrameCount();
    REQUIRE (firstTake > 0);

    // Re-arming discards the previous take instead of averaging into it.
    plugin.setSourceCapturing (true);
    CHECK (plugin.getSourceFrameCount() == 0);

    pushNoise (plugin, 10, 0.0f, 2);
    plugin.setSourceCapturing (false);

    const auto secondTake = plugin.getSourceFrameCount();
    CHECK (secondTake > 0);
    CHECK (secondTake < firstTake);
}

TEST_CASE ("Re-capturing leaves the applied match alone until you match again", "[processor]")
{
    PluginProcessor plugin;
    captureAsymmetricMatch (plugin);

    const auto appliedBefore = plugin.getCorrectionCurves().leftDb; // copy, the cache is rebuilt below
    REQUIRE_FALSE (appliedBefore.empty());

    plugin.setSourceCapturing (true);
    pushNoise (plugin, 20, 0.0f, 7);

    // The filter still follows the snapshot taken when Match was pressed, so what
    // you hear does not lurch around while a new take is accumulating.
    CHECK (plugin.isMatched());

    const auto& appliedDuring = plugin.getCorrectionCurves();
    REQUIRE (appliedDuring.leftDb.size() == appliedBefore.size());

    for (size_t k = 0; k < appliedBefore.size(); ++k)
        REQUIRE_THAT (appliedDuring.leftDb[k], Catch::Matchers::WithinAbs (appliedBefore[k], 1.0e-4f));

    plugin.setSourceCapturing (false);
}

namespace
{
    // Runs a steady tone through the plugin and reports the output RMS, ignoring
    // the first blocks so the gain ramp has settled.
    float steadyOutputRms (PluginProcessor& plugin, int settleBlocks = 20)
    {
        juce::AudioBuffer<float> buffer (2, blockSize);
        juce::MidiBuffer midi;
        double phase = 0.0;
        const auto increment = 2.0 * juce::MathConstants<double>::pi * 1000.0 / sampleRate;
        double sumOfSquares = 0.0;
        int counted = 0;

        for (int b = 0; b < settleBlocks + 10; ++b)
        {
            for (int i = 0; i < blockSize; ++i)
            {
                const auto sample = (float) std::sin (phase) * 0.25f;
                phase += increment;
                buffer.setSample (0, i, sample);
                buffer.setSample (1, i, sample);
            }

            plugin.processBlock (buffer, midi);

            if (b >= settleBlocks)
                for (int i = 0; i < blockSize; ++i)
                {
                    const auto out = (double) buffer.getSample (0, i);
                    sumOfSquares += out * out;
                    ++counted;
                }
        }

        return (float) std::sqrt (sumOfSquares / (double) counted);
    }
}

TEST_CASE ("The reference follows the sidechain only once something feeds it", "[processor]")
{
    PluginProcessor plugin;

    // Turn the optional sidechain bus on, the way a host does when the user routes
    // something to it. Note that enabling the bus is all the host has told us — it
    // says nothing about whether audio is actually arriving on it.
    auto layout = plugin.getBusesLayout();
    layout.inputBuses.getReference (1) = juce::AudioChannelSet::stereo();
    REQUIRE (plugin.setBusesLayout (layout));

    plugin.prepareToPlay (sampleRate, blockSize);

    // Feeds the main input and the sidechain independently. A dull bus gets a one-pole
    // lowpass, so the two are easy to tell apart by their high-frequency content.
    auto push = [&] (int numBlocks, bool mainIsDull, bool sidechainIsDull, bool sidechainSilent)
    {
        juce::AudioBuffer<float> buffer (4, blockSize);
        juce::MidiBuffer midi;
        juce::Random random { 99 };
        float mainState = 0.0f, sideState = 0.0f;

        for (int b = 0; b < numBlocks; ++b)
        {
            for (int i = 0; i < blockSize; ++i)
            {
                const auto white = random.nextFloat() * 2.0f - 1.0f;
                mainState = 0.9f * mainState + 0.1f * white;
                sideState = 0.9f * sideState + 0.1f * white;

                const auto mainSample = (mainIsDull ? mainState * 2.5f : white) * 0.25f;
                const auto sideSample = sidechainSilent ? 0.0f
                                                        : (sidechainIsDull ? sideState * 2.5f : white) * 0.25f;

                buffer.setSample (0, i, mainSample);
                buffer.setSample (1, i, mainSample);
                buffer.setSample (2, i, sideSample);
                buffer.setSample (3, i, sideSample);
            }

            plugin.processBlock (buffer, midi);
        }
    };

    auto bandEnergy = [] (const std::vector<float>& mags, size_t from, size_t to)
    {
        double sum = 0.0;
        for (size_t k = from; k < to && k < mags.size(); ++k)
            sum += (double) mags[k] * (double) mags[k];
        return sum;
    };

    // How much high-frequency content a learned spectrum holds, relative to its lows.
    auto brightness = [&] (const std::vector<float>& mags)
    {
        return bandEnergy (mags, 500, 800) / bandEnergy (mags, 10, 50);
    };

    std::vector<float> learned;

    SECTION ("an enabled but silent sidechain leaves the reference on the main input")
    {
        plugin.setReferenceCapturing (true);
        push (80, false, false, true); // bright main, silent sidechain
        plugin.setReferenceCapturing (false);

        CHECK_FALSE (plugin.isReferenceUsingSidechain());

        REQUIRE (plugin.getLearnedReferenceMagnitudes (learned));

        // Learning the silent sidechain would leave an empty spectrum, so the first
        // thing to establish is that real audio landed here at all.
        CHECK (bandEnergy (learned, 10, 50) > 1.0e-9);
        CHECK (brightness (learned) > 0.05);
    }

    SECTION ("a sidechain carrying audio takes over as the reference")
    {
        // A dull sidechain against a bright main input, so the learned curve says
        // plainly which of the two it came from.
        push (4, false, true, false);
        CHECK (plugin.isReferenceUsingSidechain());

        plugin.setReferenceCapturing (true);
        push (80, false, true, false);
        plugin.setReferenceCapturing (false);

        REQUIRE (plugin.getLearnedReferenceMagnitudes (learned));
        const auto fromSidechain = brightness (learned);

        // And the same run learned through the main input for comparison.
        PluginProcessor viaMain;
        viaMain.prepareToPlay (sampleRate, blockSize);
        viaMain.setReferenceCapturing (true);
        pushNoise (viaMain, 80, 0.0f, 99);
        viaMain.setReferenceCapturing (false);

        std::vector<float> mainLearned;
        REQUIRE (viaMain.getLearnedReferenceMagnitudes (mainLearned));

        CHECK (fromSidechain < brightness (mainLearned) * 0.25);
    }
}

TEST_CASE ("Output gain scales the signal", "[processor]")
{
    PluginProcessor plugin;
    plugin.prepareToPlay (sampleRate, blockSize);

    auto* gain = plugin.getAPVTS().getParameter ("outputGain");
    REQUIRE (gain != nullptr);

    const auto unity = steadyOutputRms (plugin);
    REQUIRE (unity > 0.0f);

    gain->setValueNotifyingHost (gain->convertTo0to1 (6.0f));
    const auto boosted = steadyOutputRms (plugin);

    // +6 dB is very close to a doubling.
    CHECK_THAT (boosted / unity, Catch::Matchers::WithinRel (2.0f, 0.02f));

    gain->setValueNotifyingHost (gain->convertTo0to1 (-12.0f));
    const auto cut = steadyOutputRms (plugin);

    CHECK_THAT (cut / unity, Catch::Matchers::WithinRel (0.251f, 0.02f));
}

TEST_CASE ("Bypass takes the output gain out with the EQ", "[processor]")
{
    PluginProcessor plugin;
    plugin.prepareToPlay (sampleRate, blockSize);

    auto* gain = plugin.getAPVTS().getParameter ("outputGain");
    auto* bypass = plugin.getAPVTS().getParameter ("bypass");
    REQUIRE (gain != nullptr);
    REQUIRE (bypass != nullptr);

    const auto unity = steadyOutputRms (plugin);

    gain->setValueNotifyingHost (gain->convertTo0to1 (12.0f));
    bypass->setValueNotifyingHost (1.0f);

    // Bypassed, the trim is out of circuit so an A/B compares like with like.
    CHECK_THAT (steadyOutputRms (plugin) / unity, Catch::Matchers::WithinRel (1.0f, 0.02f));
}

TEST_CASE ("Output gain does not rebuild the filter", "[processor]")
{
    PluginProcessor plugin;
    captureAsymmetricMatch (plugin);

    const auto before = plugin.getCorrectionCurves().leftDb; // copy

    auto* gain = plugin.getAPVTS().getParameter ("outputGain");
    REQUIRE (gain != nullptr);
    gain->setValueNotifyingHost (gain->convertTo0to1 (9.0f));

    // The trim rides on the output, so the correction curve itself is untouched.
    const auto& after = plugin.getCorrectionCurves();
    REQUIRE (after.leftDb.size() == before.size());

    for (size_t k = 0; k < before.size(); ++k)
        REQUIRE_THAT (after.leftDb[k], Catch::Matchers::WithinAbs (before[k], 1.0e-6f));
}

TEST_CASE ("A parameter drag is throttled but never loses the last move", "[processor]")
{
    PluginProcessor plugin;
    captureAsymmetricMatch (plugin);

    auto* amount = plugin.getAPVTS().getParameter ("amount");
    REQUIRE (amount != nullptr);

    const auto rebuildsBefore = plugin.getRebuildCountForTests();
    const auto start = juce::Time::getMillisecondCounter();
    int moves = 0;

    // Roughly what a mouse drag produces: a new value every few milliseconds. These
    // are normalised positions, so 0.5 is the middle of the bipolar range.
    while (juce::Time::getMillisecondCounter() - start < 1000)
    {
        amount->setValueNotifyingHost (0.5f + 0.4f * std::sin ((float) moves * 0.2f));
        ++moves;
        juce::MessageManager::getInstance()->runDispatchLoopUntil (8);
    }

    juce::MessageManager::getInstance()->runDispatchLoopUntil (400);

    const auto rebuilds = plugin.getRebuildCountForTests() - rebuildsBefore;
    REQUIRE (moves > 20); // otherwise the machine was too slow for this to mean anything

    // Rebuilding on every callback is what makes a drag crackle: each reload
    // restarts the convolution's crossfade before the previous one has finished.
    CHECK (rebuilds >= 1);
    CHECK (rebuilds < moves / 2);

    // Throttling must not swallow the final position.
    const auto settled = plugin.getRebuildCountForTests();
    amount->setValueNotifyingHost (amount->convertTo0to1 (0.0f));
    juce::MessageManager::getInstance()->runDispatchLoopUntil (400);

    CHECK (plugin.getRebuildCountForTests() > settled);

    // Amount 0 means no correction at all, so the applied curve is flat.
    const auto& curves = plugin.getCorrectionCurves();
    REQUIRE (curves.isValid());

    for (size_t k = 0; k < curves.leftDb.size(); ++k)
        REQUIRE_THAT (curves.leftDb[k], Catch::Matchers::WithinAbs (0.0f, 1.0e-4f));
}

TEST_CASE ("A match keeps its frequencies when the sample rate changes", "[processor]")
{
    // A snapshot is magnitudes per FFT bin, and a bin only means a frequency once you
    // know the rate it was captured at. Reload the same match at a different rate
    // without accounting for that and every feature of the curve moves: 44.1k to 48k
    // shifts it by 8.8%, which is most of a semitone, and nothing says so.
    constexpr int fftBins = 4097;

    // The frequency at which the correction first passes halfway to its peak. Read in
    // hertz, not in bins, because bins are exactly what is not comparable here.
    const auto crossoverHz = [] (const std::vector<float>& db, double rate)
    {
        const auto peak = *std::max_element (db.begin(), db.end());
        const auto binHz = rate / (double) ((fftBins - 1) * 2);

        for (size_t k = 1; k < db.size(); ++k)
            if (db[k] >= peak * 0.5f)
                return (float) ((double) k * binHz);

        return 0.0f;
    };

    PluginProcessor captured;
    captured.prepareToPlay (44100.0, blockSize);

    captured.setReferenceCapturing (true);
    pushNoise (captured, 80, 0.0f, 11);
    captured.setReferenceCapturing (false);

    captured.setSourceCapturing (true);
    pushNoise (captured, 80, 0.9f, 12);   // dull source, so the fix is a high boost
    captured.setSourceCapturing (false);

    REQUIRE (captured.performMatch());

    const auto at441 = crossoverHz (captured.getCorrectionCurves().leftDb, 44100.0);
    REQUIRE (at441 > 100.0f);

    juce::MemoryBlock state;
    captured.getStateInformation (state);

    PluginProcessor reopened;
    reopened.prepareToPlay (48000.0, blockSize);
    reopened.setStateInformation (state.getData(), (int) state.getSize());

    REQUIRE (reopened.isMatched());

    const auto at48 = crossoverHz (reopened.getCorrectionCurves().leftDb, 48000.0);

    // Within 2%. Doing nothing about the rate change puts this out by 8.8%.
    CHECK_THAT (at48, Catch::Matchers::WithinRel (at441, 0.02f));
}
