#include "PhaseTabs.h"

#include "SpectrumDisplay.h"

using namespace Celine;

namespace
{
    /** The stage's colour along the chevron's top: a clip's colour strip. */
    constexpr float stripHeight = 3.0f;

    /** Between the last word and the first field, and from the last field to the point. */
    constexpr int textToControls = 10;

    juce::Font nameFont() { return Ui::spaced (Ui::bold (8.5f), 1.4f); }
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
    action.setProminent (true);
    refreshActionLook();
    addAndMakeVisible (action);

    setMouseCursor (juce::MouseCursor::PointingHandCursor);
}

void PhaseTab::enableImport()
{
    if (importButton != nullptr)
        return;

    importButton = std::make_unique<Ui::Toggle> ("Load file");
    importButton->setGlyph (juce::String::fromUTF8 ("FILE\xe2\x80\xa6"));
    importButton->setProminent (true);
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

void PhaseTab::setEdges (bool isFirstTab, bool isLastTab)
{
    if (isFirst == isFirstTab && isLast == isLastTab)
        return;

    isFirst = isFirstTab;
    isLast = isLastTab;
    rebuildOutline();
    repaint();
}

void PhaseTab::rebuildOutline()
{
    // Square ends, sharp points: PHONICS rounds nothing, and the chevrons are the shape.
    const auto bounds = getLocalBounds().toFloat();
    const auto chevron = (float) chevronWidth;
    const auto mid = bounds.getCentreY();

    juce::Path path;
    path.startNewSubPath (bounds.getX(), bounds.getY());

    if (isLast)
    {
        path.lineTo (bounds.getRight(), bounds.getY());
        path.lineTo (bounds.getRight(), bounds.getBottom());
    }
    else
    {
        path.lineTo (bounds.getRight() - chevron, bounds.getY());
        path.lineTo (bounds.getRight(), mid);
        path.lineTo (bounds.getRight() - chevron, bounds.getBottom());
    }

    path.lineTo (bounds.getX(), bounds.getBottom());

    if (! isFirst)
        path.lineTo (bounds.getX() + chevron, mid);

    path.closeSubPath();
    outline = std::move (path);

    // Each stage draws the notch on its *left*, never the point on its right: the stages
    // paint left to right and overlap by one chevron, so a line on the right would be buried
    // under the next stage's fill, where this one lands on top of the previous stage.
    juce::Path seam;

    if (! isFirst)
    {
        seam.startNewSubPath (bounds.getX(), bounds.getY());
        seam.lineTo (bounds.getX() + chevron, mid);
        seam.lineTo (bounds.getX(), bounds.getBottom());
    }

    separator = std::move (seam);
}

bool PhaseTab::hitTest (int x, int y)
{
    // Without this the rectangular bounds of overlapping stages would swallow clicks meant
    // for the neighbour whose point sits inside them.
    return outline.contains ((float) x, (float) y);
}

void PhaseTab::resized()
{
    rebuildOutline();

    // Right to left, clear of the point: the action, then FILE before it, a cell apart, each
    // a row tall and centred on the stage.
    auto area = getLocalBounds();
    area.removeFromRight (isLast ? Ui::Space::inner : chevronWidth + Ui::Space::cell);

    const auto field = [&area] (juce::Component& c, int width)
    {
        c.setBounds (area.removeFromRight (width).withSizeKeepingCentre (width, Ui::Space::row));
        area.removeFromRight (Ui::Space::cell);
    };

    field (action, actionWidth);

    if (importButton != nullptr)
        field (*importButton, importWidth);
}

void PhaseTab::paint (juce::Graphics& g)
{
    // The stage the graph shows is a step lighter than the others, as PHONICS marks a
    // selected row; one under the pointer lifts a little.
    g.setColour (Theme::underPointer (selected ? Theme::phControl() : Theme::phTile(), hovered && ! selected, false));
    g.fillPath (outline);

    // Its colour along its top, like a clip's: full on the stage shown, half once it holds a
    // take, faint while it is empty.
    {
        juce::Graphics::ScopedSaveState clip (g);
        g.reduceClipRegion (outline);
        g.setColour (accent().withAlpha (selected ? 1.0f : hasData ? 0.5f : 0.18f));
        g.fillRect (getLocalBounds().toFloat().withHeight (stripHeight));
    }

    // The seam is the page showing through, so the strip reads as one panel cut in three.
    g.setColour (Theme::phPage());
    g.strokePath (separator, juce::PathStrokeType (2.0f));

    auto content = getLocalBounds().toFloat().withTrimmedTop (stripHeight);
    content.removeFromLeft (isFirst ? 12.0f : (float) chevronWidth + 8.0f);

    const auto controls = importButton != nullptr ? importButton->getX() : action.getX();
    content.setRight (juce::jmax (content.getX(), (float) (controls - textToControls)));

    auto top = content.withHeight (content.getHeight() * 0.5f).withTrimmedTop (2.0f);
    const auto bottom = content.withTrimmedTop (content.getHeight() * 0.5f).withTrimmedBottom (2.0f);

    Ui::text (g, index, top.removeFromLeft (20.0f), Ui::mono (8.0f), Theme::phFaint());
    Ui::text (g, title.toUpperCase(), top, nameFont(), selected ? Theme::phInk() : Theme::phDim());

    // The status is information, so it is plain text: red while recording, the label grey
    // once there is something learned, faint while the stage is empty.
    const auto ink = isCapturing() ? Theme::record() : (hasData ? Theme::phLabel() : Theme::phFaint());
    // Cut with an ellipsis where the window is narrow, never mid-word.
    g.setFont (statusFont());
    g.setColour (ink);
    g.drawText (status.toUpperCase(), bottom.withTrimmedLeft (20.0f), juce::Justification::centredLeft, true);
}

void PhaseTab::mouseUp (const juce::MouseEvent& event)
{
    // Only a click that ends where it started selects -- a drag off the stage is a change
    // of mind, not a selection.
    if (hitTest (event.x, event.y) && onSelect != nullptr)
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
    startBoth.setProminent (true);
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

void PhaseTabBar::resized()
{
    auto area = getLocalBounds();

    // START BOTH stands alone at the left, the strip's height, then a gap.
    startBoth.setBounds (area.removeFromLeft (startBothWidth));
    area.removeFromLeft (Ui::Space::gutter);

    // The overlap gives back exactly the width the chevrons take up, so the three stages come
    // out the same size and the strip fills its bounds.
    const auto overlap = PhaseTab::chevronWidth;
    const auto each = (area.getWidth() + overlap * (numStages - 1)) / numStages;

    auto x = area.getX();

    for (int i = 0; i < numStages; ++i)
    {
        auto& tab = *tabs[(size_t) i];
        const auto width = i == numStages - 1 ? area.getRight() - x : each;

        tab.setEdges (i == 0, i == numStages - 1);
        tab.setBounds (x, area.getY(), width, area.getHeight());
        x += width - overlap;
    }
}
