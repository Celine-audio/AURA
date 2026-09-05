#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace Celine
{
    //==========================================================================
    /**
        The palette, in one place, shared across the house plugins and sampled from the
        Figma files rather than eyeballed -- which is why the values are odd numbers.

        Two rules. Nothing outside this header names a hex value; and the accents are
        named for the job they do rather than for the colour they are, so a change of
        palette does not have to be chased through the call sites.

        The design is two-tone, and that is the thing to hold on to when adding
        anything: the chrome is dark aubergine and the canvas darker still, while the
        panels you reach into are near-white. A new widget has to know which side of
        that line it sits on, because the text colour flips with it.
    */
    namespace Theme
    {
        //======================================================================
        // Surfaces.

        /** Titlebar, toolbar, inspector, ruler chrome. */
        inline juce::Colour chrome() { return juce::Colour (0xff3b334b); }

        /** The sheet you draw on. */
        inline juce::Colour background() { return juce::Colour (0xff28262e); }

        /** The light panels. */
        inline juce::Colour panel() { return juce::Colour (0xfff9fbff); }

        /** Buttons, fields, dropdowns -- the dark slate that sits on chrome. */
        inline juce::Colour surface() { return juce::Colour (0xff37364a); }

        /** Hover and selection, a step up from surface. */
        inline juce::Colour surfaceBright() { return juce::Colour (0xff4f485d); }

        /** Borders. */
        inline juce::Colour line() { return juce::Colour (0xffd9d9d9); }

        /** The graph's ground, darker than anything else so it reads as a hole. */
        inline juce::Colour consoleBackground() { return juce::Colour (0xff17151a); }

        /** A row in a list. */
        inline juce::Colour pill() { return juce::Colour (0xffdcdee4); }

        /** Grid lines. Barely there on purpose -- they are a ruler you read against,
            not part of the picture. */
        inline juce::Colour grid() { return juce::Colour (0xff5c5c5c); }

        //======================================================================
        // Text. Two families, because of the two-tone split above.

        /** On chrome. Céline White -- the same value the light panels are, because the
            ink on the dark half of the design and the ground on the light half are one
            colour used two ways. It was Monokai's warm off-white, which put a faintly
            yellow white beside a faintly blue one wherever the two halves met. */
        inline juce::Colour text() { return juce::Colour (0xfff9fbff); }
        inline juce::Colour textDim() { return juce::Colour (0xffd9d9d9); }
        inline juce::Colour comment() { return juce::Colour (0xff888791); }

        /** Ink for a control that cannot be used right now. Several steps below
            textDim(), which is the *idle* look of a control that does work: if the two
            were close, "greyed out" and "not hovered" would look the same. */
        inline juce::Colour textDisabled() { return comment(); }

        /** On the light panels, where the above would be invisible. */
        inline juce::Colour textOnPanel() { return juce::Colour (0xff28262e); }

        //======================================================================
        // Accents.

        inline juce::Colour teal() { return juce::Colour (0xff8F63D5); }
        inline juce::Colour violet() { return juce::Colour (0xff9761dc); }

        /** The primary accent: whatever the plugin is doing to the signal. Every filled
            control uses this, so changing it here re-skins the plugin. */
        inline juce::Colour accent() { return violet(); }

        /** A second accent, for when one curve or channel has to be told apart from
            another. Sits opposite the primary on the wheel. */
        inline juce::Colour accentAlt() { return juce::Colour (0xff4fc9e8); }

        /** Anything live and committing. */
        inline juce::Colour record() { return juce::Colour (0xfff92672); }

        //======================================================================
        // What each colour *means* on the graph. Named by job rather than by colour, so
        // the meaning survives a change of palette.
        //
        // These three are chosen as a set, and the set is the point: blue and red are
        // what mix to violet, so the colour of the thing the plugin builds says where it
        // came from. Hue bears it out -- 199 and 357 degrees, with the correction's
        // violet at 262, very near the midpoint of the two going round through purple.

        /** The signal going through the plugin now. */
        inline juce::Colour current() { return accentAlt(); }

        /** The material being matched to. */
        inline juce::Colour reference() { return juce::Colour (0xfff2545b); }

        /** The correction the plugin is applying: the brand's own violet, which is also
            what the two signal colours above mix to. */
        inline juce::Colour correction() { return accent(); }

        /** The unfilled part of a knob's ring and of the cut slider's track. */
        inline juce::Colour track() { return juce::Colour (0xff565656); }

        /** A control that is armed or in force. */
        inline juce::Colour toolActive() { return teal(); }

        /** Bypass engaged: the plugin is passing audio through untouched, which is
            worth noticing. */
        inline juce::Colour danger()  { return juce::Colour (0xfff92672); }
        inline juce::Colour warning() { return juce::Colour (0xffe6db74); }
        inline juce::Colour error()   { return juce::Colour (0xfff92672); }

        //======================================================================
        // Geometry the mockup is consistent about, stated once rather than sprinkled
        // through four files as literals.

        /** Corner radius on every button, field and pill. */
        inline constexpr float cornerRadius = 8.0f;

        /** Border weight on buttons and fields. */
        inline constexpr float borderWidth = 1.2f;

        /** Toolbar buttons are square, and sit on a pitch of size + gap. */
        inline constexpr int buttonSize = 33;
        inline constexpr int buttonGap = 7;

        /** The toolbar band: 33px buttons with 6px of air above and below. */
        inline constexpr int toolbarHeight = 45;
    } // namespace Theme

} // namespace Celine
