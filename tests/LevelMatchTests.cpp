#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <dsp/FilterDesigner.h>
#include <dsp/MatchEngine.h>

#include <cmath>

using Catch::Matchers::WithinAbs;

namespace
{
    constexpr double sampleRate = 48000.0;
    constexpr size_t numBins = (size_t) (1 << spectrumFftOrder) / 2 + 1;
    constexpr double binHz = sampleRate / (double) ((numBins - 1) * 2);

    float fromDb (float db) { return std::pow (10.0f, db / 20.0f); }

    // A spectrum `levelDb` above a reference level, tilting by `tiltPerOctave` dB an
    // octave about 1 kHz.
    std::vector<float> spectrum (float levelDb, float tiltPerOctave = 0.0f)
    {
        std::vector<float> magnitudes (numBins);

        for (size_t k = 0; k < numBins; ++k)
        {
            const auto octaves = std::log2 (std::max (1.0, (double) k * binHz) / 1000.0);
            magnitudes[k] = 0.01f * fromDb (levelDb + tiltPerOctave * (float) octaves);
        }

        return magnitudes;
    }

    MatchEngine::CorrectionCurves match (const MatchEngine::Spectra& source, const MatchEngine::Spectra& reference,
                                         float link = 1.0f)
    {
        MatchEngine engine;
        engine.prepare (sampleRate, 512, 2);

        MatchEngine::Settings settings;
        settings.link = link;
        engine.setSettings (settings);

        engine.importTake (MatchEngine::Side::source, source, sampleRate, "source");
        engine.importTake (MatchEngine::Side::reference, reference, sampleRate, "reference");
        REQUIRE (engine.performMatch());

        return engine.getCorrectionCurves();
    }

    size_t binAt (double hz) { return (size_t) std::round (hz / binHz); }
}

//==============================================================================
TEST_CASE ("The level difference is the median across the audible range", "[level]")
{
    SECTION ("a flat difference is that difference")
    {
        const auto source = spectrum (0.0f), reference = spectrum (9.0f);
        CHECK_THAT (FilterDesigner::levelDifferenceDb ({ { source, reference } }, sampleRate),
                    WithinAbs (9.0f, 0.01f));
    }

    SECTION ("a reference that stops at 16 kHz barely moves it")
    {
        // The empty top is 3% of the band's octaves and asks for a 100 dB cut. A mean
        // would be dragged three dB down by it; the median does not notice.
        const auto source = spectrum (0.0f);
        auto reference = spectrum (9.0f);

        for (size_t k = binAt (16000.0); k < numBins; ++k)
            reference[k] = 1.0e-7f;

        CHECK_THAT (FilterDesigner::levelDifferenceDb ({ { source, reference } }, sampleRate),
                    WithinAbs (9.0f, 0.01f));
    }

    SECTION ("what lies outside the audible range has no say")
    {
        // +9 dB where you can hear it, +40 dB under 20 Hz and over 20 kHz.
        const auto source = spectrum (0.0f);
        auto reference = spectrum (9.0f);

        for (size_t k = 0; k < numBins; ++k)
            if ((double) k * binHz < 20.0 || (double) k * binHz > 20000.0)
                reference[k] = spectrum (40.0f)[k];

        CHECK_THAT (FilterDesigner::levelDifferenceDb ({ { source, reference } }, sampleRate),
                    WithinAbs (9.0f, 0.01f));
    }
}

TEST_CASE ("Moving the band edges leaves the level alone", "[level]")
{
    // The edges are dragged while listening, so the level taken out has to hold still
    // while they move: inside the band, the correction should be exactly what it is with
    // the band wide open, and only the roll-offs should differ.
    const auto source = spectrum (0.0f, -3.0f);
    const MatchEngine::Spectra sources { source, source };
    const MatchEngine::Spectra references { spectrum (12.0f), spectrum (12.0f) };

    const auto curvesWith = [&] (float lowHz, float highHz)
    {
        MatchEngine engine;
        engine.prepare (sampleRate, 512, 2);

        MatchEngine::Settings settings;
        settings.design.lowFreqHz = lowHz;
        settings.design.highFreqHz = highHz;
        settings.design.smoothingOctaves = 0.0f; // so a bin inside the band is that bin alone
        engine.setSettings (settings);

        engine.importTake (MatchEngine::Side::source, sources, sampleRate, "source");
        engine.importTake (MatchEngine::Side::reference, references, sampleRate, "reference");
        REQUIRE (engine.performMatch());

        return engine.getCorrectionCurves();
    };

    const auto open = curvesWith (20.0f, 20000.0f);
    const auto narrow = curvesWith (300.0f, 3000.0f);

    // Well inside the narrow band, a whole octave clear of either roll-off.
    for (auto k = binAt (600.0); k <= binAt (1500.0); ++k)
        REQUIRE_THAT (narrow.leftDb[k], WithinAbs (open.leftDb[k], 1.0e-4f));
}

TEST_CASE ("A louder reference builds the same correction", "[level]")
{
    // The whole point: a mastered reference 15 dB hotter than the mix asks for the same
    // tone as one at the mix's level, and no gain -- that is the output fader's job.
    const auto source = spectrum (0.0f, -3.0f);

    const auto even = match ({ source, source }, { spectrum (0.0f), spectrum (0.0f) });
    const auto hot = match ({ source, source }, { spectrum (15.0f), spectrum (15.0f) });

    REQUIRE (even.isValid());

    for (size_t k = 0; k < numBins; ++k)
        REQUIRE_THAT (hot.leftDb[k], WithinAbs (even.leftDb[k], 0.01f));

    // Centred: as much of the band is cut as boosted, about its log-centre (~632 Hz).
    CHECK_THAT (hot.leftDb[binAt (632.0)], WithinAbs (0.0f, 0.5f));
    CHECK (hot.leftDb[binAt (100.0)] < -5.0f);
    CHECK (hot.leftDb[binAt (5000.0)] > 5.0f);
}

TEST_CASE ("A louder reference does not put a slope on the band's edges", "[level]")
{
    // The band eases the correction back to 0 dB at its edges. With the loudness left in,
    // a reference 15 dB hotter in tone the same as the source was a +15 dB plateau eased
    // down to nothing at each end. With it out there is nothing to ease.
    const auto source = spectrum (0.0f);

    MatchEngine engine;
    engine.prepare (sampleRate, 512, 2);

    MatchEngine::Settings settings;
    settings.design.lowFreqHz = 200.0f;
    settings.design.highFreqHz = 5000.0f;
    engine.setSettings (settings);

    engine.importTake (MatchEngine::Side::source, { source, source }, sampleRate, "source");
    engine.importTake (MatchEngine::Side::reference, { spectrum (15.0f), spectrum (15.0f) }, sampleRate, "reference");
    REQUIRE (engine.performMatch());

    for (const auto db : engine.getCorrectionCurves().leftDb)
        REQUIRE_THAT (db, WithinAbs (0.0f, 0.01f));
}

TEST_CASE ("Only the level L and R share comes out", "[level]")
{
    // The reference's left is 6 dB louder than its right, where the source's are equal.
    // That is a real difference between the two channels and the correction keeps it;
    // what the two have in common -- the reference being 10 dB hotter -- goes.
    const auto source = spectrum (0.0f, -2.0f);
    const auto curves = match ({ source, source }, { spectrum (13.0f), spectrum (7.0f) }, 0.0f);

    for (const auto hz : { 100.0, 1000.0, 8000.0 })
        CHECK_THAT (curves.leftDb[binAt (hz)] - curves.rightDb[binAt (hz)], WithinAbs (6.0f, 0.05f));

    // Each sits 3 dB either side of where a centred curve would.
    const auto centred = match ({ source, source }, { spectrum (10.0f), spectrum (10.0f) }, 0.0f);
    const auto at1k = binAt (1000.0);

    CHECK_THAT (curves.leftDb[at1k], WithinAbs (centred.leftDb[at1k] + 3.0f, 0.05f));
    CHECK_THAT (curves.rightDb[at1k], WithinAbs (centred.rightDb[at1k] - 3.0f, 0.05f));
}
