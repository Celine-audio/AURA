#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include "Controls.h"

/**
    Running, and how many seconds back it reads, as one row of PHONICS's fields: a switch
    that reads ON or OFF and lights while the takes are the last few seconds, then a
    stepper -- minus, the length, plus -- that sets how many seconds. The stepper stays
    usable while Running is off, dimmed, so a length can be chosen before it is used.

    The length steps by half a second up to ten seconds and by a whole one beyond,
    which is about as finely as anyone can tell two windows apart at either end. Holding
    a button repeats it; double-clicking the readout types a value in.

    It carries no label of its own: the sheet it stands in names the row RUNNING.
*/
class WindowStepper : public juce::Component
{
public:
    WindowStepper (juce::AudioProcessorValueTreeState& state, const char* runningID, const char* windowID);
    ~WindowStepper() override;

    /** The size it lays out comfortably in: one row of fields. */
    static constexpr int preferredWidth = 200;
    static constexpr int preferredHeight = Ui::Space::row;

    void paint (juce::Graphics&) override;
    void resized() override;

    /** The colours its buttons and readout are told rather than read. */
    void applyColours();
    void lookAndFeelChanged() override { applyColours(); }

    Ui::Toggle& getSwitch() noexcept { return toggle; }
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

    /** A minus or a plus in a field, drawn as strokes rather than set as text: in the
        label face at this size the two glyphs came out a few pixels across. Reads its
        colours as it paints, so a theme change needs nothing from it. */
    class StepButton : public juce::Button
    {
    public:
        StepButton (const juce::String& name, bool isPlus) : juce::Button (name), plus (isPlus) {}

        void paintButton (juce::Graphics&, bool highlighted, bool down) override;

    private:
        const bool plus;
    };

    Ui::Toggle toggle { "Running" };
    bool dimmed = false;
    StepButton minus { "Shorter", false };
    StepButton plus { "Longer", true };
    juce::Label readout;

    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> toggleAttachment;
    std::unique_ptr<juce::ParameterAttachment> windowAttachment;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (WindowStepper)
};
