#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <dsp/FilterDesigner.h>
#include <dsp/SpectrumAnalyzer.h>

#include <algorithm>
#include <cmath>
#include <vector>

namespace
{
    constexpr double pi = 3.14159265358979323846;
}

TEST_CASE ("SpectrumAnalyzer peaks at a pure tone's bin", "[analyzer]")
{
    constexpr int order = 12;
    const int fftSize = 1 << order;
    const double sampleRate = 48000.0;

    SpectrumAnalyzer analyzer (order);
    analyzer.setCapturing (true);

    // A tone whose frequency lands exactly on a bin centre so it doesn't leak.
    const int targetBin = 100;
    const double freq = (double) targetBin * sampleRate / (double) fftSize;

    std::vector<float> block (512, 0.0f);
    const int numBlocks = 64; // plenty of frames to average
    double phase = 0.0;
    const double inc = 2.0 * pi * freq / sampleRate;

    for (int b = 0; b < numBlocks; ++b)
    {
        for (auto& s : block)
        {
            s = (float) std::sin (phase);
            phase += inc;
        }
        analyzer.pushBlock (block.data(), (int) block.size());
    }

    std::vector<float> mags;
    REQUIRE (analyzer.getAveragedMagnitudes (mags));
    REQUIRE (mags.size() == (size_t) (fftSize / 2 + 1));

    int peakBin = 0;
    float peakVal = 0.0f;
    for (int k = 1; k < (int) mags.size(); ++k)
        if (mags[(size_t) k] > peakVal)
        {
            peakVal = mags[(size_t) k];
            peakBin = k;
        }

    CHECK (peakBin == targetBin);
}

TEST_CASE ("SpectrumAnalyzer magnitudes are absolute, not relative", "[analyzer]")
{
    constexpr int order = 12;
    const int fftSize = 1 << order;
    const double sampleRate = 48000.0;

    // A full-scale sine parked on a bin centre is the reference point of the dBFS
    // scale the display draws against: it has to read 0 dBFS, and a sine 20 dB down
    // has to read 20 dB down, or "absolute dB" on the display means nothing.
    auto peakDbForAmplitude = [&] (float amplitude)
    {
        SpectrumAnalyzer analyzer (order);
        analyzer.setCapturing (true);

        const int targetBin = 100;
        const double freq = (double) targetBin * sampleRate / (double) fftSize;
        const double inc = 2.0 * pi * freq / sampleRate;

        std::vector<float> block (512, 0.0f);
        double phase = 0.0;

        for (int b = 0; b < 64; ++b)
        {
            for (auto& sample : block)
            {
                sample = amplitude * (float) std::sin (phase);
                phase += inc;
            }
            analyzer.pushBlock (block.data(), (int) block.size());
        }

        std::vector<float> mags;
        REQUIRE (analyzer.getAveragedMagnitudes (mags));

        return 20.0f * std::log10 (*std::max_element (mags.begin() + 1, mags.end()));
    };

    CHECK_THAT (peakDbForAmplitude (1.0f), Catch::Matchers::WithinAbs (0.0f, 0.5f));
    CHECK_THAT (peakDbForAmplitude (0.1f), Catch::Matchers::WithinAbs (-20.0f, 0.5f));
}

TEST_CASE ("A live analyzer decays towards silence when the signal stops", "[analyzer]")
{
    constexpr int order = 12;

    // The moving display traces used to be normalised to their own peak, so when the
    // audio stopped the whole trace was re-scaled back up and climbed to the top of
    // the graph. The display now draws absolute levels, which only reads correctly if
    // a starved analyzer actually falls: every bin has to go down, never up.
    SpectrumAnalyzer analyzer (order);
    analyzer.setExponentialMode (true);
    analyzer.setCapturing (true);

    juce::Random random { 4321 };
    std::vector<float> block (512, 0.0f);

    for (int b = 0; b < 64; ++b)
    {
        for (auto& sample : block)
            sample = 0.25f * (random.nextFloat() * 2.0f - 1.0f);

        analyzer.pushBlock (block.data(), (int) block.size());
    }

    std::vector<float> playing;
    REQUIRE (analyzer.getAveragedMagnitudes (playing));

    // Roughly three seconds of silence at 48 kHz. The decay is per analysis frame, so
    // it takes a beat to get all the way down — it just has to get there.
    std::fill (block.begin(), block.end(), 0.0f);
    for (int b = 0; b < 256; ++b)
        analyzer.pushBlock (block.data(), (int) block.size());

    std::vector<float> silent;
    REQUIRE (analyzer.getAveragedMagnitudes (silent));
    REQUIRE (silent.size() == playing.size());

    for (size_t k = 1; k < silent.size(); ++k)
        REQUIRE (silent[k] < playing[k]);

    // And it has to reach the display's floor, not just dip a little.
    const auto loudestWhenSilent = *std::max_element (silent.begin() + 1, silent.end());
    const auto loudestWhenPlaying = *std::max_element (playing.begin() + 1, playing.end());

    CHECK (20.0f * std::log10 (loudestWhenSilent / loudestWhenPlaying) < -90.0f);
}

TEST_CASE ("FilterDesigner produces flat correction when spectra match", "[designer]")
{
    const int fftSize = 4096;
    const int numBins = fftSize / 2 + 1;

    std::vector<float> src (numBins, 0.5f);
    std::vector<float> ref (numBins, 0.5f);

    FilterDesigner::Params params; // amount 1, no smoothing effect on flat data
    const auto correction = FilterDesigner::computeCorrectionMagnitudes (src, ref, 48000.0, params);

    for (auto c : correction)
        CHECK_THAT (c, Catch::Matchers::WithinAbs (1.0f, 1.0e-3f));
}

TEST_CASE ("FilterDesigner IR has correct DC gain", "[designer]")
{
    const int fftSize = 4096;
    const int numBins = fftSize / 2 + 1;

    // Reference is 2x the source across the board -> ~+6 dB flat correction.
    std::vector<float> src (numBins, 0.25f);
    std::vector<float> ref (numBins, 0.5f);

    FilterDesigner::Params params;
    params.smoothingOctaves = 0.0f;
    const auto correction = FilterDesigner::computeCorrectionMagnitudes (src, ref, 48000.0, params);

    CHECK_THAT (correction[0], Catch::Matchers::WithinAbs (2.0f, 1.0e-2f));

    const auto ir = FilterDesigner::buildLinearPhaseIR (correction, fftSize);
    REQUIRE (ir.size() == (size_t) fftSize);

    // DC gain of an FIR is the sum of its taps; it should match the target gain.
    double sum = 0.0;
    for (auto s : ir)
        sum += (double) s;

    CHECK_THAT ((float) sum, Catch::Matchers::WithinAbs (2.0f, 1.0e-2f));

    // The linear-phase IR's energy should be centred around fftSize/2.
    double weighted = 0.0, energy = 0.0;
    for (int i = 0; i < fftSize; ++i)
    {
        const auto e = (double) ir[(size_t) i] * (double) ir[(size_t) i];
        weighted += e * i;
        energy += e;
    }
    const auto centroid = weighted / energy;
    CHECK (std::abs (centroid - fftSize / 2) < 8.0);
}

TEST_CASE ("FilterDesigner band limits confine the correction", "[designer]")
{
    const int fftSize = 4096;
    const int numBins = fftSize / 2 + 1;
    const double sampleRate = 48000.0;
    const double binHz = sampleRate / (double) fftSize;

    // A flat +6 dB target everywhere.
    std::vector<float> src (numBins, 0.25f);
    std::vector<float> ref (numBins, 0.5f);

    FilterDesigner::Params params;
    params.smoothingOctaves = 0.0f;
    params.lowFreqHz = 200.0f;
    params.highFreqHz = 5000.0f;
    params.transitionOctaves = 0.5f;

    const auto correction = FilterDesigner::computeCorrectionMagnitudes (src, ref, sampleRate, params);

    auto binAt = [&] (double hz) { return (int) std::round (hz / binHz); };

    // Well inside the band: full correction (~2.0).
    CHECK_THAT (correction[(size_t) binAt (1000.0)], Catch::Matchers::WithinAbs (2.0f, 0.05f));

    // Well outside the band: flat (~1.0, i.e. 0 dB).
    CHECK_THAT (correction[(size_t) binAt (50.0)],    Catch::Matchers::WithinAbs (1.0f, 0.05f));
    CHECK_THAT (correction[(size_t) binAt (18000.0)], Catch::Matchers::WithinAbs (1.0f, 0.05f));
}

TEST_CASE ("smoothOctaves leaves a flat curve flat", "[designer]")
{
    std::vector<float> db (2049, -3.5f);
    FilterDesigner::smoothOctaves (db, 48000.0, 1.0f);

    for (auto v : db)
        CHECK_THAT (v, Catch::Matchers::WithinAbs (-3.5f, 1.0e-3f));
}

TEST_CASE ("smoothOctaves with zero width is a no-op", "[designer]")
{
    std::vector<float> db (2049, 0.0f);
    db[500] = 9.0f;

    const auto before = db;
    FilterDesigner::smoothOctaves (db, 48000.0, 0.0f);

    CHECK (db == before);
}

TEST_CASE ("smoothOctaves spreads a narrow peak inside its own bounds", "[designer]")
{
    constexpr size_t numBins = 2049;
    constexpr size_t peakBin = 500;

    std::vector<float> db (numBins, 0.0f);
    db[peakBin] = 12.0f;

    FilterDesigner::smoothOctaves (db, 48000.0, 1.0f);

    // A Gaussian is a positive, normalised kernel, so every output is a convex
    // combination of the inputs and can never leave their range.
    for (auto v : db)
    {
        CHECK (v >= -1.0e-4f);
        CHECK (v <= 12.0f + 1.0e-4f);
    }

    // The spike is flattened...
    CHECK (db[peakBin] < 6.0f);

    // ...and its energy has moved into the neighbourhood rather than vanishing.
    CHECK (db[peakBin - 20] > 0.0f);
    CHECK (db[peakBin + 20] > 0.0f);
}

TEST_CASE ("smoothOctaves widens with the octave setting", "[designer]")
{
    constexpr size_t numBins = 2049;
    constexpr size_t peakBin = 500;

    auto peakAfterSmoothing = [] (float octaves)
    {
        std::vector<float> db (numBins, 0.0f);
        db[peakBin] = 12.0f;
        FilterDesigner::smoothOctaves (db, 48000.0, octaves);
        return db[peakBin];
    };

    // Wider windows spread the same peak over more bins, so its height drops.
    CHECK (peakAfterSmoothing (2.0f) < peakAfterSmoothing (0.5f));
    CHECK (peakAfterSmoothing (0.5f) < peakAfterSmoothing (0.1f));
}

TEST_CASE ("applyLink blends the two channels", "[designer]")
{
    const std::vector<float> originalLeft (16, 6.0f);
    const std::vector<float> originalRight (16, -2.0f);
    const auto expectedAverage = 2.0f;

    SECTION ("fully linked gives both channels the average")
    {
        auto left = originalLeft, right = originalRight;
        FilterDesigner::applyLink (left, right, 1.0f);

        for (size_t k = 0; k < left.size(); ++k)
        {
            CHECK_THAT (left[k], Catch::Matchers::WithinAbs (expectedAverage, 1.0e-4f));
            CHECK_THAT (right[k], Catch::Matchers::WithinAbs (expectedAverage, 1.0e-4f));
        }
    }

    SECTION ("unlinked leaves each channel alone")
    {
        auto left = originalLeft, right = originalRight;
        FilterDesigner::applyLink (left, right, 0.0f);

        CHECK (left == originalLeft);
        CHECK (right == originalRight);
    }

    SECTION ("half linked moves each channel halfway to the average")
    {
        auto left = originalLeft, right = originalRight;
        FilterDesigner::applyLink (left, right, 0.5f);

        for (size_t k = 0; k < left.size(); ++k)
        {
            CHECK_THAT (left[k], Catch::Matchers::WithinAbs (4.0f, 1.0e-4f));
            CHECK_THAT (right[k], Catch::Matchers::WithinAbs (0.0f, 1.0e-4f));
        }
    }
}

TEST_CASE ("averageDb averages element-wise", "[designer]")
{
    const std::vector<float> left { 6.0f, 0.0f, -12.0f };
    const std::vector<float> right { 0.0f, 4.0f, 12.0f };

    const auto average = FilterDesigner::averageDb (left, right);

    REQUIRE (average.size() == 3);
    CHECK_THAT (average[0], Catch::Matchers::WithinAbs (3.0f, 1.0e-5f));
    CHECK_THAT (average[1], Catch::Matchers::WithinAbs (2.0f, 1.0e-5f));
    CHECK_THAT (average[2], Catch::Matchers::WithinAbs (0.0f, 1.0e-5f));
}
