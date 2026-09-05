#pragma once

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
    virtual void applyColours();

    /** Pushes the slider's text-box colours onto the box itself, which copied them when
        it was made and has held them since. */
    void refreshTextBoxColours();
    void lookAndFeelChanged() override { applyColours(); }

    void resized() override;

protected:
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
