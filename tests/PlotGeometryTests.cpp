#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <ui/PlotGeometry.h>
#include <ui/SpectrumDisplay.h>

using Catch::Matchers::WithinAbs;

TEST_CASE ("An unshifted plot is the window as designed", "[plot]")
{
    const PlotGeometry plot { { 0.0f, 0.0f, 800.0f, 480.0f } };

    CHECK_THAT (plot.correctionDbToY (24.0f), WithinAbs (0.0f, 1.0e-4f));
    CHECK_THAT (plot.correctionDbToY (0.0f), WithinAbs (240.0f, 1.0e-4f));
    CHECK_THAT (plot.correctionDbToY (-24.0f), WithinAbs (480.0f, 1.0e-4f));

    CHECK_THAT (plot.spectrumDbToY (0.0f), WithinAbs (0.0f, 1.0e-4f));
    CHECK_THAT (plot.spectrumDbToY (-96.0f), WithinAbs (480.0f, 1.0e-4f));
}

TEST_CASE ("Shifting the plot slides both scales and keeps their span", "[plot]")
{
    const PlotGeometry plot { { 0.0f, 0.0f, 800.0f, 480.0f }, -24.0f };

    // Slid down by a whole half-scale: the correction now runs 0 to -48...
    CHECK_THAT (plot.correctionDbToY (0.0f), WithinAbs (0.0f, 1.0e-4f));
    CHECK_THAT (plot.correctionDbToY (-48.0f), WithinAbs (480.0f, 1.0e-4f));

    // ...and the spectrum, which moves twice as far, -48 to -144 dBFS.
    CHECK_THAT (plot.spectrumDbToY (-48.0f), WithinAbs (0.0f, 1.0e-4f));
    CHECK_THAT (plot.spectrumDbToY (-144.0f), WithinAbs (480.0f, 1.0e-4f));

    // Out of view either way runs off that edge, where the display clips it, rather than
    // lying flat along it.
    CHECK (plot.correctionDbToY (-60.0f) > 480.0f);
    CHECK (plot.correctionDbToY (12.0f) < 0.0f);
    CHECK (plot.spectrumDbToY (-160.0f) > 480.0f);
    CHECK (plot.spectrumDbToY (-30.0f) < 0.0f);
}

TEST_CASE ("The two scales still share their gridlines however far the plot is slid", "[plot]")
{
    // The whole reason the shift moves the spectrum by twice as much: every gridline of
    // the correction is at a round 12 dB of signal, so one grid still serves both.
    for (const auto shift : { -36.0f, -17.5f, -3.0f, 0.0f, 7.25f, 12.0f })
    {
        const PlotGeometry plot { { 0.0f, 0.0f, 800.0f, 480.0f }, shift };

        for (int line = -10; line <= 6; ++line)
        {
            const auto correctionDb = (float) line * PlotGeometry::correctionStepDb;
            const auto spectrumDb = PlotGeometry::spectrumDbAtCorrection (correctionDb);

            if (correctionDb < plot.correctionBottomDb() || correctionDb > plot.correctionTopDb())
                continue;

            CHECK_THAT (plot.spectrumDbToY (spectrumDb), WithinAbs (plot.correctionDbToY (correctionDb), 1.0e-3f));
            CHECK_THAT (std::fmod (std::abs (spectrumDb), 12.0f), WithinAbs (0.0f, 1.0e-3f));
        }
    }
}

TEST_CASE ("The display keeps its shift inside the allowed range", "[plot]")
{
    SpectrumDisplay display;

    display.setViewShift (-200.0f);
    CHECK_THAT (display.getViewShift(), WithinAbs (PlotGeometry::minShiftDb, 1.0e-6f));

    display.setViewShift (200.0f);
    CHECK_THAT (display.getViewShift(), WithinAbs (PlotGeometry::maxShiftDb, 1.0e-6f));
}
