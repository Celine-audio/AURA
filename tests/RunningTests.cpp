#include "helpers/test_helpers.h"

#include <catch2/catch_test_macros.hpp>

#include <dsp/SpectrumAnalyzer.h>
#include <ui/WindowStepper.h>

#include <cmath>
#include <vector>

/*
    Running: a take that is only the last few seconds, and a match in force that
    follows it on its own. The analyzer half is pinned on pure tones, where which one
    is in the window is a yes-or-no question; the processor half on noise, through the
    curve the plugin actually applies.
*/

namespace
{
    constexpr double pi = 3.14159265358979323846;
    constexpr double sampleRate = 48000.0;
    constexpr int blockSize = 512;

    void pushTone (SpectrumAnalyzer& analyzer, int bin, int numFrames, double& phase)
    {
        const auto fftSize = analyzer.getFftSize();
        const auto increment = 2.0 * pi * (double) bin / (double) fftSize;

        // Half a window per frame, so this many frames' worth of new audio.
        std::vector<float> block ((size_t) (fftSize / 2));

        for (int f = 0; f < numFrames; ++f)
        {
            for (auto& sample : block)
            {
                sample = (float) std::sin (phase);
                phase += increment;
            }

            analyzer.pushBlock (block.data(), (int) block.size());
        }
    }

    // White noise, or with `dullness` above zero, noise through a one-pole lowpass with
    // unity gain at DC -- the same lows, much less top.
    void pushNoise (PluginProcessor& plugin, double seconds, float dullness, int seed)
    {
        juce::AudioBuffer<float> buffer (2, blockSize);
        juce::MidiBuffer midi;
        juce::Random random { seed };
        float state = 0.0f;

        const auto numBlocks = (int) std::ceil (seconds * sampleRate / blockSize);

        for (int b = 0; b < numBlocks; ++b)
        {
            for (int i = 0; i < blockSize; ++i)
            {
                const auto white = random.nextFloat() * 2.0f - 1.0f;
                state = dullness * state + (1.0f - dullness) * white;

                const auto sample = 0.25f * (dullness > 0.0f ? state : white);
                buffer.setSample (0, i, sample);
                buffer.setSample (1, i, sample);
            }

            plugin.processBlock (buffer, midi);
        }
    }

    void setParameter (PluginProcessor& plugin, const char* id, float value)
    {
        auto* parameter = plugin.getAPVTS().getParameter (id);
        REQUIRE (parameter != nullptr);
        parameter->setValueNotifyingHost (parameter->convertTo0to1 (value));
    }

    // How much more the correction gives 10 kHz than 200 Hz: large and positive for a
    // dull source matched to a flat reference, near zero once the source is flat too.
    float tiltDb (PluginProcessor& plugin)
    {
        const auto& curves = plugin.getCorrectionCurves();
        REQUIRE (curves.isValid());

        const auto binFor = [&plugin] (double hz)
        {
            return (size_t) std::lround (hz * plugin.getFftSize() / sampleRate);
        };

        return curves.leftDb[binFor (10000.0)] - curves.leftDb[binFor (200.0)];
    }

    // A flat reference, then a source that is dull for six seconds and bright for the
    // four after, matched in between -- with Running on or off.
    void learnDullThenBright (PluginProcessor& plugin, bool running)
    {
        plugin.prepareToPlay (sampleRate, blockSize);

        setParameter (plugin, ParamID::running, running ? 1.0f : 0.0f);
        setParameter (plugin, ParamID::window, 2.0f);

        plugin.setReferenceCapturing (true);
        pushNoise (plugin, 3.0, 0.0f, 1);
        plugin.setReferenceCapturing (false);

        plugin.setSourceCapturing (true);
        pushNoise (plugin, 6.0, 0.9f, 2);

        REQUIRE (plugin.performMatch());
        REQUIRE (tiltDb (plugin) > 10.0f);

        pushNoise (plugin, 4.0, 0.0f, 3);
    }
}

TEST_CASE ("A windowed analyzer reads only its last few segments", "[running][analyzer]")
{
    SpectrumAnalyzer analyzer;
    analyzer.prepareHistory (2, 20);
    analyzer.setCapturing (true);

    double phase = 0.0;
    pushTone (analyzer, 100, 200, phase);
    pushTone (analyzer, 300, 20, phase);

    std::vector<float> mags;

    SECTION ("the long-term average is still mostly the first tone")
    {
        REQUIRE (analyzer.getAveragedMagnitudes (mags));
        CHECK (mags[100] > mags[300]);
        CHECK (analyzer.getWindowedFrameCount() == analyzer.getFrameCount());
    }

    SECTION ("a five-segment window holds only the second")
    {
        analyzer.setWindowSegments (5);
        REQUIRE (analyzer.getAveragedMagnitudes (mags));

        // Ten frames of whole segments, plus however much of the head has filled.
        CHECK (analyzer.getWindowedFrameCount() >= 10);
        CHECK (analyzer.getWindowedFrameCount() <= 12);

        CHECK (mags[300] > 0.4f);
        CHECK (mags[100] < 1.0e-3f);
    }

    SECTION ("a window wider than the ring reads the whole ring")
    {
        analyzer.setWindowSegments (1000);
        CHECK (analyzer.getWindowSegments() == 19);
        CHECK (analyzer.getWindowedFrameCount() <= 40);

        // Twenty frames of each, within the ring: both tones are there.
        REQUIRE (analyzer.getAveragedMagnitudes (mags));
        CHECK (mags[100] > 0.2f);
        CHECK (mags[300] > 0.2f);
    }

    SECTION ("turning the window off gives the long-term average back")
    {
        analyzer.setWindowSegments (5);
        analyzer.setWindowSegments (0);
        REQUIRE (analyzer.getAveragedMagnitudes (mags));
        CHECK (mags[100] > mags[300]);
    }

    SECTION ("reset clears the history with the rest")
    {
        analyzer.setCapturing (false);
        analyzer.reset();
        analyzer.setWindowSegments (5);
        CHECK_FALSE (analyzer.getAveragedMagnitudes (mags));
        CHECK (analyzer.getWindowedFrameCount() == 0);
    }
}

TEST_CASE ("A running match follows the last few seconds", "[running][processor]")
{
    PluginProcessor plugin;
    learnDullThenBright (plugin, true);

    // The match in force is still the dull one until the timer comes round...
    CHECK (plugin.isMatchStale());
    CHECK (tiltDb (plugin) > 10.0f);

    // ...and then it is the last two seconds, which were all bright.
    REQUIRE (plugin.updateRunningMatchForTests());
    CHECK (std::abs (tiltDb (plugin)) < 1.5f);
    CHECK_FALSE (plugin.isMatchStale());
}

TEST_CASE ("Without Running, the match stays where it was put", "[running][processor]")
{
    PluginProcessor plugin;
    learnDullThenBright (plugin, false);

    CHECK_FALSE (plugin.updateRunningMatchForTests());
    CHECK (tiltDb (plugin) > 10.0f);

    // And pressing Match takes the whole take, six dull seconds and four bright: still
    // tilted, where the running one came back to flat.
    REQUIRE (plugin.performMatch());
    CHECK (tiltDb (plugin) > 2.5f);
}

TEST_CASE ("A running match is rebuilt at most every quarter of a second", "[running][processor]")
{
    PluginProcessor plugin;
    learnDullThenBright (plugin, true);

    REQUIRE (plugin.updateRunningMatchForTests());
    const auto rebuilds = plugin.getRebuildCountForTests();

    pushNoise (plugin, 0.2, 0.0f, 4);
    REQUIRE (plugin.isMatchStale());
    CHECK_FALSE (plugin.updateRunningMatchForTests());
    CHECK (plugin.getRebuildCountForTests() == rebuilds);

    juce::Thread::sleep ((int) MatchEngine::runningUpdateIntervalMs + 30);
    CHECK (plugin.updateRunningMatchForTests());
    CHECK (plugin.getRebuildCountForTests() == rebuilds + 1);
}

TEST_CASE ("Running never makes the first match", "[running][processor]")
{
    PluginProcessor plugin;
    plugin.prepareToPlay (sampleRate, blockSize);
    setParameter (plugin, ParamID::running, 1.0f);

    plugin.setReferenceCapturing (true);
    plugin.setSourceCapturing (true);
    pushNoise (plugin, 1.0, 0.0f, 1);

    REQUIRE (plugin.canMatch());
    CHECK_FALSE (plugin.updateRunningMatchForTests());
    CHECK_FALSE (plugin.isMatched());
}

TEST_CASE ("Turning Running on puts a learned match out of date", "[running][processor]")
{
    PluginProcessor plugin;
    plugin.prepareToPlay (sampleRate, blockSize);

    plugin.setReferenceCapturing (true);
    pushNoise (plugin, 1.0, 0.0f, 1);
    plugin.setReferenceCapturing (false);

    plugin.setSourceCapturing (true);
    pushNoise (plugin, 1.0, 0.9f, 2);
    plugin.setSourceCapturing (false);

    REQUIRE (plugin.performMatch());
    REQUIRE_FALSE (plugin.isMatchStale());

    // Nothing new has arrived and no Learn was started, but the takes now read through
    // a window, so what Match would build is not what is in force.
    setParameter (plugin, ParamID::running, 1.0f);
    CHECK (plugin.isRunning());
    CHECK (plugin.isMatchStale());

    CHECK (plugin.updateRunningMatchForTests());
    CHECK_FALSE (plugin.isMatchStale());
}

TEST_CASE ("Running and its window survive a session reload", "[running][processor]")
{
    juce::MemoryBlock state;

    {
        PluginProcessor plugin;
        setParameter (plugin, ParamID::running, 1.0f);
        setParameter (plugin, ParamID::window, 12.0f);
        plugin.getStateInformation (state);
    }

    PluginProcessor restored;
    restored.setStateInformation (state.getData(), (int) state.getSize());

    CHECK (restored.isRunning());
    CHECK (std::abs (restored.getRunningWindowSeconds() - 12.0) < 1.0e-6);
}

TEST_CASE ("The window steps by halves up to ten seconds and by wholes past it", "[running][ui]")
{
    const auto up = [] (float s) { return WindowStepper::stepFrom (s, +1, 1.0f, 30.0f); };
    const auto down = [] (float s) { return WindowStepper::stepFrom (s, -1, 1.0f, 30.0f); };

    CHECK (up (5.0f) == 5.5f);
    CHECK (down (5.0f) == 4.5f);
    CHECK (up (9.5f) == 10.0f);
    CHECK (up (10.0f) == 11.0f);
    CHECK (down (11.0f) == 10.0f);
    CHECK (down (10.0f) == 9.5f);

    // Off the grid -- typed or automated -- it snaps to the next point in that direction.
    CHECK (up (5.2f) == 5.5f);
    CHECK (down (5.2f) == 5.0f);
    CHECK (down (10.4f) == 10.0f);

    // And never leaves the range.
    CHECK (down (1.0f) == 1.0f);
    CHECK (up (30.0f) == 30.0f);
}
