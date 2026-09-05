#include "PluginEditor.h"

#include "ProductInfo.h"

#include "ui/EmbeddedAssets.h"
#include "ui/Fonts.h"

using namespace Celine;

namespace
{
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
        return juce::String (seconds, seconds < 10.0 ? 1 : 0) + " s learned";
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

    display.setTooltip ("The signal, the reference and the correction between them. "
                        "Drag the band edges to choose how much of the spectrum is matched.");
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

    addAndMakeVisible (tabBar);

    exportButton.setTooltip ("Export the correction out as an impulse response.");
    exportButton.onClick = [this] { showExportPanel(); };
    addAndMakeVisible (exportButton);

    logo = Celine::Assets::drawable ("logo.svg");
    wordmark = Celine::Assets::drawable (ProductInfo::wordmarkAsset,
                                        Celine::Assets::IfMissing::returnNull);

    if (logo != nullptr)
        Celine::Assets::tint (*logo, Theme::text());

    if (wordmark != nullptr)
        Celine::Assets::tint (*wordmark, Theme::text());

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
    bypassButton.setActiveColour (Theme::danger());
    bypassButton.onStateChange = [this] { refreshBypassLook(); };
    addAndMakeVisible (bypassButton);

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

    applyPanelColours();

    display.setFftSize (processorRef.getFftSize());
    display.setView (viewFor (tabBar.getSelected()));
    processorRef.setUiActive (true);

    // The theme is process-wide, so a colour changed in one window has to reach every
    // other -- including this one, when the change was made somewhere else.
    Theme::palette().addChangeListener (this);

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
    startTimerHz (30);
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
    // While a capture is running the underlying spectra keep changing, so the
    // preview curve has to be re-derived — but a few times a second is plenty.
    if (++curveTick >= 4)
    {
        curveTick = 0;

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

    // A stage that already holds a take says how much it learned; an empty one says
    // what to do about it.
    auto learnStatus = [&] (bool capturing, std::int64_t frames, const juce::String& what)
    {
        if (capturing)
            return frames > 0 ? "Learning" + ellipsis + "  " + describeFrames (frames, sampleRate, fftSize)
                              : "Learning" + ellipsis;

        if (frames > 0)
            return describeFrames (frames, sampleRate, fftSize);

        return "Learn the " + what;
    };

    const auto sourceCapturing = processorRef.isSourceCapturing();
    const auto referenceCapturing = processorRef.isReferenceCapturing();

    auto& currentTab = tabBar.getTab (PhaseTabBar::current);
    currentTab.setStatus (learnStatus (sourceCapturing, srcFrames, "input"), srcFrames > 0);
    currentTab.setActionActive (sourceCapturing);

    const juce::String referenceHint = processorRef.isReferenceUsingSidechain() ? "sidechain" : "input";
    auto& referenceTab = tabBar.getTab (PhaseTabBar::reference);
    referenceTab.setStatus (learnStatus (referenceCapturing, refFrames, referenceHint), refFrames > 0);
    referenceTab.setActionActive (referenceCapturing);

    const auto matched = processorRef.isMatched();
    auto& curveTab = tabBar.getTab (PhaseTabBar::eqCurve);
    curveTab.setStatus (matched ? "Match active" : (srcFrames > 0 && refFrames > 0 ? "Ready to match"
                                                                                  : "Not matched"),
                        matched);
    curveTab.setActionActive (matched);
    curveTab.getActionButton().setEnabled (srcFrames > 0 && refFrames > 0);

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

    // The two moving traces fade out when their analyzer stops producing frames,
    // rather than standing still on the last one. Hosts differ on what they do to a
    // plugin when the transport stops -- some keep calling processBlock with silence,
    // in which case the average decays on its own, and some stop calling it at all,
    // which used to leave the last spectrum frozen on screen looking like live audio.
    // Driving the fade off the frame counter covers both, because it asks the question
    // that actually matters: is anything still arriving?
    const auto fade = [] (std::int64_t frames, std::int64_t& lastSeen, float& level)
    {
        if (frames != lastSeen)
        {
            lastSeen = frames;
            level = 1.0f;
        }
        else
        {
            // The timer runs at 30 Hz, so this is gone in about a second: slow enough
            // to read as a decay rather than a cut, quick enough that a stopped
            // transport does not leave a ghost sitting on the graph.
            level *= 0.86f;
        }

        return level;
    };

    display.setLiveFade (fade (processorRef.getLiveOutputFrameCount(), lastCurrentFrames, currentFade),
                         fade (processorRef.getLiveReferenceFrameCount(), lastReferenceFrames, referenceFade));

    using Curve = SpectrumDisplay::Curve;
    supply (Curve::liveCurrent, processorRef.getLiveOutputMagnitudes (liveCurrentScratch), liveCurrentScratch);
    supply (Curve::liveReference, processorRef.getLiveReferenceMagnitudes (liveReferenceScratch), liveReferenceScratch);
    supply (Curve::learnedCurrent, processorRef.getLearnedSourceMagnitudes (learnedCurrentScratch), learnedCurrentScratch);
    supply (Curve::learnedReference, processorRef.getLearnedReferenceMagnitudes (learnedReferenceScratch), learnedReferenceScratch);

    const auto value = [&apvts] (const char* id) { return apvts.getRawParameterValue (id)->load(); };

    const auto& curves = processorRef.getCorrectionCurves();
    display.setCorrection (curves.leftDb, curves.rightDb, value (ParamID::link) >= 0.999f);

    display.setBand (value (ParamID::lowFreq), value (ParamID::highFreq));

    // The trim is part of what the plugin does to the signal, so the curve rides with it.
    display.setCorrectionOffsetDb (value (ParamID::outputGain));

    // Only the EQ Curve tab needs an empty state: the two signal tabs have something
    // to show the moment audio is playing, match or no match.
    const auto bypassed = value (ParamID::bypass) > 0.5f;
    const auto needsMatch = display.getView() == SpectrumDisplay::View::eqCurve && ! curves.isValid();

    display.setOverlayMessage (bypassed   ? "Bypassed"
                             : needsMatch ? "Learn a current and a reference signal, then press Match"
                                          : juce::String());

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

void PluginEditor::changeListenerCallback (juce::ChangeBroadcaster*)
{
    // Everything JUCE draws for us is *told* its colours, so the look and feel has to
    // re-read them before anything repaints -- see PluginLookAndFeel::applyPalette.
    lookAndFeel.applyPalette();

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

void PluginEditor::paint (juce::Graphics& g)
{
    // The surround is the darkest thing here, so the three panels standing on it —
    // the two faders and the graph — read as openings rather than as boxes.
    g.fillAll (Theme::consoleBackground());

    g.setColour (Theme::chrome());
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
            constexpr int wordmarkHeight = 14;
            const auto wordmarkWidth = juce::roundToInt (wordmarkHeight * aspect);

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
