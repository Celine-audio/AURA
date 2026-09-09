#include "helpers/test_helpers.h"

#include <catch2/catch_test_macros.hpp>

#include <dsp/FilterDesigner.h>
#include <dsp/SpectrumAnalyzer.h>

#include <chrono>
#include <cmath>

namespace
{
    constexpr int irLength = (1 << spectrumFftOrder) * 2;
    constexpr int numBins = (1 << spectrumFftOrder) / 2 + 1;

    // The size the minimum-phase build works at: nextPowerOfTwo (irLength * 4),
    // four times the response it keeps. 32768 points, so order 15.
    constexpr int minimumPhaseFftOrder = spectrumFftOrder + 3;

    std::vector<float> aCurve()
    {
        std::vector<float> mag ((size_t) numBins);

        for (int k = 0; k < numBins; ++k)
            mag[(size_t) k] = 0.5f + 0.4f * std::sin (0.01f * (float) k);

        return mag;
    }

    template <typename Fn>
    double millisecondsPerCall (int reps, Fn&& fn)
    {
        fn(); // once first, so neither side is charged for a cold cache

        const auto start = std::chrono::steady_clock::now();

        for (int i = 0; i < reps; ++i)
            fn();

        const std::chrono::duration<double, std::milli> elapsed { std::chrono::steady_clock::now() - start };
        return elapsed.count() / (double) reps;
    }
}

TEST_CASE ("A kept builder returns exactly what a thrown-away one did", "[irbuilder]")
{
    const auto mag = aCurve();

    FilterDesigner::IrBuilder builder;

    // Twice each, because the second call is the one that takes the cached transform
    // rather than making it, and it has to give the same answer as the first.
    for (int pass = 0; pass < 2; ++pass)
    {
        CHECK (builder.buildMinimumPhase (mag, irLength) == FilterDesigner::buildMinimumPhaseIR (mag, irLength));
        CHECK (builder.buildLinearPhase (mag, irLength) == FilterDesigner::buildLinearPhaseIR (mag, irLength));
    }

    // The two modes work at different sizes, so alternating between them makes the
    // builder discard one transform and make the other. If that ever stops happening
    // the results come out of a transform of the wrong length.
    for (int pass = 0; pass < 2; ++pass)
    {
        CHECK (builder.buildLinearPhase (mag, irLength / 2) == FilterDesigner::buildLinearPhaseIR (mag, irLength / 2));
        CHECK (builder.buildMinimumPhase (mag, irLength) == FilterDesigner::buildMinimumPhaseIR (mag, irLength));
    }
}

TEST_CASE ("Keeping the transform is most of what a rebuild costs", "[irbuilder]")
{
    // Constructing the FFT the minimum-phase build works at costs whatever the
    // platform's engine charges for a setup. vDSP precomputes twiddle tables for
    // 32768 points and wants around fifteen milliseconds for them -- against a
    // single millisecond for the three transforms the build performs -- so keeping
    // the transform is nearly the whole saving, and making one per call put a
    // stereo rebuild at about thirty milliseconds of message-thread time, every
    // 120 ms for as long as a drag lasted.
    //
    // Not every engine charges that. One that allocates a table instead of filling
    // it in -- IPP, or JUCE's own fallback, which is what the Windows and Linux
    // builds get -- makes a setup nearly free, and there the ratio carries no
    // signal at all: the same correct code measures about 1.1 rather than 15.
    //
    // So the setup is timed rather than assumed. The wide margin is asserted only
    // where a setup really is most of a build, which is where it can detect
    // anything; everywhere else the assertion is the part that holds on any engine
    // -- keeping a transform is never slower than making one every time.
    //
    // Measured as a ratio rather than against a wall-clock figure, because both
    // halves run on the same machine in the same run: a slow machine moves both.
    const auto mag = aCurve();

    FilterDesigner::IrBuilder builder;

    const auto made  = millisecondsPerCall (8, [&] { auto ir = FilterDesigner::buildMinimumPhaseIR (mag, irLength); (void) ir.size(); });
    const auto kept  = millisecondsPerCall (8, [&] { auto ir = builder.buildMinimumPhase (mag, irLength); (void) ir.size(); });
    const auto setup = millisecondsPerCall (8, []  { juce::dsp::FFT fft (minimumPhaseFftOrder); (void) fft.getSize(); });

    INFO ("made per call: " << made << " ms, kept: " << kept << " ms, setup: " << setup << " ms");

    // The margin is for measurement noise, not for a regression: eight reps of a
    // few milliseconds on a loaded CI runner move by more than a clean percent.
    CHECK (kept < made * 1.25);

    if (setup > made * 0.5)
        CHECK (kept * 4.0 < made);
}

TEST_CASE ("Linked channels are convolved with the same response", "[irbuilder]")
{
    // Fully linked, applyLink assigns both channels the average rather than
    // interpolating towards it, so the two curves are equal to the bit and the second
    // response is the first one built again. It is skipped -- and this is the check
    // that skipping it really did leave both channels carrying the filter, rather
    // than leaving the second one empty.
    PluginProcessor plugin;

    constexpr double sampleRate = 48000.0;
    constexpr int blockSize = 512;

    plugin.prepareToPlay (sampleRate, blockSize);

    juce::AudioBuffer<float> buffer (2, blockSize);
    juce::MidiBuffer midi;
    juce::Random random { 4 };

    const auto pushNoise = [&] (int blocks, float rightTilt)
    {
        float state = 0.0f;

        for (int b = 0; b < blocks; ++b)
        {
            for (int i = 0; i < blockSize; ++i)
            {
                const auto white = random.nextFloat() * 2.0f - 1.0f;
                state = rightTilt * state + (1.0f - rightTilt) * white;

                buffer.setSample (0, i, white * 0.25f);
                buffer.setSample (1, i, (rightTilt > 0.0f ? state * 0.8f : white) * 0.25f);
            }

            plugin.processBlock (buffer, midi);
        }
    };

    plugin.setReferenceCapturing (true);
    pushNoise (80, 0.0f);
    plugin.setReferenceCapturing (false);

    plugin.setSourceCapturing (true);
    pushNoise (80, 0.9f);
    plugin.setSourceCapturing (false);

    REQUIRE (plugin.performMatch());

    // Link ships at 1, so this is the shipped configuration.
    const auto& curves = plugin.getCorrectionCurves();
    REQUIRE (curves.isValid());
    REQUIRE (curves.leftDb == curves.rightDb);

    // The same signal in both channels has to come back the same in both channels.
    // Long enough to run past the response, so a channel filtered with silence would
    // read as silence rather than as latency.
    juce::AudioBuffer<float> probe (2, blockSize);
    float worst = 0.0f, loudest = 0.0f;

    for (int b = 0; b < 40; ++b)
    {
        for (int i = 0; i < blockSize; ++i)
        {
            const auto sample = (b == 0 && i == 0) ? 0.5f : 0.0f;
            probe.setSample (0, i, sample);
            probe.setSample (1, i, sample);
        }

        plugin.processBlock (probe, midi);

        for (int i = 0; i < blockSize; ++i)
        {
            worst = std::max (worst, std::abs (probe.getSample (0, i) - probe.getSample (1, i)));
            loudest = std::max (loudest, std::abs (probe.getSample (0, i)));
        }
    }

    CHECK (loudest > 1.0e-3f); // the filter is actually doing something
    CHECK (worst < 1.0e-6f);
}
