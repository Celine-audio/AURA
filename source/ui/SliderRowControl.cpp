#include "SliderRowControl.h"

#include <CelineUI/Fonts.h>
#include <CelineUI/Theme.h>

using namespace Celine;

//==============================================================================
SliderRowControl::SliderRowControl (juce::AudioProcessorValueTreeState& state,
                                    const juce::String& parameterID,
                                    const juce::String& displayName)
    : ParameterControl (state, parameterID, displayName, juce::Slider::LinearHorizontal, defaultValueWidth)
{
    // The base class stacks a name above the slider; this one puts it alongside, so
    // the inherited label is hidden and a second one placed to the left.
    slider.setTextBoxStyle (juce::Slider::TextBoxRight, false, valueWidth, 18);

    // These two stand on the light panel at the bottom of the window, so every part
    // of them wears the dark ink that goes with it.
    slider.setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    slider.setColour (juce::Slider::textBoxBackgroundColourId, juce::Colours::transparentBlack);

    rowName.setText (displayName.toUpperCase(), juce::dontSendNotification);
    rowName.setFont (Fonts::light (11.0f));
    rowName.setJustificationType (juce::Justification::centredRight);
    rowName.setInterceptsMouseClicks (false, false);
    addAndMakeVisible (rowName);

    applyColours();
}

void SliderRowControl::applyColours()
{
    ParameterControl::applyColours();

    // Which side of the two-tone split this row stands on decides every colour on it,
    // so it is remembered rather than applied once: a theme change has to put the row
    // back the way it was, not the way rows start out.
    // Every colour is set on both sides, including the ones the two agree about. A
    // branch that leaves one alone leaves whatever the other branch last wrote -- which
    // is how the track stayed on the accent it was built with while everything around
    // it followed the theme.
    slider.setColour (juce::Slider::trackColourId, Theme::accent());

    if (onDark)
    {
        slider.setColour (juce::Slider::backgroundColourId, Theme::surface());
        slider.setColour (juce::Slider::thumbColourId, Theme::handle());
        slider.setColour (juce::Slider::textBoxTextColourId, Theme::text());
        rowName.setColour (juce::Label::textColourId, Theme::textDim());
    }
    else
    {
        slider.setColour (juce::Slider::backgroundColourId, Theme::background());
        slider.setColour (juce::Slider::thumbColourId, Theme::handleOnPanel());
        slider.setColour (juce::Slider::textBoxTextColourId, Theme::textOnPanel());
        rowName.setColour (juce::Label::textColourId, Theme::textOnPanel());
    }

    slider.refreshTextBoxColours();
}

void SliderRowControl::setOnDark()
{
    onDark = true;
    applyColours();
}

void SliderRowControl::setCompact()
{
    nameWidth = 22;
    valueWidth = 44;

    slider.setTextBoxStyle (juce::Slider::TextBoxRight, false, valueWidth, 18);
    resized();
}

void SliderRowControl::resized()
{
    auto area = getLocalBounds();

    // The name's column and the readout are sized for the widest row; on a band that
    // has to hold three settings at a narrow window, the track is what would pay for
    // that. So when the track would come out shorter than it can be dragged, the name
    // column gives up the room its own word does not need, then the readout down to
    // what a value actually takes -- and only then does the track get shorter.
    constexpr int nameGap = 10;
    // The slider's own box rather than the line drawn in it: the thumb's travel stops
    // its radius short of either end, so this is about eighty pixels of visible track.
    constexpr int shortestTrack = 110;
    constexpr int narrowestReadout = 44;

    auto name = nameWidth;
    auto value = valueWidth;
    auto shortfall = name + nameGap + value + shortestTrack - area.getWidth();

    if (shortfall > 0)
    {
        const auto ownWidth = juce::GlyphArrangement::getStringWidthInt (rowName.getFont(), rowName.getText()) + 4;
        const auto spareName = juce::jmax (0, name - ownWidth);
        const auto fromName = juce::jmin (shortfall, spareName);

        name -= fromName;
        shortfall -= fromName;
    }

    if (shortfall > 0)
        value = juce::jmax (narrowestReadout, value - shortfall);

    if (slider.getTextBoxWidth() != value)
        slider.setTextBoxStyle (juce::Slider::TextBoxRight, false, value, 18);

    rowName.setBounds (area.removeFromLeft (name));
    area.removeFromLeft (nameGap);

    slider.setBounds (area);
}
