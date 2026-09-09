#pragma once

#include "ParameterControl.h"

/*
    AURA's own, and deliberately not part of the shared kit.

    It is the one control in the house that has to work on both halves of the two-tone
    design, because AURA is the one window with a light panel in it -- the band along
    the bottom that Smoothing and L/R Link stand on. Everything about it that is not
    layout is that fact: the palette it swaps, the roles it reaches for, and the flag
    it remembers so a theme change puts it back on the side it was standing on.

    A plugin with no light half has no use for any of that, and carrying it meant
    carrying the roles it paints with -- which is how "Text on a panel" came to sit in
    GALLERY's theme editor moving nothing at all.
*/
/**
    A horizontal fader with its name to the left and its readout to the right, for
    the two controls that live along the bottom of the window.

    Laid out on one line rather than stacked, because a row of them reads as a list
    of settings — which is what smoothing and channel linking are — where a column
    of knobs reads as a console.
*/
class SliderRowControl : public ParameterControl
{
public:
    SliderRowControl (juce::AudioProcessorValueTreeState& state,
                      const juce::String& parameterID,
                      const juce::String& displayName);

    /** Swaps the palette for a dark surround. The row is built for the light panel
        along the bottom of the window, where its thumb and readout are near-black;
        standing on the console background instead, those would be invisible. */
    void setOnDark();

    /** Trims the name and the readout for a row sharing a strip with other things.
        The full-width version reserves room for a nine-letter name, which is most of
        a header on its own. */
    void setCompact();

    void applyColours() override;
    void resized() override;

private:
    juce::Label rowName;

    /** Which side of the two-tone split this row stands on. Remembered, because a theme
        change has to put it back the way it was rather than the way rows start out. */
    bool onDark = false;

    /** Room for the name on the left, and for the value on the right that the slider
        draws itself. Wide enough for a nine-letter name at the label size, so two
        rows side by side agree about where their tracks start.

        The constants exist separately because the base class is constructed before
        any member of this one, so the value passed up to it cannot be read from
        `valueWidth` -- doing that reads it before it has been initialised. */
    static constexpr int defaultNameWidth = 78;
    static constexpr int defaultValueWidth = 62;

    int nameWidth = defaultNameWidth;
    int valueWidth = defaultValueWidth;
};
