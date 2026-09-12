#pragma once

#include "Theme.h"

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>

/**
    A named, editable control bound to an APVTS parameter: the name above, the slider
    below, and the slider's own text box as the readout.

    Subclasses pick the slider style and the layout; everything else — the palette,
    the label, the attachment — is the same for all of them, which is the only reason
    this class exists.
*/
class ParameterControl : public juce::Component
{
public:
    ParameterControl (juce::AudioProcessorValueTreeState& state,
                      const juce::String& parameterID,
                      const juce::String& displayName,
                      juce::Slider::SliderStyle style,
                      int textBoxWidth);

    /**
        A slider that drags the way this house expects one to.

        Two departures from JUCE's default, both of them things a plugin has to decide
        rather than inherit:

        **The wheel does nothing.** A window with forty-odd controls in it is a window
        somebody scrolls past, and a wheel that changes whatever it happens to be over
        turns that into an edit nobody made and cannot see to undo.

        **Holding the fine modifier drags finely rather than by velocity.** JUCE's
        default is to swap into velocity mode when a modifier is held, which hides the
        pointer and moves the value by how *fast* the mouse is going -- so a slow hand
        does nothing and a twitch jumps a long way. It reads as the control being
        broken. This keeps the drag absolute and simply makes it take six times the
        distance, which is what "fine" is supposed to mean.
    */
    class Slider : public juce::Slider
    {
    public:
        Slider();

        void mouseDown (const juce::MouseEvent&) override;

        /** Puts this slider's text-box colours onto the box itself.

            The box copies them out of the slider when it is *made*, and JUCE remakes it
            on every colour change and every look-and-feel change -- so whoever set the
            colours has no way to know their push was not undone a moment later by a
            rebuild it did not ask for. Doing it here, after the base class has finished
            rebuilding, is the only point at which the last word is ours. */
        void refreshTextBoxColours();

        void lookAndFeelChanged() override;
        void colourChanged() override;

    private:
        /** Pixels for the whole range: JUCE's own default, and six times it. Chosen at
            the top of a drag rather than during one -- changing sensitivity underneath
            a gesture already in progress is itself a jump. */
        static constexpr int normalSensitivity = 250;
        static constexpr int fineSensitivity = 1500;
    };

    juce::Slider& getSlider() noexcept { return slider; }

    /** Every colour this takes once rather than reading as it draws. See the note in
        Theme.h: a colour handed to setColour is a snapshot, and a snapshot does not
        follow a theme change unless something hands it back. */
    /** Which colour fills the travelled part of the arc or track. The plugin's accent
        unless something says otherwise -- GALLERY's strips say otherwise, because a
        knob belonging to one cabinet wears that cabinet's colour.

        Given as a *role* rather than a colour, and kept, for the reason every override
        in this house is: applyColours() runs again on every theme change and on every
        lookAndFeelChanged(), so a colour written in from outside is overwritten the
        next time either happens. That is not hypothetical -- it is exactly what put
        the four strips' knobs back on the accent. */
    void setFillRole (Celine::Theme::Role role);

    virtual void applyColours();

    /** Pushes the slider's text-box colours onto the box itself, which copied them when
        it was made and has held them since. */
    void refreshTextBoxColours();
    void lookAndFeelChanged() override { applyColours(); }

    void resized() override;

protected:
    /** Which role fills the travelled part of the arc or track -- see setFillRole. */
    Celine::Theme::Role fillRole = Celine::Theme::Role::accent;

    /** The area left for the slider once the name has taken its row. */
    virtual void layOutSlider (juce::Rectangle<int> area) { slider.setBounds (area); }

    /** Restyles the name above the slider — a subclass that wants a different face
        for it says so here rather than reaching into the label. */
    void setNameFont (juce::Font font, const juce::String& text);

    Slider slider;

private:
    juce::Label nameLabel;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ParameterControl)
};

/** A rotary knob. Used for everything that shapes the curve. */
class KnobControl : public ParameterControl
{
public:
    KnobControl (juce::AudioProcessorValueTreeState& state,
                 const juce::String& parameterID,
                 const juce::String& displayName);

protected:
    void layOutSlider (juce::Rectangle<int> area) override;
};

/** A vertical fader, for the two that ride down either side of the graph. */
class FaderControl : public ParameterControl
{
public:
    FaderControl (juce::AudioProcessorValueTreeState& state,
                  const juce::String& parameterID,
                  const juce::String& displayName);
};
