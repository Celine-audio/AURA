#pragma once

#include "../dsp/IrExport.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>

/**
    The contents of the "Export IR" popover.

    Stereo writes the plugin's current left and right corrections as they stand, so
    it takes no further options; only a mono export needs to be told which curve to
    render, and that row appears just for mono.
*/
class ExportPanel : public juce::Component
{
public:
    /** linearPhase says which way the plugin is currently running, which is what the
        panel opens on: an export defaults to reproducing what you hear. */
    explicit ExportPanel (bool linearPhase);

    /** Called with the chosen options when the user commits. The owner is
        responsible for picking a file and doing the write. */
    std::function<void (IrExport::Options)> onExport;

    void resized() override;
    void paint (juce::Graphics&) override;

private:
    bool isMono() const;
    void refreshLayout();
    IrExport::Options currentOptions() const;

    juce::Label titleLabel { {}, "Export Impulse Response" };

    juce::Label layoutLabel { {}, "Channels" };
    juce::ComboBox layoutBox;

    juce::Label sourceLabel { {}, "Derive from" };
    juce::ComboBox sourceBox;

    juce::Label phaseLabel { {}, "Phase" };
    juce::ComboBox phaseBox;

    juce::TextButton exportButton { juce::String::fromUTF8 ("Export\xe2\x80\xa6") };
    juce::TextButton cancelButton { "Cancel" };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ExportPanel)
};
