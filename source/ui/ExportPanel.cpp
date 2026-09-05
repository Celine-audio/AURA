#include "ExportPanel.h"

#include "Fonts.h"
#include "Theme.h"

using namespace Celine;

namespace
{
    // ComboBox item IDs start at 1; keep the mapping in one place.
    enum LayoutId { monoLayout = 1, stereoLayout };
    enum SourceId { leftSource = 1, rightSource, midSource };
    enum PhaseId { linearPhaseId = 1, minimumPhaseId };

    constexpr int panelWidth = 290;
    constexpr int rowHeight = 30;      // control plus the gap under it
    constexpr int baseHeight = 136;    // title, the layout and phase rows, the buttons
}

ExportPanel::ExportPanel (bool linearPhase)
{
    titleLabel.setFont (Fonts::bold (13.0f));
    titleLabel.setColour (juce::Label::textColourId, Theme::text());
    addAndMakeVisible (titleLabel);

    for (auto* label : { &layoutLabel, &sourceLabel, &phaseLabel })
    {
        label->setFont (Fonts::light (11.5f));
        label->setColour (juce::Label::textColourId, Theme::textDim());
        addAndMakeVisible (label);
    }

    layoutBox.setTooltip ("Whether the file carries one correction or a different one "
                          "per channel.");
    layoutBox.addItem ("Mono", monoLayout);
    layoutBox.addItem ("Stereo", stereoLayout);
    layoutBox.setSelectedId (stereoLayout, juce::dontSendNotification);
    layoutBox.onChange = [this] { refreshLayout(); };
    addAndMakeVisible (layoutBox);

    sourceBox.setTooltip ("Which channel's correction a mono file is written from.");
    sourceBox.addItem ("Left", leftSource);
    sourceBox.addItem ("Right", rightSource);
    sourceBox.addItem ("L+R", midSource);
    sourceBox.setSelectedId (midSource, juce::dontSendNotification);
    addAndMakeVisible (sourceBox);

    // Opens on whatever the plugin is doing, so the obvious action reproduces what
    // you hear; the other is there because a minimum-phase file costs the convolver
    // loading it no latency and leaves no pre-ringing.
    phaseBox.setTooltip ("How the exported response is built. Linear phase keeps every "
                         "frequency in step and costs latency wherever it is loaded; "
                         "minimum phase costs none.");
    phaseBox.addItem ("Linear", linearPhaseId);
    phaseBox.addItem ("Minimum", minimumPhaseId);
    phaseBox.setSelectedId (linearPhase ? linearPhaseId : minimumPhaseId, juce::dontSendNotification);
    addAndMakeVisible (phaseBox);

    exportButton.setTooltip ("Choose where to write the file.");
    exportButton.setColour (juce::TextButton::buttonColourId, Theme::correction());
    exportButton.setColour (juce::TextButton::textColourOffId, Theme::chrome());
    exportButton.onClick = [this]
    {
        if (onExport != nullptr)
            onExport (currentOptions());

        if (auto* box = findParentComponentOfClass<juce::CallOutBox>())
            box->dismiss();
    };
    addAndMakeVisible (exportButton);

    cancelButton.onClick = [this]
    {
        if (auto* box = findParentComponentOfClass<juce::CallOutBox>())
            box->dismiss();
    };
    addAndMakeVisible (cancelButton);

    refreshLayout();
}

bool ExportPanel::isMono() const
{
    return layoutBox.getSelectedId() == monoLayout;
}

IrExport::Options ExportPanel::currentOptions() const
{
    IrExport::Options options;
    options.layout = isMono() ? IrExport::Options::Layout::mono : IrExport::Options::Layout::stereo;

    switch (sourceBox.getSelectedId())
    {
        case leftSource:  options.channel = IrExport::Options::Channel::left;  break;
        case rightSource: options.channel = IrExport::Options::Channel::right; break;
        default:          options.channel = IrExport::Options::Channel::mid;   break;
    }

    options.phase = phaseBox.getSelectedId() == minimumPhaseId
                        ? IrExport::Options::Phase::minimum
                        : IrExport::Options::Phase::linear;

    return options;
}

void ExportPanel::refreshLayout()
{
    const auto mono = isMono();

    sourceLabel.setVisible (mono);
    sourceBox.setVisible (mono);

    // The CallOutBox repositions itself around its content, so growing and
    // shrinking here keeps the popover snug either way.
    setSize (panelWidth, baseHeight + (mono ? rowHeight : 0));
}

void ExportPanel::paint (juce::Graphics& g)
{
    // Matching the bubble it sits in — see drawCallOutBoxBackground. Filling with
    // anything else would draw a square of it over the rounded corners.
    g.fillAll (Theme::consoleBackground());
}

void ExportPanel::resized()
{
    auto area = getLocalBounds().reduced (14, 12);

    titleLabel.setBounds (area.removeFromTop (18));
    area.removeFromTop (8);

    auto row = [&] (juce::Label& label, juce::ComboBox& box)
    {
        auto r = area.removeFromTop (rowHeight - 6);
        label.setBounds (r.removeFromLeft (86));
        box.setBounds (r);
        area.removeFromTop (6);
    };

    row (layoutLabel, layoutBox);

    if (sourceBox.isVisible())
        row (sourceLabel, sourceBox);

    row (phaseLabel, phaseBox);

    auto buttons = area.removeFromBottom (26);
    cancelButton.setBounds (buttons.removeFromLeft (84));
    exportButton.setBounds (buttons.removeFromRight (100));
}
