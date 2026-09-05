#include "ParameterControl.h"

#include "PluginLookAndFeel.h"
#include "Fonts.h"
#include "Theme.h"

using namespace Celine;

namespace
{
    constexpr int nameGap = 2;
}

//==============================================================================
ParameterControl::Slider::Slider()
{
    setScrollWheelEnabled (false);

    // The fourth argument is what matters: it is `userCanPressKeyToSwapMode`, and with
    // it true -- which is the default -- holding any of ctrl, alt or command swaps the
    // slider into velocity mode. That is the jumping about. Off, the modifier is left
    // free to mean what mouseDown makes it mean.
    setVelocityModeParameters (1.0, 1, 0.0, false);
    setMouseDragSensitivity (normalSensitivity);
}

void ParameterControl::Slider::mouseDown (const juce::MouseEvent& event)
{
    setMouseDragSensitivity (event.mods.isAltDown() ? fineSensitivity : normalSensitivity);

    juce::Slider::mouseDown (event);
}

//==============================================================================
void ParameterControl::setNameFont (juce::Font font, const juce::String& text)
{
    // Without this the look and feel replaces the face at paint time — see
    // keepFontProperty for why it otherwise does.
    nameLabel.getProperties().set (keepFontProperty, true);
    nameLabel.setFont (font);
    nameLabel.setText (text, juce::dontSendNotification);
    resized();
}

ParameterControl::ParameterControl (juce::AudioProcessorValueTreeState& state,
                                    const juce::String& parameterID,
                                    const juce::String& displayName,
                                    juce::Slider::SliderStyle style,
                                    int textBoxWidth)
{
    nameLabel.setText (displayName.toUpperCase(), juce::dontSendNotification);
    nameLabel.setFont (Fonts::bold (10.0f));
    nameLabel.setColour (juce::Label::textColourId, Theme::textDim());
    nameLabel.setJustificationType (juce::Justification::centred);
    nameLabel.setInterceptsMouseClicks (false, false);
    addAndMakeVisible (nameLabel);

    // The slider bakes its text box colours in when the box is created, which happens
    // before this component is ever parented to something carrying PluginLookAndFeel —
    // so they have to be set on the slider itself, and set before setTextBoxStyle.
    slider.setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    slider.setColour (juce::Slider::textBoxBackgroundColourId, juce::Colours::transparentBlack);
    slider.setColour (juce::Slider::textBoxTextColourId, Theme::text());
    slider.setColour (juce::Slider::textBoxHighlightColourId, Theme::accent().withAlpha (0.3f));
    slider.setColour (juce::Slider::rotarySliderFillColourId, Theme::accent());
    slider.setColour (juce::Slider::rotarySliderOutlineColourId, Theme::line());

    slider.setSliderStyle (style);
    slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, textBoxWidth, 16);
    addAndMakeVisible (slider);

    attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
        state, parameterID, slider);
}

void ParameterControl::resized()
{
    auto area = getLocalBounds();

    // Off the label's own font, so a subclass can set a bigger face for the name
    // without the row it sits in having to be told about it.
    const auto nameHeight = juce::roundToInt (nameLabel.getFont().getHeight()) + 3;

    nameLabel.setBounds (area.removeFromTop (nameHeight));
    area.removeFromTop (nameGap);
    layOutSlider (area);
}

//==============================================================================
KnobControl::KnobControl (juce::AudioProcessorValueTreeState& state,
                          const juce::String& parameterID,
                          const juce::String& displayName)
    : ParameterControl (state, parameterID, displayName,
                        juce::Slider::RotaryHorizontalVerticalDrag, 68)
{
    slider.setRotaryParameters (juce::MathConstants<float>::pi * 1.25f,
                                juce::MathConstants<float>::pi * 2.75f,
                                true);
}

void KnobControl::layOutSlider (juce::Rectangle<int> area)
{
    // The slider owns its readout, so it gets the whole remaining box; keeping it
    // square-ish stops the knob stretching on wide layouts.
    const auto width = juce::jmin (area.getWidth(), area.getHeight() + 20);
    slider.setBounds (area.withSizeKeepingCentre (width, area.getHeight()));
}

//==============================================================================
FaderControl::FaderControl (juce::AudioProcessorValueTreeState& state,
                            const juce::String& parameterID,
                            const juce::String& displayName)
    : ParameterControl (state, parameterID, displayName, juce::Slider::LinearVertical, 56)
{
    // The wordmark's face, and lowercase like the wordmark: these two name what goes
    // in and what comes out, which is the plugin in one line, so they carry the
    // identity rather than the label style everything else uses.
    setNameFont (Fonts::logo (12.0f), displayName.toLowerCase());
}

//==============================================================================
SliderRowControl::SliderRowControl (juce::AudioProcessorValueTreeState& state,
                                    const juce::String& parameterID,
                                    const juce::String& displayName)
    : ParameterControl (state, parameterID, displayName, juce::Slider::LinearHorizontal, valueWidth)
{
    // The base class stacks a name above the slider; this one puts it alongside, so
    // the inherited label is hidden and a second one placed to the left.
    slider.setTextBoxStyle (juce::Slider::TextBoxRight, false, valueWidth, 18);

    // These two stand on the light panel at the bottom of the window, so every part
    // of them wears the dark ink that goes with it.
    slider.setColour (juce::Slider::backgroundColourId, Theme::background());
    slider.setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    slider.setColour (juce::Slider::textBoxBackgroundColourId, juce::Colours::transparentBlack);
    slider.setColour (juce::Slider::trackColourId, Theme::correction());
    slider.setColour (juce::Slider::thumbColourId, Theme::textOnPanel());
    slider.setColour (juce::Slider::textBoxTextColourId, Theme::textOnPanel());

    rowName.setText (displayName.toUpperCase(), juce::dontSendNotification);
    rowName.setFont (Fonts::light (11.0f));
    rowName.setColour (juce::Label::textColourId, Theme::textOnPanel());
    rowName.setJustificationType (juce::Justification::centredRight);
    rowName.setInterceptsMouseClicks (false, false);
    addAndMakeVisible (rowName);
}

void SliderRowControl::resized()
{
    auto area = getLocalBounds();

    rowName.setBounds (area.removeFromLeft (nameWidth));
    area.removeFromLeft (10);

    slider.setBounds (area);
}
