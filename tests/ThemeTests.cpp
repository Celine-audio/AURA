#include <ui/Theme.h>
#include <ui/ThemePalette.h>

#include <catch2/catch_test_macros.hpp>

#include <set>

/*
    The theming engine. Two things are worth protecting here and neither is visible by
    looking at the window: that a theme somebody shares survives the round trip through
    a file, and that the list every part of the engine is generated from has no two
    roles claiming the same key.
*/

using namespace Celine;

namespace
{
    /** Puts the palette back however the test leaves, so one case cannot colour the
        next -- there is one palette per process and these all share it. */
    struct ShippedPalette
    {
        ShippedPalette() { Theme::palette().reset(); }
        ~ShippedPalette() { Theme::palette().reset(); }
    };
}

//==============================================================================
TEST_CASE ("Every role has its own key, label and group", "[theme]")
{
    // The keys are what a .celthm file is written and read by, so two roles sharing one
    // would quietly make them a single colour -- saved from whichever was written last,
    // and loaded into both. A copy-and-paste in ThemeRoles.h is all it takes.
    std::set<juce::String> keys;

    for (size_t i = 0; i < Theme::numRoles; ++i)
    {
        const auto& entry = Theme::info()[i];

        INFO ("role " << i << " is " << entry.key);

        CHECK (juce::String (entry.key).isNotEmpty());
        CHECK (juce::String (entry.label).isNotEmpty());
        CHECK (juce::String (entry.group).isNotEmpty());
        CHECK (keys.insert (entry.key).second);
    }

    CHECK (keys.size() == Theme::numRoles);
}

TEST_CASE ("The shipped colours are what the accessors answer", "[theme]")
{
    ShippedPalette shipped;

    CHECK (Theme::palette().isShipped());
    CHECK (Theme::chrome() == juce::Colour (0xff3b334b));
    CHECK (Theme::text() == juce::Colour (0xfff9fbff));
}

TEST_CASE ("A theme survives the round trip through a file", "[theme]")
{
    ShippedPalette shipped;
    auto& palette = Theme::palette();

    palette.set (Theme::Role::chrome, juce::Colour (0xff102030));
    palette.set (Theme::Role::accent, juce::Colour (0xffabcdef));
    palette.setName ("Round trip");

    juce::TemporaryFile temporary (Theme::Palette::fileExtension);
    REQUIRE (palette.saveTo (temporary.getFile()).wasOk());

    palette.reset();
    REQUIRE (Theme::chrome() == juce::Colour (0xff3b334b));

    REQUIRE (palette.loadFrom (temporary.getFile()).wasOk());

    CHECK (Theme::chrome() == juce::Colour (0xff102030));
    CHECK (Theme::accent() == juce::Colour (0xffabcdef));
    CHECK (palette.getName() == "Round trip");
    CHECK_FALSE (palette.isShipped());
}

TEST_CASE ("A theme file forgives what it can and refuses what it cannot", "[theme]")
{
    ShippedPalette shipped;
    auto& palette = Theme::palette();

    SECTION ("a key this build does not know is ignored")
    {
        // Which is what lets a theme written by a plugin with more colours than this one
        // still load. GALLERY has cabinets; SPACE has none, and must not choke on them.
        juce::var parsed;
        REQUIRE (juce::JSON::parse (R"({"colours":{"chrome":"#112233","somethingElse":"#445566"}})",
                                    parsed).wasOk());

        CHECK (palette.fromVar (parsed).wasOk());
        CHECK (Theme::chrome() == juce::Colour (0xff112233));
    }

    SECTION ("a key the file omits comes back shipped, not left as it was")
    {
        palette.set (Theme::Role::accent, juce::Colour (0xff00ff00));

        juce::var parsed;
        REQUIRE (juce::JSON::parse (R"({"colours":{"chrome":"#112233"}})", parsed).wasOk());
        REQUIRE (palette.fromVar (parsed).wasOk());

        // Loading a theme has to give the same result whatever was loaded before it.
        CHECK (Theme::accent() == juce::Colour (Theme::info()[(size_t) Theme::Role::accent].shipped));
    }

    SECTION ("hex is read the way people write it")
    {
        for (const auto* spelling : { R"({"colours":{"chrome":"#112233"}})",
                                      R"({"colours":{"chrome":"112233"}})",
                                      R"({"colours":{"chrome":"  #112233  "}})" })
        {
            juce::var parsed;
            REQUIRE (juce::JSON::parse (spelling, parsed).wasOk());
            REQUIRE (palette.fromVar (parsed).wasOk());

            INFO (spelling);
            CHECK (Theme::chrome() == juce::Colour (0xff112233));
        }
    }

    SECTION ("three digits mean what they do everywhere else")
    {
        juce::var parsed;
        REQUIRE (juce::JSON::parse (R"({"colours":{"chrome":"#abc"}})", parsed).wasOk());
        REQUIRE (palette.fromVar (parsed).wasOk());

        CHECK (Theme::chrome() == juce::Colour (0xffaabbcc));
    }

    SECTION ("nonsense in a value leaves that colour shipped rather than black")
    {
        juce::var parsed;
        REQUIRE (juce::JSON::parse (R"({"colours":{"chrome":"not a colour"}})", parsed).wasOk());
        REQUIRE (palette.fromVar (parsed).wasOk());

        CHECK (Theme::chrome() == juce::Colour (0xff3b334b));
    }

    SECTION ("a file that is not a theme is refused")
    {
        juce::var parsed;
        REQUIRE (juce::JSON::parse (R"({"something":"else"})", parsed).wasOk());

        CHECK (palette.fromVar (parsed).failed());
    }
}

TEST_CASE ("Every colour is reachable through the file", "[theme]")
{
    // A role that is in the enum but never written is a colour the editor can change and
    // a theme cannot carry -- which is the failure somebody discovers by sharing a theme
    // and being told it looks wrong.
    ShippedPalette shipped;
    auto& palette = Theme::palette();

    for (size_t i = 0; i < Theme::numRoles; ++i)
        palette.set ((Theme::Role) i, juce::Colour (0xff123456));

    juce::TemporaryFile temporary (Theme::Palette::fileExtension);
    REQUIRE (palette.saveTo (temporary.getFile()).wasOk());

    palette.reset();
    REQUIRE (palette.loadFrom (temporary.getFile()).wasOk());

    for (size_t i = 0; i < Theme::numRoles; ++i)
    {
        INFO ("role " << Theme::info()[i].key);
        CHECK (palette.get ((Theme::Role) i) == juce::Colour (0xff123456));
    }
}
