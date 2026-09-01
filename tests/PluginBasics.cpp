#include "helpers/test_helpers.h"
#include <PluginProcessor.h>
#include <Parameters.h>
#include <ui/AuraLookAndFeel.h>
#include <ui/SpectrumDisplay.h>
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

TEST_CASE ("one is equal to one", "[dummy]")
{
    REQUIRE (1 == 1);
}

TEST_CASE ("The view colours are the palette's, not a default-constructed Colour", "[ui]")
{
    // These were once static Colour objects copied from the palette, which lives in
    // another translation unit with nothing ordering the two. When the link order put
    // them first they held an opaque black, and the graph and tab bar drew in greys.
    using View = SpectrumDisplay::View;

    CHECK (SpectrumDisplay::colourFor (View::current) == Celine::Theme::current());
    CHECK (SpectrumDisplay::colourFor (View::reference) == Celine::Theme::reference());
    CHECK (SpectrumDisplay::colourFor (View::eqCurve) == Celine::Theme::correction());

    // Belt and braces: whatever they are, they must not be the black a
    // default-constructed Colour would give, and they must be told apart.
    for (const auto view : { View::current, View::reference, View::eqCurve })
        CHECK (SpectrumDisplay::colourFor (view) != juce::Colour());

    CHECK (SpectrumDisplay::colourFor (View::current) != SpectrumDisplay::colourFor (View::reference));
    CHECK (SpectrumDisplay::colourFor (View::current) != SpectrumDisplay::colourFor (View::eqCurve));
}

TEST_CASE ("A fresh editor opens at its default size, not its minimum", "[ui]")
{
    // setResizeLimits constrains the bounds it finds, and at construction those are
    // still 0x0 — so it resizes to the minimum, and resized() writes that back into
    // the state the default size is read from. Reading after the call returned the
    // minimum it had just written, and every fresh instance opened at the smallest
    // size allowed. Nothing failed; it just quietly ignored the default.
    runWithinPluginEditor ([] (PluginProcessor& plugin)
    {
        auto* editor = plugin.getActiveEditor();
        REQUIRE (editor != nullptr);

        const auto* constrainer = editor->getConstrainer();
        REQUIRE (constrainer != nullptr);

        CHECK (editor->getWidth() > constrainer->getMinimumWidth());
        CHECK (editor->getHeight() > constrainer->getMinimumHeight());

        // The constrainer holds the ratio: a resize moves both dimensions together,
        // so the graph never ends up a different shape than it was drawn for. The
        // number itself is the editor's to choose, so this checks the two agree
        // rather than pinning a value the design is still tuning.
        REQUIRE (constrainer->getFixedAspectRatio() > 0.0);

        const auto ratio = (double) editor->getWidth() / (double) editor->getHeight();
        CHECK_THAT (ratio, Catch::Matchers::WithinRel (constrainer->getFixedAspectRatio(), 0.01));
        CHECK (ratio > 1.0);
    });
}

TEST_CASE ("Minimum phase is the default", "[instance]")
{
    PluginProcessor plugin;
    plugin.prepareToPlay (48000.0, 512);

    // The mode that behaves like an EQ: nothing for the host to compensate.
    CHECK (plugin.getMatchLatencySamples() == 0);

    const auto* phase = plugin.getAPVTS().getParameter (ParamID::phase);
    REQUIRE (phase != nullptr);
    CHECK (phase->getCurrentValueAsText() == "Minimum");
}

TEST_CASE ("Plugin instance", "[instance]")
{
    PluginProcessor testPlugin;

    SECTION ("name")
    {
        // Version-agnostic so PRODUCT_NAME bumps don't break the test.
        CHECK_THAT (testPlugin.getName().toStdString(),
            Catch::Matchers::StartsWith ("AURA"));
    }
}


#ifdef PAMPLEJUCE_IPP
    #include <ipp.h>

TEST_CASE ("IPP version", "[ipp]")
{
    #if defined(__APPLE__)
        // macOS uses 2021.9.1 from pip wheel (only x86_64 version available)
        CHECK_THAT (ippsGetLibVersion()->Version, Catch::Matchers::Equals ("2021.9.1 (r0x7e208212)"));
    #else
        CHECK_THAT (ippsGetLibVersion()->Version, Catch::Matchers::Equals ("2026.0.0 (r0xa7ad6ebc)"));
    #endif
}
#endif
