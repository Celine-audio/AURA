#pragma once

#include <CelineUI/Fonts.h>
#include <CelineUI/Theme.h>

#include <juce_gui_basics/juce_gui_basics.h>

/**
    The page's typography and the handful of things every part of it draws the same way.

    Two faces: Inter for every label and button, in Medium (ExtraBold for the two section titles),
    and the kit's
    JetBrains Mono for every number -- numbers line up in columns, and a readout that
    shifts sideways as its value changes reads as a control that is jittering. Inter is
    embedded (assets/inter) for the reason the kit embeds Jura: a plugin cannot assume a
    font is installed, and a fallback is a different width on every label.

    Sizes are pixels, taken off the design at the size the page is laid out at; the window
    scales the whole page rather than re-laying it out, so they are never recomputed.
*/
namespace Ui
{
    using namespace Celine;

    /** The page's spacing, in design pixels: one set of numbers for the whole page, so a margin on
        the left half is the margin on the right half, and an inspector's padding is the page's
        padding. Nothing on the page is placed with a spacing literal of its own. */
    namespace Space
    {
        /** From the page's edge to its first block. */
        inline constexpr int outer = 12;

        /** Between two blocks. */
        inline constexpr int gutter = 8;

        /** From a block's edge to what is inside it. */
        inline constexpr int inner = 8;

        /** Between two cells of a grid: a field's border sits half of this inside its cell, so two
            neighbouring fields are a cell's width apart. */
        inline constexpr int cell = 4;

        /** One row of controls or readouts. */
        inline constexpr int row = 22;

        /** A section's heading band, which carries its own rule and needs no gap under it. */
        inline constexpr int sectionHeader = 18;
    }

    /** How much larger every type on the page is drawn than the size its call names. The sizes in
        `source/ui` were set for the design's first, smaller type; this raises them all at once, so
        the page's proportions between titles, labels and numbers are kept. Rows and fields are
        sized to the type they carry (`Space`), so raising this is followed by checking they hold it. */
    inline constexpr float typeScale = 1.4f;

    /** Inter, 700. */
    juce::Font bold (float height);

    /** Inter, 500: every label, heading, button and menu item on the page. At the page's type size
        the Bold read heavier than the design meant, and Medium keeps the size without the weight. */
    juce::Font medium (float height);

    /** Inter, 800. */
    juce::Font heavy (float height);

    /** JetBrains Mono. */
    inline juce::Font mono (float height) { return Fonts::mono (height * typeScale); }

    /** A font with letter-spacing in pixels, the way the design states it. */
    juce::Font spaced (juce::Font font, float letterSpacingPx);

    /** A control's title, "MICROPHONE", in the label grey. */
    void drawTitle (juce::Graphics&, juce::Rectangle<float> area, const juce::String& title, float height = 9.0f);

    /** A label on the left and a value on the right, in mono: the readout rows. */
    void drawReadout (juce::Graphics&, juce::Rectangle<float> row, const juce::String& label,
                      const juce::String& value, juce::Colour valueColour);

    /** How tall a font's capitals are, in pixels: the height of an H as it is drawn. */
    float capHeight (const juce::Font&);

    /** Text drawn on one line with its capitals centred on the area's middle, rather than its whole
        line box. Two sizes of type set side by side in one row this way share a centre line, which
        is what makes a title and the controls beside it look level. Justified left, right or
        centred across the area; never wrapped. */
    void textOnCaps (juce::Graphics&, const juce::String&, juce::Rectangle<float>, juce::Font,
                     juce::Colour, juce::Justification = juce::Justification::centredLeft);

    /** Text drawn into a rectangle, single line, in the given font and colour. */
    void text (juce::Graphics&, const juce::String&, juce::Rectangle<float>, juce::Font,
               juce::Colour, juce::Justification = juce::Justification::centredLeft);

    /** The design's mono values are upper-case: "7.0 CM", "0.34 S". */
    inline juce::String upper (const juce::String& s) { return s.toUpperCase(); }
}
