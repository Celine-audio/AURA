#pragma once

#include "Theme.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <array>
#include <functional>

/**
    One segment of the match workflow, drawn as a Logic-style chevron: a title, a
    status line, and an action button (Learn, or Match on the last one).

    Clicking the body selects the tab, which is what the curve display is showing.
    Clicking the action button is a separate thing entirely — it starts a capture or
    commits the match — so it never changes the selection on its own.
*/
class PhaseTab : public juce::Component,
                 public juce::SettableTooltipClient
{
public:
    /** armsCapture separates the two kinds of action button: a Learn arms a capture,
        so it reads red while it runs, whereas Match commits one and reads as done. */
    PhaseTab (const juce::String& tabTitle, const juce::String& actionText,
              Celine::Theme::Role tabAccent, bool armsCapture);

    /** Selecting a tab is what puts its curve on screen. */
    void setSelected (bool shouldBeSelected);
    bool isSelected() const noexcept { return selected; }

    /** Updates the line under the title, and whether this stage already holds data
        (which is what its dot reports). Only repaints when something changed. */
    void setStatus (const juce::String& newStatus, bool stageHasData);

    /** Draws the action button as engaged — a capture running, or a match applied. */
    void setActionActive (bool shouldBeActive);

    /** Draws the action button as wanting pressing again: what it committed no longer
        reflects what has been learned since. Only the Match tab uses this — a Learn
        cannot go out of date, it either ran or it did not. */
    void setActionAttention (bool shouldWantAttention);

    /** True while this tab's action is running a capture, which is the only state the
        red indicator means. */
    bool isCapturing() const noexcept { return arms && action.getToggleState(); }

    juce::TextButton& getActionButton() noexcept { return action; }

    /** The colours this takes once rather than reading as it draws. See Theme.h. */
    void applyColours();
    void lookAndFeelChanged() override { applyColours(); }

    /** Called when the body (not the action button) is clicked. */
    std::function<void()> onSelect;

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseEnter (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;
    bool hitTest (int x, int y) override;

    /** How far the chevron point juts out, and therefore how much neighbouring tabs
        have to overlap for the point to sit in the next tab's notch. */
    static constexpr int chevronWidth = 14;

    /** Set by the bar so the end tabs get a flat outer edge rather than a stray
        notch or point hanging off the end of the row. */
    void setEdges (bool isFirstTab, bool isLastTab);

private:
    // The chevron only changes with the bounds, but hitTest runs on every mouse move
    // over the bar and paint on every repaint, so it is built once in resized().
    void rebuildOutline();

    juce::Path outline;
    juce::Path separator;

    juce::String title, status;

    /** The stage's own colour, held as the role rather than as the colour. It was the
        colour once, taken in the constructor, which meant the selected tab's fill and
        the state dot were the two things in this window a theme change could not
        reach -- and neither is caught by the reach test, because one is a blend and
        the other only appears once a stage holds data. */
    const Celine::Theme::Role accentRole;
    juce::Colour accent() const { return Celine::Theme::colour (accentRole); }

    juce::TextButton action;
    const bool arms;

    // Which colour the action button wears. Held rather than read as it draws, the
    // way every other colour in here is, so a theme change goes through applyColours.
    void refreshActionColours();

    bool selected = false;
    bool hasData = false;
    bool attention = false;
    bool hovered = false;
    bool isFirst = false;
    bool isLast = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PhaseTab)
};

/**
    The three-stage strip under the curve display: Current, Reference, EQ Curve.
    Lays the tabs out overlapping so each chevron point nests into the next notch.
*/
class PhaseTabBar : public juce::Component
{
public:
    enum Stage { current = 0, reference, eqCurve, numStages };

    PhaseTabBar();

    PhaseTab& getTab (Stage stage) noexcept { return *tabs[(size_t) stage]; }

    void setSelected (Stage stage);
    Stage getSelected() const noexcept { return selected; }

    /** Called with the newly selected stage whenever the selection changes, including
        when it is set programmatically. */
    std::function<void (Stage)> onSelectionChanged;

    void resized() override;

private:
    std::array<std::unique_ptr<PhaseTab>, (size_t) numStages> tabs;
    Stage selected = current;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PhaseTabBar)
};
