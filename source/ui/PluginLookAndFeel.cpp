#include "PluginLookAndFeel.h"

#include "Style.h"

#include <cmath>

using namespace Celine;

namespace
{
    /** Popup rows are 22px, and the words in them are Inter Bold at 11px in capitals with
        a little air between the letters. Sized to the page's own labels rather than the
        house menus, which were 15px and read as a list from a different window. */
    constexpr int rowHeight = 22;

    /** The gap a row keeps from the rule round the menu, on each side. The rule is drawn at
        the edge of the window, so a row that touched it would paint over it. */
    constexpr float rowInset = 1.0f;

    /** Where the tick square starts, from the edge of the menu, and how big it is. The
        square replaces JUCE's tick, and the words start after it on every row so a ticked
        row and an unticked one line up. */
    constexpr float tickLeft = 12.0f;
    constexpr float tickSize = 6.0f;
    constexpr float textLeft = 26.0f;
    constexpr float textRight = 12.0f;

    // The first column of a row split by a tab: room for a size, "1X12", in the mono.
    constexpr float firstColumnWidth = 44.0f;

    /** The chevron that marks a row opening a submenu, and the room it takes. */
    constexpr float chevronInset = 12.0f;

    /** A section head is a row of its own, shorter than an item: it names a group and
        does not select anything. */
    constexpr int sectionHeaderHeight = 20;

    /** A separator is a rule with a little air each side of it. */
    constexpr int separatorHeight = 5;

    /** Air between the rule round the menu and its first and last rows. JUCE adds this
        above the first row and below the last. */
    constexpr int menuBorder = 4;

    /** The margin a tooltip keeps round its words, and the widest it will get before the
        words wrap. */
    constexpr float tooltipPaddingX = 9.0f;
    constexpr float tooltipPaddingY = 6.0f;
    constexpr float tooltipMaximumWidth = 300.0f;

    /** The thickness of a scrollbar's thumb. A thin bar is the whole of the look: the
        track is not drawn at all, only the thumb. */
    constexpr float scrollbarThickness = 4.0f;

    /** Everything that is a label in this window: Inter, small, in capitals. The letter
        spacing is the design's, and it is kept in one place so the menus, the buttons and
        the measurements that size them all agree. */
    juce::Font labelFont (float height)
    {
        return Ui::spaced (Ui::medium (height), 0.9f);
    }

    juce::Font headerFont()
    {
        return Ui::spaced (Ui::bold (9.0f), 1.2f);
    }

    juce::Font buttonFont()
    {
        return Ui::spaced (Ui::medium (9.0f), 1.0f);
    }

    /** A tooltip's text in the house face for sentences. Not capitals: a tip is a sentence
        and reads as one. */
    juce::TextLayout layoutTooltip (const juce::String& text, float maximumWidth)
    {
        juce::AttributedString attributed;
        attributed.setWordWrap (juce::AttributedString::WordWrap::byWord);
        attributed.setJustification (juce::Justification::centredLeft);
        attributed.append (text, Ui::medium (11.0f), Theme::phInk());

        juce::TextLayout layout;
        layout.createLayoutWithBalancedLineLengths (attributed, maximumWidth);

        return layout;
    }
}

//==============================================================================
PluginLookAndFeel::PluginLookAndFeel()
{
    // The base constructor calls the base's applyPalette, which cannot reach this one while
    // it is still being built, so this look's own colours are added here.
    applyPalette();
}

PluginLookAndFeel::~PluginLookAndFeel() = default;

void PluginLookAndFeel::applyPalette()
{
    LookAndFeelBase::applyPalette();

    using namespace Theme;

    // Left transparent, as the house has it. The fill in drawPopupMenuBackground covers every
    // pixel of the menu either way, so opacity would change nothing on screen, but it would
    // change what the window is -- and PluginBasics pins this colour as transparent, because
    // a transparent window is the one the popup's own shadow handling was written against.
    setColour (juce::PopupMenu::backgroundColourId, juce::Colours::transparentBlack);
    setColour (juce::PopupMenu::textColourId, phDim());
    setColour (juce::PopupMenu::headerTextColourId, phFaint());
    setColour (juce::PopupMenu::highlightedBackgroundColourId, phControl());
    setColour (juce::PopupMenu::highlightedTextColourId, phInk());

    // A field is a well: darker than the page it sits on, with a rule round it, and the
    // accent round it only while somebody is typing in it.
    setColour (juce::TextEditor::backgroundColourId, phWell());
    setColour (juce::TextEditor::textColourId, phInk());
    setColour (juce::TextEditor::highlightColourId, accent().withAlpha (0.4f));
    setColour (juce::TextEditor::highlightedTextColourId, phInk());
    setColour (juce::TextEditor::outlineColourId, phRule());
    setColour (juce::TextEditor::focusedOutlineColourId, accent());
    setColour (juce::CaretComponent::caretColourId, phInk());

    setColour (juce::ComboBox::backgroundColourId, phControl());
    setColour (juce::ComboBox::textColourId, phInk());
    setColour (juce::ComboBox::arrowColourId, phDim());

    setColour (juce::ScrollBar::thumbColourId, phTrack());

    setColour (juce::TooltipWindow::backgroundColourId, phTile());
    setColour (juce::TooltipWindow::textColourId, phInk());
    setColour (juce::TooltipWindow::outlineColourId, phRule());

    setColour (juce::AlertWindow::backgroundColourId, phTile());
    setColour (juce::AlertWindow::outlineColourId, phRule());
}

//==============================================================================
// Popup menus.

void PluginLookAndFeel::drawPopupMenuBackground (juce::Graphics& g, int width, int height)
{
    const auto bounds = juce::Rectangle<float> (0.0f, 0.0f, (float) width, (float) height);

    g.setColour (Theme::phTile());
    g.fillRect (bounds);

    // A rule round the whole menu, so it reads as a thing laid on the page rather than as a
    // hole in it. Square, so there is nothing for a shadow to round off.
    g.setColour (Theme::phRule());
    g.drawRect (bounds, 1.0f);
}

std::unique_ptr<juce::DropShadower> PluginLookAndFeel::createDropShadowerForComponent (juce::Component&)
{
    return nullptr;
}

int PluginLookAndFeel::getPopupMenuBorderSizeWithOptions (const juce::PopupMenu::Options&)
{
    return menuBorder;
}

juce::Font PluginLookAndFeel::getPopupMenuFont()
{
    return labelFont (11.0f);
}

void PluginLookAndFeel::getIdealPopupMenuItemSize (const juce::String& text, bool isSeparator,
                                                   int standardMenuItemHeight,
                                                   int& idealWidth, int& idealHeight)
{
    if (isSeparator)
    {
        idealWidth = 50;
        idealHeight = separatorHeight;
        return;
    }

    // Measured in capitals, because that is what is drawn. Never shorter than the row the
    // combo box it opened from asked for, so a list lines up with its own button.
    idealHeight = juce::jmax (rowHeight, standardMenuItemHeight);

    // A tab splits a row into a fixed first column and the rest (see drawPopupMenuItem). JUCE
    // measures a row with its shortcut text appended, so the width already allows for it.
    const auto columns = text.containsChar ('\t');
    const auto rest = columns ? text.fromFirstOccurrenceOf ("\t", false, false) : text;
    idealWidth = juce::GlyphArrangement::getStringWidthInt (getPopupMenuFont(), rest.toUpperCase())
                 + (int) (textLeft + textRight) + (columns ? (int) firstColumnWidth : 0);
}

void PluginLookAndFeel::drawPopupMenuItem (juce::Graphics& g, const juce::Rectangle<int>& area,
                                           bool isSeparator, bool isActive, bool isHighlighted,
                                           bool isTicked, bool hasSubMenu,
                                           const juce::String& text,
                                           const juce::String& shortcutKeyText,
                                           const juce::Drawable* icon,
                                           const juce::Colour* textColour)
{
    juce::ignoreUnused (icon);

    const auto row = area.toFloat().reduced (rowInset, 0.0f);

    if (isSeparator)
    {
        g.setColour (Theme::phRule());
        g.fillRect (row.withSizeKeepingCentre (row.getWidth(), 1.0f));
        return;
    }

    // The row under the pointer is filled, and a bar of accent down its left edge says it
    // is the one that would be chosen. Only for a row that can be chosen at all.
    if (isHighlighted && isActive)
    {
        g.setColour (Theme::phControl());
        g.fillRect (row);

        g.setColour (Theme::accent());
        g.fillRect (row.withWidth (2.0f));
    }

    // A ticked row says so with an accent square, not with a tick: the square is the same
    // colour as the bar, so the one accent on the page marks the same kind of fact.
    if (isTicked)
    {
        g.setColour (Theme::accent());
        g.fillRect (juce::Rectangle<float> (tickSize, tickSize)
                        .withCentre ({ row.getX() + tickLeft + tickSize * 0.5f, row.getCentreY() }));
    }

    auto ink = Theme::phDim();

    if (! isActive)
        ink = Theme::phFaint();
    else if (isHighlighted || isTicked)
        ink = Theme::phInk();

    if (isActive && textColour != nullptr)
        ink = *textColour;

    const auto chevronRoom = hasSubMenu ? chevronInset : 0.0f;
    const auto labelArea = row.withTrimmedLeft (textLeft).withTrimmedRight (textRight + chevronRoom);

    g.setColour (ink);

    // A tab makes two columns: the first, in mono, a fixed width, so a list of sizes and names
    // lines its names up whatever the sizes are ("1X12", "4X10"); the rest after it.
    if (text.containsChar ('\t'))
    {
        g.setFont (Ui::mono (9.5f));
        g.drawText (text.upToFirstOccurrenceOf ("\t", false, false).toUpperCase(), labelArea.withWidth (firstColumnWidth),
                    juce::Justification::centredLeft, true);
        g.setFont (getPopupMenuFont());
        g.drawText (text.fromFirstOccurrenceOf ("\t", false, false).toUpperCase(), labelArea.withTrimmedLeft (firstColumnWidth),
                    juce::Justification::centredLeft, true);
    }
    else
    {
        g.setFont (getPopupMenuFont());
        g.drawText (text.toUpperCase(), labelArea, juce::Justification::centredLeft, true);
    }

    if (shortcutKeyText.isNotEmpty())
    {
        g.setColour (Theme::phFaint());
        g.setFont (Ui::mono (9.0f));
        g.drawText (shortcutKeyText, row.withTrimmedRight (textRight + chevronRoom),
                    juce::Justification::centredRight, true);
    }

    // A submenu is a chevron, drawn as two strokes so it sits at the weight of the rule.
    if (hasSubMenu)
    {
        const auto tip = juce::Point<float> (row.getRight() - textRight - 2.0f, row.getCentreY());

        juce::Path chevron;
        chevron.startNewSubPath (tip.x - 2.0f, tip.y - 3.0f);
        chevron.lineTo (tip.x + 1.0f, tip.y);
        chevron.lineTo (tip.x - 2.0f, tip.y + 3.0f);

        g.setColour (isActive ? Theme::phDim() : Theme::phFaint());
        g.strokePath (chevron, juce::PathStrokeType (1.2f, juce::PathStrokeType::curved,
                                                     juce::PathStrokeType::rounded));
    }
}

void PluginLookAndFeel::drawPopupMenuSectionHeader (juce::Graphics& g, const juce::Rectangle<int>& area,
                                                    const juce::String& sectionName)
{
    // A group's name, small and spaced out, so it reads as a heading over the rows under it
    // and not as one more row. The faintest ink on the menu, because it is read last.
    g.setColour (Theme::phFaint());
    g.setFont (headerFont());
    g.drawText (sectionName.toUpperCase(), area.withTrimmedLeft ((int) tickLeft),
                juce::Justification::centredLeft, true);
}

void PluginLookAndFeel::getIdealPopupMenuSectionHeaderSizeWithOptions (const juce::String& text,
                                                                       int /*standardMenuItemHeight*/,
                                                                       int& idealWidth, int& idealHeight,
                                                                       const juce::PopupMenu::Options&)
{
    // The house makes a heading half as tall again as an item. Here it is a fixed, smaller
    // row: the words are small and the space is what separates one group from the next.
    idealWidth = juce::GlyphArrangement::getStringWidthInt (headerFont(), text.toUpperCase())
                 + (int) (tickLeft + textRight);
    idealHeight = sectionHeaderHeight;
}

//==============================================================================
// Combo boxes and text fields.

void PluginLookAndFeel::drawComboBox (juce::Graphics& g, int width, int height, bool isButtonDown,
                                      int, int, int, int, juce::ComboBox& box)
{
    const auto bounds = juce::Rectangle<float> (0.0f, 0.0f, (float) width, (float) height);
    const auto alpha = box.isEnabled() ? 1.0f : 0.5f;

    g.setColour (Theme::underPointer (box.findColour (juce::ComboBox::backgroundColourId),
                                      false, isButtonDown)
                     .withMultipliedAlpha (alpha));
    g.fillRect (bounds);

    // Open, the box says so with the accent rule round it. Shut, it has none: the control is
    // the square and the arrow, and a rule would be a second edge where one is enough.
    if (isButtonDown)
    {
        g.setColour (Theme::accent());
        g.drawRect (bounds, 1.0f);
    }

    // A small down-pointing triangle at the right, filled, in the dim ink.
    const auto tip = juce::Point<float> (bounds.getRight() - 12.0f, bounds.getCentreY());

    juce::Path arrow;
    arrow.startNewSubPath (tip.x - 3.5f, tip.y - 2.0f);
    arrow.lineTo (tip.x + 3.5f, tip.y - 2.0f);
    arrow.lineTo (tip.x, tip.y + 2.0f);
    arrow.closeSubPath();

    g.setColour (box.findColour (juce::ComboBox::arrowColourId).withMultipliedAlpha (alpha));
    g.fillPath (arrow);
}

juce::Font PluginLookAndFeel::getComboBoxFont (juce::ComboBox& box)
{
    // What is chosen, in the label face and in sentence case: it is a value, and a preset's
    // name is easier to read as the name it was given.
    return Ui::medium (juce::jmin (12.0f, (float) box.getHeight() * 0.5f));
}

void PluginLookAndFeel::fillTextEditorBackground (juce::Graphics& g, int width, int height,
                                                  juce::TextEditor& editor)
{
    g.setColour (editor.findColour (juce::TextEditor::backgroundColourId));
    g.fillRect (0.0f, 0.0f, (float) width, (float) height);
}

void PluginLookAndFeel::drawTextEditorOutline (juce::Graphics& g, int width, int height,
                                               juce::TextEditor& editor)
{
    // A read-only field is text on the panel it sits on, not a field: it has no rule, and
    // that is what lets the notices in the About window read as the page rather than as a
    // box inside it.
    if (! editor.isEnabled() || editor.isReadOnly())
        return;

    const auto focused = editor.hasKeyboardFocus (true);
    const auto colour = editor.findColour (focused ? juce::TextEditor::focusedOutlineColourId
                                                   : juce::TextEditor::outlineColourId);

    if (colour.isTransparent())
        return;

    g.setColour (colour);
    g.drawRect (juce::Rectangle<float> (0.0f, 0.0f, (float) width, (float) height), 1.0f);
}

juce::Font PluginLookAndFeel::getLabelFont (juce::Label& label)
{
    // A label someone can type into is a number, and numbers are set in the mono face so they
    // line up in a column. Anything that asked for a face of its own keeps it, as it always did.
    if (label.isEditable() && ! label.getProperties().contains (keepFontProperty))
        return Ui::mono (label.getFont().getHeight());

    return LookAndFeelBase::getLabelFont (label);
}

juce::Label* PluginLookAndFeel::createSliderTextBox (juce::Slider& slider)
{
    auto* label = LookAndFeelBase::createSliderTextBox (slider);

    // A slider's readout is a number, and it is set in the mono face for the reason the
    // editable labels are: it should not change width as its value does.
    label->setFont (Ui::mono (11.0f));

    return label;
}

//==============================================================================
// Buttons.

juce::Font PluginLookAndFeel::getTextButtonFont (juce::TextButton& button, int buttonHeight)
{
    if (button.getProperties().contains (largeGlyphProperty))
        return LookAndFeelBase::getTextButtonFont (button, buttonHeight);

    return buttonFont();
}

void PluginLookAndFeel::drawButtonText (juce::Graphics& g, juce::TextButton& button,
                                        bool shouldDrawButtonAsHighlighted,
                                        bool shouldDrawButtonAsDown)
{
    // A button that is a single glyph keeps the house drawing: its job is to fill its
    // target, which is a question of size and not of letters.
    if (button.getProperties().contains (largeGlyphProperty))
    {
        LookAndFeelBase::drawButtonText (g, button, shouldDrawButtonAsHighlighted, shouldDrawButtonAsDown);
        return;
    }

    g.setFont (buttonFont());
    g.setColour (button.findColour (button.getToggleState() ? juce::TextButton::textColourOnId
                                                            : juce::TextButton::textColourOffId)
                     .withMultipliedAlpha (button.isEnabled() ? 1.0f : 0.5f));

    // Capitals, as every label on the page is. The words are drawn centred in the button and
    // allowed to shrink to fit it, so a long one does not run over its edges.
    g.drawFittedText (button.getButtonText().toUpperCase(),
                      button.getLocalBounds().reduced (6, 0),
                      juce::Justification::centred, 1);
}

//==============================================================================
// Scrollbars, tooltips, call-outs and alerts.

void PluginLookAndFeel::drawScrollbar (juce::Graphics& g, juce::ScrollBar&, int x, int y,
                                       int width, int height, bool isScrollbarVertical,
                                       int thumbStartPosition, int thumbSize,
                                       bool isMouseOver, bool isMouseDown)
{
    if (thumbSize <= 0)
        return;

    // No track, and a thumb that is a thin square. It takes the label grey when it is
    // being looked at or dragged, which is the only time a scrollbar should be noticed.
    const auto bounds = juce::Rectangle<float> ((float) x, (float) y, (float) width, (float) height);

    const auto thumb = isScrollbarVertical
        ? juce::Rectangle<float> (bounds.getCentreX() - scrollbarThickness * 0.5f,
                                  (float) thumbStartPosition, scrollbarThickness, (float) thumbSize)
        : juce::Rectangle<float> ((float) thumbStartPosition,
                                  bounds.getCentreY() - scrollbarThickness * 0.5f,
                                  (float) thumbSize, scrollbarThickness);

    g.setColour ((isMouseOver || isMouseDown) ? Theme::phLabel() : Theme::phTrack());
    g.fillRect (thumb);
}

void PluginLookAndFeel::drawTooltip (juce::Graphics& g, const juce::String& text, int width, int height)
{
    const auto whole = juce::Rectangle<float> (0.0f, 0.0f, (float) width, (float) height);

    g.setColour (Theme::phTile());
    g.fillRect (whole);

    g.setColour (Theme::phRule());
    g.drawRect (whole, 1.0f);

    layoutTooltip (text, whole.getWidth() - tooltipPaddingX * 2.0f)
        .draw (g, whole.reduced (tooltipPaddingX, tooltipPaddingY));
}

juce::Rectangle<int> PluginLookAndFeel::getTooltipBounds (const juce::String& tipText,
                                                          juce::Point<int> screenPosition,
                                                          juce::Rectangle<int> parentArea)
{
    // Measured with the layout that draws it, so the box is sized for the words it holds.
    const auto layout = layoutTooltip (tipText, tooltipMaximumWidth);

    const auto width = (int) std::ceil (layout.getWidth() + tooltipPaddingX * 2.0f) + 1;
    const auto height = (int) std::ceil (layout.getHeight() + tooltipPaddingY * 2.0f);

    // Below and to the right of the pointer, unless that would take it off the edge. There is
    // no margin for a shadow, because this one has none: the rule is its edge.
    return juce::Rectangle<int> (screenPosition.x > parentArea.getCentreX() ? screenPosition.x - (width + 12)
                                                                           : screenPosition.x + 18,
                                 screenPosition.y > parentArea.getCentreY() ? screenPosition.y - (height + 6)
                                                                           : screenPosition.y + 18,
                                 width, height)
        .constrainedWithin (parentArea);
}

float PluginLookAndFeel::getCallOutBoxCornerSize (const juce::CallOutBox&)
{
    return 0.0f;
}

void PluginLookAndFeel::drawCallOutBoxBackground (juce::CallOutBox&, juce::Graphics& g,
                                                  const juce::Path& path, juce::Image& cachedImage)
{
    juce::ignoreUnused (cachedImage);

    // The bubble the colour picker and the other call-outs open in: the popup's own panel and
    // its own rule, so a picker looks like the menus it sits beside. No shadow -- the house
    // draws one, and a shadow is what makes a box look as though it floats above the page
    // rather than belonging to it.
    g.setColour (Theme::phTile());
    g.fillPath (path);

    g.setColour (Theme::phRule());
    g.strokePath (path, juce::PathStrokeType (1.0f));
}

juce::Font PluginLookAndFeel::getAlertWindowTitleFont()
{
    return Ui::spaced (Ui::heavy (13.0f), 1.0f);
}

juce::Font PluginLookAndFeel::getAlertWindowMessageFont()
{
    return Ui::medium (12.0f);
}
