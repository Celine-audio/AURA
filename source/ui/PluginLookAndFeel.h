#pragma once

#include <CelineUI/LookAndFeelBase.h>

/**
    This plugin's look, for everything JUCE draws for it: popup menus, combo boxes, text
    fields, buttons, scrollbars, tooltips and call-out boxes.

    The house kit draws all of those in the house's own shape, and PHONICS is not that
    shape. Its controls are square, its labels are small upper-case Inter, its numbers are
    JetBrains Mono, and the accent is kept for what is selected or being touched. The
    colours and the corner radius come from PluginHouseOverrides.h through the kit; what is
    here is the drawing that the kit's base cannot express: the menu rows, the ticks, the
    fields' outlines and the letter-spacing.

    Every colour is read at paint time from Theme:: -- nothing here holds a colour across
    a paint, so a theme change is picked up by the next repaint. applyPalette() is the one
    place a colour is handed to JUCE, which needs to be told the colours it draws with.
*/
class PluginLookAndFeel : public LookAndFeelBase
{
public:
    PluginLookAndFeel();
    ~PluginLookAndFeel() override;

    /** Adds this look's colours to the house ones. The base constructor calls the base
        version, so the constructor below calls this one as well. */
    void applyPalette() override;

    //==========================================================================
    // Popup menus.

    void drawPopupMenuBackground (juce::Graphics&, int width, int height) override;

    /** A menu has no shadow: the house draws its menus flat on the page, and a shadow is what makes a
        square menu look as though it floats above the page rather than belonging to it. */
    std::unique_ptr<juce::DropShadower> createDropShadowerForComponent (juce::Component&) override;
    int getPopupMenuBorderSizeWithOptions (const juce::PopupMenu::Options&) override;
    juce::Font getPopupMenuFont() override;

    void getIdealPopupMenuItemSize (const juce::String& text, bool isSeparator,
                                    int standardMenuItemHeight,
                                    int& idealWidth, int& idealHeight) override;

    void drawPopupMenuItem (juce::Graphics&, const juce::Rectangle<int>& area,
                            bool isSeparator, bool isActive, bool isHighlighted,
                            bool isTicked, bool hasSubMenu, const juce::String& text,
                            const juce::String& shortcutKeyText,
                            const juce::Drawable* icon,
                            const juce::Colour* textColour) override;

    void drawPopupMenuSectionHeader (juce::Graphics&, const juce::Rectangle<int>& area,
                                     const juce::String& sectionName) override;

    void getIdealPopupMenuSectionHeaderSizeWithOptions (const juce::String& text,
                                                        int standardMenuItemHeight,
                                                        int& idealWidth, int& idealHeight,
                                                        const juce::PopupMenu::Options&) override;

    //==========================================================================
    // Combo boxes and text fields.

    void drawComboBox (juce::Graphics&, int width, int height, bool isButtonDown,
                       int buttonX, int buttonY, int buttonW, int buttonH,
                       juce::ComboBox&) override;

    juce::Font getComboBoxFont (juce::ComboBox&) override;

    void fillTextEditorBackground (juce::Graphics&, int width, int height,
                                   juce::TextEditor&) override;

    void drawTextEditorOutline (juce::Graphics&, int width, int height,
                                juce::TextEditor&) override;

    /** A label that can be edited is a number, so it is set in the mono face. */
    juce::Font getLabelFont (juce::Label&) override;

    juce::Label* createSliderTextBox (juce::Slider&) override;

    //==========================================================================
    // Buttons.

    juce::Font getTextButtonFont (juce::TextButton&, int buttonHeight) override;

    void drawButtonText (juce::Graphics&, juce::TextButton&,
                         bool shouldDrawButtonAsHighlighted,
                         bool shouldDrawButtonAsDown) override;

    //==========================================================================
    // Scrollbars, tooltips, call-outs and alerts.

    void drawScrollbar (juce::Graphics&, juce::ScrollBar&, int x, int y, int width, int height,
                        bool isScrollbarVertical, int thumbStartPosition, int thumbSize,
                        bool isMouseOver, bool isMouseDown) override;

    void drawTooltip (juce::Graphics&, const juce::String& text, int width, int height) override;

    juce::Rectangle<int> getTooltipBounds (const juce::String& tipText,
                                           juce::Point<int> screenPosition,
                                           juce::Rectangle<int> parentArea) override;

    /** Square. The kit's call-out boxes are rounded by JUCE's own corner size, which
        this sets to nothing: the arrow stays, the corners do not. */
    float getCallOutBoxCornerSize (const juce::CallOutBox&) override;

    void drawCallOutBoxBackground (juce::CallOutBox&, juce::Graphics&,
                                   const juce::Path&, juce::Image&) override;

    juce::Font getAlertWindowTitleFont() override;
    juce::Font getAlertWindowMessageFont() override;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PluginLookAndFeel)
};
