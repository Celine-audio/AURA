/*
    The colours ThemeReachTests cannot see.

    That test moves every role, re-renders the window, and fails if a shipped colour is
    still on screen. It catches a control that took a colour once -- but only if the
    control draws that colour *plainly* and *in the state the test finds it in*. Two
    kinds of straggler slip through:

      - a colour drawn as a blend, which never equals the shipped value exactly;
      - a colour drawn only in a state nothing has put the window into.

    Both applied to the phase tabs, and both were true at once: the selected tab's fill
    is the stage colour at a fifth of its strength, and the state dot only appears once
    that stage holds data. The accent was taken in the constructor and the window came
    out half-themed with nothing to say so.
*/
#include <ui/PhaseTabs.h>
#include <ui/Theme.h>
#include <ui/ThemePalette.h>

#include <catch2/catch_test_macros.hpp>

using namespace Celine;

TEST_CASE ("A tab's stage colour follows the theme", "[theme]")
{
    const struct Restore { ~Restore() { Theme::palette().reset(); } } restore;

    PhaseTabBar bar;
    bar.setBounds (0, 0, 900, 56);
    bar.setSelected (PhaseTabBar::current);

    auto& tab = bar.getTab (PhaseTabBar::current);

    // The action button is left out of the count. It already followed the theme -- it
    // is told its colours in applyColours() -- so counting it would let this pass on
    // the strength of the one part that was never broken.
    const auto button = tab.getActionButton().getBounds();

    const auto shoot = [&tab] { return tab.createComponentSnapshot (tab.getLocalBounds(), false, 1.0f); };

    const auto before = shoot();

    Theme::palette().set (Theme::Role::current, juce::Colour (0xff20c020));
    Theme::palette().sendSynchronousChangeMessage();

    const auto after = shoot();

    REQUIRE (before.getBounds() == after.getBounds());

    int moved = 0;
    {
        const juce::Image::BitmapData a (before, juce::Image::BitmapData::readOnly);
        const juce::Image::BitmapData b (after, juce::Image::BitmapData::readOnly);

        for (int y = 0; y < before.getHeight(); ++y)
            for (int x = 0; x < before.getWidth(); ++x)
                if (! button.contains (x, y) && a.getPixelColour (x, y) != b.getPixelColour (x, y))
                    ++moved;
    }

    // The fill is most of the tab. Anything in the low hundreds would mean only an
    // edge moved; zero is what the constructor snapshot gave.
    INFO ("pixels that followed the role, outside the action button: " << moved);
    CHECK (moved > 2000);
}
