#include "helpers/test_helpers.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <ui/SpectrumDisplay.h>
#include <ui/Theme.h>
#include <ui/ThemePalette.h>

namespace
{
    /** A synthesised pointer event at a position inside the component. */
    juce::MouseEvent eventAt (juce::Component& component, juce::Point<float> position,
                              juce::Point<float> downPosition, bool dragged)
    {
        return { juce::Desktop::getInstance().getMainMouseSource(),
                 position,
                 juce::ModifierKeys::currentModifiers,
                 juce::MouseInputSource::defaultPressure,
                 juce::MouseInputSource::defaultOrientation,
                 juce::MouseInputSource::defaultRotation,
                 juce::MouseInputSource::defaultTiltX,
                 juce::MouseInputSource::defaultTiltY,
                 &component, &component,
                 juce::Time::getCurrentTime(),
                 downPosition,
                 juce::Time::getCurrentTime(),
                 1, dragged };
    }
}

TEST_CASE ("A band edge can be dragged on the EQ Curve graph", "[ui]")
{
    SpectrumDisplay display;
    display.setSize (900, 400);
    display.setSampleRate (48000.0);
    display.setView (SpectrumDisplay::View::eqCurve);
    display.setBand (100.0f, 10000.0f);

    float draggedLow = 0.0f;
    int lowWrites = 0, highWrites = 0;
    int gesturesStarted = 0, gesturesEnded = 0;
    std::optional<SpectrumDisplay::BandEdge> gesturedEdge;

    display.onBandDragged = [&] (SpectrumDisplay::BandEdge edge, float hz)
    {
        if (edge == SpectrumDisplay::BandEdge::low) { draggedLow = hz; ++lowWrites; }
        else                                        { ++highWrites; }
    };
    display.onBandGesture = [&] (SpectrumDisplay::BandEdge edge, bool starting)
    {
        gesturedEdge = edge;
        starting ? ++gesturesStarted : ++gesturesEnded;
    };

    // Find the low edge by sweeping for the x that takes hold of it. The geometry is
    // the display's own business, so the test asks rather than assumes.
    int grabbedAt = -1;

    for (int x = 0; x < display.getWidth() && grabbedAt < 0; ++x)
    {
        const juce::Point<float> at { (float) x, (float) display.getHeight() * 0.5f };
        display.mouseDown (eventAt (display, at, at, false));

        if (gesturesStarted > 0)
            grabbedAt = x;
        else
            display.mouseUp (eventAt (display, at, at, false));
    }

    REQUIRE (grabbedAt > 0);
    CHECK (gesturesStarted == 1);

    // Dragging right raises the frequency, because the axis runs that way.
    const juce::Point<float> from { (float) grabbedAt, (float) display.getHeight() * 0.5f };
    const juce::Point<float> to { from.x + 80.0f, from.y };

    display.mouseDrag (eventAt (display, to, from, true));

    CHECK (draggedLow > 100.0f);
    CHECK (lowWrites == 1);

    // The edge that was not touched is not reported at all. Reporting the pair meant
    // the editor wrote both parameters, and a host recording automation logged a
    // change to High Freq every time you moved Low Freq.
    CHECK (highWrites == 0);
    CHECK (gesturedEdge == SpectrumDisplay::BandEdge::low);

    display.mouseUp (eventAt (display, to, from, true));
    CHECK (gesturesEnded == 1);
}

TEST_CASE ("The band edges cannot be dragged past each other", "[ui]")
{
    SpectrumDisplay display;
    display.setSize (900, 400);
    display.setSampleRate (48000.0);
    display.setView (SpectrumDisplay::View::eqCurve);
    display.setBand (100.0f, 1000.0f);

    // Seeded with the band the display was given, since only the moved edge is now
    // reported and the other one keeps the value it already had.
    float low = 100.0f, high = 1000.0f;
    display.onBandDragged = [&] (SpectrumDisplay::BandEdge edge, float hz)
    {
        (edge == SpectrumDisplay::BandEdge::low ? low : high) = hz;
    };

    int grabbedAt = -1;
    bool grabbed = false;
    display.onBandGesture = [&] (SpectrumDisplay::BandEdge, bool starting)
    {
        if (starting) grabbed = true;
    };

    for (int x = 0; x < display.getWidth() && grabbedAt < 0; ++x)
    {
        const juce::Point<float> at { (float) x, (float) display.getHeight() * 0.5f };
        display.mouseDown (eventAt (display, at, at, false));

        if (grabbed)
            grabbedAt = x;
        else
            display.mouseUp (eventAt (display, at, at, false));
    }

    REQUIRE (grabbedAt > 0);

    // Haul the low edge far past the high one: an inverted band would correct
    // nowhere, which looks like the plugin has stopped working.
    const juce::Point<float> from { (float) grabbedAt, (float) display.getHeight() * 0.5f };
    const juce::Point<float> to { (float) display.getWidth(), from.y };

    display.mouseDrag (eventAt (display, to, from, true));

    CHECK (low <= high);
    CHECK_THAT (low, Catch::Matchers::WithinRel (high, 0.001f));
}

TEST_CASE ("Both ends outside the band are veiled, and by the same role", "[ui]")
{
    // The two rectangles are drawn by separate branches, and for a while they were
    // painted with separate colours: the low end followed graphShade() and the high end
    // was still on chrome(), left behind when the graph's furniture was given roles of
    // its own. Nothing looked wrong -- the two ship at the same value -- until somebody
    // moved "Outside the band" in the theme editor and only the left end followed,
    // which reads as the control not working rather than as half of it working.
    using namespace Celine::Theme;

    const struct Restore { ~Restore() { Celine::Theme::palette().reset(); } } restore;

    SpectrumDisplay display;
    display.setBounds (0, 0, 1000, 400);
    display.setView (SpectrumDisplay::View::eqCurve);
    display.setBand (200.0f, 4000.0f);

    palette().set (Role::graphShade, juce::Colour (0xffff00ff));
    palette().sendSynchronousChangeMessage();

    const auto shot = display.createComponentSnapshot (display.getLocalBounds(), false, 1.0f);

    // The veil goes on at 0.55 alpha over whatever is behind it, so look for the tint
    // rather than for the colour: red and blue well up, green left behind.
    const auto veiled = [] (juce::Colour c)
    {
        return c.getRed() > 90 && c.getBlue() > 90 && c.getGreen() < 60;
    };

    int below = 0, above = 0;
    {
        const juce::Image::BitmapData data (shot, juce::Image::BitmapData::readOnly);

        for (int y = 0; y < shot.getHeight(); ++y)
            for (int x = 0; x < shot.getWidth(); ++x)
                if (veiled (data.getPixelColour (x, y)))
                    (x < shot.getWidth() / 2 ? below : above) += 1;
    }

    INFO ("veiled pixels below the band: " << below << ", above it: " << above);

    CHECK (below > 0);
    CHECK (above > 0);
}
