#include "helpers/test_helpers.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <ui/SpectrumDisplay.h>

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
