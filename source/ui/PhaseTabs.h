#pragma once

#include "Controls.h"

#include <array>
#include <functional>
#include <memory>

/**
    One stage of the match workflow, drawn as a chevron in the strip under the graph:
    Current > Reference > EQ Curve, so the strip reads as the order you work in.

    PHONICS's greys and square ends; the chevron points are the one shape it adds. The
    stage's curve colour runs along its top as a strip, the way a clip carries its colour:
    full on the stage the graph is showing, half once it holds a take, faint while it is
    empty. Under its number and name, a status line in plain text (red while recording),
    and at the right its fields -- FILE…, and the action: LEARN, or MATCH on the last.

    Clicking the body selects the stage, which is what the graph shows; clicking a field is
    a separate thing -- it starts a capture or commits the match -- and never changes the
    selection on its own. Every colour is read as it paints, and the stage's colour is held
    as its role, so a theme change needs nothing but a repaint.
*/
class PhaseTab : public juce::Component,
                 public juce::SettableTooltipClient
{
public:
    /** armsCapture separates the two kinds of action: a Learn arms a capture, so it lights
        in the recording red while it runs, whereas Match commits one. */
    PhaseTab (const juce::String& tabTitle, const juce::String& actionText,
              Celine::Theme::Role tabAccent, bool armsCapture);

    /** Selecting a stage is what puts its curve on screen. */
    void setSelected (bool shouldBeSelected);
    bool isSelected() const noexcept { return selected; }

    /** Updates the status line, and whether this stage already holds data. Only repaints
        when something changed. */
    void setStatus (const juce::String& newStatus, bool stageHasData);

    /** The number before the name, "01" to "03": where the stage stands in the chain. */
    void setIndex (const juce::String& newIndex) { index = newIndex; repaint(); }

    /** Lights the action: a capture running, or a match applied. */
    void setActionActive (bool shouldBeActive);

    /** Lights the action in the out-of-date orange: what it committed no longer reflects
        what has been learned since. Only MATCH uses this -- a Learn cannot go out of date. */
    void setActionAttention (bool shouldWantAttention);

    /** True while this stage's action is running a capture. */
    bool isCapturing() const noexcept { return arms && action.getToggleState(); }

    Ui::Toggle& getActionButton() noexcept { return action; }

    /** Gives this stage a FILE… field before its action: learning it from an audio file
        instead of from what is playing. */
    void enableImport();

    /** The field enableImport() added, or null on a stage without one. */
    Ui::Toggle* getImportButton() noexcept { return importButton.get(); }

    /** Called when the row (not one of its fields) is clicked. */
    std::function<void()> onSelect;

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseEnter (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;
    void lookAndFeelChanged() override { repaint(); }
    bool hitTest (int x, int y) override;

    /** How far the chevron point juts out, and so how much neighbouring stages overlap for
        the point to sit in the next one's notch. */
    static constexpr int chevronWidth = 14;

    /** Set by the strip so the end stages get a flat outer edge rather than a stray notch or
        point hanging off the end of the row. */
    void setEdges (bool isFirstTab, bool isLastTab);

    static constexpr int actionWidth = 60;
    static constexpr int importWidth = 54;

private:
    void refreshActionLook();

    // Built once in resized(): hitTest runs on every mouse move over the strip.
    void rebuildOutline();
    juce::Path outline, separator;
    bool isFirst = false, isLast = false;

    juce::String title, status, index;

    /** The stage's own colour, held as the role rather than as the colour, so the legend
        line follows a theme change. */
    const Celine::Theme::Role accentRole;
    juce::Colour accent() const { return Celine::Theme::colour (accentRole); }

    Ui::Toggle action;
    const bool arms;

    std::unique_ptr<Ui::Toggle> importButton;

    bool selected = false;
    bool hasData = false;
    bool attention = false;
    bool hovered = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PhaseTab)
};

/**
    The strip under the graph: START BOTH standing alone at its left, then the three stages
    as chevrons -- Current, Reference, EQ Curve -- overlapping so each point nests into the
    next one's notch.
*/
class PhaseTabBar : public juce::Component
{
public:
    enum Stage { current = 0, reference, eqCurve, numStages };

    PhaseTabBar();

    PhaseTab& getTab (Stage stage) noexcept { return *tabs[(size_t) stage]; }

    /** Starts both Learns at once -- or, when both are running, stops them. */
    Ui::Toggle& getStartBothButton() noexcept { return startBoth; }

    /** Which of the two sides is learning: START BOTH lights, and reads STOP BOTH, once both run. */
    void setLearning (bool currentIsLearning, bool referenceIsLearning);

    void setSelected (Stage stage);
    Stage getSelected() const noexcept { return selected; }

    /** Called with the newly selected stage whenever the selection changes, including
        when it is set programmatically. */
    std::function<void (Stage)> onSelectionChanged;

    /** Tall enough for a name over a status line beside a field. */
    static constexpr int preferredHeight = 48;

    /** START BOTH's width, and its gap to Current. */
    static constexpr int startBothWidth = 104;

    void resized() override;

private:
    std::array<std::unique_ptr<PhaseTab>, (size_t) numStages> tabs;
    Stage selected = current;

    Ui::Toggle startBoth { "Start both" };
    bool currentLearning = false, referenceLearning = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PhaseTabBar)
};
