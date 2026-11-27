#pragma once

#include "PluginProcessor.h"
#include <CelineUI/AboutPanel.h>
#include <CelineUI/ThemePanel.h>
#include "ui/ExportPanel.h"
#include "ui/PluginLookAndFeel.h"
#include <CelineUI/Theme.h>
#include "ui/Controls.h"
#include "ui/PhaseTabs.h"
#include "ui/SpectrumDisplay.h"
#include "ui/WindowStepper.h"

//==============================================================================
class PluginEditor : public juce::AudioProcessorEditor,
                     private juce::Timer,
                     private juce::ChangeListener
{
public:
    explicit PluginEditor (PluginProcessor&);
    ~PluginEditor() override;

    //==============================================================================
    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;
    void refreshState();
    void refreshDisplay();

    // A Learn always starts a fresh take, and brings its own tab up so you can watch
    // the capture build.
    void toggleLearn (PhaseTabBar::Stage stage);
    void performMatch();

    // The licence notice, which inside a host is the only route to it.
    void showSettingsMenu();

    // Update checks, Céline's mechanism -- see UpdateCheck.h.

    /** Asks once per machine whether to check for updates. Closing the question means no. */
    void offerUpdateChecksOnFirstRun();

    /** Starts this launch's one check, if the user wants checks and none has started. */
    void startUpdateCheckIfEnabled();

    /** "A new update is available": Download, Remind me later, or turn checks off. */
    void announceUpdate (const juce::String& version);

    /** Puts a themed question up in a window of its own, asynchronously -- never a
        modal loop inside a host -- and remembers it as openPrompt. */
    juce::DialogWindow* launchPrompt (const juce::String& title, std::unique_ptr<juce::Component> content);

    /** Whether this window has asked its first-run question yet. Asked from the timer,
        once the window is actually on screen in front of somebody. */
    bool updateQuestionChecked = false;

    /** The question this window has up, if any; one at a time. Null once answered. */
    juce::Component::SafePointer<juce::DialogWindow> openPrompt;

    void showExportPanel();
    void chooseFileAndExport (IrExport::Options options);

    // Learns a stage from an audio file: asks for one, then hands it to the processor,
    // which reads it off the message thread.
    void chooseFileToImport (PhaseTabBar::Stage stage);

    PluginProcessor& processorRef;
    PluginLookAndFeel lookAndFeel;

    // Every control in the window has a tooltip and none of them can show one without
    // this: JUCE needs a window to draw them in, and there is no default.
    juce::TooltipWindow tooltips { this, 600 };

    SpectrumDisplay display;
    PhaseTabBar tabBar;

    // The toolbar, PHONICS's: EXPORT IR in words, the output trim, and two square icon
    // buttons -- bypass, which fills with the danger red while bypassed, and settings.
    juce::TextButton exportButton { "EXPORT IR" };
    Ui::OutputFader outputFader { processorRef.getAPVTS(), ParamID::outputGain };
    Ui::PaintedButton bypassButton { "Bypass" }, settingsButton { "Settings" };
    std::unique_ptr<juce::ButtonParameterAttachment> bypassAttachment;
    std::unique_ptr<juce::Drawable> logo, powerIcon, gearIcon;

    // Fade state for one of the two moving traces: the last frame count seen from its
    // analyzer, how many ticks have passed without that changing, and how far the trace
    // has decayed once the wait ran out.
    struct TraceFade
    {
        std::int64_t lastFrames = -1;
        int ticksWithoutFrame = 0;
        float level = 1.0f;
    };

    TraceFade currentFade, referenceFade;

    // Laid out in resized() and painted in paint(), because they sit behind the children
    // rather than being children: the page, its title row, and the settings row's columns --
    // each a caption over a rule, and a well under its control.
    enum SettingsColumn { phaseColumn, amountColumn, smoothingColumn, linkColumn, runningColumn, numSettingsColumns };
    juce::Rectangle<int> page, titleRow;
    std::array<juce::Rectangle<int>, numSettingsColumns> settingsColumns;

    /** The toolbar, PHONICS's: the house mark, a hairline, the product's name. */
    void paintToolbar (juce::Graphics&);

    /** The artwork this window tints once rather than at every paint. Called from the
        constructor and again on every theme change. */
    void applyColours();

    /** The theme moved. Re-reads the look and feel's colours, tells every child, and
        repaints -- which is the whole of what a theme change is from here. */
    void changeListenerCallback (juce::ChangeBroadcaster*) override;

    // Linear phase or minimum phase: the same curve, realised two ways, and the only
    // control here that changes what the plugin costs in latency. A row of segments, the one
    // in force accent-filled.
    Ui::Segments phaseSegments;
    std::unique_ptr<juce::ParameterAttachment> phaseAttachment;
    bool linearPhase = false;

    // Starts both Learns at once, or stops both -- see PhaseTabBar.
    void startOrStopBoth();

    // What shapes and scales the curve. Fields dragged like PHONICS's number boxes,
    // in each parameter's own range and words. The band limits are not here: they are
    // dragged on the graph itself, where you can see what they are doing.
    Ui::ParameterBox amountBox    { *processorRef.getAPVTS().getParameter (ParamID::amount),    "%" };
    Ui::ParameterBox smoothingBox { *processorRef.getAPVTS().getParameter (ParamID::smoothing), "oct" };
    Ui::ParameterBox linkBox      { *processorRef.getAPVTS().getParameter (ParamID::link),      "%" };

    // RUNNING: the switch, and how many seconds back a take reads.
    WindowStepper windowControl { processorRef.getAPVTS(), ParamID::running, ParamID::window };

    // Writes a band edge dragged on the graph back to the parameters, bracketed so
    // the host sees one gesture.
    static const char* parameterFor (SpectrumDisplay::BandEdge) noexcept;
    void setBand (SpectrumDisplay::BandEdge, float hz);
    void beginBandGesture (SpectrumDisplay::BandEdge, bool starting);

    std::unique_ptr<juce::FileChooser> fileChooser;

    // Refilled every frame and handed to the display, which copies into storage it
    // already owns. Members rather than locals so neither side allocates per frame.
    std::vector<float> liveCurrentScratch, liveReferenceScratch,
                       learnedCurrentScratch, learnedReferenceScratch;

    // The correction is only re-derived a few times a second while a capture is
    // accumulating; the live traces still move at the full frame rate.
    /** Where both captures stood on the previous tick. The preview curve is re-derived
        when this changes, which is exactly when there is something new to draw. */
    struct CaptureProgress
    {
        std::int64_t source = -1, reference = -1;

        bool operator== (const CaptureProgress&) const = default;
    };

    CaptureProgress captureProgress;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PluginEditor)
};
