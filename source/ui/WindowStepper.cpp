#include "WindowStepper.h"

#include <CelineUI/Fonts.h>
#include <CelineUI/Theme.h>

#include <cmath>

using namespace Celine;

namespace
{
    // Past this the window steps by whole seconds rather than halves.
    constexpr float coarseFrom = 10.0f;

    constexpr int switchWidth = 74;
    constexpr int stepButtonSize = 22;
    constexpr int readoutWidth = 52;

    // How soon a held button starts repeating, and how often after that.
    constexpr int repeatDelayMs = 400;
    constexpr int repeatIntervalMs = 90;
}

WindowStepper::WindowStepper (juce::AudioProcessorValueTreeState& state, const char* runningID, const char* windowID)
    : windowParameter (*state.getParameter (windowID))
{
    toggle.setClickingTogglesState (true);
    toggle.setWantsKeyboardFocus (false);
    toggle.setTooltip ("Running: match to the last few seconds only. Each take forgets what is older than the window, "
                       "and a match in force follows it on its own.");
    toggle.onStateChange = [this] { refreshEnablement(); };
    addAndMakeVisible (toggle);

    for (auto* button : { &minus, &plus })
    {
        button->setWantsKeyboardFocus (false);
        button->setRepeatSpeed (repeatDelayMs, repeatIntervalMs);
        addAndMakeVisible (button);
    }

    minus.setTooltip ("A shorter window.");
    plus.setTooltip ("A longer window.");
    minus.onClick = [this] { step (-1); };
    plus.onClick = [this] { step (+1); };

    readout.setJustificationType (juce::Justification::centred);
    readout.setFont (Fonts::bold (12.0f));
    readout.setEditable (false, true, false);
    readout.setTooltip ("How many seconds back a running take reaches. Double-click to type a length.");
    readout.onTextChange = [this]
    {
        // Typed: read the number, whatever unit follows it, and let the parameter's range
        // have the last word.
        const auto typed = readout.getText().retainCharacters ("0123456789.,").replaceCharacter (',', '.');

        if (typed.isNotEmpty() && windowAttachment != nullptr)
            windowAttachment->setValueAsCompleteGesture (typed.getFloatValue());
        else
            showValue (windowParameter.convertFrom0to1 (windowParameter.getValue()));
    };
    addAndMakeVisible (readout);

    toggleAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (state, runningID, toggle);

    // Called on the message thread whatever moved the parameter -- the attachment takes
    // care of that -- so automation and a session reload both reach the readout.
    windowAttachment = std::make_unique<juce::ParameterAttachment> (
        windowParameter, [this] (float seconds) { showValue (seconds); }, nullptr);
    windowAttachment->sendInitialUpdate();

    applyColours();
    refreshEnablement();

    // Last, per the house rule: it fires resized().
    setSize (preferredWidth, 24);
}

WindowStepper::~WindowStepper() = default;

float WindowStepper::stepFrom (float seconds, int direction, float minimum, float maximum) noexcept
{
    // Whole seconds above ten, halves up to it. Going down from just past ten lands on
    // ten rather than skipping it, and a value off the grid -- typed, or automated --
    // snaps to the next point in the direction asked rather than moving by a full step.
    const auto coarse = direction > 0 ? seconds >= coarseFrom : seconds > coarseFrom;
    const auto size = coarse ? 1.0f : 0.5f;

    auto next = direction > 0 ? std::floor (seconds / size + 1.0e-4f) * size + size
                              : std::ceil (seconds / size - 1.0e-4f) * size - size;

    // Stepping down across ten from above uses whole seconds, which could pass over it.
    if (direction < 0 && seconds > coarseFrom && next < coarseFrom)
        next = coarseFrom;

    return juce::jlimit (minimum, maximum, next);
}

void WindowStepper::step (int direction)
{
    const auto& range = windowParameter.getNormalisableRange();
    const auto current = windowParameter.convertFrom0to1 (windowParameter.getValue());

    windowAttachment->setValueAsCompleteGesture (stepFrom (current, direction, range.start, range.end));
}

void WindowStepper::showValue (float seconds)
{
    readout.setText (windowParameter.getText (windowParameter.convertTo0to1 (seconds), 16), juce::dontSendNotification);

    const auto& range = windowParameter.getNormalisableRange();
    minus.setEnabled (seconds > range.start + 1.0e-3f);
    plus.setEnabled (seconds < range.end - 1.0e-3f);
}

void WindowStepper::refreshEnablement()
{
    // Dimmed rather than disabled while Running is off: the length can still be set
    // ahead, it just is not doing anything yet.
    const auto alpha = toggle.getToggleState() ? 1.0f : 0.45f;

    for (juce::Component* part : { static_cast<juce::Component*> (&minus), static_cast<juce::Component*> (&plus),
                                   static_cast<juce::Component*> (&readout) })
        part->setAlpha (alpha);
}

void WindowStepper::applyColours()
{
    const auto ink = Theme::textOnPanel();

    // Off, the switch is a word on the panel like PHASE beside it; on, it is filled with
    // the violet of the correction it keeps moving, the way the phase box and Export are.
    toggle.setColour (juce::TextButton::buttonColourId, ink.withAlpha (0.08f));
    toggle.setColour (juce::TextButton::buttonOnColourId, Theme::correction());
    toggle.setColour (juce::TextButton::textColourOffId, ink);
    toggle.setColour (juce::TextButton::textColourOnId, ink);

    readout.setColour (juce::Label::textColourId, ink);
    readout.setColour (juce::Label::backgroundWhenEditingColourId, Theme::panel());
    readout.setColour (juce::Label::textWhenEditingColourId, ink);
    readout.setColour (juce::Label::outlineWhenEditingColourId, ink.withAlpha (0.35f));
    readout.setColour (juce::TextEditor::textColourId, ink);
    readout.setColour (juce::TextEditor::highlightColourId, Theme::correction().withAlpha (0.4f));
}

void WindowStepper::StepButton::paintButton (juce::Graphics& g, bool highlighted, bool down)
{
    const auto ink = Theme::textOnPanel();
    const auto bounds = getLocalBounds().toFloat().reduced (0.5f);

    // The same faint ink wash the switch wears when it is off, deepening under the pointer
    // and again under the press, so the three read as one group.
    g.setColour (ink.withAlpha (down ? 0.22f : highlighted ? 0.14f : 0.08f));
    g.fillRoundedRectangle (bounds, Theme::cornerRadius);

    // A disabled end of the range keeps its button in place, faded, rather than leaving
    // a hole where it was.
    g.setColour (ink.withAlpha (isEnabled() ? 1.0f : 0.3f));

    const auto centre = bounds.getCentre();
    const auto arm = juce::jmax (3.0f, bounds.getWidth() * 0.22f);
    constexpr auto thickness = 1.6f;

    g.fillRoundedRectangle (juce::Rectangle<float> (arm * 2.0f, thickness).withCentre (centre), thickness / 2.0f);

    if (plus)
        g.fillRoundedRectangle (juce::Rectangle<float> (thickness, arm * 2.0f).withCentre (centre), thickness / 2.0f);
}

void WindowStepper::resized()
{
    auto area = getLocalBounds();

    toggle.setBounds (area.removeFromLeft (switchWidth));

    // The stepper centred in what is left, so it sits in the middle of its own space
    // however wide the band makes this.
    const auto stepperWidth = stepButtonSize * 2 + readoutWidth;
    auto stepper = area.withSizeKeepingCentre (juce::jmin (stepperWidth, area.getWidth()), area.getHeight());

    const auto square = [&stepper] (bool fromLeft)
    {
        auto slot = fromLeft ? stepper.removeFromLeft (stepButtonSize) : stepper.removeFromRight (stepButtonSize);
        return slot.withSizeKeepingCentre (stepButtonSize, juce::jmin (stepButtonSize, slot.getHeight()));
    };

    minus.setBounds (square (true));
    plus.setBounds (square (false));
    readout.setBounds (stepper);
}
