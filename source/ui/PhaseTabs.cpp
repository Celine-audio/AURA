#include "PhaseTabs.h"

#include "SpectrumDisplay.h"

using namespace Celine;

namespace
{
    /** From the row's left edge to its first word: clear of the selection's bar, so the bar
        marks the row and touches nothing. PHONICS's mic sheet's inset. */
    constexpr float labelInset = 8.0f;

    /** The legend line: the stage's curve colour, as short and thin as a chart's key. */
    constexpr float legendWidth = 14.0f;
    constexpr float legendThickness = 2.0f;

    /** The columns the words take: the number, the name, and the status after them. */
    constexpr float indexWidth = 22.0f;
    constexpr float nameWidth = 96.0f;

    juce::Font nameFont() { return Ui::spaced (Ui::medium (8.0f), 1.2f); }
    juce::Font statusFont() { return Ui::spaced (Ui::medium (7.5f), 1.0f); }
}

//==============================================================================
PhaseTab::PhaseTab (const juce::String& tabTitle, const juce::String& actionText,
                    Theme::Role tabAccent, bool armsCapture)
    : title (tabTitle), accentRole (tabAccent), action (actionText), arms (armsCapture)
{
    action.setGlyph (actionText.toUpperCase());
    action.setTooltip (arms ? "Learn this signal from what is playing. Press again to stop."
                            : "Match the current signal to the reference.");
    refreshActionLook();
    addAndMakeVisible (action);

    setMouseCursor (juce::MouseCursor::PointingHandCursor);
}

void PhaseTab::enableImport()
{
    if (importButton != nullptr)
        return;

    importButton = std::make_unique<Ui::Toggle> ("Load file");
    importButton->setGlyph ("FILE");
    importButton->setTooltip ("Learn this signal from an audio file.");

    addAndMakeVisible (*importButton);
    resized();
}

void PhaseTab::refreshActionLook()
{
    // A Learn lights in the recording red, as anything recording does. MATCH lights in the
    // correction's violet while a match is in force, and in the out-of-date orange once what
    // it committed no longer reflects what has been learned -- orange because red here
    // already means "recording".
    if (arms)
        action.setLitRole (Theme::Role::record);
    else
        action.setLitRole (attention ? Theme::Role::stale : Theme::Role::correction);
}

void PhaseTab::setSelected (bool shouldBeSelected)
{
    if (selected == shouldBeSelected)
        return;

    selected = shouldBeSelected;
    repaint();
}

void PhaseTab::setActionAttention (bool shouldWantAttention)
{
    if (attention == shouldWantAttention)
        return;

    attention = shouldWantAttention;
    refreshActionLook();

    // Lit either way while it asks: the orange is the news.
    action.setToggleState (attention || action.getToggleState(), juce::dontSendNotification);
}

void PhaseTab::setStatus (const juce::String& newStatus, bool stageHasData)
{
    if (status == newStatus && hasData == stageHasData)
        return;

    status = newStatus;
    hasData = stageHasData;
    repaint();
}

void PhaseTab::setActionActive (bool shouldBeActive)
{
    const auto lit = shouldBeActive || attention;

    if (action.getToggleState() == lit)
        return;

    action.setToggleState (lit, juce::dontSendNotification);
}

void PhaseTab::resized()
{
    // Right to left: the action in the last column, FILE before it, each a cell's gap from
    // the next and half a cell in from the row's top and foot, as PHONICS insets its fields.
    auto area = getLocalBounds().reduced (0, Ui::Space::cell / 2);

    action.setBounds (area.removeFromRight (actionWidth));
    area.removeFromRight (Ui::Space::cell);

    if (importButton != nullptr)
        importButton->setBounds (area.removeFromRight (importWidth));
}

void PhaseTab::paint (juce::Graphics& g)
{
    const auto bounds = getLocalBounds().toFloat();

    // The selected row: a band a step lighter than the well, and a short accent bar at its
    // left edge. A row under the pointer gets half of that band, so it reads as clickable.
    if (selected || hovered)
    {
        g.setColour (selected ? Theme::phTile() : Theme::phTile().withAlpha (0.5f));
        g.fillRect (bounds);
    }

    if (selected)
    {
        g.setColour (Theme::accent());
        g.fillRect (bounds.withWidth (2.0f).reduced (0.0f, 3.0f));
    }

    auto row = bounds.withTrimmedLeft (labelInset);

    // The legend: which curve on the graph is this stage's.
    const auto legend = row.removeFromLeft (legendWidth);
    g.setColour (accent().withAlpha (hasData || selected ? 1.0f : 0.5f));
    g.fillRect (legend.withSizeKeepingCentre (legendWidth, legendThickness));
    row.removeFromLeft (8.0f);

    Ui::text (g, index, row.removeFromLeft (indexWidth), Ui::mono (8.0f), Theme::phFaint());
    Ui::text (g, title.toUpperCase(), row.removeFromLeft (nameWidth), nameFont(),
              selected ? Theme::phInk() : Theme::phLabel());

    // The status is information, so it is plain text: red while recording, the secondary ink
    // once there is something learned, faint while the stage is empty.
    const auto right = (float) (importButton != nullptr ? importButton->getX() : action.getX()) - (float) Ui::Space::inner;
    const auto ink = isCapturing() ? Theme::record() : (hasData ? Theme::phDim() : Theme::phFaint());
    Ui::text (g, status.toUpperCase(), row.withRight (juce::jmax (row.getX(), right)), statusFont(), ink);
}

void PhaseTab::mouseUp (const juce::MouseEvent& event)
{
    // Only a click that ends where it started selects -- a drag off the row is a change
    // of mind, not a selection.
    if (getLocalBounds().contains (event.getPosition()) && onSelect != nullptr)
        onSelect();
}

void PhaseTab::mouseEnter (const juce::MouseEvent&)
{
    hovered = true;
    repaint();
}

void PhaseTab::mouseExit (const juce::MouseEvent&)
{
    hovered = false;
    repaint();
}

//==============================================================================
PhaseTabBar::PhaseTabBar()
{
    tabs[(size_t) current]   = std::make_unique<PhaseTab> ("Current",   "Learn", SpectrumDisplay::roleFor (SpectrumDisplay::View::current),   true);
    tabs[(size_t) reference] = std::make_unique<PhaseTab> ("Reference", "Learn", SpectrumDisplay::roleFor (SpectrumDisplay::View::reference), true);
    tabs[(size_t) eqCurve]   = std::make_unique<PhaseTab> ("EQ Curve",  "Match", SpectrumDisplay::roleFor (SpectrumDisplay::View::eqCurve),  false);

    tabs[(size_t) current]->enableImport();
    tabs[(size_t) reference]->enableImport();

    tabs[(size_t) current]->setIndex ("01");
    tabs[(size_t) reference]->setIndex ("02");
    tabs[(size_t) eqCurve]->setIndex ("03");

    for (size_t i = 0; i < tabs.size(); ++i)
    {
        tabs[i]->onSelect = [this, i] { setSelected ((Stage) i); };
        addAndMakeVisible (*tabs[i]);
    }

    startBoth.setGlyph ("START BOTH");
    startBoth.setLitRole (Theme::Role::record);
    startBoth.setTooltip ("Start learning the current signal and the reference together.");
    addAndMakeVisible (startBoth);

    tabs[(size_t) selected]->setSelected (true);
}

void PhaseTabBar::setLearning (bool currentIsLearning, bool referenceIsLearning)
{
    if (currentLearning == currentIsLearning && referenceLearning == referenceIsLearning)
        return;

    currentLearning = currentIsLearning;
    referenceLearning = referenceIsLearning;

    const auto both = currentLearning && referenceLearning;
    startBoth.setGlyph (both ? "STOP BOTH" : "START BOTH");
    startBoth.setToggleState (both, juce::dontSendNotification);
    startBoth.setTooltip (both ? "Stop learning both signals."
                               : "Start learning the current signal and the reference together.");
}

void PhaseTabBar::setSelected (Stage stage)
{
    if (selected == stage)
        return;

    selected = stage;

    for (size_t i = 0; i < tabs.size(); ++i)
        tabs[i]->setSelected ((Stage) i == stage);

    if (onSelectionChanged != nullptr)
        onSelectionChanged (stage);
}

void PhaseTabBar::paint (juce::Graphics& g)
{
    const auto bounds = getLocalBounds().toFloat();
    const auto band = bounds.withHeight ((float) headingHeight);

    Ui::Sheet::drawHeading (g, band, "STAGES");

    // The status column's name, over where the status lines start.
    Ui::Sheet::drawHint (g, band, labelInset + legendWidth + 8.0f + indexWidth + nameWidth, 60.0f, "STATUS");

    // The rows' well: PHONICS's inspector ground, recessed under the page.
    g.setColour (Theme::phWell());
    g.fillRect (bounds.withTrimmedTop ((float) (headingHeight + Ui::Space::cell)));
}

void PhaseTabBar::resized()
{
    auto area = getLocalBounds();
    auto band = area.removeFromTop (headingHeight);
    area.removeFromTop (Ui::Space::cell);

    // START BOTH in the heading band, over the two columns it acts on -- FILE and LEARN -- and
    // as wide as the two together, so it reads as their header-level action.
    const auto both = PhaseTab::importWidth + Ui::Space::cell + PhaseTab::actionWidth;
    startBoth.setBounds (band.removeFromRight (both).reduced (0, 2).withTrimmedBottom (1));

    for (auto& tab : tabs)
        tab->setBounds (area.removeFromTop (rowHeight));
}
