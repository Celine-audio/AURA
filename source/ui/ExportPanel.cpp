#include "ExportPanel.h"

using namespace Ui;

namespace
{
    constexpr float labelWidth = 80.0f;

    // Segment indices, kept in one place.
    enum { monoIndex = 0, stereoIndex };
    enum { leftIndex = 0, rightIndex, midIndex };
    enum { linearIndex = 0, minimumIndex };
}

ExportPanel::ExportPanel (bool linearPhase)
{
    channels.setOptions ({ "MONO", "STEREO" });
    channels.setOptionTooltips ({ "One correction in one channel",
                                  "The left and right corrections as they stand" });
    channels.setSelected (stereoIndex);
    channels.setTitle ("Channels");

    source.setOptions ({ "L", "R", "L+R" });
    source.setOptionTooltips ({ "The left channel's correction", "The right channel's correction",
                                "The average of the two" });
    source.setSelected (midIndex);
    source.setTitle ("Derive a mono file from");

    // Opens on whatever the plugin is doing, so the obvious action reproduces what you hear;
    // the other is there because a minimum-phase file costs the convolver loading it no
    // latency and leaves no pre-ringing.
    phase.setOptions ({ "LINEAR", "MINIMUM" });
    phase.setOptionTooltips ({ "Every frequency in step, at the cost of latency wherever it is loaded",
                               "No latency, at the cost of some phase shift" });
    phase.setSelected (linearPhase ? linearIndex : minimumIndex);
    phase.setTitle ("Phase");

    // A row of segments reports a click and leaves the choosing to its owner, which here is
    // this panel. PHONICS's once did not, and its choice could be clicked with nothing changing.
    for (auto* row : { &channels, &source, &phase })
    {
        row->setStyle (false, 8.5f);
        row->onSelect = [this, row] (int index)
        {
            row->setSelected (index);

            // There is nothing to derive a stereo file from: it is the two corrections.
            source.setLocked (channels.getSelected() != monoIndex);
        };
        addAndMakeVisible (row);
    }

    source.setLocked (true);

    exportButton.setTooltip ("Choose where to write the file");
    exportButton.onClick = [this]
    {
        const auto options = currentOptions();
        close();

        if (onExport)
            onExport (options);
    };

    cancelButton.onClick = [this] { close(); };

    for (auto* button : { &exportButton, &cancelButton })
    {
        button->setWantsKeyboardFocus (false);
        button->setMouseClickGrabsKeyboardFocus (false);
        addAndMakeVisible (button);
    }

    // Read once, at the moment the panel opens: it lives only as long as the call-out, and the
    // theme cannot be edited while it is up.
    exportButton.setColour (juce::TextButton::buttonColourId, Theme::accent());

    setSize (designWidth, designHeight);
}

IrExport::Options ExportPanel::currentOptions() const
{
    IrExport::Options options;
    options.layout = channels.getSelected() == monoIndex ? IrExport::Options::Layout::mono
                                                         : IrExport::Options::Layout::stereo;

    switch (source.getSelected())
    {
        case leftIndex:  options.channel = IrExport::Options::Channel::left;  break;
        case rightIndex: options.channel = IrExport::Options::Channel::right; break;
        default:         options.channel = IrExport::Options::Channel::mid;   break;
    }

    options.phase = phase.getSelected() == minimumIndex ? IrExport::Options::Phase::minimum
                                                        : IrExport::Options::Phase::linear;
    return options;
}

void ExportPanel::close()
{
    if (auto* box = findParentComponentOfClass<juce::CallOutBox>())
        box->dismiss();
}

void ExportPanel::resized()
{
    auto area = getLocalBounds().reduced (Space::outer);
    area.removeFromTop (Space::sectionHeader + Space::gutter);

    for (auto* row : { &channels, &source, &phase })
        row->setBounds (area.removeFromTop (Space::row).withTrimmedLeft ((int) labelWidth).reduced (0, Space::cell / 2));

    auto buttons = area.removeFromBottom (Space::row + Space::cell);
    cancelButton.setBounds (buttons.removeFromLeft (96).reduced (0, Space::cell / 2));
    exportButton.setBounds (buttons.removeFromRight (120).reduced (0, Space::cell / 2));
}

void ExportPanel::paint (juce::Graphics& g)
{
    // The call-out's own ground, as the menus beside it.
    g.fillAll (Theme::phTile());

    const auto inner = getLocalBounds().toFloat().reduced ((float) Space::outer);
    Sheet::drawHeading (g, inner.withHeight ((float) Space::sectionHeader), "EXPORT IR");

    auto top = inner.getY() + (float) (Space::sectionHeader + Space::gutter);

    for (const auto* label : { "CHANNELS", "FROM", "PHASE" })
    {
        Sheet::drawLabel (g, { inner.getX(), top, labelWidth, (float) Space::row }, label);
        top += (float) Space::row;
    }
}
