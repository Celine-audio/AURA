#pragma once

#include "Theme.h"

#include <juce_gui_basics/juce_gui_basics.h>

/** Marks a ToggleButton as one of ours, so it draws as the design's pill rather
    than JUCE's tick box. A property rather than a subclass because the look and
    feel is the only thing that cares. */
inline constexpr const char* pillSwitchProperty = "celinePillSwitch";

/** Marks a Label as keeping the font it was given.

    getLabelFont is otherwise a net under every label in the window, forcing the
    design's face on all of them — which is what stops JUCE's own labels, the ones
    inside a combo box or a slider, from coming out in the platform sans. The cost
    is that it also overrode a face chosen on purpose: the wordmark's font, set on
    the fader names, was being replaced at paint time and the setter looked as
    though it had done nothing. */
inline constexpr const char* keepFontProperty = "auraKeepFont";

/**
    Applies Celine's palette and typeface to everything JUCE draws for us.

    Two jobs, the same two Celine's own look and feel has. The first is colour:
    sliders, combo boxes, popup menus and the file dialogs are drawn by the
    LookAndFeel and not by us, so without this half the window is the design and
    the other half is JUCE's default grey.

    The second is the two controls the design draws its own way — the knob, which
    is `knob.svg` rotated by the value rather than an arc and a pointer, and the
    switch, a pill with a travelling dot.

    Lives in a .cpp, unlike the palette it reads, because it needs the artwork out
    of BinaryData, and Theme.h is included nearly everywhere.
*/
class AuraLookAndFeel : public juce::LookAndFeel_V4
{
public:
    AuraLookAndFeel();
    ~AuraLookAndFeel() override;

    void drawRotarySlider (juce::Graphics&, int x, int y, int width, int height,
                           float sliderPosProportional, float rotaryStartAngle,
                           float rotaryEndAngle, juce::Slider&) override;

    void drawLinearSlider (juce::Graphics&, int x, int y, int width, int height,
                           float sliderPos, float minSliderPos, float maxSliderPos,
                           juce::Slider::SliderStyle, juce::Slider&) override;

    void drawToggleButton (juce::Graphics&, juce::ToggleButton&,
                           bool shouldDrawButtonAsHighlighted,
                           bool shouldDrawButtonAsDown) override;

    void drawButtonBackground (juce::Graphics&, juce::Button&, const juce::Colour& backgroundColour,
                               bool shouldDrawButtonAsHighlighted, bool shouldDrawButtonAsDown) override;

    /** Centred optically rather than geometrically — see opticalRise in the .cpp
        for why the two are not the same in Jura. */
    void drawButtonText (juce::Graphics&, juce::TextButton&,
                         bool shouldDrawButtonAsHighlighted,
                         bool shouldDrawButtonAsDown) override;

    void drawComboBox (juce::Graphics&, int width, int height, bool isButtonDown,
                       int buttonX, int buttonY, int buttonW, int buttonH,
                       juce::ComboBox&) override;

    void positionComboBoxText (juce::ComboBox&, juce::Label&) override;

    /** The bubble the Export options open in. Without this it is JUCE's default,
        which is a light grey panel with a light border — the one bright rectangle
        in a dark window, and it read as a system dialog rather than as part of
        the plugin. */
    void drawCallOutBoxBackground (juce::CallOutBox&, juce::Graphics&,
                                   const juce::Path&, juce::Image&) override;

    //==========================================================================
    /** Jura, for everything JUCE picks a font for itself.

        These matter more than they look like they should. A font built from
        FontOptions(height) carries no typeface, so it is resolved at render time
        against the *default* LookAndFeel — not the one the component is using. */
    juce::Font getTextButtonFont (juce::TextButton&, int buttonHeight) override;
    juce::Font getComboBoxFont (juce::ComboBox&) override;
    juce::Font getPopupMenuFont() override;
    juce::Font getLabelFont (juce::Label&) override;
    juce::Font getAlertWindowTitleFont() override;
    juce::Font getAlertWindowMessageFont() override;

    /** A menu's section headers. Overridden because JUCE draws them as
        getPopupMenuFont().boldened(), and boldening is the one thing this font
        cannot be asked for: Jura-Light has no bold face to find, so the request
        falls off Jura altogether. */
    void drawPopupMenuSectionHeader (juce::Graphics&, const juce::Rectangle<int>& area,
                                     const juce::String& sectionName) override;

    juce::Label* createSliderTextBox (juce::Slider&) override;

private:
    /** The knob face, parsed once. Null if the asset is missing, in which case the
        rotary falls back to JUCE's own drawing rather than vanishing. */
    std::unique_ptr<juce::Drawable> knob;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AuraLookAndFeel)
};
