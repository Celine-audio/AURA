#pragma once

#include "Style.h"

#include <juce_audio_processors/juce_audio_processors.h>

#include <functional>
#include <memory>
#include <optional>

/**
    The page's small controls, each drawn the one way the design draws it: square corners, the
    accent for what is selected or being touched, and the greys for everything else.

    **Editable and information are drawn differently, and that is the point.** An editable value
    sits in a field -- a box with a rule round it -- so it is visibly something you can grab. A
    readout that cannot be changed is plain text with no box at all. Nothing is a box by accident.

    **Press state is never kept past its gesture.** A control is "held" from its mouse-down until
    its mouse-up, and a held control whose mouse-up never arrives (because it was hidden or moved
    under the pointer by the zoom) would stay lit until somebody clicked it again. So every control
    here clears its held and hovered state when it is hidden, disabled or left, and none of them
    takes keyboard focus, which is the other way a control stays lit after it has been used.

    None of these owns a parameter's state. Choices and values are set from `refresh()` by the page
    and report an edit through a callback, so the page is the one place that writes to the processor.
*/
namespace Ui
{
    /** The box a value sits in. Its fill lifts under the pointer; its rule turns to the accent while
        the value is being changed or is switched on. Drawn inside `area`, which the caller has
        already inset by half a cell so that two fields are a cell's width apart. */
    void drawField (juce::Graphics&, juce::Rectangle<float> area, bool hovered, bool active, bool dimmed = false);

    /** The same box with its active rule in `rule` rather than the accent: a switch that lights in
        a colour of its own -- LEARN in the recording red, MATCH in the out-of-date orange. */
    void drawField (juce::Graphics&, juce::Rectangle<float> area, bool hovered, bool active, juce::Colour rule);

    //==========================================================================
    /** A row (or column) of buttons, one of which is selected. */
    class Segments : public juce::Component,
                     public juce::SettableTooltipClient
    {
    public:
        Segments();

        void setOptions (const juce::StringArray& labels);

        /** A short label in front of the buttons, in the faint mono: "DYN", "PORT". */
        void setPrefix (const juce::String& prefix, int width);

        /** Mono numerals rather than Inter capitals, and the height of either. */
        void setStyle (bool monoText, float textHeight);

        void setVertical (bool shouldBeVertical);

        /** -1 for none. */
        void setSelected (int index);
        int getSelected() const noexcept { return selected; }

        /** A locked row is drawn at a third and does not answer; its selection is still
            shown, in grey, because "this is what it is, and you cannot change it here"
            is the message. */
        void setLocked (bool shouldBeLocked);

        /** Per-button tooltips, by index. */
        void setOptionTooltips (const juce::StringArray& tips) { optionTips = tips; }

        std::function<void (int)> onSelect;

        void paint (juce::Graphics&) override;
        void mouseMove (const juce::MouseEvent&) override;
        void mouseExit (const juce::MouseEvent&) override;
        void mouseDown (const juce::MouseEvent&) override;
        void mouseUp (const juce::MouseEvent&) override;
        void visibilityChanged() override;
        void enablementChanged() override;
        juce::String getTooltip() override;

    private:
        juce::Rectangle<float> buttonBounds (int index) const;
        int indexAt (juce::Point<float>) const;

        juce::StringArray options, optionTips;
        juce::String prefixText;
        int prefixWidth = 0;
        bool mono = false;
        float height = 11.0f;
        bool vertical = false;
        bool locked = false;
        int selected = -1;
        int hovered = -1;
        int pressed = -1;
    };

    //==========================================================================
    /** A switch drawn as a field: the glyph it is given ("ON", "Ø") in the field's ink, lit in the
        accent when it is on. There is no inner box, so the switch and the value cells in its row
        are the same kind of thing. */
    class Toggle : public juce::Button
    {
    public:
        explicit Toggle (const juce::String& name);

        void setGlyph (const juce::String& glyph);
        void setDimmed (bool shouldBeDimmed);

        /** What it lights in while on, as a role so a theme change reaches it: the accent unless
            told otherwise. AURA's LEARN lights in `record`, as anything recording does. */
        void setLitRole (Celine::Theme::Role role);
        void clearLitRole();

        void paintButton (juce::Graphics&, bool shouldDrawButtonAsHighlighted, bool shouldDrawButtonAsDown) override;

        void visibilityChanged() override;
        void enablementChanged() override;

    private:
        juce::Colour litColour() const;

        juce::String glyph;
        bool dimmed = false;
        std::optional<Celine::Theme::Role> litRole;
    };

    //==========================================================================
    /** A parameter's value in a field, dragged like PHONICS's number boxes: up to raise it, down to
        lower it, shift for a tenth of the step, a double-click back to its default. AURA's own,
        because its settings are skewed and stepped -- Smoothing is not linear in octaves, the
        Window is in half seconds -- so the drag runs in the parameter's own normalised range and
        the field shows the parameter's own text. A drag is one host gesture. */
    class ParameterBox : public juce::Component,
                         public juce::SettableTooltipClient
    {
    public:
        ParameterBox (juce::RangedAudioParameter&, const juce::String& unit = {});
        ~ParameterBox() override;

        void setDimmed (bool shouldBeDimmed);

        /** Called as the value changes, from whatever moved it. */
        std::function<void()> onValueShown;

        void paint (juce::Graphics&) override;
        void mouseDown (const juce::MouseEvent&) override;
        void mouseDrag (const juce::MouseEvent&) override;
        void mouseUp (const juce::MouseEvent&) override;
        void mouseExit (const juce::MouseEvent&) override;
        void mouseDoubleClick (const juce::MouseEvent&) override;
        void visibilityChanged() override;
        void enablementChanged() override;

    private:
        void endDrag();

        juce::RangedAudioParameter& parameter;
        juce::String unit;
        std::unique_ptr<juce::ParameterAttachment> attachment;
        float shown = 0.0f, exact = 0.0f, lastY = 0.0f;
        bool dragging = false, dimmed = false;
    };

    //==========================================================================
    /** The drawing the sheets share: a row's label and a cell's ground. */
    namespace Sheet
    {
        /** A row's label, in the tiny capitals the design writes labels in, left-aligned. */
        void drawLabel (juce::Graphics&, juce::Rectangle<float> area, const juce::String& label);

        /** A section's heading: small capitals over a rule. */
        void drawHeading (juce::Graphics&, juce::Rectangle<float> area, const juce::String& heading);

        /** A column's name in a heading's band (`band`, the heading's own area), over the field at
            `x` and `width`: centred on the same capitals as the heading, so the two read as one line. */
        void drawHint (juce::Graphics&, juce::Rectangle<float> band, float x, float width, const juce::String& hint);
    }

    //==========================================================================
    /** The toolbar's output trim: a label, a thin horizontal track filled from the centre,
        and the value. */
    class OutputFader : public juce::Component
    {
    public:
        OutputFader (juce::AudioProcessorValueTreeState&, const char* parameterID);

        void paint (juce::Graphics&) override;
        void resized() override;

        static constexpr int preferredWidth = 254;

    private:
        struct Track : juce::Slider
        {
            void paint (juce::Graphics&) override;
        };

        Track track;
        std::unique_ptr<juce::SliderParameterAttachment> attachment;
        juce::Rectangle<float> labelArea, valueArea;
    };

    //==========================================================================
    /** A button painted by its owner. For the ones whose face is a small layout of its
        own: the preset menu, the speaker model picker, the toolbar's two icons. */
    class PaintedButton : public juce::Button
    {
    public:
        using Painter = std::function<void (juce::Graphics&, juce::Rectangle<float>, bool hovered, bool down)>;

        explicit PaintedButton (const juce::String& name) : juce::Button (name)
        {
            setWantsKeyboardFocus (false);
            setMouseClickGrabsKeyboardFocus (false);
        }

        Painter painter;

        void paintButton (juce::Graphics& g, bool hovered, bool down) override
        {
            if (painter)
                painter (g, getLocalBounds().toFloat(), hovered, down);
        }
    };

    /** A down chevron, the design's 9 px one. */
    void drawChevron (juce::Graphics&, juce::Point<float> centre, juce::Colour);

    /** The fill of a choice: accent when selected, the control grey when not, both lifted
        under the pointer. */
    juce::Colour choiceFill (bool selected, bool hovered, bool down);
}
