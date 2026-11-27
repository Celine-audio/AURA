#pragma once

#include "../dsp/IrExport.h"
#include "Controls.h"

#include <functional>

/**
    The "EXPORT IR" panel, in the call-out the toolbar's button opens, drawn as PHONICS
    draws its own: a heading over a rule, a labelled row of segments per choice, then
    CANCEL and EXPORT… to pick where the file goes.

    - **CHANNELS**: MONO or STEREO. Stereo writes the left and right corrections as they
      stand.
    - **FROM**: which correction a mono file is written from -- L, R or L+R. Locked while
      the export is stereo, where there is nothing to choose.
    - **PHASE**: LINEAR or MINIMUM, opening on whichever the plugin is running.

    Holds no state but its choices; the owner picks the file and writes it.
*/
class ExportPanel : public juce::Component
{
public:
    /** linearPhase says which way the plugin is currently running, which is what the
        panel opens on: an export defaults to reproducing what you hear. */
    explicit ExportPanel (bool linearPhase);

    /** Called with the chosen options on EXPORT…; the call-out then closes. */
    std::function<void (IrExport::Options)> onExport;

    void paint (juce::Graphics&) override;
    void resized() override;

    static constexpr int designWidth = 360;
    static constexpr int designHeight = 12 + Ui::Space::sectionHeader + Ui::Space::gutter
                                      + 3 * Ui::Space::row + Ui::Space::gutter
                                      + Ui::Space::row + Ui::Space::cell + 12;

private:
    IrExport::Options currentOptions() const;
    void close();

    Ui::Segments channels, source, phase;
    juce::TextButton exportButton { juce::String::fromUTF8 ("EXPORT\xe2\x80\xa6") }, cancelButton { "CANCEL" };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ExportPanel)
};
