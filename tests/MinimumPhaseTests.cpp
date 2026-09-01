#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "helpers/test_helpers.h"

#include <Parameters.h>
#include <dsp/FilterDesigner.h>
#include <dsp/IrExport.h>
#include <juce_dsp/juce_dsp.h>

#include <cmath>
#include <numeric>

namespace
{
    // The magnitude response an impulse response actually has, at the given bin of an
    // analysis of the given size.
    std::vector<float> magnitudeResponse (const std::vector<float>& ir, int fftSize)
    {
        juce::dsp::FFT fft ((int) std::round (std::log2 ((double) fftSize)));

        std::vector<std::complex<float>> time ((size_t) fftSize, { 0.0f, 0.0f }), freq ((size_t) fftSize);

        for (size_t i = 0; i < std::min (ir.size(), (size_t) fftSize); ++i)
            time[i] = { ir[i], 0.0f };

        fft.perform (time.data(), freq.data(), false);

        std::vector<float> mags ((size_t) (fftSize / 2 + 1));

        for (size_t k = 0; k < mags.size(); ++k)
            mags[k] = std::abs (freq[k]);

        return mags;
    }

    // A smooth, distinctly non-flat target: a broad boost low, a broad cut high.
    std::vector<float> targetCurve (int numBins)
    {
        std::vector<float> mag ((size_t) numBins);

        for (int k = 0; k < numBins; ++k)
        {
            const auto position = (float) k / (float) (numBins - 1);
            const auto db = 8.0f * std::cos (position * juce::MathConstants<float>::pi * 1.5f);
            mag[(size_t) k] = std::pow (10.0f, db / 20.0f);
        }

        return mag;
    }
}

TEST_CASE ("A minimum-phase response carries the magnitude it was asked for", "[designer]")
{
    constexpr int numBins = 2049;
    constexpr int irLength = 8192;
    constexpr int analysis = 16384;

    const auto target = targetCurve (numBins);
    const auto ir = FilterDesigner::buildMinimumPhaseIR (target, irLength);

    REQUIRE (ir.size() == (size_t) irLength);

    const auto actual = magnitudeResponse (ir, analysis);

    // Compared in dB across the audible span, skipping the very ends where the
    // truncation taper and the log floor both bite.
    for (int k = analysis / 200; k < analysis / 2 - analysis / 200; ++k)
    {
        const auto wanted = 20.0f * std::log10 (std::max (
            target[(size_t) juce::jmin ((int) target.size() - 1,
                                        k * (numBins - 1) / (analysis / 2))], 1.0e-9f));
        const auto got = 20.0f * std::log10 (std::max (actual[(size_t) k], 1.0e-9f));

        REQUIRE_THAT (got, Catch::Matchers::WithinAbs (wanted, 0.75f));
    }
}

TEST_CASE ("Minimum phase front-loads the energy that linear phase centres", "[designer]")
{
    constexpr int numBins = 2049;
    constexpr int irLength = 8192;

    const auto target = targetCurve (numBins);

    const auto linear = FilterDesigner::buildLinearPhaseIR (target, irLength);
    const auto minimum = FilterDesigner::buildMinimumPhaseIR (target, irLength);

    const auto centroid = [] (const std::vector<float>& ir)
    {
        double weighted = 0.0, energy = 0.0;

        for (size_t i = 0; i < ir.size(); ++i)
        {
            const auto e = (double) ir[i] * (double) ir[i];
            weighted += e * (double) i;
            energy += e;
        }

        return energy > 0.0 ? weighted / energy : 0.0;
    };

    // Linear phase is symmetric, so its energy sits at the middle: that centre is
    // exactly the latency the mode costs.
    CHECK_THAT ((float) centroid (linear),
                Catch::Matchers::WithinRel ((float) (irLength / 2), 0.05f));

    // Minimum phase puts it as early as the magnitude allows, which is what lets the
    // mode report no latency at all.
    CHECK (centroid (minimum) < (double) irLength * 0.05);

    // And there is no pre-ringing to smear ahead of a transient: nothing before the
    // peak comes close to it.
    const auto peak = std::max_element (minimum.begin(), minimum.end(),
                                        [] (float a, float b) { return std::abs (a) < std::abs (b); });
    CHECK (std::distance (minimum.begin(), peak) < irLength / 100);
}

TEST_CASE ("Each phase mode reports the latency it actually costs", "[processor]")
{
    PluginProcessor plugin;
    plugin.prepareToPlay (48000.0, 512);

    auto* phase = plugin.getAPVTS().getParameter (ParamID::phase);
    REQUIRE (phase != nullptr);

    // Linear phase is symmetric about the middle of the filter, so half its length is
    // what it costs. The convolver adds nothing of its own on top.
    phase->setValueNotifyingHost (0.0f);
    CHECK (plugin.getMatchLatencySamples() == plugin.getFilterLength() / 2);
    CHECK (plugin.getMatchLatencySamples() == 4096);

    // Minimum phase is the mode that exists to cost nothing.
    phase->setValueNotifyingHost (1.0f);
    CHECK (plugin.getMatchLatencySamples() == 0);
}

TEST_CASE ("A minimum-phase export is minimum phase", "[export]")
{
    constexpr size_t numBins = 2049;

    // Asymmetric on purpose: a flat curve produces a single spike either way, which
    // would not tell the two modes apart.
    std::vector<float> leftDb (numBins), rightDb (numBins);

    for (size_t k = 0; k < numBins; ++k)
    {
        const auto position = (float) k / (float) (numBins - 1);
        leftDb[k] = 9.0f * std::cos (position * juce::MathConstants<float>::pi * 1.5f);
        rightDb[k] = leftDb[k];
    }

    const auto centroid = [] (const juce::AudioBuffer<float>& buffer)
    {
        double weighted = 0.0, energy = 0.0;

        for (int i = 0; i < buffer.getNumSamples(); ++i)
        {
            const auto value = (double) buffer.getSample (0, i);
            const auto e = value * value;
            weighted += e * (double) i;
            energy += e;
        }

        return energy > 0.0 ? weighted / energy : 0.0;
    };

    IrExport::Options options;
    options.layout = IrExport::Options::Layout::mono;
    options.channel = IrExport::Options::Channel::left;
    options.length = 8192;

    options.phase = IrExport::Options::Phase::linear;
    const auto linear = IrExport::buildBuffer (leftDb, rightDb, options);

    options.phase = IrExport::Options::Phase::minimum;
    const auto minimum = IrExport::buildBuffer (leftDb, rightDb, options);

    REQUIRE (linear.getNumSamples() == 8192);
    REQUIRE (minimum.getNumSamples() == 8192);

    // Linear phase puts its energy in the middle, which is the delay a convolver
    // loading the file would inherit. Minimum phase puts it at the front.
    CHECK (centroid (linear) > 8192.0 * 0.4);
    CHECK (centroid (minimum) < 8192.0 * 0.05);

    // Same filter either way, which is what makes the choice free: the DC gain a
    // +9 dB curve asks for at the bottom end survives both.
    const auto dcGain = [] (const juce::AudioBuffer<float>& buffer)
    {
        double sum = 0.0;

        for (int i = 0; i < buffer.getNumSamples(); ++i)
            sum += (double) buffer.getSample (0, i);

        return sum;
    };

    CHECK_THAT ((float) dcGain (minimum),
                Catch::Matchers::WithinRel ((float) dcGain (linear), 0.02f));
}
