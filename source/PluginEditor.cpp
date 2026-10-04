#include "PluginEditor.h"

#include "ProductInfo.h"

#include <CelineUI/EmbeddedAssets.h>
#include <CelineUI/Fonts.h>

#include <cmath>

using namespace Celine;

namespace
{
    /** How often the window redraws. Sixty so the curve follows the pointer rather than
        stepping after it. */
    constexpr int refreshHz = 60;

    /** How long a live trace waits, with nothing arriving from its analyzer, before it
        begins to fade. A quarter of a second -- comfortably longer than the gap between
        analyzer frames at any sample rate the plugin will see, and short enough that a
        stopped transport does not leave the trace sitting there. */
    constexpr int ticksBeforeFading = refreshHz / 4;

    /** How much of itself a fading trace keeps per tick. Held against the rate so the
        fade still takes about a second whatever the rate is. */
    const float fadePerTick = std::pow (0.86f, 30.0f / (float) refreshHz);

    // 1:2.25, and locked there — see the constrainer in the constructor. Read as
    // "one unit tall by two and a quarter across", which is the shape the graph
    // wants: a frequency axis eight octaves wide against a dB axis a fraction of
    // that.
    constexpr float aspectRatio = 2.25f;

    constexpr int defaultWidth = 1200;
    constexpr int defaultHeight = (int) (defaultWidth / aspectRatio);

    // Celine's toolbar exactly: a 45px band holding 33px buttons, which leaves six
    // pixels of air above and below them.
    constexpr int headerHeight = Celine::Theme::toolbarHeight;

    // Set by what is inside it, not the other way round: a 30px action button, a
    // title, and the line of status under it, with air enough that the buttons keep
    // the size they were given. Anything less and the two lines of type start to
    // crowd the button rather than sit beside it.
    constexpr int tabRowHeight = 46;
    constexpr int bottomRowHeight = 30;
    constexpr int gap = 10;

    /** Width of the two faders flanking the graph. Set by their names rather than by
        their tracks: "amount" in Nico Moji at the size below measures 56px, and the
        column has to hold it — the wordmark's face is a good deal wider than the one
        the rest of the window is set in, and at 46px the label came out as "amo...". */
    constexpr int faderWidth = 64;

    // juce::String treats a plain char* as Latin-1, so a UTF-8 glyph needs fromUTF8.
    const juce::String ellipsis = juce::String::fromUTF8 ("\xe2\x80\xa6");

    juce::String describeFrames (std::int64_t frames, double sampleRate, int fftSize)
    {
        if (frames <= 0)
            return "empty";

        if (sampleRate <= 0.0)
            return juce::String (frames) + " frames learned";

        // Frames overlap by 50%, so each one adds half a window of new audio.
        const auto seconds = (double) frames * (double) (fftSize / 2) / sampleRate;
        // Rounded to a whole second past ten, and *not* by asking String for zero
        // decimal places: to juce::String, zero means "as many as it takes" -- it is
        // what the plain String(double) constructor passes -- so past ten seconds this
        // printed the double in full, all fifteen digits of it.
        return (seconds < 10.0 ? juce::String (seconds, 1)
                               : juce::String (juce::roundToInt (seconds)))
               + " s learned";
    }

    SpectrumDisplay::View viewFor (PhaseTabBar::Stage stage)
    {
        switch (stage)
        {
            case PhaseTabBar::current:   return SpectrumDisplay::View::current;
            case PhaseTabBar::reference: return SpectrumDisplay::View::reference;
            case PhaseTabBar::eqCurve:   return SpectrumDisplay::View::eqCurve;
            case PhaseTabBar::numStages: break;
        }

        return SpectrumDisplay::View::current;
    }

    PluginProcessor::Side sideFor (PhaseTabBar::Stage stage)
    {
        return stage == PhaseTabBar::current ? PluginProcessor::Side::source
                                             : PluginProcessor::Side::reference;
    }

    // Where the window keeps what it remembers about itself besides its size.
    // Both are properties of the session rather than parameters: neither is something a
    // host should automate.
    const juce::Identifier predictProperty { "predict" };
    const juce::Identifier importFolderProperty { "importFolder" };
    const juce::Identifier viewShiftProperty { "viewShift" };
}

PluginEditor::PluginEditor (PluginProcessor& p)
    : AudioProcessorEditor (&p), processorRef (p)
{
    setLookAndFeel (&lookAndFeel);

    // A tooltip paints a rounded panel, so it must not be opaque -- an opaque component
    // has to fill every pixel it owns, and the four corners outside the rounding are
    // exactly the ones it does not paint; left opaque they came out as square spikes of
    // whatever was in the buffer. TooltipWindow sets the flag in its constructor and
    // offers no way to ask otherwise. Safe because this one is parented to the editor
    // rather than put on the desktop, so what shows through the corners is this window.
    tooltips.setOpaque (false);

    //display.setTooltip ("The signal, the reference and the correction between them. Drag the band edges to choose how much of the spectrum is matched.");
    addAndMakeVisible (display);

    tabBar.onSelectionChanged = [this] (PhaseTabBar::Stage stage)
    {
        display.setView (viewFor (stage));
        refreshDisplay();
    };

    tabBar.getTab (PhaseTabBar::current).getActionButton().onClick =
        [this] { toggleLearn (PhaseTabBar::current); };

    tabBar.getTab (PhaseTabBar::reference).getActionButton().onClick =
        [this] { toggleLearn (PhaseTabBar::reference); };

    tabBar.getTab (PhaseTabBar::eqCurve).getActionButton().onClick =
        [this] { performMatch(); };

    for (auto stage : { PhaseTabBar::current, PhaseTabBar::reference })
        if (auto* import = tabBar.getTab (stage).getImportButton())
            import->onClick = [this, stage] { chooseFileToImport (stage); };

    addAndMakeVisible (tabBar);

    exportButton.setTooltip ("Export the correction out as an impulse response.");
    exportButton.onClick = [this] { showExportPanel(); };
    addAndMakeVisible (exportButton);

    settingsButton.onClick = [this] { showSettingsMenu(); };
    addAndMakeVisible (settingsButton);

    phaseLabel.setFont (Fonts::bold (10.0f));
    phaseLabel.setColour (juce::Label::textColourId, Theme::textDim());
    phaseLabel.setJustificationType (juce::Justification::centredRight);
    phaseLabel.setInterceptsMouseClicks (false, false);
    addAndMakeVisible (phaseLabel);

    // Item IDs are the choice indices plus one, which is what the attachment expects.
    phaseBox.setTooltip ("Linear phase adds latency, minimum phase costs none but adds phaseshift.");
    phaseBox.addItem ("Linear", 1);
    phaseBox.addItem ("Minimum", 2);
    addAndMakeVisible (phaseBox);

    phaseAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (
        processorRef.getAPVTS(), ParamID::phase, phaseBox);

    // Running is the ordinary state, so the button looks like its neighbours;
    // bypassed is the state worth noticing, so that is the one that goes red.
    bypassButton.setClickingTogglesState (true);

    bypassButton.onStateChange = [this] { refreshBypassLook(); };
    addAndMakeVisible (bypassButton);

    applyColours();

    bypassAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (
        processorRef.getAPVTS(), ParamID::bypass, bypassButton);

    refreshBypassLook();

    amountFader.getSlider().setTooltip ("How much of the measured difference to apply.");
    addAndMakeVisible (amountFader);

    outputFader.getSlider().setTooltip ("Output gain.");
    addAndMakeVisible (outputFader);

    smoothingRow.getSlider().setTooltip ("How finely the two curves are matched.");

    linkRow.getSlider().setTooltip ("How independent the correction is for L and R channels.");

    for (auto* row : { &smoothingRow, &linkRow })
        addAndMakeVisible (row);

    // The band limits are dragged on the graph rather than dialled in beside it.
    display.onBandDragged = [this] (SpectrumDisplay::BandEdge edge, float hz) { setBand (edge, hz); };
    display.onBandGesture = [this] (SpectrumDisplay::BandEdge edge, bool starting)
    {
        beginBandGesture (edge, starting);
    };

    display.setFftSize (processorRef.getFftSize());
    display.setView (viewFor (tabBar.getSelected()));
    processorRef.setUiActive (true);

    // The theme is process-wide, so a colour changed in one window has to reach every
    // other -- including this one, when the change was made somewhere else.
    Theme::palette().addChangeListener (this);

    // Once, here, rather than on a timer: another instance may have saved a theme since
    // this module last looked, and a window opening is the moment that can matter. The
    // disk is not touched again unless somebody asks it to be.
    Theme::palette().refreshFromDisk();

    // Restore whatever size the user last left the window at.
    //
    // Read before setResizeLimits, not after. That call constrains the bounds it finds
    // — which at this point are still 0x0 — up to the minimum, and that fires
    // resized(), which writes the size back into the state. Reading afterwards returns
    // the minimum it just wrote, so the default below never applied and every fresh
    // instance opened at the smallest size allowed.
    const auto& state = processorRef.getAPVTS().state;
    const auto storedWidth = (int) state.getProperty ("uiWidth", defaultWidth);
    const auto storedHeight = (int) state.getProperty ("uiHeight", defaultHeight);

    // On unless it was turned off: seeing what Match will do before doing it is the
    // safer way to work, and the switch is right there for anyone who would rather not.
    display.setPredictionShown ((bool) state.getProperty (predictProperty, true));
    display.onPredictionShownChanged = [this] (bool shown)
    {
        processorRef.getAPVTS().state.setProperty (predictProperty, shown, nullptr);
        refreshDisplay();
    };

    // Where the graph was last slid to, so reopening the window finds it there.
    display.setViewShift ((float) state.getProperty (viewShiftProperty, 0.0f));
    display.onViewShiftChanged = [this] (float shift)
    {
        processorRef.getAPVTS().state.setProperty (viewShiftProperty, shift, nullptr);
    };

    // The second flag is the corner grip. With a fixed ratio below, that is the
    // only handle that means anything: dragging an edge would have to move the
    // other dimension with it, which reads as the window fighting the mouse.
    setResizable (true, true);

    // The floor is what leaves the graph readable once the toolbar, the tabs, the
    // slider rows and the footer have taken their fixed share.
    setResizeLimits (780, (int) (780 / aspectRatio), 2400, (int) (2400 / aspectRatio));

    if (auto* constrainer = getConstrainer())
        constrainer->setFixedAspectRatio ((double) aspectRatio);

    setSize (storedWidth, storedHeight);

    refreshState();
    startTimerHz (refreshHz);
}

PluginEditor::~PluginEditor()
{
    Theme::palette().removeChangeListener (this);

    stopTimer();
    processorRef.setUiActive (false);
    setLookAndFeel (nullptr);
}

void PluginEditor::toggleLearn (PhaseTabBar::Stage stage)
{
    const auto isCurrent = stage == PhaseTabBar::current;
    const auto wasCapturing = isCurrent ? processorRef.isSourceCapturing()
                                        : processorRef.isReferenceCapturing();

    if (isCurrent)
        processorRef.setSourceCapturing (! wasCapturing);
    else
        processorRef.setReferenceCapturing (! wasCapturing);

    // Starting a Learn discards the previous take, so put the signal being learned on
    // screen rather than leaving the user watching a curve that no longer exists.
    if (! wasCapturing)
        tabBar.setSelected (stage);

    refreshState();
}

void PluginEditor::performMatch()
{
    // The tabs already say what happened — "Match active" against "Ready to match" —
    // and the Match button is disabled until both sides have been learned, so there
    // is no failure here to report either.
    if (processorRef.performMatch())
        tabBar.setSelected (PhaseTabBar::eqCurve);

    refreshState();
}

void PluginEditor::timerCallback()
{
    // While a capture is running the underlying spectra keep changing, so the preview
    // curve has to be re-derived -- and the moment to do it is when the capture has
    // actually moved on, rather than every nth tick.
    //
    // An analyzer frame covers half an FFT, so one lands about 23 times a second at
    // 48k and twice that at 96k. Re-deriving faster than that rebuilds a curve identical
    // to the one already on screen; slower, and the preview visibly lags the capture,
    // which is what the fixed eighth-of-a-tick divisor this replaces was doing at seven
    // and a half a second. Asking the frame counts costs nothing and is right at every
    // rate, where any divisor is right at one of them.
    //
    // It stays cheap because it is only the preview: deriving the curve is 0.06 ms, and
    // the filter actually in force is untouched until Match is pressed.
    const CaptureProgress progress { processorRef.getSourceFrameCount(),
                                     processorRef.getReferenceFrameCount() };

    if (progress != captureProgress)
    {
        captureProgress = progress;

        if (processorRef.isSourceCapturing() || processorRef.isReferenceCapturing())
            processorRef.markCorrectionDirty();
    }

    refreshState();
}

void PluginEditor::refreshState()
{
    const auto sampleRate = processorRef.getSampleRate();
    const auto fftSize = processorRef.getFftSize();

    const auto srcFrames = processorRef.getSourceFrameCount();
    const auto refFrames = processorRef.getReferenceFrameCount();

    // A stage that already holds a take says how much it learned -- or, for one taken
    // from a file, which file; an empty one says what to do about it.
    auto learnStatus = [&] (PluginProcessor::Side side, bool capturing, std::int64_t frames,
                            const juce::String& what)
    {
        if (processorRef.isImportingTake (side))
            return "Reading file" + ellipsis;

        if (capturing)
            return frames > 0 ? "Learning" + ellipsis + "  " + describeFrames (frames, sampleRate, fftSize)
                              : "Learning" + ellipsis;

        if (const auto file = processorRef.getImportedTakeName (side); file.isNotEmpty())
            return file;

        if (frames > 0)
            return describeFrames (frames, sampleRate, fftSize);

        return "Learn the " + what;
    };

    const auto holdsTake = [this] (PluginProcessor::Side side, std::int64_t frames)
    {
        return frames > 0 || processorRef.getImportedTakeName (side).isNotEmpty();
    };

    const auto sourceCapturing = processorRef.isSourceCapturing();
    const auto referenceCapturing = processorRef.isReferenceCapturing();

    using Side = PluginProcessor::Side;

    auto& currentTab = tabBar.getTab (PhaseTabBar::current);
    currentTab.setStatus (learnStatus (Side::source, sourceCapturing, srcFrames, "input"),
                          holdsTake (Side::source, srcFrames));
    currentTab.setActionActive (sourceCapturing);

    const juce::String referenceHint = processorRef.isReferenceUsingSidechain() ? "sidechain" : "input";
    auto& referenceTab = tabBar.getTab (PhaseTabBar::reference);
    referenceTab.setStatus (learnStatus (Side::reference, referenceCapturing, refFrames, referenceHint),
                            holdsTake (Side::reference, refFrames));
    referenceTab.setActionActive (referenceCapturing);

    // One file at a time: a second chosen while the first is still being read would
    // only overtake it.
    const auto importing = processorRef.isImportingTake (Side::source)
                        || processorRef.isImportingTake (Side::reference);

    for (auto stage : { PhaseTabBar::current, PhaseTabBar::reference })
        if (auto* import = tabBar.getTab (stage).getImportButton())
            import->setEnabled (! importing);

    const auto matched = processorRef.isMatched();

    // A match is stale once either capture has moved on from the one it was built from,
    // which is what starting a Learn does immediately. Saying so is the point: the
    // filter you are hearing is not the one the captures now describe, and nothing else
    // on screen would tell you.
    const auto stale = processorRef.isMatchStale();

    auto& curveTab = tabBar.getTab (PhaseTabBar::eqCurve);
    const auto canMatch = processorRef.canMatch();

    curveTab.setStatus (stale    ? "Match out of date"
                      : matched  ? "Match active"
                      : canMatch ? "Ready to match"
                                 : "Not matched",
                        matched);
    curveTab.setActionActive (matched);
    curveTab.setActionAttention (stale);
    curveTab.getActionButton().setEnabled (canMatch);

    refreshDisplay();


    exportButton.setEnabled (processorRef.getCorrectionCurves().isValid());
}

void PluginEditor::refreshDisplay()
{
    auto& apvts = processorRef.getAPVTS();

    display.setSampleRate (processorRef.getSampleRate());

    // A curve with nothing behind it is cleared rather than left showing the last
    // frame it had.
    auto supply = [this] (SpectrumDisplay::Curve curve, bool hasData, std::vector<float>& scratch)
    {
        if (! hasData)
            scratch.clear();

        display.setCurve (curve, scratch);
    };

    // The two moving traces fade out once their analyzer has stopped producing frames,
    // rather than standing still on the last one. Hosts differ on what they do to a
    // plugin when the transport stops -- some keep calling processBlock with silence,
    // in which case the average decays on its own, and some stop calling it at all,
    // which used to leave the last spectrum frozen on screen looking like live audio.
    // Driving the fade off the frame counter covers both, because it asks the question
    // that actually matters: is anything still arriving?
    //
    // The wait before it starts is the point: an analyzer frame covers half an FFT, so
    // one arrives around every 43 ms, while this timer comes round every 17. Most ticks
    // therefore find no new frame even with audio playing, and fading on the first of
    // them made both traces flicker.
    const auto fade = [] (std::int64_t frames, TraceFade& state)
    {
        if (frames != state.lastFrames)
        {
            state.lastFrames = frames;
            state.ticksWithoutFrame = 0;
            state.level = 1.0f;
        }
        else if (++state.ticksWithoutFrame > ticksBeforeFading)
        {
            state.level *= fadePerTick;
        }

        return state.level;
    };

    display.setLiveFade (fade (processorRef.getLiveOutputFrameCount(), currentFade),
                         fade (processorRef.getLiveReferenceFrameCount(), referenceFade));

    using Curve = SpectrumDisplay::Curve;
    supply (Curve::liveCurrent, processorRef.getLiveOutputMagnitudes (liveCurrentScratch), liveCurrentScratch);
    supply (Curve::liveReference, processorRef.getLiveReferenceMagnitudes (liveReferenceScratch), liveReferenceScratch);
    supply (Curve::learnedCurrent, processorRef.getLearnedSourceMagnitudes (learnedCurrentScratch), learnedCurrentScratch);
    supply (Curve::learnedReference, processorRef.getLearnedReferenceMagnitudes (learnedReferenceScratch), learnedReferenceScratch);

    const auto value = [&apvts] (const char* id) { return apvts.getRawParameterValue (id)->load(); };

    // Violet is what the plugin is doing, so it is only drawn once a match is in
    // force. Before that the engine still derives a curve from the captures, but it
    // is a preview of a filter nobody is hearing -- which is the prediction's job, in
    // the prediction's colour.
    static const PluginProcessor::CorrectionCurves noCurves;

    const auto& curves = processorRef.getCorrectionCurves();
    const auto& applied = processorRef.isMatched() ? curves : noCurves;
    display.setCorrection (applied.leftDb, applied.rightDb, value (ParamID::link) >= 0.999f);

    // Only asked for while it is wanted: it is a second derivation of the curve, and
    // while a capture runs it is re-derived every time the capture moves.
    const auto& predicted = display.isPredictionShown() ? processorRef.getPredictedCurves() : noCurves;
    display.setPrediction (predicted.leftDb, predicted.rightDb);

    display.setBand (value (ParamID::lowFreq), value (ParamID::highFreq));

    // The trim is part of what the plugin does to the signal, so the curve rides with it.
    display.setCorrectionOffsetDb (value (ParamID::outputGain));

    // Only the EQ Curve tab needs an empty state: the two signal tabs have something
    // to show the moment audio is playing, match or no match.
    const auto bypassed = value (ParamID::bypass) > 0.5f;
    const auto needsMatch = display.getView() == SpectrumDisplay::View::eqCurve
                         && ! applied.isValid() && ! predicted.isValid();

    // With both takes in and Predict off there is a curve to build and nothing on screen
    // to say so, so the empty state says it instead.
    const auto emptyState = ! needsMatch           ? juce::String()
                          : processorRef.canMatch() ? juce::String ("Press Match to apply the correction")
                                                    : juce::String ("Record a current and reference signal, then press Match");

    display.setOverlayMessage (bypassed ? juce::String ("Bypassed") : emptyState);

    display.repaint();
}

const char* PluginEditor::parameterFor (SpectrumDisplay::BandEdge edge) noexcept
{
    return edge == SpectrumDisplay::BandEdge::low ? ParamID::lowFreq : ParamID::highFreq;
}

void PluginEditor::setBand (SpectrumDisplay::BandEdge edge, float hz)
{
    // Only the edge that moved. Writing both meant a drag of one recorded host
    // automation for the other, at a value nobody had asked to change.
    if (auto* parameter = processorRef.getAPVTS().getParameter (parameterFor (edge)))
        parameter->setValueNotifyingHost (parameter->convertTo0to1 (hz));
}

void PluginEditor::beginBandGesture (SpectrumDisplay::BandEdge edge, bool starting)
{
    if (auto* parameter = processorRef.getAPVTS().getParameter (parameterFor (edge)))
        starting ? parameter->beginChangeGesture() : parameter->endChangeGesture();
}

void PluginEditor::applyColours()
{
    // The bottom band's own controls, which take their colours rather than reading them.
    // This used to run once at construction, so a theme change reached everything in the
    // window except the three things standing on the light panel.
    applyPanelColours();

    // Explicitly chosen, so explicitly handed back: an override set once is a snapshot
    // like any other, and this one is the whole of what "bypassed" looks like.
    bypassButton.setActiveColour (Theme::danger());

    // Re-read from the binary and tinted here rather than in the constructor. Tinting
    // writes the colour into the drawable, so a second pass would be colouring the
    // result of the first rather than the artwork -- which is how a mark ends up stuck
    // on whatever colour the theme happened to be when the window opened.
    logo = Celine::Assets::drawable ("logo.svg");

    if (logo != nullptr)
        Celine::Assets::tint (*logo, Theme::headerText());

    wordmark = Celine::Assets::drawable (ProductInfo::wordmarkAsset, Celine::Assets::IfMissing::returnNull);

    if (wordmark != nullptr)
        Celine::Assets::tint (*wordmark, Theme::headerText());
}

void PluginEditor::changeListenerCallback (juce::ChangeBroadcaster*)
{
    // First, because applyColours below reads colours back out of it.
    //
    // Everything JUCE draws for us is *told* its colours, so the look and feel has to
    // re-read them before anything repaints -- see PluginLookAndFeel::applyPalette.
    lookAndFeel.applyPalette();

    applyColours();

    // And every child that took a colour once and kept it gets a chance to take it
    // again. JUCE walks the tree for us; a control that snapshots colours says so by
    // overriding lookAndFeelChanged().
    sendLookAndFeelChange();

    repaint();
}

void PluginEditor::applyPanelColours()
{
    // The bottom band is the design's near-white panel. Everything standing on it
    // has to flip: light-on-dark is the rest of the window's rule, and it is
    // invisible here.
    phaseLabel.setColour (juce::Label::textColourId, Theme::textOnPanel());

    // Accent-filled, which is what Celine's pickers wear on the light panel — the
    // one thing you have chosen, said in the one colour that means "chosen".
    phaseBox.setColour (juce::ComboBox::backgroundColourId, Theme::correction());
    phaseBox.setColour (juce::ComboBox::textColourId, Theme::textOnPanel());
    phaseBox.setColour (juce::ComboBox::arrowColourId, Theme::textOnPanel());
    phaseBox.setColour (juce::ComboBox::outlineColourId, Theme::textOnPanel().withAlpha (0.35f));

    exportButton.setColour (juce::TextButton::buttonColourId, Theme::correction());
    exportButton.setColour (juce::TextButton::textColourOffId, Theme::textOnPanel());
    exportButton.setColour (juce::TextButton::textColourOnId, Theme::textOnPanel());
}

void PluginEditor::refreshBypassLook()
{
    bypassButton.setActive (bypassButton.getToggleState());
}

void PluginEditor::showSettingsMenu()
{
    juce::PopupMenu menu;

    juce::PopupMenu::Item theme ("Theme" + ellipsis);
    theme.setAction ([this] { showThemeWindow (this); });
    menu.addItem (theme);

    juce::PopupMenu::Item about ("About " + juce::String (JucePlugin_Name) + ellipsis);
    about.setAction ([this] { showAboutWindow (this); });
    menu.addItem (about);

    // A menu has no parent to inherit a look and feel from.
    menu.setLookAndFeel (&lookAndFeel);
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&settingsButton));
}

void PluginEditor::showExportPanel()
{
    const auto linearPhase = processorRef.getAPVTS().getRawParameterValue (ParamID::phase)->load() < 0.5f;
    auto panel = std::make_unique<ExportPanel> (linearPhase);
    panel->onExport = [this] (IrExport::Options options) { chooseFileAndExport (options); };

    juce::CallOutBox::launchAsynchronously (std::move (panel),
                                            getLocalArea (&exportButton, exportButton.getLocalBounds()),
                                            this);
}

void PluginEditor::chooseFileAndExport (IrExport::Options options)
{
    auto suggested = juce::File::getSpecialLocation (juce::File::userMusicDirectory)
                         .getChildFile ("AURA IR.wav");

    fileChooser = std::make_unique<juce::FileChooser> ("Export Impulse Response", suggested, "*.wav");

    const auto flags = juce::FileBrowserComponent::saveMode
                     | juce::FileBrowserComponent::canSelectFiles
                     | juce::FileBrowserComponent::warnAboutOverwriting;

    fileChooser->launchAsync (flags, [this, options] (const juce::FileChooser& chooser)
    {
        const auto file = chooser.getResult();

        if (file == juce::File {})
            return;

        const auto result = processorRef.exportImpulseResponse (file.withFileExtension ("wav"), options);

        // Only the failure is worth interrupting for: a successful write is
        // confirmed by the file being where the chooser put it.
        if (result.failed())
        {
            juce::NativeMessageBox::showAsync (
                juce::MessageBoxOptions()
                    .withIconType (juce::MessageBoxIconType::WarningIcon)
                    .withTitle ("Export failed")
                    .withMessage (result.getErrorMessage())
                    .withButton ("OK")
                    .withAssociatedComponent (this),
                nullptr);
        }

        refreshState();
    });
}

void PluginEditor::chooseFileToImport (PhaseTabBar::Stage stage)
{
    auto& state = processorRef.getAPVTS().state;

    // The folder the last file came from, since references tend to live together; the
    // music folder the first time, or if that folder has gone.
    const juce::File remembered (state.getProperty (importFolderProperty).toString());
    const auto start = remembered.isDirectory() ? remembered
                                                : juce::File::getSpecialLocation (juce::File::userMusicDirectory);

    fileChooser = std::make_unique<juce::FileChooser> ("Learn the " + juce::String (stage == PhaseTabBar::current ? "current signal" : "reference")
                                                           + " from a file",
                                                       start, AudioFileSpectrum::supportedWildcard());

    const auto flags = juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles;

    fileChooser->launchAsync (flags, [this, stage] (const juce::FileChooser& chooser)
    {
        const auto file = chooser.getResult();

        if (! file.existsAsFile())
            return;

        processorRef.getAPVTS().state.setProperty (importFolderProperty,
                                                   file.getParentDirectory().getFullPathName(), nullptr);

        // Brought up now, like a Learn, so "Reading file..." is said where you are looking.
        tabBar.setSelected (stage);

        // The processor can outlive this window, and the read can outlast it.
        processorRef.importTakeFromFile (sideFor (stage), file,
                                         [safe = juce::Component::SafePointer<PluginEditor> (this)] (const juce::Result& result)
        {
            if (safe == nullptr)
                return;

            // Like an export, only the failure is worth interrupting for: success shows
            // as the file's name on the tab and its spectrum on the graph.
            if (result.failed())
            {
                juce::NativeMessageBox::showAsync (
                    juce::MessageBoxOptions()
                        .withIconType (juce::MessageBoxIconType::WarningIcon)
                        .withTitle ("Could not load the file")
                        .withMessage (result.getErrorMessage())
                        .withButton ("OK")
                        .withAssociatedComponent (safe.getComponent()),
                    nullptr);
            }

            safe->refreshState();
        });

        refreshState();
    });
}

void PluginEditor::paint (juce::Graphics& g)
{
    // The surround is the darkest thing here, so the three panels standing on it —
    // the two faders and the graph — read as openings rather than as boxes.
    g.fillAll (Theme::consoleBackground());

    g.setColour (Theme::headerBackground());
    g.fillRect (toolbarBand);

    // The faders stand on the surround itself rather than on a ground of their own.
    // Only the graph is a panel, which is the point: it is the one thing here you
    // look *into*, and giving its neighbours the same treatment flattened that.

    // The light panel, which is the design's other half: everything on it is drawn
    // in dark ink — see applyPanelColours.
    if (! bottomBand.isEmpty())
    {
        // No border: against the dark window a white panel is already the strongest
        // edge in the picture, and a line around it only fought with its own contrast.
        g.setColour (Theme::panel());
        g.fillRoundedRectangle (bottomBand.toFloat(), Theme::cornerRadius);
    }

    // The house mark first, drawn off its ink rather than its viewBox: the wordmark
    // is not centred in its own box, so placing it by the box sits it visibly high.
    if (logo != nullptr && ! logoBounds.isEmpty())
        logo->drawWithin (g, logoBounds.toFloat(), juce::RectanglePlacement::centred, 1.0f);

    // Then the product's name, as artwork rather than as type. Set as text it could
    // only ever be as centred as the font's metrics allowed: "aura" in Nico Moji is
    // all x-height, with no ascender and no descender, so it fills a little under
    // half its line box and neither drawText nor GlyphArrangement::getBoundingBox —
    // both of which work from ascent and descent — puts it where the eye wants it.
    // Drawn from the SVG, it is placed off its own ink by exactly the call that
    // places the mark beside it, and the two cannot disagree.
    // Centred on its letters rather than on its box. A wordmark with descenders in it
    // has a bounding box reaching below the line the word stands on, so centring the
    // box sits the word visibly high against the house mark beside it.
    if (wordmark != nullptr && ! wordmarkBounds.isEmpty())
        Celine::Assets::drawWordmark (g, *wordmark, wordmarkBounds.toFloat());

}

void PluginEditor::resized()
{
    // Remember the size so reopening the editor lands where the user left it.
    auto& state = processorRef.getAPVTS().state;
    if (state.isValid())
    {
        state.setProperty ("uiWidth", getWidth(), nullptr);
        state.setProperty ("uiHeight", getHeight(), nullptr);
    }

    auto area = getLocalBounds();
    toolbarBand = area.removeFromTop (headerHeight);

    {
        auto header = toolbarBand.reduced (gap + 2, 0);

        constexpr auto size = Theme::buttonSize;
        constexpr auto pitch = Theme::buttonGap;

        // The house mark leads, off its own aspect so it is never squashed, with the
        // product's name beside it: whose it is, then what it is.
        if (logo != nullptr)
        {
            const auto ink = logo->getDrawableBounds();
            const auto aspect = ink.getHeight() > 0.0f ? ink.getWidth() / ink.getHeight() : 1.0f;
            constexpr int logoHeight = 20;
            const auto logoWidth = juce::roundToInt (logoHeight * aspect);

            logoBounds = header.removeFromLeft (logoWidth).withSizeKeepingCentre (logoWidth, logoHeight);
            header.removeFromLeft (14);
        }

        // Fitted to its aspect rather than given the rest of the strip: drawWithin
        // centres the artwork's ink in whatever box it is handed, so a box that is
        // exactly the ink's shape and centred on the band is ink centred on the band.
        // 14 against the mark's 20 is the pairing that reads as its equal — this is
        // a lowercase word, and matching its height to a capital would tower.
        if (wordmark != nullptr)
        {
            const auto ink = wordmark->getDrawableBounds();
            const auto aspect = ink.getHeight() > 0.0f ? ink.getWidth() / ink.getHeight() : 1.0f;
            // Sized by the letters rather than by the ink box: see
            // Assets::xHeightFraction. A word with an ascender or a descender needs a
            // taller box to put the same sized letters in it.
            const auto wordmarkHeight =
                juce::roundToInt (14.0f / Celine::Assets::xHeightFraction (*wordmark));
            const auto wordmarkWidth = juce::roundToInt ((float) wordmarkHeight * aspect);

            wordmarkBounds = header.removeFromLeft (wordmarkWidth)
                                   .withSizeKeepingCentre (wordmarkWidth, wordmarkHeight);
        }

        const auto square = [&header] (juce::Component& c)
        {
            c.setBounds (header.removeFromRight (size).withSizeKeepingCentre (size, size));
            header.removeFromRight (pitch);
        };

        // Phase and Export have moved to the bottom row; what is left up here is the
        // pair that act on the plugin as a whole, and the mark.
        square (settingsButton);
        square (bypassButton);
    }

    area = area.reduced (gap, 0);
    area.removeFromTop (gap);

    // No status row: everything it used to say is on the tabs, which say it where
    // you are already looking.
    area.removeFromBottom (gap);

    // The bottom line: how the curve is realised on the left, what shapes it in the
    // middle, and the one thing that writes a file on the right.
    {
        bottomBand = area.removeFromBottom (bottomRowHeight + gap);
        auto row = bottomBand.reduced (gap, gap / 2);

        phaseLabel.setBounds (row.removeFromLeft (46));
        row.removeFromLeft (6);
        phaseBox.setBounds (row.removeFromLeft (108));

        exportButton.setBounds (row.removeFromRight (108));

        // Both settings side by side in what is left: two of them do not make a
        // list, and stacking them pushed the graph up for no reason.
        row.reduce (gap * 2, 0);
        const auto half = row.getWidth() / 2;

        smoothingRow.setBounds (row.removeFromLeft (half).withTrimmedRight (gap * 2));
        linkRow.setBounds (row);
    }

    area.removeFromBottom (gap);

    // In goes on the left, out on the right, with the picture between them. Their
    // columns are taken before the tab row is, so the two run the whole height of
    // this part of the window — the graph's top edge down to the bottom of the tabs
    // — rather than stopping where the graph does. The travel is as long as the
    // window can give it, and the readout at the foot of each lands on the tab row's
    // own baseline instead of floating in the gap above it.
    amountPanel = area.removeFromLeft (faderWidth);
    amountFader.setBounds (amountPanel);
    area.removeFromLeft (gap);

    outputPanel = area.removeFromRight (faderWidth);
    outputFader.setBounds (outputPanel);
    area.removeFromRight (gap);

    // The stages span the graph, not the window: they are three views of what is
    // drawn above them, so running past its edges would say they were something
    // wider than that. Taking them out of the already-narrowed area is what makes
    // that true, rather than a width copied across afterwards.
    tabBar.setBounds (area.removeFromBottom (tabRowHeight));

    area.removeFromBottom (gap);
    display.setBounds (area);
}
