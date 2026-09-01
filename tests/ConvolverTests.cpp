#include "helpers/test_helpers.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <dsp/PartitionedConvolver.h>

#include <cmath>

namespace
{
    constexpr double sampleRate = 48000.0;
    constexpr int blockSize = 256;

    // A 220 Hz sine at 0.25 climbs by at most this much between samples. Anything
    // beyond it is a step the signal did not ask for.
    constexpr float sineSlope = 0.25f * 2.0f * 3.14159265f * 220.0f / (float) sampleRate;

    void learn (PluginProcessor& plugin, bool reference, int blocks, float dullness, int seed)
    {
        juce::AudioBuffer<float> buffer (2, blockSize);
        juce::MidiBuffer midi;
        juce::Random random { seed };
        float state = 0.0f;

        if (reference) plugin.setReferenceCapturing (true);
        else           plugin.setSourceCapturing (true);

        for (int b = 0; b < blocks; ++b)
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

    /** Sweeps one parameter the whole way across its range while a steady sine plays,
        and returns the worst sample-to-sample jump in the output. */
    float worstJumpWhileSweeping (const char* parameterID, float from, float to,
                                  bool linearPhase = true)
    {
        PluginProcessor plugin;
        plugin.prepareToPlay (sampleRate, blockSize);

        if (auto* mode = plugin.getAPVTS().getParameter (ParamID::phase))
            mode->setValueNotifyingHost (linearPhase ? 0.0f : 1.0f);

        learn (plugin, true, 60, 0.0f, 1);
        learn (plugin, false, 60, 0.85f, 2);
        REQUIRE (plugin.performMatch());

        auto* parameter = plugin.getAPVTS().getParameter (parameterID);
        REQUIRE (parameter != nullptr);

        juce::AudioBuffer<float> buffer (2, blockSize);
        juce::MidiBuffer midi;
        double phase = 0.0;
        const double increment = 2.0 * juce::MathConstants<double>::pi * 220.0 / sampleRate;

        float previous = 0.0f, worst = 0.0f;
        bool primed = false;
        constexpr int totalBlocks = 400;

        for (int b = 0; b < totalBlocks; ++b)
        {
            for (int i = 0; i < blockSize; ++i)
            {
                const auto sample = (float) std::sin (phase) * 0.25f;
                phase += increment;
                buffer.setSample (0, i, sample);
                buffer.setSample (1, i, sample);
            }

            parameter->setValueNotifyingHost (
                parameter->convertTo0to1 (from + (to - from) * (float) b / (float) totalBlocks));

            // Let the rebuild throttle's timer run, the way it would in a host.
            juce::MessageManager::getInstance()->runDispatchLoopUntil (3);

            plugin.processBlock (buffer, midi);

            for (int i = 0; i < blockSize; ++i)
            {
                const auto value = buffer.getSample (0, i);

                if (primed)
                    worst = std::max (worst, std::abs (value - previous));

                previous = value;
                primed = true;
            }
        }

        return worst;
    }
}

TEST_CASE ("The convolver reproduces its impulse response exactly", "[convolver]")
{
    constexpr int irLength = 4096;
    constexpr int block = 100; // deliberately not a multiple of the partition size

    juce::AudioBuffer<float> ir (1, irLength);
    ir.clear();
    ir.setSample (0, 0, 0.5f);
    ir.setSample (0, 1, -0.25f);
    ir.setSample (0, 700, 0.75f);
    ir.setSample (0, 3000, 0.125f);

    PartitionedConvolver convolver;
    convolver.prepare (1, irLength);
    convolver.setImpulseResponse (ir);

    // Feed a unit impulse: what comes back has to be the impulse response itself,
    // delayed by exactly the latency the convolver reports.
    std::vector<float> out;
    juce::AudioBuffer<float> buffer (1, block);
    int fed = 0;

    while ((int) out.size() < irLength + convolver.getLatencySamples() + block)
    {
        buffer.clear();

        for (int i = 0; i < block; ++i, ++fed)
            if (fed == 0)
                buffer.setSample (0, i, 1.0f);

        convolver.process (buffer);

        for (int i = 0; i < block; ++i)
            out.push_back (buffer.getSample (0, i));
    }

    const auto latency = convolver.getLatencySamples();

    for (int i = 0; i < latency; ++i)
        REQUIRE_THAT (out[(size_t) i], Catch::Matchers::WithinAbs (0.0f, 1.0e-6f));

    for (int tap = 0; tap < irLength; ++tap)
        REQUIRE_THAT (out[(size_t) (latency + tap)],
                      Catch::Matchers::WithinAbs (ir.getSample (0, tap), 1.0e-5f));
}

TEST_CASE ("Sweeping a curve control does not click", "[convolver]")
{
    // Every one of these reloads the filter while audio is running. They used to jump
    // by ~0.64 -- around ninety times the sine's own slope -- because juce::dsp::
    // Convolution builds a new engine per load and a new engine has no memory of the
    // input that came before it. PartitionedConvolver keeps that history and walks
    // between filters instead, which is what this pins.
    constexpr float ceiling = 8.0f * sineSlope;

    CHECK (worstJumpWhileSweeping (ParamID::smoothing, 0.05f, 3.0f) < ceiling);
    CHECK (worstJumpWhileSweeping (ParamID::amount, -1.0f, 1.0f) < ceiling);
    CHECK (worstJumpWhileSweeping (ParamID::lowFreq, 20.0f, 2000.0f) < ceiling);
    CHECK (worstJumpWhileSweeping (ParamID::link, 0.0f, 1.0f) < ceiling);

    // The trim is ramped rather than rebuilt, so it is the control none of this ever
    // applied to -- it stands here as the floor the others are measured against.
    CHECK (worstJumpWhileSweeping (ParamID::outputGain, -12.0f, 12.0f) < ceiling);
}

TEST_CASE ("Sweeping a curve control does not click in minimum phase either", "[convolver]")
{
    // The zero-latency mode runs the same coefficients through a directly-convolved
    // head as well as the partitioned tail, so it has a second path that has to be
    // walked rather than switched.
    constexpr float ceiling = 8.0f * sineSlope;

    CHECK (worstJumpWhileSweeping (ParamID::smoothing, 0.05f, 3.0f, false) < ceiling);
    CHECK (worstJumpWhileSweeping (ParamID::amount, -1.0f, 1.0f, false) < ceiling);
    CHECK (worstJumpWhileSweeping (ParamID::lowFreq, 20.0f, 2000.0f, false) < ceiling);
    CHECK (worstJumpWhileSweeping (ParamID::link, 0.0f, 1.0f, false) < ceiling);
}
