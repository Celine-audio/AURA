#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

/**
    Running, and how many seconds back it reads, as one control on the bottom band.

    The word WINDOW is the switch: click it and the takes become the last few seconds,
    lit in the correction's violet while they are. Beside it, centred in what is left,
    a stepper -- minus, the length, plus -- that sets how many seconds. The stepper stays
    usable while Running is off, dimmed, so a length can be chosen before it is used.

    The length steps by half a second up to ten seconds and by a whole one beyond,
    which is about as finely as anyone can tell two windows apart at either end. Holding
    a button repeats it; double-clicking the readout types a value in.

    It stands on the light panel, so it wears the panel's dark ink -- see
    PluginEditor::applyPanelColours.
*/
class WindowStepper : public juce::Component
{
public:
    WindowStepper (juce::AudioProcessorValueTreeState& state, const char* runningID, const char* windowID);
    ~WindowStepper() override;

    /** The width it lays out comfortably in: the switch, and the stepper beside it. */
    static constexpr int preferredWidth = 176;

    void resized() override;

    /** The colours its buttons and readout are told rather than read. */
    void applyColours();
    void lookAndFeelChanged() override { applyColours(); }

    juce::TextButton& getSwitch() noexcept { return toggle; }
    juce::Button& getMinusButton() noexcept { return minus; }
    juce::Button& getPlusButton() noexcept { return plus; }
    juce::Label& getReadout() noexcept { return readout; }

    /** One step from `seconds` in `direction` (+1 or -1), on the grid the stepper uses,
        and clamped to the parameter's range. Public so the grid can be tested. */
    static float stepFrom (float seconds, int direction, float minimum, float maximum) noexcept;

private:
    void step (int direction);
    void showValue (float seconds);
    void refreshEnablement();

    juce::RangedAudioParameter& windowParameter;

    /** A minus or a plus, drawn as strokes rather than set as text: in the button font
        at this size the two glyphs came out a few pixels across and hard to hit by eye.
        Reads its colours as it paints, so a theme change needs nothing from it. */
    class StepButton : public juce::Button
    {
    public:
        StepButton (const juce::String& name, bool isPlus) : juce::Button (name), plus (isPlus) {}

        void paintButton (juce::Graphics&, bool highlighted, bool down) override;

    private:
        const bool plus;
    };

    juce::TextButton toggle { "WINDOW" };
    StepButton minus { "Shorter", false };
    StepButton plus { "Longer", true };
    juce::Label readout;

    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> toggleAttachment;
    std::unique_ptr<juce::ParameterAttachment> windowAttachment;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (WindowStepper)
};
