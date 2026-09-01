#include "AuraLookAndFeel.h"

#include "EmbeddedAssets.h"
#include "Fonts.h"

using namespace Celine;

namespace
{
    /** How far text is lifted off the geometric centre, as a fraction of the font
        height.

        Not a fudge for a positioning bug. Jura's ascenders reach cap height and UI
        strings here carry no descenders, so what JUCE centres is the block from cap
        top to baseline, exactly — while what the eye centres on is the x-height
        mass, which sits in the lower half of that block. Arithmetically centred text
        therefore reads low in every button and dropdown. Zero restores JUCE's own
        centring. */
    constexpr float opticalRise = 0.09f;

    // The combo chevron's placement, shared between the two functions that need to
    // agree about it: where it is drawn, and how much room the text has beside it.
    constexpr float comboArrowInset = 16.0f;   // of its centre, from the right edge
    constexpr float comboArrowReach = 4.5f;

    int riseFor (const juce::Font& font) noexcept
    {
        return juce::roundToInt (font.getHeight() * opticalRise);
    }

    /** Half the shorter side, which is what makes a rectangle a pill rather than
        merely a rounded one: the short ends come out as full semicircles however the
        thumb is proportioned. */
    float pillRadius (juce::Rectangle<float> bounds) noexcept
    {
        return juce::jmin (bounds.getWidth(), bounds.getHeight()) * 0.5f;
    }
}

//==============================================================================
AuraLookAndFeel::AuraLookAndFeel()
{
    using namespace Theme;

    if (auto face = Fonts::typeface (Fonts::Weight::Light))
        setDefaultSansSerifTypeface (face);

    setColour (juce::ResizableWindow::backgroundColourId, chrome());
    setColour (juce::DocumentWindow::textColourId, text());

    setColour (juce::TextButton::buttonColourId, surface());
    setColour (juce::TextButton::buttonOnColourId, surfaceBright());
    setColour (juce::TextButton::textColourOffId, text());
    setColour (juce::TextButton::textColourOnId, text());

    setColour (juce::ToggleButton::textColourId, text());
    setColour (juce::ToggleButton::tickColourId, teal());
    setColour (juce::ToggleButton::tickDisabledColourId, line());

    setColour (juce::Label::textColourId, text());
    setColour (juce::Label::backgroundColourId, juce::Colours::transparentBlack);
    setColour (juce::Label::outlineColourId, juce::Colours::transparentBlack);

    setColour (juce::TextEditor::backgroundColourId, background());
    setColour (juce::TextEditor::textColourId, text());
    setColour (juce::TextEditor::highlightColourId, surfaceBright());
    setColour (juce::TextEditor::highlightedTextColourId, text());
    setColour (juce::TextEditor::outlineColourId, line());
    setColour (juce::TextEditor::focusedOutlineColourId, teal());
    setColour (juce::CaretComponent::caretColourId, text());

    setColour (juce::ComboBox::backgroundColourId, surface());
    setColour (juce::ComboBox::textColourId, text());
    setColour (juce::ComboBox::outlineColourId, line());
    setColour (juce::ComboBox::arrowColourId, textDim());
    setColour (juce::ComboBox::buttonColourId, surface());

    // A menu drops out of a toolbar button, so it belongs on the dark side of the
    // two-tone split — which is what surface() means when it says "dropdowns".
    setColour (juce::PopupMenu::backgroundColourId, surface());
    setColour (juce::PopupMenu::textColourId, text());
    setColour (juce::PopupMenu::headerTextColourId, comment());
    setColour (juce::PopupMenu::highlightedBackgroundColourId, surfaceBright());
    setColour (juce::PopupMenu::highlightedTextColourId, text());

    setColour (juce::Slider::rotarySliderFillColourId, teal());
    setColour (juce::Slider::rotarySliderOutlineColourId, surfaceBright());
    setColour (juce::Slider::thumbColourId, text());
    // The fill is the correction's own violet, and the groove is the graph's ground.
    setColour (juce::Slider::trackColourId, correction());
    setColour (juce::Slider::backgroundColourId, background());
    setColour (juce::Slider::textBoxTextColourId, text());
    setColour (juce::Slider::textBoxBackgroundColourId, juce::Colours::transparentBlack);
    setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    setColour (juce::Slider::textBoxHighlightColourId, surfaceBright());

    setColour (juce::ScrollBar::thumbColourId, line());
    setColour (juce::ScrollBar::trackColourId, juce::Colours::transparentBlack);

    setColour (juce::TooltipWindow::backgroundColourId, surface());
    setColour (juce::TooltipWindow::textColourId, text());
    setColour (juce::TooltipWindow::outlineColourId, line());

    setColour (juce::AlertWindow::backgroundColourId, chrome());
    setColour (juce::AlertWindow::textColourId, text());
    setColour (juce::AlertWindow::outlineColourId, line());

    // The knob face. Tinted once, at load, rather than per frame: it is the same
    // colour every time it is drawn, and a drawable copy per knob per repaint would
    // be allocation on the paint path.
    // The cap is a silhouette — the notch is a hole in it rather than a second
    // shape — so the tint decides which of the two you read. Celine tints it dark
    // because its control strip is the near-white panel; this one sits on chrome,
    // so the same drawing has to go the other way round: a light cap with the
    // chrome showing through the notch.
    knob = Assets::drawable ("knob.svg");

    if (knob != nullptr)
        Assets::tint (*knob, Theme::textDim());
}

AuraLookAndFeel::~AuraLookAndFeel() = default;

//==============================================================================
void AuraLookAndFeel::drawRotarySlider (juce::Graphics& g, int x, int y, int width, int height,
                                         float sliderPosProportional, float rotaryStartAngle,
                                         float rotaryEndAngle, juce::Slider& slider)
{
    if (knob == nullptr)
    {
        // No artwork: JUCE's own rotary beats drawing nothing.
        LookAndFeel_V4::drawRotarySlider (g, x, y, width, height, sliderPosProportional,
                                          rotaryStartAngle, rotaryEndAngle, slider);
        return;
    }

    const auto area = juce::Rectangle<int> (x, y, width, height).toFloat();

    // Square, because the cap is. A knob in a cell taller than it is wide would
    // otherwise be drawn as an ellipse.
    const auto side = juce::jmin (area.getWidth(), area.getHeight());
    const auto face = area.withSizeKeepingCentre (side, side);

    const auto angle = rotaryStartAngle + sliderPosProportional * (rotaryEndAngle - rotaryStartAngle);

    juce::Graphics::ScopedSaveState state (g);

    // The pointer is drawn straight up in the file, so rotating the whole cap about
    // its centre is the readout. No arc: the design has none, and inventing one
    // would be my design rather than the drawn one.
    g.addTransform (juce::AffineTransform::rotation (angle, face.getCentreX(), face.getCentreY()));
    knob->drawWithin (g, face, juce::RectanglePlacement::centred, 1.0f);
}

void AuraLookAndFeel::drawLinearSlider (juce::Graphics& g, int x, int y, int width, int height,
                                         float sliderPos, float minSliderPos, float maxSliderPos,
                                         juce::Slider::SliderStyle style, juce::Slider& slider)
{
    const auto full = juce::Rectangle<int> (x, y, width, height).toFloat();

    if (style == juce::Slider::LinearHorizontal)
    {
        constexpr float trackHeight = 6.0f;
        constexpr float thumbWidth = 14.0f;

        const auto track = full.withSizeKeepingCentre (full.getWidth() - thumbWidth, trackHeight);
        const auto position = juce::jlimit (track.getX(), track.getRight(), sliderPos);

        // The groove is the graph's own ground, whichever side of the two-tone split
        // the slider stands on: it reads as a channel cut into the surface rather than
        // as a gap left in it, and it is the same colour on the light panel as on the
        // dark surround so the two rows of controls agree.
        g.setColour (slider.findColour (juce::Slider::backgroundColourId));
        g.fillRoundedRectangle (track, trackHeight * 0.5f);
        g.setColour (Theme::line().withAlpha (0.25f));
        g.drawRoundedRectangle (track, trackHeight * 0.5f, Theme::borderWidth);

        const auto filled = track.withRight (position);

        if (filled.getWidth() > 1.0f)
        {
            g.setColour (slider.isEnabled() ? slider.findColour (juce::Slider::trackColourId)
                                            : Theme::textDisabled());
            g.fillRoundedRectangle (filled, trackHeight * 0.5f);
        }

        const auto thumb = juce::Rectangle<float> (thumbWidth, full.getHeight() * 0.62f)
                               .withCentre ({ position, full.getCentreY() });

        g.setColour (slider.findColour (juce::Slider::thumbColourId));
        g.fillRoundedRectangle (thumb, pillRadius (thumb));
        return;
    }

    if (style != juce::Slider::LinearVertical)
    {
        juce::LookAndFeel_V4::drawLinearSlider (g, x, y, width, height, sliderPos,
                                                minSliderPos, maxSliderPos, style, slider);
        return;
    }

    const auto bounds = full;
    constexpr float trackWidth = 6.0f;
    constexpr float thumbHeight = 14.0f;

    // Keep the thumb fully inside the component at both extremes.
    const auto track = bounds.withSizeKeepingCentre (trackWidth, bounds.getHeight() - thumbHeight);
    const auto position = juce::jlimit (track.getY(), track.getBottom(), sliderPos);

    g.setColour (slider.findColour (juce::Slider::backgroundColourId));
    g.fillRoundedRectangle (track, trackWidth * 0.5f);
    g.setColour (Theme::line().withAlpha (0.25f));
    g.drawRoundedRectangle (track, trackWidth * 0.5f, Theme::borderWidth);

    // A bipolar parameter fills out from its zero point in whichever direction it
    // has been pushed, so the sign is visible at a glance; everything else fills up
    // from the bottom like a mix fader.
    const auto bipolar = slider.getMinimum() < 0.0 && slider.getMaximum() > 0.0;

    auto origin = track.getBottom();

    if (bipolar)
    {
        const auto span = slider.getMaximum() - slider.getMinimum();
        const auto zero = (float) ((0.0 - slider.getMinimum()) / span);
        origin = track.getBottom() - zero * track.getHeight();

        g.setColour (Theme::line().withAlpha (0.5f));
        g.drawHorizontalLine ((int) origin, track.getX() - 4.0f, track.getRight() + 4.0f);
    }

    const auto filled = juce::Rectangle<float> (track.getX(), std::min (position, origin),
                                                track.getWidth(), std::abs (position - origin));

    if (filled.getHeight() > 1.0f)
    {
        g.setColour (slider.isEnabled() ? slider.findColour (juce::Slider::trackColourId)
                                        : Theme::textDisabled());
        g.fillRoundedRectangle (filled, trackWidth * 0.5f);
    }

    // Capped rather than a flat fraction of the column, so this comes out the same
    // compact pill the bottom row wears: the two faders sit in a column much wider
    // than that row is tall, and 62% of it made a slab where the other is a lozenge.
    const auto thumbWidth = juce::jmin (bounds.getWidth() * 0.62f, thumbHeight * 1.3f);

    const auto thumb = juce::Rectangle<float> (thumbWidth, thumbHeight)
                           .withCentre ({ bounds.getCentreX(), position });

    g.setColour (slider.findColour (juce::Slider::thumbColourId));
    g.fillRoundedRectangle (thumb, pillRadius (thumb));
}

//==============================================================================
void AuraLookAndFeel::drawToggleButton (juce::Graphics& g, juce::ToggleButton& button,
                                         bool shouldDrawButtonAsHighlighted,
                                         bool shouldDrawButtonAsDown)
{
    if (! button.getProperties().contains (pillSwitchProperty))
    {
        LookAndFeel_V4::drawToggleButton (g, button, shouldDrawButtonAsHighlighted,
                                          shouldDrawButtonAsDown);
        return;
    }

    juce::ignoreUnused (shouldDrawButtonAsDown);

    // A pill with a travelling dot, which is what the design draws wherever a switch
    // appears. JUCE's tick box would be the only square-cornered, unfilled control
    // in the window.
    const auto bounds = button.getLocalBounds().toFloat();
    const bool on = button.getToggleState();

    const float height = juce::jmin (bounds.getHeight(), 20.0f);
    const float width = height * 2.8f;
    const bool labelled = button.getButtonText().isNotEmpty();

    const auto pill = juce::Rectangle<float> (width, height)
                          .withPosition (labelled ? bounds.getX() : bounds.getCentreX() - width * 0.5f,
                                         bounds.getCentreY() - height * 0.5f);

    g.setColour (on ? Theme::teal() : Theme::surfaceBright());
    g.fillRoundedRectangle (pill, height * 0.5f);

    if (shouldDrawButtonAsHighlighted)
    {
        g.setColour (Theme::text().withAlpha (0.12f));
        g.fillRoundedRectangle (pill, height * 0.5f);
    }

    // The dot is the throw, and it sits on the side that says which one is made.
    const float inset = 1.5f;
    const float dot = height - inset * 2.0f;
    const float travel = pill.getWidth() - dot - inset * 2.0f;
    const float dotX = pill.getX() + inset + (on ? travel : 0.0f);

    g.setColour (Theme::text());
    g.fillEllipse (juce::Rectangle<float> (dot, dot).withPosition (dotX, pill.getY() + inset));

    if (labelled)
    {
        g.setColour (button.findColour (juce::ToggleButton::textColourId));
        g.setFont (Fonts::light (13.0f));
        g.drawText (button.getButtonText(), bounds.withTrimmedLeft (pill.getWidth() + 8.0f),
                    juce::Justification::centredLeft, true);
    }
}

void AuraLookAndFeel::drawButtonBackground (juce::Graphics& g, juce::Button& button,
                                             const juce::Colour& backgroundColour,
                                             bool shouldDrawButtonAsHighlighted,
                                             bool shouldDrawButtonAsDown)
{
    const auto bounds = button.getLocalBounds().toFloat().reduced (Theme::borderWidth * 0.5f);

    auto fill = backgroundColour;

    if (shouldDrawButtonAsDown || shouldDrawButtonAsHighlighted)
        fill = fill.overlaidWith (Theme::text().withAlpha (shouldDrawButtonAsDown ? 0.16f : 0.08f));

    if (! button.isEnabled())
        fill = fill.withMultipliedAlpha (0.5f);

    // Fill only. Every button this draws is filled with a colour that already
    // separates it from what it stands on — the actions in their red, Export in the
    // correction's purple — so a rule around it drew a second edge where one was
    // doing the job. Note that this reaches the editor's buttons only: the About
    // dialog is a desktop window and inherits no look and feel from it, and the
    // toolbar's icons are IconButtons, which paint themselves.
    g.setColour (fill);
    g.fillRoundedRectangle (bounds, Theme::cornerRadius);
}

void AuraLookAndFeel::drawButtonText (juce::Graphics& g, juce::TextButton& button, bool, bool)
{
    const auto font = getTextButtonFont (button, button.getHeight());
    g.setFont (font);
    g.setColour (button.findColour (button.getToggleState() ? juce::TextButton::textColourOnId
                                                            : juce::TextButton::textColourOffId)
                     .withMultipliedAlpha (button.isEnabled() ? 1.0f : 0.5f));

    // Room for the rounded ends, then the same lift the dropdowns get.
    const int margin = juce::jmin (6, button.getWidth() / 6);

    g.drawFittedText (button.getButtonText(),
                      button.getLocalBounds().reduced (margin, 0).translated (0, -riseFor (font)),
                      juce::Justification::centred, 2);
}

//==============================================================================
void AuraLookAndFeel::drawComboBox (juce::Graphics& g, int width, int height, bool isButtonDown,
                                     int, int, int, int, juce::ComboBox& box)
{
    const auto bounds = juce::Rectangle<float> (0.0f, 0.0f, (float) width, (float) height)
                            .reduced (Theme::borderWidth * 0.5f);

    const auto outline = box.findColour (juce::ComboBox::outlineColourId);

    g.setColour (box.findColour (juce::ComboBox::backgroundColourId));
    g.fillRoundedRectangle (bounds, Theme::cornerRadius);

    // With no border to darken, the open state is shown by this wash alone, so it is
    // worth a little more than it was when it only had to tint an outlined box.
    if (isButtonDown)
    {
        g.setColour (outline.withAlpha (0.22f));
        g.fillRoundedRectangle (bounds, Theme::cornerRadius);
    }

    // Unoutlined, like the buttons: the phase box stands beside Export on the same
    // panel and in the same purple, and a rule on one of the pair and not the other
    // read as an oversight rather than a distinction.
    //
    // A chevron, drawn rather than JUCE's filled triangle: the design's arrow is two
    // strokes, and it has to match the weight of the border it sits in.
    const auto centre = juce::Point<float> (bounds.getRight() - comboArrowInset, bounds.getCentreY());
    constexpr float reach = comboArrowReach;

    juce::Path chevron;
    chevron.startNewSubPath (centre.x - reach, centre.y - reach * 0.55f);
    chevron.lineTo (centre.x, centre.y + reach * 0.55f);
    chevron.lineTo (centre.x + reach, centre.y - reach * 0.55f);

    g.setColour (box.findColour (juce::ComboBox::arrowColourId));
    g.strokePath (chevron, juce::PathStrokeType (2.0f, juce::PathStrokeType::curved,
                                                 juce::PathStrokeType::rounded));
}

void AuraLookAndFeel::drawCallOutBoxBackground (juce::CallOutBox& box, juce::Graphics& g,
                                                const juce::Path& path, juce::Image& cachedImage)
{
    // The shadow is cached because it is a blur over the whole bubble and the bubble
    // does not change shape once it is up — this is JUCE's own arrangement for it, and
    // the reason the image is passed in by reference.
    if (cachedImage.isNull())
    {
        cachedImage = { juce::Image::ARGB, box.getWidth(), box.getHeight(), true };
        juce::Graphics shadow (cachedImage);
        juce::DropShadow (juce::Colours::black.withAlpha (0.55f), 9, { 0, 3 }).drawForPath (shadow, path);
    }

    g.setColour (juce::Colours::black);
    g.drawImageAt (cachedImage, 0, 0);

    // The surround the graph and the faders stand on, which is the darkest thing in
    // the window. Chrome was the obvious choice for something floating above the
    // window, but chrome is the aubergine of the toolbar and read as purple rather
    // than as dark; this reads as a hole cut through to the same ground the rest of
    // the plugin sits on.
    g.setColour (Theme::consoleBackground());
    g.fillPath (path);

    g.setColour (Theme::line().withAlpha (0.2f));
    g.strokePath (path, juce::PathStrokeType (Theme::borderWidth));
}

void AuraLookAndFeel::positionComboBoxText (juce::ComboBox& box, juce::Label& label)
{
    // The text is centred in the run from the left edge to where the chevron starts,
    // not in the box. Mirroring the chevron's inset on the left instead centres it in
    // the whole box, which sounds right and looks wrong: the chevron is then the only
    // thing in the right margin, so "Minimum" sat 28px from the left border and 8px
    // from the arrow. Balancing it against what is actually beside it puts equal air
    // on both sides of the word.
    label.setBounds (0, 1 - riseFor (getComboBoxFont (box)),
                     juce::roundToInt ((float) box.getWidth() - comboArrowInset - comboArrowReach),
                     box.getHeight() - 2);
    label.setFont (getComboBoxFont (box));
    label.setJustificationType (juce::Justification::centred);
}

//==============================================================================
juce::Font AuraLookAndFeel::getTextButtonFont (juce::TextButton&, int buttonHeight)
{
    return Fonts::light (juce::jmin (16.0f, (float) buttonHeight * 0.5f));
}

juce::Font AuraLookAndFeel::getComboBoxFont (juce::ComboBox& box)
{
    return Fonts::light (juce::jmin (15.0f, (float) box.getHeight() * 0.55f));
}

juce::Font AuraLookAndFeel::getPopupMenuFont() { return Fonts::light (15.0f); }

void AuraLookAndFeel::drawPopupMenuSectionHeader (juce::Graphics& g,
                                                   const juce::Rectangle<int>& area,
                                                   const juce::String& sectionName)
{
    g.setFont (Fonts::bold (13.0f));
    g.setColour (findColour (juce::PopupMenu::headerTextColourId));

    auto r = area.reduced (1);
    r.reduce (juce::jmin (5, area.getWidth() / 20), 0);
    r.removeFromRight (3);

    g.drawFittedText (sectionName, r, juce::Justification::centredLeft, 1);
}

juce::Font AuraLookAndFeel::getLabelFont (juce::Label& label)
{
    // A label that has asked for a particular face keeps it.
    if (label.getProperties().contains (keepFontProperty))
        return label.getFont();

    // Otherwise the label keeps the size it was given and gets the design's face
    // whatever it asked for. Blunt on purpose: this is the net under every label in
    // the window, including the ones JUCE makes for itself inside a combo box or a
    // slider.
    return Fonts::light (label.getFont().getHeight());
}

juce::Font AuraLookAndFeel::getAlertWindowTitleFont() { return Fonts::bold (17.0f); }
juce::Font AuraLookAndFeel::getAlertWindowMessageFont() { return Fonts::light (15.0f); }

juce::Label* AuraLookAndFeel::createSliderTextBox (juce::Slider& slider)
{
    auto* label = LookAndFeel_V4::createSliderTextBox (slider);

    label->setFont (Fonts::light (13.0f));
    label->setJustificationType (juce::Justification::centred);

    // Set on the label rather than the look and feel: the first text box is built
    // while the slider still has the default look and feel attached.
    label->setColour (juce::Label::backgroundColourId, juce::Colours::transparentBlack);
    label->setColour (juce::Label::outlineColourId, juce::Colours::transparentBlack);
    label->setColour (juce::Label::textColourId, Theme::text());
    label->setColour (juce::Label::outlineWhenEditingColourId, Theme::teal());

    return label;
}
