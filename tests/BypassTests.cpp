#include "helpers/test_helpers.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <cmath>

namespace
{
    constexpr double sampleRate = 48000.0;
    constexpr int blockSize = 256;

    // A 220 Hz sine at 0.25 moves by at most this much between samples. A jump well
    // beyond it is a step the signal did not ask for: a click.
    constexpr float sineSlope = 0.25f * 2.0f * 3.14159265f * 220.0f / (float) sampleRate;

    void setParameter (PluginProcessor& plugin, const char* id, float value)
    {
        auto* parameter = plugin.getAPVTS().getParameter (id);
        REQUIRE (parameter != nullptr);
        parameter->setValueNotifyingHost (parameter->convertTo0to1 (value));
    }

    void learn (PluginProcessor& plugin, bool reference, float dullness, int seed)
    {
        juce::AudioBuffer<float> buffer (2, blockSize);
        juce::MidiBuffer midi;
        juce::Random random { seed };
        float state = 0.0f;

        if (reference) plugin.setReferenceCapturing (true);
        else           plugin.setSourceCapturing (true);

        for (int b = 0; b < 80; ++b)
        {
            for (int i = 0; i < blockSize; ++i)
            {
                const auto white = random.nextFloat() * 2.0f - 1.0f;
                state = dullness * state + (1.0f - dullness) * white;
                const auto sample = (dullness > 0.0f ? state * 2.5f : white) * 0.25f;
                buffer.setSample (0, i, sample);
                buffer.setSample (1, i, sample);
            }

            plugin.processBlock (buffer, midi);
        }

        if (reference) plugin.setReferenceCapturing (false);
        else           plugin.setSourceCapturing (false);
    }

    // A matched plugin with a real curve on it -- a dull source against a bright
    // reference -- and the output trim where the user left it.
    void prepareMatched (PluginProcessor& plugin, bool linearPhase, float outputDb)
    {
        plugin.prepareToPlay (sampleRate, blockSize);
        setParameter (plugin, ParamID::phase, linearPhase ? 0.0f : 1.0f);

        learn (plugin, true, 0.0f, 1);
        learn (plugin, false, 0.85f, 2);
        REQUIRE (plugin.performMatch());

        setParameter (plugin, ParamID::outputGain, outputDb);
    }

    struct SineRun
    {
        std::vector<float> output; // left channel, every sample
        float worstJump = 0.0f;
    };

    /** Plays a steady sine through the plugin for `blocks` blocks, calling `atBlock`
        before each one, and records what comes out. */
    SineRun playSine (PluginProcessor& plugin, int blocks, const std::function<void (int)>& atBlock)
    {
        juce::AudioBuffer<float> buffer (2, blockSize);
        juce::MidiBuffer midi;
        double phase = 0.0;
        const auto increment = 2.0 * juce::MathConstants<double>::pi * 220.0 / sampleRate;

        SineRun run;

        for (int b = 0; b < blocks; ++b)
        {
            for (int i = 0; i < blockSize; ++i)
            {
                const auto sample = (float) std::sin (phase) * 0.25f;
                phase += increment;
                buffer.setSample (0, i, sample);
                buffer.setSample (1, i, sample);
            }

            atBlock (b);
            plugin.processBlock (buffer, midi);

            for (int i = 0; i < blockSize; ++i)
                run.output.push_back (buffer.getSample (0, i));
        }

        // From the second sample on, and past the first filter's worth of warm-up.
        for (size_t i = 1 + 8192; i < run.output.size(); ++i)
            run.worstJump = std::max (run.worstJump, std::abs (run.output[i] - run.output[i - 1]));

        return run;
    }

    // Toggles the bypass every 60 blocks -- about a third of a second -- after a warm-up.
    float worstJumpTogglingBypass (bool linearPhase, float outputDb)
    {
        PluginProcessor plugin;
        prepareMatched (plugin, linearPhase, outputDb);

        bool bypassed = false;

        const auto run = playSine (plugin, 600, [&] (int block)
        {
            if (block >= 120 && block % 60 == 0)
            {
                bypassed = ! bypassed;
                setParameter (plugin, ParamID::bypass, bypassed ? 1.0f : 0.0f);
            }
        });

        return run.worstJump;
    }
}

TEST_CASE ("Toggling the bypass does not click", "[bypass]")
{
    // Twice the sine's own steepest step, at the louder of the two levels being faded
    // between. A hard switch between two signals out of step with each other -- a
    // minimum-phase EQ shifts the phase, a linear-phase one delays the whole signal by
    // half its length -- lands far above it, and the resumed convolution this replaced
    // burst out at ten times the signal.
    for (const bool linearPhase : { false, true })
        for (const float outputDb : { 0.0f, -12.0f, 9.0f })
        {
            INFO ((linearPhase ? "linear" : "minimum") << " phase, output " << outputDb << " dB");

            const auto ceiling = 2.0f * sineSlope * juce::jmax (1.0f, juce::Decibels::decibelsToGain (outputDb));
            CHECK (worstJumpTogglingBypass (linearPhase, outputDb) < ceiling);
        }
}

TEST_CASE ("Coming out of bypass does not overshoot", "[bypass]")
{
    // The level moves from where the dry signal was to where the corrected one settles,
    // and never past either: no burst, and no stretch of the correction without the trim.
    for (const bool linearPhase : { false, true })
    {
        INFO ((linearPhase ? "linear" : "minimum") << " phase");

        PluginProcessor plugin;
        prepareMatched (plugin, linearPhase, -12.0f);
        setParameter (plugin, ParamID::bypass, 1.0f);

        constexpr int unbypassAt = 200;
        const auto run = playSine (plugin, 400, [&] (int block)
        {
            if (block == unbypassAt)
                setParameter (plugin, ParamID::bypass, 0.0f);
        });

        const auto peakOver = [&run] (int fromBlock, int toBlock)
        {
            float peak = 0.0f;
            for (auto i = (size_t) (fromBlock * blockSize); i < (size_t) (toBlock * blockSize); ++i)
                peak = std::max (peak, std::abs (run.output[i]));
            return peak;
        };

        const auto before = peakOver (unbypassAt - 40, unbypassAt);
        const auto after = peakOver (400 - 40, 400);
        const auto during = peakOver (unbypassAt, unbypassAt + 20);

        CHECK (during <= std::max (before, after) * 1.01f);
    }
}

TEST_CASE ("Bypassed, the plugin still delays by the latency it reports", "[bypass]")
{
    // Otherwise the host's latency compensation, which is still applied while the plugin
    // is bypassed, puts the dry signal early by that much -- 85 ms in linear phase.
    PluginProcessor plugin;
    prepareMatched (plugin, true, 0.0f);

    const auto latency = plugin.getLatencySamples();
    REQUIRE (latency > 0);

    setParameter (plugin, ParamID::bypass, 1.0f);

    juce::AudioBuffer<float> buffer (2, blockSize);
    juce::MidiBuffer midi;
    std::vector<float> in, out;
    juce::Random random { 9 };

    for (int b = 0; b < 100; ++b)
    {
        for (int i = 0; i < blockSize; ++i)
        {
            const auto sample = random.nextFloat() * 0.5f - 0.25f;
            buffer.setSample (0, i, sample);
            buffer.setSample (1, i, sample);
            in.push_back (sample);
        }

        plugin.processBlock (buffer, midi);

        for (int i = 0; i < blockSize; ++i)
            out.push_back (buffer.getSample (0, i));
    }

    // Past the fade and the latency, every sample out is the one that went in that long ago.
    for (auto i = (size_t) (latency + 4096); i < out.size(); ++i)
        REQUIRE (juce::exactlyEqual (out[i], in[i - (size_t) latency]));
}

TEST_CASE ("The host's bypass is the plugin's own", "[bypass]")
{
    PluginProcessor plugin;
    CHECK (plugin.getBypassParameter() == plugin.getAPVTS().getParameter (ParamID::bypass));
}

