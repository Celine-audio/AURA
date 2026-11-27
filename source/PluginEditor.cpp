#include "PluginEditor.h"

#include "ProductInfo.h"
#include "UpdateCheck.h"

#include <CelineUI/EmbeddedAssets.h>
#include <CelineUI/Fonts.h>
#include <CelineUI/IconButton.h>

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

    // 2:1, and locked there -- see the constrainer in the constructor. The graph wants to be
    // wide, an axis eight octaves across against a dB axis a fraction of that, and the sheets
    // under it want their three rows.
    constexpr float aspectRatio = 2.0f;

    constexpr int defaultWidth = 1200;
    constexpr int defaultHeight = (int) (defaultWidth / aspectRatio);

    /** The narrowest the window goes: what leaves the stage rows room for their status line
        beside MATCH and PROCESSING. */
    constexpr int minimumWidth = 1000;

    constexpr int toolbarHeight = Celine::Theme::toolbarHeight;

    /** PHONICS's page: 10 px in from the window's edges under the toolbar, its contents
        Ui::Space::outer in from its own. */
    constexpr int pageInset = 10;

    /** The two settings groups beside the stages, and the label column their rows share. */
    constexpr int matchWidth = 230;
    constexpr int processingWidth = 250;
    constexpr int groupLabelWidth = 84;

    // juce::String treats a plain char* as Latin-1, so a UTF-8 glyph needs fromUTF8.
    const juce::String ellipsis = juce::String::fromUTF8 ("\xe2\x80\xa6");

    /** How much audio a count of frames is, as "12.4 s". */
    juce::String describeSeconds (std::int64_t frames, double sampleRate, int fftSize)
    {
        if (sampleRate <= 0.0)
            return juce::String (frames) + " frames";

        const auto seconds = (double) frames * (double) (fftSize / 2) / sampleRate;
        return (seconds < 10.0 ? juce::String (seconds, 1) : juce::String (juce::roundToInt (seconds))) + " s";
    }

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
    const juce::Identifier previewProperty { "preview" };
    const juce::Identifier importFolderProperty { "importFolder" };
    const juce::Identifier viewShiftProperty { "viewShift" };

    /** A themed message with a row of buttons -- juce::AlertWindow ignores the palette.
        The first button is the accented choice. Each reports its index through onChoice;
        the caller closes the window. Céline's, as it is there. */
    class ChoicePrompt : public juce::Component
    {
    public:
        ChoicePrompt (PluginLookAndFeel& lnf, const juce::String& text, const juce::StringArray& choices)
        {
            setLookAndFeel (&lnf);

            message.setText (text, juce::dontSendNotification);
            message.setFont (Fonts::light (14.0f));
            message.setColour (juce::Label::textColourId, Theme::text());
            message.setJustificationType (juce::Justification::topLeft);
            addAndMakeVisible (message);

            for (int i = 0; i < choices.size(); ++i)
            {
                auto* button = buttons.add (new juce::TextButton (choices[i]));

                if (i == 0)
                {
                    button->setColour (juce::TextButton::buttonColourId, Theme::accent());
                    button->setColour (juce::TextButton::textColourOffId, Theme::text());
                }

                button->onClick = [this, i] { if (onChoice) onChoice (i); };
                addAndMakeVisible (button);
            }

            // Wide enough for the buttons side by side, tall enough for the wrapped text.
            int rowWidth = 0;

            for (auto* button : buttons)
            {
                button->changeWidthToFitText (buttonHeight);
                button->setSize (juce::jmax (button->getWidth() + 16, 96), buttonHeight);
                rowWidth += button->getWidth() + buttonGap;
            }

            const int width = juce::jmax (360, rowWidth - buttonGap + 2 * margin);

            juce::AttributedString wrapped;
            wrapped.append (text, message.getFont());
            juce::TextLayout layout;
            layout.createLayout (wrapped, static_cast<float> (width - 2 * margin - 10));

            // Last, per the house rule: it fires resized(), which places the buttons.
            setSize (width, 2 * margin + juce::roundToInt (layout.getHeight()) + 14 + 14 + buttonHeight);
        }

        ~ChoicePrompt() override { setLookAndFeel (nullptr); }

        void paint (juce::Graphics& g) override { g.fillAll (Theme::chrome()); }

        void resized() override
        {
            auto area = getLocalBounds().reduced (margin);
            auto row = area.removeFromBottom (buttonHeight);

            // Right to left, so the first choice sits leftmost.
            for (int i = buttons.size(); --i >= 0;)
            {
                buttons[i]->setBounds (row.removeFromRight (buttons[i]->getWidth()));
                row.removeFromRight (buttonGap);
            }

            area.removeFromBottom (14);
            message.setBounds (area);
        }

        std::function<void (int choice)> onChoice;

    private:
        static constexpr int margin = 18;
        static constexpr int buttonHeight = 30;
        static constexpr int buttonGap = 8;

        juce::Label message;
        juce::OwnedArray<juce::TextButton> buttons;
    };
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

    tabBar.getStartBothButton().onClick = [this] { startOrStopBoth(); };
    addAndMakeVisible (tabBar);

    // The toolbar, PHONICS's: settings and bypass as two square icon buttons, the output trim
    // before them, and EXPORT IR in words before that.
    const auto iconButton = [this] (Ui::PaintedButton& button, std::unique_ptr<juce::Drawable>& icon, bool isBypass)
    {
        button.painter = [&icon, isBypass, &button] (juce::Graphics& g, juce::Rectangle<float> area, bool hovered, bool down)
        {
            // Running is the ordinary state, so the button looks like its neighbours;
            // bypassed is the state worth noticing, so that is the one that goes red.
            const auto on = isBypass && button.getToggleState();
            g.setColour (Theme::underPointer (on ? Theme::danger() : Theme::phTile(), hovered, down));
            g.fillRect (area);

            if (icon != nullptr)
                icon->drawWithin (g, area.withSizeKeepingCentre (17.0f, 17.0f), juce::RectanglePlacement::centred, 1.0f);
        };
        button.setTooltip (button.getName());
        addAndMakeVisible (button);
    };

    bypassButton.setClickingTogglesState (true);
    iconButton (bypassButton, powerIcon, true);
    iconButton (settingsButton, gearIcon, false);
    settingsButton.onClick = [this] { showSettingsMenu(); };

    // Read back out of the parameter rather than out of the button, so the red fill follows
    // automation as well as clicks.
    bypassAttachment = std::make_unique<juce::ButtonParameterAttachment> (
        *processorRef.getAPVTS().getParameter (ParamID::bypass), bypassButton);

    exportButton.setTooltip ("Export the correction as an impulse response.");
    exportButton.setWantsKeyboardFocus (false);
    exportButton.setMouseClickGrabsKeyboardFocus (false);
    exportButton.onClick = [this] { showExportPanel(); };
    addAndMakeVisible (exportButton);

    addAndMakeVisible (outputFader);

    // PROCESSING: the phase, a row of two segments. The choice parameter's value is its
    // index, so Linear is 0 and Minimum 1.
    phaseSegments.setOptions ({ "LINEAR", "MINIMUM" });
    phaseSegments.setOptionTooltips ({ "Linear phase: no phase shift, at the cost of latency.",
                                       "Minimum phase: no latency, at the cost of some phase shift." });
    phaseSegments.setStyle (false, 8.5f);
    phaseSegments.setTitle ("Phase");
    addAndMakeVisible (phaseSegments);

    if (auto* phase = processorRef.getAPVTS().getParameter (ParamID::phase))
    {
        // Called on the message thread whatever moved the parameter, automation included.
        phaseAttachment = std::make_unique<juce::ParameterAttachment> (*phase, [this] (float index)
        {
            linearPhase = index < 0.5f;
            phaseSegments.setSelected (linearPhase ? 0 : 1);
            repaint (processingGroup);
        }, nullptr);

        phaseAttachment->sendInitialUpdate();
    }

    phaseSegments.onSelect = [this] (int index)
    {
        if (phaseAttachment != nullptr)
            phaseAttachment->setValueAsCompleteGesture ((float) index);
    };

    addAndMakeVisible (windowControl);

    amountBox.setTooltip ("How much of the measured difference to apply. Below zero, the inverse. Drag up or down; double-click for 100 %.");
    smoothingBox.setTooltip ("How finely the two curves are matched, in octaves. Drag up or down; double-click for the default.");
    linkBox.setTooltip ("How much the left and right corrections are shared. Drag up or down; double-click for 100 %.");

    for (auto* box : { &amountBox, &smoothingBox, &linkBox })
        addAndMakeVisible (box);

    applyColours();

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
    display.setPreviewShown ((bool) state.getProperty (previewProperty, true));
    display.onPreviewShownChanged = [this] (bool shown)
    {
        processorRef.getAPVTS().state.setProperty (previewProperty, shown, nullptr);
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
    // slider rows and the footer have taken their fixed share -- and, since Running's
    // window joined the bottom band, what leaves Smoothing and L/R Link a track that can
    // still be dragged beside it. At 780 the band held two settings; with three, each
    // came out with no track at all.
    setResizeLimits (minimumWidth, (int) (minimumWidth / aspectRatio), 2400, (int) (2400 / aspectRatio));

    if (auto* constrainer = getConstrainer())
        constrainer->setFixedAspectRatio ((double) aspectRatio);

    setSize (storedWidth, storedHeight);

    refreshState();
    startTimerHz (refreshHz);
}

PluginEditor::~PluginEditor()
{
    Theme::palette().removeChangeListener (this);

    // A question still up goes with the window. It is a desktop window of its own, so
    // nothing else would close it, and it draws with this editor's look and feel, which
    // is about to be destroyed. The modal manager sees the deletion and does not delete
    // it again.
    if (openPrompt != nullptr)
        delete openPrompt.getComponent();

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

void PluginEditor::startOrStopBoth()
{
    // Both learning: stop both. Otherwise start both -- and start both *fresh*, the one
    // already running included, so the two takes begin on the same block and cover the
    // same stretch of music. That is the whole reason to start them together.
    const auto both = processorRef.isSourceCapturing() && processorRef.isReferenceCapturing();

    if (both)
    {
        processorRef.setSourceCapturing (false);
        processorRef.setReferenceCapturing (false);
    }
    else
    {
        for (auto side : { PluginProcessor::Side::source, PluginProcessor::Side::reference })
        {
            processorRef.setCapturing (side, false);
            processorRef.setCapturing (side, true);
        }

        tabBar.setSelected (PhaseTabBar::current);
    }

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

    // The first-run question and the update notice, only with the window on screen, and
    // one dialog at a time.
    if (isShowing() && openPrompt == nullptr)
    {
        if (! updateQuestionChecked)
        {
            updateQuestionChecked = true;
            offerUpdateChecksOnFirstRun();
            startUpdateCheckIfEnabled();
        }
        else if (const auto release = UpdateCheck::Session::takeUpdateToAnnounce())
        {
            announceUpdate (release->version);
        }
    }
}

void PluginEditor::refreshState()
{
    const auto sampleRate = processorRef.getSampleRate();
    const auto fftSize = processorRef.getFftSize();

    const auto srcFrames = processorRef.getSourceFrameCount();
    const auto refFrames = processorRef.getReferenceFrameCount();

    // A stage that already holds a take says how much it learned -- or, for one taken
    // from a file, which file; an empty one says what to do about it.
    const auto running = processorRef.isRunning();

    auto learnStatus = [&] (PluginProcessor::Side side, bool capturing, std::int64_t frames,
                            const juce::String& what)
    {
        // Said the way the stage row says everything: a word, a middle dot, a figure.
        const juce::String dot = juce::String::fromUTF8 ("  \xc2\xb7  ");

        if (processorRef.isImportingTake (side))
            return "Reading file" + ellipsis;

        // Running, a take is the window rather than everything learned, so that is what
        // it reports -- and it stops growing once the window is full, which is how you
        // can tell it is rolling rather than stuck.
        if (capturing && running)
        {
            const auto windowed = processorRef.getWindowedFrameCount (side);
            return windowed > 0 ? "Running" + dot + "last " + describeSeconds (windowed, sampleRate, fftSize)
                                : juce::String ("Running");
        }

        if (capturing)
            return frames > 0 ? "Learning" + dot + describeSeconds (frames, sampleRate, fftSize)
                              : juce::String ("Learning");

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

    // Start both's two lights, and whether it reads Start or Stop.
    tabBar.setLearning (sourceCapturing, referenceCapturing);

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
    //
    // Except while Running, where a match is out of date for a quarter of a second at a
    // time by design and brings itself up to date without being asked.
    const auto following = running && matched;
    const auto stale = processorRef.isMatchStale() && ! following;

    auto& curveTab = tabBar.getTab (PhaseTabBar::eqCurve);
    const auto canMatch = processorRef.canMatch();

    curveTab.setStatus (following ? "Match running"
                      : stale     ? "Match out of date"
                      : matched   ? "Match active"
                      : canMatch  ? "Ready to match"
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
    // force. Before that the engine still derives a curve from the captures, but it is
    // a filter nobody is hearing yet: it is drawn as the preview, in the preview's
    // colour.
    static const PluginProcessor::CorrectionCurves noCurves;

    const auto& curves = processorRef.getCorrectionCurves();
    const auto& applied = processorRef.isMatched() ? curves : noCurves;
    display.setCorrection (applied.leftDb, applied.rightDb, value (ParamID::link) >= 0.999f);

    // Only asked for while it is wanted: it is a second derivation of the curve, and
    // while a capture runs it is re-derived every time the capture moves.
    const auto& preview = display.isPreviewShown() ? processorRef.getPreviewCurves() : noCurves;
    display.setPreview (preview.leftDb, preview.rightDb);

    display.setBand (value (ParamID::lowFreq), value (ParamID::highFreq));

    // The trim is part of what the plugin does to the signal, so the curve rides with it.
    display.setCorrectionOffsetDb (value (ParamID::outputGain));

    // Only the EQ Curve tab needs an empty state: the two signal tabs have something
    // to show the moment audio is playing, match or no match.
    const auto bypassed = value (ParamID::bypass) > 0.5f;
    const auto needsMatch = display.getView() == SpectrumDisplay::View::eqCurve
                         && ! applied.isValid() && ! preview.isValid();

    // With both takes in and Preview off there is a curve to build and nothing on screen
    // to say so, so the empty state says it instead.
    if (bypassed)
        display.setOverlay ("Bypassed", juce::String::fromUTF8 ("The signal passes untouched \xc2\xb7 the match keeps running"), true);
    else if (! needsMatch)
        display.setOverlay ({}, {}, false);
    else if (processorRef.canMatch())
        display.setOverlay ("Ready to match", "Press Match to apply the correction", false);
    else
        display.setOverlay ("Nothing learned yet", "Record a current and a reference signal, then press Match", false);

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
    // Re-read from the binary and tinted here rather than in the constructor. Tinting
    // writes the colour into the drawable, so a second pass would be colouring the result
    // of the first rather than the artwork -- which is how a mark ends up stuck on whatever
    // colour the theme happened to be when the window opened.
    logo = Celine::Assets::drawable ("logo.svg");
    powerIcon = Celine::Assets::drawable ("power-off-solid-full.svg");
    gearIcon = Celine::Assets::drawable ("gear-solid-full.svg");

    for (auto* art : { logo.get(), powerIcon.get(), gearIcon.get() })
        if (art != nullptr)
            Celine::Assets::tint (*art, Theme::phInk());

    repaint();
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

void PluginEditor::showSettingsMenu()
{
    juce::PopupMenu menu;

    // Whether to look for a newer version once each time the plugin starts.
    juce::PopupMenu::Item updates ("Check for updates at launch");
    updates.isTicked = UpdateCheck::Preferences().isEnabled();
    updates.setAction ([this, wasOn = updates.isTicked]
    {
        UpdateCheck::Preferences().setEnabled (! wasOn);

        // This launch too, if it has not checked already.
        if (! wasOn)
            startUpdateCheckIfEnabled();
    });
    menu.addItem (updates);

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

//==============================================================================
juce::DialogWindow* PluginEditor::launchPrompt (const juce::String& title, std::unique_ptr<juce::Component> content)
{
    juce::DialogWindow::LaunchOptions options;
    options.dialogTitle = title;
    options.dialogBackgroundColour = Theme::chrome();
    options.escapeKeyTriggersCloseButton = true;
    options.useNativeTitleBar = true;
    options.resizable = false;
    options.content.setOwned (content.release());

    // Async, and the window deletes itself, so it is closed with exitModalState().
    auto* window = options.launchAsync();
    openPrompt = window;
    return window;
}

void PluginEditor::offerUpdateChecksOnFirstRun()
{
    UpdateCheck::Preferences preferences;

    if (preferences.hasBeenOffered())
        return;

    // Only with a window on screen, and marked as soon as it is asked: closing the
    // question leaves the checks off, and it is not asked again.
    jassert (isShowing());
    preferences.markOffered();

    const auto name = UpdateCheck::productName();

    auto prompt = std::make_unique<ChoicePrompt> (
        lookAndFeel,
        "Check for a newer version of " + name + " when it starts?\n\n"
            "Once per launch, " + name + " asks GitHub for its newest release and tells "
            "you if it is newer than the one you have. Nothing about you or your "
            "sessions is sent.\n\n"
            "You can change this later in the Settings menu.",
        juce::StringArray { "Check for updates", "No thanks" });

    auto* raw = prompt.get();
    const juce::Component::SafePointer<juce::DialogWindow> dialog (launchPrompt (name + " updates", std::move (prompt)));
    const juce::Component::SafePointer<PluginEditor> safe (this);

    raw->onChoice = [safe, dialog] (int choice)
    {
        if (dialog != nullptr)
            dialog->exitModalState (0);

        if (choice != 0)
            return;

        UpdateCheck::Preferences().setEnabled (true);

        // And check this launch too.
        if (safe != nullptr)
            safe->startUpdateCheckIfEnabled();
    };
}

void PluginEditor::startUpdateCheckIfEnabled()
{
    // Already checked this launch, or turned off.
    if (UpdateCheck::Session::hasStarted() || ! UpdateCheck::Preferences().isEnabled())
        return;

    UpdateCheck::Session::startIfNeeded();
}

void PluginEditor::announceUpdate (const juce::String& version)
{
    // Turned off in another window meanwhile.
    if (! UpdateCheck::Preferences().isEnabled())
        return;

    const auto name = UpdateCheck::productName();

    auto prompt = std::make_unique<ChoicePrompt> (
        lookAndFeel,
        "A new update is available.\n\n"
            + name + " " + version + " is out. You have " + UpdateCheck::installedVersion() + ".",
        juce::StringArray { "Download", "Remind me later", "Turn off update checks" });

    auto* raw = prompt.get();
    const juce::Component::SafePointer<juce::DialogWindow> dialog (launchPrompt (name + " update", std::move (prompt)));

    raw->onChoice = [dialog] (int choice)
    {
        if (dialog != nullptr)
            dialog->exitModalState (0);

        // The releases page rather than a file: the right download depends on the machine.
        if (choice == 0)
            UpdateCheck::releasesPage().launchInDefaultBrowser();
        else if (choice == 2)
            UpdateCheck::Preferences().setEnabled (false);

        // "Remind me later" does nothing: the next launch asks again.
    };
}

void PluginEditor::showExportPanel()
{
    // The export panel starts on the phase the plugin is running.
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
    g.fillAll (Theme::phWindow());
    paintToolbar (g);

    // The page, a step lighter than the window round it.
    g.setColour (Theme::phPage());
    g.fillRect (page);

    // The section's title, in PHONICS's section-title face, and a note to be read last.
    Ui::textOnCaps (g, "SPECTRUM", titleRow.toFloat(), Ui::spaced (Ui::heavy (11.0f), 2.0f), Theme::phInk());
    Ui::textOnCaps (g, "WHAT IS PLAYING, WHAT IT SHOULD SOUND LIKE, AND THE EQ BETWEEN THEM",
                    titleRow.toFloat(), Ui::spaced (Ui::medium (8.0f), 1.2f), Theme::phIndex(),
                    juce::Justification::centredRight);

    // The rule between the stages and the settings.
    g.setColour (Theme::phRule());
    g.fillRect (groupsRule);

    // What the phase in force costs: half the filter at the running rate in linear phase,
    // nothing in minimum. Information, so plain text with no box.
    const auto rate = processorRef.getSampleRate();
    const auto latencyMs = linearPhase && rate > 0.0
                               ? juce::roundToInt (1000.0 * (double) (processorRef.getFilterLength() / 2) / rate)
                               : 0;

    paintGroup (g, matchGroup, "MATCH", { "AMOUNT", "SMOOTHING", "L/R LINK" });
    paintGroup (g, processingGroup, "PROCESSING", { "PHASE", "RUNNING", "LATENCY" });

    const auto latencyRow = processingGroup.withTrimmedTop (PhaseTabBar::headingHeight + Ui::Space::cell + 2 * PhaseTabBar::rowHeight)
                                           .withHeight (PhaseTabBar::rowHeight)
                                           .withTrimmedLeft (groupLabelWidth)
                                           .toFloat();
    Ui::text (g, juce::String (latencyMs) + " MS", latencyRow.reduced (6.0f, 0.0f), Ui::mono (9.5f), Theme::phDim());
}

void PluginEditor::paintGroup (juce::Graphics& g, juce::Rectangle<int> area, const juce::String& heading,
                               std::initializer_list<const char*> labels)
{
    Ui::Sheet::drawHeading (g, area.removeFromTop (PhaseTabBar::headingHeight).toFloat(), heading);
    area.removeFromTop (Ui::Space::cell);

    // The rows' well, as the stages have theirs.
    g.setColour (Theme::phWell());
    g.fillRect (area);

    for (const auto* label : labels)
        Ui::Sheet::drawLabel (g, area.removeFromTop (PhaseTabBar::rowHeight).withTrimmedLeft (8).toFloat(), label);
}

void PluginEditor::paintToolbar (juce::Graphics& g)
{
    // PHONICS's toolbar: the window's ground, a rule under it, and whose it is, a hairline,
    // what it is.
    const auto width = (float) getWidth();

    g.setColour (Theme::phControl());
    g.fillRect (0.0f, (float) toolbarHeight - 1.0f, width, 1.0f);

    auto x = 12.0f;

    if (logo != nullptr)
    {
        const auto area = juce::Rectangle<float> (x, 0.0f, 78.0f, (float) toolbarHeight).withSizeKeepingCentre (78.0f, 17.0f);
        logo->drawWithin (g, area, juce::RectanglePlacement::centred, 1.0f);
        x += 78.0f + 14.0f;
    }

    g.setColour (Theme::phTrack());
    g.fillRect (x, (float) toolbarHeight * 0.5f - 9.0f, 1.0f, 18.0f);
    x += 15.0f;

    Ui::text (g, juce::String (PRODUCT_NAME_WITHOUT_VERSION).toUpperCase(), { x, 0.0f, 200.0f, (float) toolbarHeight },
              Ui::spaced (Ui::heavy (15.0f), 3.0f), Theme::phInk());
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

    // The toolbar, right to left from the edge: settings, bypass, the trim, and EXPORT IR --
    // PHONICS's places and sizes.
    settingsButton.setBounds (getWidth() - 12 - 33, 6, 33, 33);
    bypassButton.setBounds (settingsButton.getX() - 4 - 33, 6, 33, 33);
    outputFader.setBounds (bypassButton.getX() - 16 - Ui::OutputFader::preferredWidth, 10,
                           Ui::OutputFader::preferredWidth, 24);
    exportButton.setBounds (outputFader.getX() - 24 - 104, 9, 104, 27);

    page = getLocalBounds().withTrimmedTop (toolbarHeight).reduced (pageInset);
    auto area = page.reduced (Ui::Space::outer);

    titleRow = area.removeFromTop (Ui::Space::sectionHeader);
    area.removeFromTop (Ui::Space::gutter);

    // The sheets along the foot: the stages, a rule, then MATCH and PROCESSING.
    auto sheets = area.removeFromBottom (PhaseTabBar::preferredHeight);
    area.removeFromBottom (Ui::Space::gutter);
    display.setBounds (area);

    processingGroup = sheets.removeFromRight (processingWidth);
    sheets.removeFromRight (Ui::Space::gutter * 2);
    matchGroup = sheets.removeFromRight (matchWidth);
    sheets.removeFromRight (Ui::Space::gutter);
    groupsRule = sheets.removeFromRight (1);
    sheets.removeFromRight (Ui::Space::gutter);
    tabBar.setBounds (sheets);

    // A group's rows: the label column, then the control, half a cell in from the row's top
    // and foot and an inner margin from the well's right edge.
    const auto rowOf = [] (juce::Rectangle<int> group, int index)
    {
        return group.withTrimmedTop (PhaseTabBar::headingHeight + Ui::Space::cell + index * PhaseTabBar::rowHeight)
                    .withHeight (PhaseTabBar::rowHeight)
                    .withTrimmedLeft (groupLabelWidth)
                    .withTrimmedRight (Ui::Space::cell)
                    .reduced (0, Ui::Space::cell / 2);
    };

    amountBox.setBounds (rowOf (matchGroup, 0));
    smoothingBox.setBounds (rowOf (matchGroup, 1));
    linkBox.setBounds (rowOf (matchGroup, 2));

    phaseSegments.setBounds (rowOf (processingGroup, 0));
    windowControl.setBounds (rowOf (processingGroup, 1));
}
