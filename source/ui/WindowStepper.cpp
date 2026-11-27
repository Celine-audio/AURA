#include "WindowStepper.h"


#include <cmath>

using namespace Celine;

namespace
{
    // Past this the window steps by whole seconds rather than halves.
    constexpr float coarseFrom = 10.0f;

    constexpr int switchWidth = 44;
    constexpr int stepButtonSize = 24;

    // How soon a held button starts repeating, and how often after that.
    constexpr int repeatDelayMs = 400;
    constexpr int repeatIntervalMs = 90;
}

WindowStepper::WindowStepper (juce::AudioProcessorValueTreeState& state, const char* runningID, const char* windowID)
    : windowParameter (*state.getParameter (windowID))
{
    toggle.setClickingTogglesState (true);
    toggle.setGlyph ("OFF");
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
    readout.getProperties().set ("celineKeepFont", true);
    readout.setFont (Ui::mono (9.5f));
    readout.setBorderSize ({});
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
    setSize (preferredWidth, preferredHeight);
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
    const auto text = windowParameter.getText (windowParameter.convertTo0to1 (seconds), 16);
    readout.setText (text.toUpperCase(), juce::dontSendNotification);
    toggle.setGlyph (toggle.getToggleState() ? "ON" : "OFF");
    repaint();

    const auto& range = windowParameter.getNormalisableRange();
    minus.setEnabled (seconds > range.start + 1.0e-3f);
    plus.setEnabled (seconds < range.end - 1.0e-3f);
}

void WindowStepper::refreshEnablement()
{
    // Dimmed rather than disabled while Running is off: the length can still be set
    // ahead, it just is not doing anything yet.
    dimmed = ! toggle.getToggleState();
    applyColours();

    showValue (windowParameter.convertFrom0to1 (windowParameter.getValue()));
}

void WindowStepper::applyColours()
{
    // The readout is a field like the steppers either side of it: the field is painted
    // behind it (paint), and the label carries only the figures, in the ink -- or the faint
    // ink while Running is off and the length is not doing anything yet.
    readout.setColour (juce::Label::backgroundColourId, juce::Colours::transparentBlack);
    readout.setColour (juce::Label::textColourId, dimmed ? Theme::phFaint() : Theme::phInk());
    readout.setColour (juce::Label::backgroundWhenEditingColourId, Theme::phTile());
    readout.setColour (juce::Label::textWhenEditingColourId, Theme::phInk());
    readout.setColour (juce::Label::outlineWhenEditingColourId, Theme::accent());
    readout.setColour (juce::TextEditor::textColourId, Theme::phInk());
    readout.setColour (juce::TextEditor::highlightColourId, Theme::accent().withAlpha (0.4f));
    readout.setColour (juce::CaretComponent::caretColourId, Theme::accent());

    minus.repaint();
    plus.repaint();
    repaint();
}

void WindowStepper::StepButton::paintButton (juce::Graphics& g, bool highlighted, bool down)
{
    const auto bounds = getLocalBounds().toFloat();
    Ui::drawField (g, bounds, highlighted && isEnabled(), down);

    // An end of the range keeps its button in place, faint, rather than leaving a hole.
    const auto* owner = dynamic_cast<const WindowStepper*> (getParentComponent());
    const auto faint = ! isEnabled() || (owner != nullptr && owner->dimmed);
    g.setColour (faint ? Theme::phFaint() : Theme::phDim());

    const auto centre = bounds.getCentre();
    constexpr auto arm = 3.5f;
    constexpr auto thickness = 1.2f;

    g.fillRect (juce::Rectangle<float> (arm * 2.0f, thickness).withCentre (centre));

    if (plus)
        g.fillRect (juce::Rectangle<float> (thickness, arm * 2.0f).withCentre (centre));
}

void WindowStepper::paint (juce::Graphics& g)
{
    // The readout's field, behind the label that carries its figures.
    Ui::drawField (g, readout.getBounds().toFloat(), readout.isMouseOver (true), readout.isBeingEdited());
}

void WindowStepper::resized()
{
    // One row: the switch, then the stepper -- minus, the length, plus -- in the rest, each a
    // cell's gap from the next, as PHONICS spaces its fields.
    auto area = getLocalBounds();

    toggle.setBounds (area.removeFromLeft (switchWidth));
    area.removeFromLeft (Ui::Space::cell);

    minus.setBounds (area.removeFromLeft (stepButtonSize));
    area.removeFromLeft (Ui::Space::cell);
    plus.setBounds (area.removeFromRight (stepButtonSize));
    area.removeFromRight (Ui::Space::cell);
    readout.setBounds (area);
}
