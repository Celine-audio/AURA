#include "Controls.h"

#include <cmath>

namespace Ui
{
    namespace
    {

        /** The gap between the buttons of a segmented row. */
        constexpr float gap = 2.0f;

        /** The ink of a selected segment: the accent is the fill, so the words are white. */
        const juce::Colour selectedText { 0xffffffff };

        /** How far the pointer is held to the field's text on each side. */
        constexpr float textInset = 6.0f;
    }

    void drawField (juce::Graphics& g, juce::Rectangle<float> area, bool hovered, bool active, bool dimmed)
    {
        juce::ignoreUnused (dimmed);

        // A field is a flat box: the fill lifts under the pointer and the rule says it can be
        // changed. A square, so there is no corner to round and nothing to shadow.
        g.setColour (Theme::underPointer (Theme::phControl(), hovered, active));
        g.fillRect (area);

        g.setColour (active ? Theme::accent() : Theme::phRule());
        g.drawRect (area.reduced (0.5f), 1.0f);
    }

    void drawField (juce::Graphics& g, juce::Rectangle<float> area, bool hovered, bool active, juce::Colour rule)
    {
        g.setColour (Theme::underPointer (Theme::phControl(), hovered, active));
        g.fillRect (area);

        g.setColour (active ? rule : Theme::phRule());
        g.drawRect (area.reduced (0.5f), 1.0f);
    }

    void drawChevron (juce::Graphics& g, juce::Point<float> centre, juce::Colour colour)
    {
        juce::Path chevron;
        chevron.startNewSubPath (centre.translated (-2.7f, -1.35f));
        chevron.lineTo (centre.translated (0.0f, 1.35f));
        chevron.lineTo (centre.translated (2.7f, -1.35f));

        g.setColour (colour);
        g.strokePath (chevron, juce::PathStrokeType (1.35f));
    }

    juce::Colour choiceFill (bool selected, bool hovered, bool down)
    {
        return Theme::underPointer (selected ? Theme::accent() : Theme::phControl(), hovered, down);
    }

    //==========================================================================
    Segments::Segments()
    {
        setWantsKeyboardFocus (false);
        setMouseClickGrabsKeyboardFocus (false);
    }

    void Segments::setOptions (const juce::StringArray& labels)
    {
        options = labels;
        repaint();
    }

    void Segments::setPrefix (const juce::String& prefix, int width)
    {
        prefixText = prefix;
        prefixWidth = width;
        repaint();
    }

    void Segments::setStyle (bool monoText, float textHeight)
    {
        mono = monoText;
        height = textHeight;
        repaint();
    }

    void Segments::setVertical (bool shouldBeVertical)
    {
        vertical = shouldBeVertical;
        repaint();
    }

    void Segments::setSelected (int index)
    {
        if (index != selected)
        {
            selected = index;
            repaint();
        }
    }

    void Segments::setLocked (bool shouldBeLocked)
    {
        if (locked != shouldBeLocked)
        {
            locked = shouldBeLocked;
            setMouseCursor (locked ? juce::MouseCursor::NormalCursor : juce::MouseCursor::PointingHandCursor);
            repaint();
        }
    }

    juce::Rectangle<float> Segments::buttonBounds (int index) const
    {
        auto area = getLocalBounds().toFloat();
        area.removeFromLeft ((float) prefixWidth);

        const auto n = (float) juce::jmax (1, options.size());

        if (vertical)
        {
            const auto h = (area.getHeight() - gap * (n - 1.0f)) / n;
            return { area.getX(), area.getY() + (float) index * (h + gap), area.getWidth(), h };
        }

        const auto w = (area.getWidth() - gap * (n - 1.0f)) / n;
        return { area.getX() + (float) index * (w + gap), area.getY(), w, area.getHeight() };
    }

    int Segments::indexAt (juce::Point<float> p) const
    {
        for (int i = 0; i < options.size(); ++i)
            if (buttonBounds (i).contains (p))
                return i;

        return -1;
    }

    void Segments::paint (juce::Graphics& g)
    {
        if (locked)
            g.beginTransparencyLayer (0.35f);

        if (prefixWidth > 0)
            text (g, prefixText, getLocalBounds().toFloat().withWidth ((float) prefixWidth),
                  Ui::mono (9.0f), Theme::phFaint());

        for (int i = 0; i < options.size(); ++i)
        {
            const auto area = buttonBounds (i);
            const auto isSelected = i == selected;
            const auto isHovered = ! locked && i == hovered;

            auto fill = choiceFill (isSelected, isHovered, ! locked && i == pressed);

            // Locked and selected: the grey a step up from the control, because the accent
            // says "you chose this" and nobody did.
            if (locked && isSelected)
                fill = Theme::phControl().brighter (0.25f);

            g.setColour (fill);
            g.fillRect (area);

            // The selected one a step heavier than the rest, not two: its fill already says it is chosen.
            const auto font = mono ? Ui::mono (height)
                                   : (isSelected ? bold (height) : medium (height));
            const auto colour = isSelected ? (locked ? Theme::phInk() : selectedText) : Theme::phDim();

            text (g, options[i], area, font, colour, juce::Justification::centred);
        }

        if (locked)
            g.endTransparencyLayer();
    }

    void Segments::mouseMove (const juce::MouseEvent& e)
    {
        const auto index = indexAt (e.position);

        if (index != hovered)
        {
            hovered = index;
            repaint();
        }
    }

    void Segments::mouseExit (const juce::MouseEvent& e)
    {
        hovered = -1;

        // A press is the mouse's until it is released, and JUCE keeps delivering the release to
        // this component even outside it; so a press survives leaving only when no button is down.
        if (! e.mods.isAnyMouseButtonDown())
            pressed = -1;

        repaint();
    }

    void Segments::mouseDown (const juce::MouseEvent& e)
    {
        if (locked)
            return;

        pressed = indexAt (e.position);
        repaint();
    }

    void Segments::mouseUp (const juce::MouseEvent& e)
    {
        const auto index = indexAt (e.position);
        const auto clicked = ! locked && index >= 0 && index == pressed;
        pressed = -1;
        repaint();

        if (clicked && onSelect)
            onSelect (index);
    }

    void Segments::visibilityChanged()
    {
        // Hidden under the pointer, the release never comes: nothing it was doing may outlive it.
        if (! isVisible())
        {
            hovered = -1;
            pressed = -1;
        }

        repaint();
    }

    void Segments::enablementChanged()
    {
        if (! isEnabled())
        {
            hovered = -1;
            pressed = -1;
        }

        repaint();
    }

    juce::String Segments::getTooltip()
    {
        if (hovered >= 0 && hovered < optionTips.size() && optionTips[hovered].isNotEmpty())
            return optionTips[hovered];

        return SettableTooltipClient::getTooltip();
    }

    //==========================================================================
    Toggle::Toggle (const juce::String& name) : juce::Button (name)
    {
        setWantsKeyboardFocus (false);
        setMouseClickGrabsKeyboardFocus (false);
    }

    void Toggle::setGlyph (const juce::String& newGlyph)
    {
        if (glyph != newGlyph)
        {
            glyph = newGlyph;
            repaint();
        }
    }

    void Toggle::setDimmed (bool shouldBeDimmed)
    {
        if (dimmed != shouldBeDimmed)
        {
            dimmed = shouldBeDimmed;
            repaint();
        }
    }

    void Toggle::paintButton (juce::Graphics& g, bool hovered, bool down)
    {
        const auto bounds = getLocalBounds().toFloat();
        const auto lit = getToggleState();

        // Lit is the accent: on the glyph and on the box's rule. The box stays, so the switch
        // is a cell like the values beside it; the glyph is the only thing that changes.
        // Disabled, the whole cell is drawn at half, like every disabled control here.
        if (! isEnabled())
            g.beginTransparencyLayer (0.45f);

        if (prominent)
        {
            g.setColour (Theme::underPointer (Theme::phTrack(), hovered && isEnabled(), false));
            g.fillRect (bounds);

            if (lit)
            {
                g.setColour (litColour().withAlpha (0.18f));
                g.fillRect (bounds);
            }

            g.setColour (lit ? litColour() : Theme::phTrack().brighter (0.15f));
            g.drawRect (bounds.reduced (0.5f), 1.0f);
        }
        else
        {
            drawField (g, bounds, hovered && isEnabled(), lit, litColour());
        }

        if (glyph.isNotEmpty())
        {
            const auto unlit = prominent ? Theme::phInk() : Theme::phDim();
            const auto ink = lit ? (prominent ? litColour().brighter (0.3f) : litColour())
                                 : (dimmed ? Theme::phFaint() : unlit);
            Ui::text (g, glyph, bounds, medium (9.0f), ink, juce::Justification::centred);
        }

        if (! isEnabled())
            g.endTransparencyLayer();

        juce::ignoreUnused (down);
    }

    void Toggle::setProminent (bool shouldBeProminent)
    {
        if (prominent != shouldBeProminent)
        {
            prominent = shouldBeProminent;
            repaint();
        }
    }

    void Toggle::setLitRole (Theme::Role role)
    {
        litRole = role;
        repaint();
    }

    void Toggle::clearLitRole()
    {
        litRole.reset();
        repaint();
    }

    juce::Colour Toggle::litColour() const
    {
        return litRole.has_value() ? Theme::colour (*litRole) : Theme::accent();
    }

    void Toggle::visibilityChanged()
    {
        if (! isVisible())
            setState (juce::Button::buttonNormal);

        repaint();
    }

    void Toggle::enablementChanged()
    {
        if (! isEnabled())
            setState (juce::Button::buttonNormal);

        repaint();
    }

    //==========================================================================
    ParameterBox::ParameterBox (juce::RangedAudioParameter& p, const juce::String& unitText)
        : parameter (p), unit (unitText)
    {
        setMouseCursor (juce::MouseCursor::UpDownResizeCursor);
        setTitle (p.getName (64));
        setWantsKeyboardFocus (false);
        setMouseClickGrabsKeyboardFocus (false);

        // Called on the message thread whatever moved the parameter, automation included.
        attachment = std::make_unique<juce::ParameterAttachment> (parameter, [this] (float value)
        {
            shown = value;

            if (! dragging)
                exact = parameter.convertTo0to1 (value);

            repaint();

            if (onValueShown)
                onValueShown();
        }, nullptr);

        attachment->sendInitialUpdate();
    }

    ParameterBox::~ParameterBox()
    {
        endDrag();
    }

    void ParameterBox::setDimmed (bool shouldBeDimmed)
    {
        if (dimmed != shouldBeDimmed)
        {
            dimmed = shouldBeDimmed;
            repaint();
        }
    }

    void ParameterBox::paint (juce::Graphics& g)
    {
        const auto bounds = getLocalBounds().toFloat();

        if (! isEnabled())
            g.beginTransparencyLayer (0.45f);

        drawField (g, bounds, isEnabled() && isMouseOver (true), dragging, dimmed);

        // The parameter's own words for its value, so the field says what a host would; the unit in
        // capitals after it, as every mono value on the page is written.
        auto words = parameter.getText (parameter.convertTo0to1 (shown), 16).toUpperCase();

        // A value said as a word -- Smoothing's OFF -- takes no unit.
        if (unit.isNotEmpty() && words.containsAnyOf ("0123456789") && ! words.endsWithIgnoreCase (unit))
            words << " " << unit.toUpperCase();

        text (g, words, bounds.reduced (textInset, 0.0f), mono (9.5f), dimmed ? Theme::phFaint() : Theme::phInk(),
              juce::Justification::centred);

        if (! isEnabled())
            g.endTransparencyLayer();
    }

    void ParameterBox::mouseDown (const juce::MouseEvent& e)
    {
        lastY = e.position.y;
        exact = parameter.getValue();
        dragging = true;
        attachment->beginGesture();
        repaint();
    }

    void ParameterBox::mouseDrag (const juce::MouseEvent& e)
    {
        if (! dragging)
            return;

        // A pixel is a hundred-and-sixtieth of the range, PHONICS's number box; shift a tenth of that.
        const auto fine = e.mods.isShiftDown() ? 0.1f : 1.0f;
        exact = juce::jlimit (0.0f, 1.0f, exact + (lastY - e.position.y) / 160.0f * fine);
        lastY = e.position.y;

        attachment->setValueAsPartOfGesture (parameter.convertFrom0to1 (exact));
    }

    void ParameterBox::mouseUp (const juce::MouseEvent&)
    {
        endDrag();
    }

    void ParameterBox::mouseExit (const juce::MouseEvent& e)
    {
        // A drag that leaves is still the user's, and ends at its release.
        if (! e.mods.isAnyMouseButtonDown())
            endDrag();

        repaint();
    }

    void ParameterBox::mouseDoubleClick (const juce::MouseEvent&)
    {
        attachment->setValueAsCompleteGesture (parameter.convertFrom0to1 (parameter.getDefaultValue()));
    }

    void ParameterBox::endDrag()
    {
        // The host's gesture is closed with the box's own state, so a drag cut off by the box
        // being hidden cannot leave the host thinking the parameter is still moving.
        if (! dragging)
            return;

        dragging = false;
        attachment->endGesture();
        repaint();
    }

    void ParameterBox::visibilityChanged()
    {
        if (! isVisible())
            endDrag();
    }

    void ParameterBox::enablementChanged()
    {
        if (! isEnabled())
            endDrag();

        repaint();
    }

    //==========================================================================
    namespace Sheet
    {
        void drawLabel (juce::Graphics& g, juce::Rectangle<float> area, const juce::String& label)
        {
            text (g, label, area, spaced (medium (7.5f), 1.0f), Theme::phLabel());
        }

        void drawHint (juce::Graphics& g, juce::Rectangle<float> band, float x, float width, const juce::String& hint)
        {
            textOnCaps (g, hint, { x, band.getY(), width, band.getHeight() - 1.0f }, spaced (medium (8.0f), 1.0f), Theme::phFaint(),
                        juce::Justification::centred);
        }

        void drawHeading (juce::Graphics& g, juce::Rectangle<float> area, const juce::String& heading)
        {
            // A heading takes its own band and a rule under it: the rule is what separates one group
            // of rows from the next, so the groups need no gap between them.
            // On its capitals, in the band above the rule, so a column's name set in the same band
            // (`drawHint`) sits on the same line.
            textOnCaps (g, heading, area.withTrimmedBottom (1.0f), spaced (medium (8.0f), 1.2f), Theme::phLabel());

            g.setColour (Theme::phRule());
            g.fillRect (juce::Rectangle<float> (area.getX(), area.getBottom() - 1.0f, area.getWidth(), 1.0f));
        }
    }

    //==========================================================================
    OutputFader::OutputFader (juce::AudioProcessorValueTreeState& state, const char* parameterID)
    {
        auto& parameter = *state.getParameter (parameterID);

        track.setSliderStyle (juce::Slider::LinearHorizontal);
        track.setTextBoxStyle (juce::Slider::NoTextBox, true, 0, 0);
        track.setDoubleClickReturnValue (true, parameter.convertFrom0to1 (parameter.getDefaultValue()));
        track.setTitle ("Output");
        track.setTooltip ("Output trim. Double-click for 0 dB.");
        track.onValueChange = [this] { repaint(); };
        addAndMakeVisible (track);

        attachment = std::make_unique<juce::SliderParameterAttachment> (parameter, track);
    }

    void OutputFader::resized()
    {
        // The label and the readout as wide as their text at the page's type size -- the readout
        // as wide as its widest value, "-12.0 dB" -- and the track the rest of the house's width.
        auto area = getLocalBounds().toFloat();
        const auto labelWidth = juce::GlyphArrangement::getStringWidth (spaced (medium (9.0f), 1.2f), "OUTPUT") + 2.0f;
        const auto valueWidth = juce::GlyphArrangement::getStringWidth (mono (11.0f), "-12.0 dB") + 2.0f;
        labelArea = area.removeFromLeft (labelWidth);
        area.removeFromLeft (10.0f);
        valueArea = area.removeFromRight (valueWidth);
        area.removeFromRight (10.0f);
        track.setBounds (area.toNearestInt());
    }

    void OutputFader::paint (juce::Graphics& g)
    {
        text (g, "OUTPUT", labelArea, spaced (medium (9.0f), 1.2f), Theme::phLabel(), juce::Justification::centredRight);

        const auto value = track.getValue();
        text (g, juce::String (value, 1) + " dB", valueArea, mono (11.0f), Theme::phInk());
    }

    void OutputFader::Track::paint (juce::Graphics& g)
    {
        const auto bounds = getLocalBounds().toFloat();
        const auto bar = bounds.withSizeKeepingCentre (bounds.getWidth(), 4.0f);

        g.setColour (Theme::underPointer (Theme::phControl(), isMouseOverOrDragging(), false));
        g.fillRect (bar);

        const auto x = bounds.getX() + (float) valueToProportionOfLength (getValue()) * bounds.getWidth();
        const auto zero = bounds.getX() + (float) valueToProportionOfLength (0.0) * bounds.getWidth();

        g.setColour (Theme::accent());
        g.fillRect (juce::Rectangle<float>::leftTopRightBottom (juce::jmin (x, zero), bar.getY(),
                                                                juce::jmax (x, zero), bar.getBottom()));

        g.setColour (Theme::phInk());
        g.fillRect (juce::Rectangle<float> (2.0f, 18.0f).withCentre ({ juce::jlimit (1.0f, bounds.getRight() - 1.0f, x),
                                                                      bounds.getCentreY() }));
    }

}
