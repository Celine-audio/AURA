#include "PhaseTabs.h"

#include <CelineUI/Fonts.h>
#include <CelineUI/Theme.h>
#include "SpectrumDisplay.h"

using namespace Celine;

namespace
{
    // Big enough to read at a glance: these are the three things the plugin asks you
    // to do, and they were sized like incidental toggles.
    constexpr int actionWidth = 88;
    constexpr int actionHeight = 30;

    // Until the window is narrowed so far that the title starts to go: then the button
    // gives way, down to this, so "Reference" is not traded for air inside "Learn".
    constexpr int narrowActionWidth = 60;
    constexpr int narrowTabWidth = 240;
    constexpr int wideTabWidth = 340;

    // The import button is square and as tall as the action beside it, so the pair
    // read as one row of controls rather than a button and an afterthought.
    constexpr int importSize = actionHeight;
    constexpr int importGap = 6;

    // Between the last line of the tab's text and the first of its buttons.
    constexpr int textToControls = 10;

    int actionWidthFor (int tabWidth) noexcept
    {
        const auto clamped = juce::jlimit (narrowTabWidth, wideTabWidth, tabWidth);
        return juce::roundToInt (juce::jmap ((float) clamped, (float) narrowTabWidth, (float) wideTabWidth,
                                             (float) narrowActionWidth, (float) actionWidth));
    }
}

//==============================================================================
PhaseTab::PhaseTab (const juce::String& tabTitle, const juce::String& actionText,
                    Theme::Role tabAccent, bool armsCapture)
    : title (tabTitle), accentRole (tabAccent), action (actionText), arms (armsCapture)
{
    applyColours();

    // Said on the button rather than only in the tab's own status line, because the
    // button is what the pointer is over when the question comes up.
    action.setTooltip (arms ? "Record signal."
                            : "Match the current signal to the reference.");

    addAndMakeVisible (action);
}

void PhaseTab::enableImport()
{
    if (importButton != nullptr)
        return;

    importButton = std::make_unique<Celine::IconButton> ("Load file", "file-import-solid-full.svg");

    // A touch more air than the toolbar's: at thirty pixels, the toolbar's inset left
    // the page heavier than the word in the button beside it.
    importButton->setIconInset (0.22f);
    importButton->setTooltip ("Import audio reference.");

    addAndMakeVisible (*importButton);
    resized();
}

int PhaseTab::controlsLeft() const noexcept
{
    return importButton != nullptr ? importButton->getX() : action.getX();
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

void PhaseTab::setSelected (bool shouldBeSelected)
{
    if (selected == shouldBeSelected)
        return;

    selected = shouldBeSelected;
    repaint();
}

void PhaseTab::applyColours()
{
    refreshActionColours();
}

void PhaseTab::refreshActionColours()
{
    // Red is what "this is armed" looks like everywhere else in the plugin, so the
    // action button borrows it whether it is a Learn or the Match -- until the match it
    // committed goes out of date, when it turns orange to say so. Orange because red is
    // already spoken for here: it would read as "capturing", which is the one thing
    // this is not.
    const auto accentColour = attention ? Theme::stale() : Theme::record();

    action.setColour (juce::TextButton::buttonColourId, accentColour.withAlpha (0.22f));
    action.setColour (juce::TextButton::buttonOnColourId, accentColour);
    action.setColour (juce::TextButton::textColourOffId, accentColour.brighter (0.35f));
    action.setColour (juce::TextButton::textColourOnId, Theme::text());
}

void PhaseTab::setActionAttention (bool shouldWantAttention)
{
    if (attention == shouldWantAttention)
        return;

    attention = shouldWantAttention;
    refreshActionColours();
    action.repaint();
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
    if (action.getToggleState() == shouldBeActive)
        return;

    action.setToggleState (shouldBeActive, juce::dontSendNotification);
    repaint();
}

void PhaseTab::rebuildOutline()
{
    const auto bounds = getLocalBounds().toFloat().reduced (0.5f);
    const auto chevron = (float) chevronWidth;
    const auto mid = bounds.getCentreY();

    // Only where the row meets the window does it round off, and to the graph's own
    // radius: the chevrons are the whole point of the shape and stay sharp, so the
    // softening reads as the outline of one panel rather than three lozenges.
    const auto radius = juce::jmin (Theme::cornerRadius, bounds.getHeight() * 0.5f);

    juce::Path path;

    // A true quarter circle rather than a pulled corner, which is what the rounded
    // rectangles elsewhere draw; the arc also carries the line into itself, so the
    // straight run before each call only has to get near the right place.
    const auto corner = [&path, radius] (float centreX, float centreY, float from)
    {
        path.addCentredArc (centreX, centreY, radius, radius, 0.0f,
                            from, from + juce::MathConstants<float>::halfPi, false);
    };

    path.startNewSubPath (bounds.getX() + (isFirst ? radius : 0.0f), bounds.getY());

    if (isLast)
    {
        path.lineTo (bounds.getRight() - radius, bounds.getY());
        corner (bounds.getRight() - radius, bounds.getY() + radius, 0.0f);
        path.lineTo (bounds.getRight(), bounds.getBottom() - radius);
        corner (bounds.getRight() - radius, bounds.getBottom() - radius,
                juce::MathConstants<float>::halfPi);
    }
    else
    {
        path.lineTo (bounds.getRight() - chevron, bounds.getY());
        path.lineTo (bounds.getRight(), mid);
        path.lineTo (bounds.getRight() - chevron, bounds.getBottom());
    }

    if (isFirst)
    {
        path.lineTo (bounds.getX() + radius, bounds.getBottom());
        corner (bounds.getX() + radius, bounds.getBottom() - radius,
                juce::MathConstants<float>::pi);
        path.lineTo (bounds.getX(), bounds.getY() + radius);
        corner (bounds.getX() + radius, bounds.getY() + radius,
                juce::MathConstants<float>::pi * 1.5f);
    }
    else
    {
        path.lineTo (bounds.getX(), bounds.getBottom());
        path.lineTo (bounds.getX() + chevron, mid);
    }

    path.closeSubPath();
    outline = std::move (path);

    // The seam is kept as its own path so it can be drawn without the rest of the
    // outline. Each tab draws the notch on its *left*, never the point on its right:
    // the tabs paint left to right and overlap by exactly one chevron, so a line on
    // the right would be buried under the next tab's fill, where this one lands on
    // top of the previous tab and stays visible.
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
    // Without this the rectangular bounds of overlapping tabs would swallow clicks
    // meant for the neighbour whose point sits inside them.
    return outline.contains ((float) x, (float) y);
}

void PhaseTab::paint (juce::Graphics& g)
{
    auto fill = selected ? accent().withAlpha (0.20f) : Theme::surface();
    if (hovered && ! selected)
        fill = fill.brighter (0.07f);

    g.setColour (fill);
    g.fillPath (outline);

    // Only the seam is drawn, not the shape. With no line around the row the three
    // tabs read as one panel divided into three, which is what they are, and the
    // selected one is told apart by the colour it is filled with. The seam is the
    // window's own ground rather than a lighter rule: it should look like the panel
    // has been cut through to what is behind it, the way the fader grooves do.
    g.setColour (Theme::background());
    g.strokePath (separator, juce::PathStrokeType (2.0f));

    auto content = getLocalBounds().toFloat().reduced (0.0f, 6.0f);
    content.removeFromLeft (isFirst ? 12.0f : (float) chevronWidth + 10.0f);

    // Off the buttons themselves rather than off their sizes, so a tab with an extra
    // one -- or a window narrow enough that they give way -- keeps the same margin.
    content.setRight (juce::jmax (content.getX(), (float) (controlsLeft() - textToControls)));

    // State dot: red while capturing, the stage's own colour once it holds data.
    const auto dot = juce::Rectangle<float> (7.0f, 7.0f)
                         .withCentre ({ content.getX() + 3.5f, content.getCentreY() - 5.0f });

    if (isCapturing())
    {
        g.setColour (Theme::record());
        g.fillEllipse (dot);
    }
    else if (hasData)
    {
        g.setColour (accent());
        g.fillEllipse (dot);
    }
    else
    {
        g.setColour (Theme::line());
        g.drawEllipse (dot.reduced (0.5f), 1.2f);
    }

    content.removeFromLeft (14.0f);

    g.setColour (selected ? Theme::text() : Theme::textDim());
    g.setFont (Fonts::bold (14.0f));
    g.drawText (title, content.removeFromTop (14.0f), juce::Justification::centredLeft);

    // A step below the title, and it follows the title down: the line under a tab you
    // are not on used to stay exactly as bright as the line under the one you are,
    // which read as the two tabs being equally in front. Its own role below the
    // title's, so a theme can separate them.
    const auto statusInk = selected ? Theme::textDim() : Theme::comment();

    g.setColour (hasData ? statusInk : statusInk.withAlpha (0.6f));
    g.setFont (Fonts::light (11.5f));
    g.drawText (status, content, juce::Justification::topLeft);
}

void PhaseTab::resized()
{
    rebuildOutline();

    auto area = getLocalBounds();
    area.removeFromRight (isLast ? 8 : chevronWidth + 4);

    const auto width = actionWidthFor (getWidth());
    action.setBounds (area.removeFromRight (width).withSizeKeepingCentre (width, actionHeight));

    if (importButton != nullptr)
    {
        area.removeFromRight (importGap);
        importButton->setBounds (area.removeFromRight (importSize).withSizeKeepingCentre (importSize, importSize));
    }
}

void PhaseTab::mouseUp (const juce::MouseEvent& event)
{
    if (event.mouseWasDraggedSinceMouseDown() || ! hitTest (event.x, event.y))
        return;

    if (onSelect != nullptr)
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

    for (int i = 0; i < numStages; ++i)
    {
        auto& tab = *tabs[(size_t) i];
        tab.setEdges (i == 0, i == numStages - 1);
        tab.onSelect = [this, stage = (Stage) i] { setSelected (stage); };

        // Left to right, so each tab's notch outline is stroked over the point of the
        // one before it and the seam reads as a single continuous edge. Clicks sort
        // themselves out regardless: hitTest follows the chevron, not the bounds.
        addAndMakeVisible (tab);
    }

    //tabs[(size_t) current]->setTooltip ("Current signal.");
    //tabs[(size_t) reference]->setTooltip ("Reference signal.");
    //abs[(size_t) eqCurve]->setTooltip ("Matched curve.");

    // Both signals can come from a file. The reference most often -- a finished track is
    // exactly what people want to match to -- but the current too: a bounce of the mix
    // being corrected learns as well as the mix playing does, and two files make a
    // correction, and an impulse response, without the transport running at all.
    tabs[(size_t) current]->enableImport();
    tabs[(size_t) reference]->enableImport();

    tabs[(size_t) current]->setSelected (true);
}

void PhaseTabBar::setSelected (Stage stage)
{
    selected = stage;

    for (int i = 0; i < numStages; ++i)
        tabs[(size_t) i]->setSelected (i == (int) stage);

    if (onSelectionChanged != nullptr)
        onSelectionChanged (stage);
}

void PhaseTabBar::resized()
{
    const auto bounds = getLocalBounds();

    // The overlap gives back exactly the width the chevrons take up, so all three
    // tabs end up the same size and the row fills its bounds.
    const auto overlap = PhaseTab::chevronWidth;
    const auto each = (bounds.getWidth() + overlap * (numStages - 1)) / numStages;

    auto x = bounds.getX();

    for (int i = 0; i < numStages; ++i)
    {
        const auto width = i == numStages - 1 ? bounds.getRight() - x : each;
        tabs[(size_t) i]->setBounds (x, bounds.getY(), width, bounds.getHeight());
        x += width - overlap;
    }
}
