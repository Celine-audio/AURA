#include "Style.h"

#include <BinaryData.h>

namespace Ui
{
    namespace
    {
        juce::Typeface::Ptr load (const char* filename)
        {
            for (int i = 0; i < BinaryData::namedResourceListSize; ++i)
            {
                const auto* resource = BinaryData::namedResourceList[i];

                if (juce::String (filename) != BinaryData::getNamedResourceOriginalFilename (resource))
                    continue;

                int size = 0;

                if (const auto* data = BinaryData::getNamedResource (resource, size); data != nullptr && size > 0)
                    return juce::Typeface::createSystemTypefaceFor (data, (size_t) size);
            }

            return nullptr;
        }

        /** Parsed once per process, and held for its life: fonts are asked for on every
            paint, and parsing a typeface is not a paint-time cost. */
        struct Faces
        {
            juce::Typeface::Ptr bold = load ("Inter-Bold.ttf");
            juce::Typeface::Ptr medium = load ("Inter-Medium.ttf");
            juce::Typeface::Ptr heavy = load ("Inter-ExtraBold.ttf");
        };

        Faces& faces()
        {
            static Faces instance;
            return instance;
        }

        juce::Font make (const juce::Typeface::Ptr& face, float height)
        {
            // A missing face falls back to the kit's bold rather than the platform sans,
            // so the page still looks like one design.
            if (face == nullptr)
                return Fonts::bold (height);

            return juce::Font (juce::FontOptions (face).withHeight (height));
        }
    }

    juce::Font bold (float height) { return make (faces().bold, height * typeScale); }

    juce::Font medium (float height)
    {
        // Without the Medium file, the Bold it replaced.
        return faces().medium != nullptr ? make (faces().medium, height * typeScale) : bold (height);
    }
    juce::Font heavy (float height) { return make (faces().heavy, height * typeScale); }

    juce::Font spaced (juce::Font font, float letterSpacingPx)
    {
        // The spacing grows with the type, so a spaced label keeps its look at any scale.
        return font.withExtraKerningFactor (letterSpacingPx * typeScale / juce::jmax (1.0f, font.getHeight()));
    }

    void text (juce::Graphics& g, const juce::String& s, juce::Rectangle<float> area, juce::Font font,
               juce::Colour colour, juce::Justification justification)
    {
        g.setFont (font);
        g.setColour (colour);
        g.drawText (s, area, justification, false);
    }

    float capHeight (const juce::Font& font)
    {
        // The ink of an H, not its glyph's box: a glyph's bounds run from the font's ascent to its
        // descent, which is the whole line, and centring on that sits capitals low in their row.
        juce::GlyphArrangement probe;
        probe.addLineOfText (font, "H", 0.0f, 0.0f);

        juce::Path ink;
        probe.createPath (ink);
        return ink.getBounds().getHeight();
    }

    void textOnCaps (juce::Graphics& g, const juce::String& s, juce::Rectangle<float> area, juce::Font font,
                     juce::Colour colour, juce::Justification justification)
    {
        const auto width = juce::GlyphArrangement::getStringWidth (font, s);
        const auto baseline = area.getCentreY() + 0.5f * capHeight (font);

        auto x = area.getX();

        if (justification.testFlags (juce::Justification::right))
            x = area.getRight() - width;
        else if (justification.testFlags (juce::Justification::horizontallyCentred))
            x = area.getCentreX() - 0.5f * width;

        juce::GlyphArrangement glyphs;
        glyphs.addLineOfText (font, s, x, baseline);

        g.setColour (colour);
        glyphs.draw (g);
    }

    void drawTitle (juce::Graphics& g, juce::Rectangle<float> area, const juce::String& title, float height)
    {
        text (g, title, area, spaced (bold (height), 1.2f), Theme::phLabel());
    }

    void drawReadout (juce::Graphics& g, juce::Rectangle<float> row, const juce::String& label,
                      const juce::String& value, juce::Colour valueColour)
    {
        text (g, label, row, mono (8.0f), Theme::phLabel());
        text (g, value, row, mono (9.5f), valueColour, juce::Justification::centredRight);
    }

}
