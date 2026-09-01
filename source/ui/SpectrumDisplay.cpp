#include "SpectrumDisplay.h"

#include "Fonts.h"
#include "Theme.h"

#include <cmath>
#include <limits>

using namespace Celine;

namespace
{
    // Below this a bin is silence as far as the display is concerned; log10 of
    // anything smaller lands far under the floor anyway.
    constexpr float magnitudeEpsilon = 1.0e-7f;

    float magnitudeToDb (float magnitude) noexcept
    {
        return 20.0f * std::log10 (std::max (magnitude, magnitudeEpsilon));
    }

    juce::String formatFrequency (float freq)
    {
        return freq >= 1000.0f ? juce::String (freq / 1000.0f, 2) + " kHz"
                               : juce::String ((int) freq) + " Hz";
    }
}

SpectrumDisplay::SpectrumDisplay()
{
    // Opaque, and paint() fills the corners itself. This looks like a detail and is
    // not: the view repaints thirty times a second, and a non-opaque child makes JUCE
    // redraw the parent underneath it every time — for this editor that is a
    // full-window fillAll plus both logo SVGs, measured at 0.78ms, three times what
    // drawing the graph itself costs. It is wasted on every frame and it competes
    // with layout during a resize drag, which is where it showed.
    setOpaque (true);
}

juce::Colour SpectrumDisplay::colourFor (View v)
{
    switch (v)
    {
        case View::current:   return Theme::current();
        case View::reference: return Theme::reference();
        case View::eqCurve:   return Theme::correction();
    }

    return Theme::current();
}

void SpectrumDisplay::setView (View newView)
{
    if (view == newView)
        return;

    view = newView;
    repaint();
}

void SpectrumDisplay::setCurve (Curve curve, const std::vector<float>& mags)
{
    switch (curve)
    {
        case Curve::liveCurrent:      liveCurrent = mags;      break;
        case Curve::liveReference:    liveReference = mags;    break;
        case Curve::learnedCurrent:   learnedCurrent = mags;   break;
        case Curve::learnedReference: learnedReference = mags; break;
    }
}

void SpectrumDisplay::setCorrection (const std::vector<float>& leftDb, const std::vector<float>& rightDb,
                                     bool channelsAreLinked)
{
    correctionLeft = leftDb;
    correctionRight = rightDb;
    linked = channelsAreLinked;
}

void SpectrumDisplay::setOverlayMessage (const juce::String& message)
{
    overlayMessage = message;
}

PlotGeometry SpectrumDisplay::getPlot() const
{
    auto area = getLocalBounds().toFloat().reduced (1.0f);
    area.removeFromTop (axisTop);
    area.removeFromBottom (axisBottom);
    area.removeFromLeft (axisLeft);
    area.removeFromRight (axisRight);
    return { area };
}

float SpectrumDisplay::interpolateAt (const std::vector<float>& values, float freq) const
{
    if (values.size() < 2)
        return 0.0f;

    const auto binHz = (float) (sampleRate / (double) fftSize);
    if (binHz <= 0.0f)
        return 0.0f;

    const auto pos = juce::jlimit (0.0f, (float) (values.size() - 1), freq / binHz);
    const auto k0 = (size_t) pos;
    const auto k1 = juce::jmin (k0 + 1, values.size() - 1);
    const auto frac = pos - (float) k0;

    return values[k0] + frac * (values[k1] - values[k0]);
}

void SpectrumDisplay::drawGrid (juce::Graphics& g, PlotGeometry plot, juce::Rectangle<float> full) const
{
    struct FreqLine { float hz; const char* label; };
    const FreqLine freqLines[] = {
        { 20.0f, "20" },     { 30.0f, nullptr },   { 50.0f, "50" },     { 70.0f, nullptr },
        { 100.0f, "100" },   { 200.0f, "200" },    { 300.0f, nullptr }, { 500.0f, "500" },
        { 700.0f, nullptr }, { 1000.0f, "1k" },    { 2000.0f, "2k" },   { 3000.0f, nullptr },
        { 5000.0f, "5k" },   { 7000.0f, nullptr }, { 10000.0f, "10k" }, { 20000.0f, "20k" },
    };

    // Sub-pixel, not drawVerticalLine's integer y. The window is locked to an aspect
    // ratio, so dragging it one pixel wider moves the layout by a fraction of one, and
    // truncating each line to an int meant they crossed their pixel boundaries at
    // different moments — the grid crawled and shimmered through a resize instead of
    // sliding. A 1px anti-aliased fill moves smoothly and lands identically when the
    // coordinate happens to be whole.
    const auto hairline = [&g] (float x, float y, float w, float h)
    {
        g.fillRect (juce::Rectangle<float> (x, y, w, h));
    };

    for (const auto& line : freqLines)
    {
        g.setColour (juce::Colours::white.withAlpha (line.label != nullptr ? 0.07f : 0.035f));
        hairline (plot.freqToX (line.hz), plot.getY(), 1.0f, plot.getHeight());
    }

    // One grid, and the same one on every tab. The horizontal lines used to belong to
    // whichever scale the view was about, which meant they jumped by half a division
    // every time you switched — the two scales are 96dB and 48dB, and the old spectrum
    // window put its lines half a step off the correction's. Both are now divided into
    // the same eight, so a single set of lines serves both and the picture holds still
    // while the subject changes. PlotGeometry::gridDivisions is what keeps that true: change either
    // range without keeping them a 2:1 pair and the labels stop landing on the lines.
    for (int i = 0; i <= PlotGeometry::gridDivisions; ++i)
    {
        const auto y = plot.getBottom() - (float) i / (float) PlotGeometry::gridDivisions * plot.getHeight();

        // The middle is the correction's zero — no boost, no cut. Now that the
        // correction is drawn on every tab, that line means something on every tab.
        const auto centre = i * 2 == PlotGeometry::gridDivisions;
        g.setColour (juce::Colours::white.withAlpha (centre ? 0.22f : 0.055f));
        hairline (plot.getX(), y, plot.getWidth(), 1.0f);
    }

    g.setFont (Fonts::light (10.0f));
    g.setColour (Theme::textDim());

    for (const auto& line : freqLines)
    {
        if (line.label == nullptr)
            continue;

        // Clamp so the outermost labels stay inside the view instead of running under
        // one of the dB columns.
        const auto x = plot.freqToX (line.hz);
        const auto left = juce::jlimit (full.getX(), full.getRight() - 44.0f, x - 22.0f);
        g.drawText (line.label,
                    juce::Rectangle<float> (left, plot.getBottom() + 2.0f, 44.0f, 12.0f),
                    juce::Justification::centred);
    }

    // Both scales are labelled, at one weight. The idle one used to be dimmed to say
    // which was in play, but with the correction drawn on every tab both always are,
    // and a column that changed weight on a tab switch was the same restlessness the
    // gridlines had. The unit captions at the foot of each are what tell them apart.
    const auto axisText = Theme::text().withAlpha (0.8f);

    g.setColour (axisText);

    // Down to -84 rather than the floor: the bottom gridline's label would sit in the
    // corner the 20Hz caption is clamped into.
    for (auto db : { 0.0f, -12.0f, -24.0f, -36.0f, -48.0f, -60.0f, -72.0f, -84.0f })
    {
        const auto y = plot.spectrumDbToY (db);
        g.drawText (juce::String ((int) db),
                    juce::Rectangle<float> (full.getX() + 2.0f, y - 7.0f, axisLeft - 8.0f, 14.0f),
                    juce::Justification::centredRight);
    }

    for (auto db : { 24.0f, 12.0f, 0.0f, -12.0f, -24.0f })
    {
        const auto y = plot.correctionDbToY (db);
        const auto text = db == 0.0f ? juce::String ("0") : (db > 0.0f ? "+" : "") + juce::String ((int) db);
        g.drawText (text,
                    juce::Rectangle<float> (plot.getRight() + 5.0f, y - 7.0f, axisRight - 7.0f, 14.0f),
                    juce::Justification::centredLeft);
    }

    // Name the units once, at the foot of each column, so the two scales can't be
    // mistaken for one another.
    g.setFont (Fonts::bold (9.0f));
    // A row of their own, under the frequency labels: the 20 Hz label is clamped
    // inwards far enough to sit over the left column otherwise.
    const auto unitRow = juce::Rectangle<float> (0.0f, plot.getBottom() + 14.0f, 0.0f, 11.0f);

    g.setColour (axisText);
    g.drawText ("dBFS", unitRow.withX (full.getX() + 2.0f).withWidth (axisLeft - 8.0f),
                juce::Justification::centredRight);
    g.drawText ("dB", unitRow.withX (plot.getRight() + 5.0f).withWidth (axisRight - 7.0f),
                juce::Justification::centredLeft);
}

juce::Path SpectrumDisplay::buildCurvePath (PlotGeometry area, const std::vector<float>& values,
                                            Scale scale) const
{
    juce::Path path;

    const auto numBins = (int) values.size();
    const auto binHz = (float) (sampleRate / (double) fftSize);

    if (numBins < 2 || binHz <= 0.0f)
        return path;

    // Absolute: no peak normalisation anywhere in here. A trace that fades out sinks
    // to the floor instead of being re-scaled back up into view.
    auto yFor = [&] (int k)
    {
        return scale == Scale::spectrum
                   ? area.spectrumDbToY (magnitudeToDb (values[(size_t) k]))
                   : area.correctionDbToY (values[(size_t) k] + correctionOffsetDb);
    };

    // Peak-per-column, the usual analyser convention: the top of the ink is the
    // loudest thing in that column rather than whichever bin happened to land last.
    // Screen y grows downwards, so the peak is the smallest y.
    int column = std::numeric_limits<int>::min();
    float columnX = 0.0f;
    float columnY = 0.0f;
    bool started = false;

    auto emit = [&]
    {
        if (! started) { path.startNewSubPath (columnX, columnY); started = true; }
        else            path.lineTo (columnX, columnY);
    };

    // The ends are read off the axis, not off whichever bin happens to fall nearest
    // it. Starting at the first bin at or above 20 Hz began the curve at 23.4 Hz on a
    // 4096-point FFT at 48k -- two and a half percent of a log decade, a visible gap
    // inside the left edge -- and the same at the top. Interpolating the value at the
    // limit itself puts the curve on the border where the axis says it should be.
    const auto valueAt = [&] (float freq)
    {
        const auto v = interpolateAt (values, freq);
        return scale == Scale::spectrum ? area.spectrumDbToY (magnitudeToDb (v))
                                        : area.correctionDbToY (v + correctionOffsetDb);
    };

    // Honest about the top when there is nothing up there: at sample rates whose
    // Nyquist is under 20 kHz the curve stops where the data does rather than being
    // run out flat to the edge.
    const auto highestFreq = juce::jmin (PlotGeometry::maxFreq, (float) (numBins - 1) * binHz);

    column = (int) area.getX();
    columnX = area.getX();
    columnY = valueAt (PlotGeometry::minFreq);

    for (int k = 1; k < numBins; ++k)
    {
        const auto freq = (float) k * binHz;
        if (freq < PlotGeometry::minFreq) continue;
        if (freq > PlotGeometry::maxFreq) break;

        const auto x = area.freqToX (freq);
        const auto y = yFor (k);
        const auto thisColumn = (int) x;

        if (thisColumn != column)
        {
            if (column != std::numeric_limits<int>::min())
                emit();

            column = thisColumn;
            columnX = x;
            columnY = y;
        }
        else
        {
            columnY = std::min (columnY, y);
        }
    }

    if (column != std::numeric_limits<int>::min())
        emit();

    // And close on the axis limit the same way.
    columnX = area.freqToX (highestFreq);
    columnY = valueAt (highestFreq);
    emit();

    return path;
}

void SpectrumDisplay::drawSpectrum (juce::Graphics& g, PlotGeometry area,
                                    const std::vector<float>& mags,
                                    juce::Colour colour, TraceStyle style) const
{
    if (mags.size() < 2 || style.alpha <= 0.004f)
        return;

    const auto path = buildCurvePath (area, mags, Scale::spectrum);

    if (path.isEmpty())
        return;

    const auto firstX = path.getBounds().getX();
    const auto lastX = path.getBounds().getRight();

    if (style.fill)
    {
        juce::Path filled (path);
        filled.lineTo (lastX, area.getBottom());
        filled.lineTo (firstX, area.getBottom());
        filled.closeSubPath();

        g.setGradientFill (juce::ColourGradient (colour.withAlpha (0.28f * style.alpha), area.getCentreX(), area.getY(),
                                                 colour.withAlpha (0.02f * style.alpha), area.getCentreX(), area.getBottom(),
                                                 false));
        g.fillPath (filled);
    }

    if (! style.stroke)
        return;

    g.setColour (colour.withAlpha (0.9f * style.alpha));
    g.strokePath (path, juce::PathStrokeType (style.thickness, juce::PathStrokeType::mitered,
                                              juce::PathStrokeType::butt));
}

void SpectrumDisplay::drawCorrection (juce::Graphics& g, PlotGeometry area,
                                      const std::vector<float>& db, juce::Colour colour, bool fill,
                                      float thickness) const
{
    if (db.size() < 2)
        return;

    const auto zeroY = area.correctionDbToY (0.0f);
    const auto path = buildCurvePath (area, db, Scale::correction);

    if (path.isEmpty())
        return;

    const auto firstX = path.getBounds().getX();
    const auto lastX = path.getBounds().getRight();

    if (fill)
    {
        // Filling back to the 0 dB line makes boosts and cuts readable at a glance.
        juce::Path filled (path);
        filled.lineTo (lastX, zeroY);
        filled.lineTo (firstX, zeroY);
        filled.closeSubPath();

        g.setColour (colour.withAlpha (0.20f));
        g.fillPath (filled);
    }

    g.setColour (colour);
    g.strokePath (path, juce::PathStrokeType (thickness, juce::PathStrokeType::mitered,
                                              juce::PathStrokeType::butt));
}

void SpectrumDisplay::drawBandShading (juce::Graphics& g, PlotGeometry area) const
{
    // The band limits only shape the correction, so they are only shown — and only
    // draggable — while that is what you are looking at.
    if (! showingCorrectionScale())
        return;

    // The excluded ends, dimmed back towards the chrome.
    if (bandLow > PlotGeometry::minFreq)
    {
        const auto x = area.freqToX (bandLow);
        g.setColour (Theme::chrome().withAlpha (0.55f));
        g.fillRect (juce::Rectangle<float> (area.getX(), area.getY(), x - area.getX(), area.getHeight()));
    }

    if (bandHigh < PlotGeometry::maxFreq)
    {
        const auto x = area.freqToX (bandHigh);
        g.setColour (Theme::chrome().withAlpha (0.55f));
        g.fillRect (juce::Rectangle<float> (x, area.getY(), area.getRight() - x, area.getHeight()));
    }

    // And the edges themselves, as grips you can take hold of: a line the full height
    // with a tab at the top, lit when the pointer is on it or dragging it.
    const auto drawEdge = [&] (float frequency, BandEdge which)
    {
        const auto x = area.freqToX (frequency);
        const auto live = dragging == which || (! dragging.has_value() && hovered == which);

        g.setColour (live ? Theme::correction() : Theme::line().withAlpha (0.75f));
        g.fillRect (juce::Rectangle<float> (x, area.getY(), 1.0f, area.getHeight()));

        // The tab. Something to aim at, and the only thing that says the line moves.
        const auto tab = juce::Rectangle<float> (9.0f, 16.0f)
                             .withCentre ({ x, area.getY() + 8.0f });

        g.setColour (live ? Theme::correction() : Theme::surface());
        g.fillRoundedRectangle (tab, 2.5f);
        g.setColour (live ? Theme::text() : Theme::line().withAlpha (0.75f));
        g.drawRoundedRectangle (tab.reduced (0.5f), 2.5f, 1.0f);
    };

    drawEdge (bandLow, BandEdge::low);
    drawEdge (bandHigh, BandEdge::high);
}

std::optional<SpectrumDisplay::BandEdge> SpectrumDisplay::handleAt (juce::Point<float> position) const
{
    if (! showingCorrectionScale())
        return {};

    const auto plot = getPlot();

    if (! plot.bounds.expanded (handleReach, 0.0f).contains (position))
        return {};

    const auto lowDistance = std::abs (position.x - plot.freqToX (bandLow));
    const auto highDistance = std::abs (position.x - plot.freqToX (bandHigh));

    if (juce::jmin (lowDistance, highDistance) > handleReach)
        return {};

    // The nearer one, so the two are still separable once they are dragged together.
    return lowDistance <= highDistance ? BandEdge::low : BandEdge::high;
}

bool SpectrumDisplay::readoutValueAt (float freq, juce::String& text) const
{
    if (showingCorrectionScale())
    {
        if (correctionLeft.size() < 2)
            return false;

        const auto leftDb = interpolateAt (correctionLeft, freq) + correctionOffsetDb;

        if (linked)
        {
            text << juce::String (leftDb, 1) << " dB";
        }
        else
        {
            const auto rightDb = interpolateAt (correctionRight, freq) + correctionOffsetDb;
            text << "L " << juce::String (leftDb, 1) << "   R " << juce::String (rightDb, 1) << " dB";
        }

        return true;
    }

    // On the signal tabs the settled Learn curve is the more useful number; the
    // moving trace stands in until there is one.
    const auto& learned = view == View::current ? learnedCurrent : learnedReference;
    const auto& liveTrace = view == View::current ? liveCurrent : liveReference;
    const auto& source = learned.size() > 1 ? learned : liveTrace;

    if (source.size() < 2)
        return false;

    text << juce::String (magnitudeToDb (interpolateAt (source, freq)), 1) << " dBFS";
    return true;
}

void SpectrumDisplay::drawReadout (juce::Graphics& g, PlotGeometry area) const
{
    if (! mouseIsOver || ! area.contains (mousePosition))
        return;

    const auto freq = area.xToFreq (mousePosition.x);

    juce::String text;
    text << formatFrequency (freq) << "   ";

    if (! readoutValueAt (freq, text))
        return;

    const auto x = area.freqToX (freq);

    g.setColour (Theme::text().withAlpha (0.25f));
    g.fillRect (juce::Rectangle<float> (x, area.getY(), 1.0f, area.getHeight()));

    const auto font = Fonts::light (11.0f);
    const auto width = juce::GlyphArrangement::getStringWidth (font, text) + 16.0f;
    const auto box = juce::Rectangle<float> (width, 20.0f)
                         .withCentre ({ juce::jlimit (area.getX() + width * 0.5f,
                                                      area.getRight() - width * 0.5f, x),
                                        area.getY() + 14.0f });

    g.setColour (Theme::surface().withAlpha (0.94f));
    g.fillRoundedRectangle (box, 4.0f);
    g.setColour (Theme::line());
    g.drawRoundedRectangle (box, 4.0f, 1.0f);

    g.setColour (Theme::text());
    g.setFont (font);
    g.drawText (text, box, juce::Justification::centred);
}

void SpectrumDisplay::paint (juce::Graphics& g)
{
    const auto full = getLocalBounds().toFloat();
    const auto plot = getPlot();

    // The surround first, so the rounded corners have something behind them and this
    // component can still be opaque — see the constructor. It has to be the colour the
    // editor paints around the graph, which is the one thing here that reaches outside
    // this class for a value.
    g.fillAll (Theme::consoleBackground());

    // A panel with no frame, but with the design's own corners: rounded like every
    // other surface here, and unbordered because a line around it fought the band-edge
    // grips, which are lines of their own.
    g.setColour (Theme::background());
    g.fillRoundedRectangle (full, Theme::cornerRadius);

    drawGrid (g, plot, full);

    // The correction, faint, underneath everything, on the two tabs where it is not
    // the subject. It is the one thing the plugin is actually doing, and having it
    // vanish the moment you looked at either signal meant you could not see what you
    // had built while judging the material it was built from. Drawn before the
    // spectra so it reads as something behind them rather than over them.
    if (view != View::eqCurve)
        drawCorrection (g, plot, correctionLeft, Theme::correction().withAlpha (0.5f), false, 1.5f);

    // Every tab shows all three, so the picture stays a comparison rather than a
    // single curve on its own: the tab decides which one is the subject, and the
    // other two stay legible behind it in their own colours.
    constexpr TraceStyle subjectLive { true, true, 1.2f, 0.75f };
    constexpr TraceStyle subjectLearned { false, true, 2.3f, 1.0f };
    constexpr TraceStyle contextLive { true, false, 0.0f, 0.35f };
    constexpr TraceStyle contextLearned { false, true, 1.4f, 0.5f };

    // The moving traces dim as their fade winds down; the settled Learn curves do not,
    // because those are stored data and are just as true when nothing is playing.
    const auto faded = [] (TraceStyle style, float fade)
    {
        style.alpha *= fade;
        return style;
    };

    const auto drawSignals = [&] (bool currentIsSubject)
    {
        // Subject last, so it lands on top of the one it is being compared with.
        const auto order = currentIsSubject ? std::pair { false, true } : std::pair { true, false };

        for (const bool doingCurrent : { order.first, order.second })
        {
            const auto subject = doingCurrent == currentIsSubject;
            const auto colour = doingCurrent ? Theme::current() : Theme::reference();
            const auto& live = doingCurrent ? liveCurrent : liveReference;
            const auto& learned = doingCurrent ? learnedCurrent : learnedReference;
            const auto fade = doingCurrent ? liveCurrentFade : liveReferenceFade;

            drawSpectrum (g, plot, live, colour, faded (subject ? subjectLive : contextLive, fade));
            drawSpectrum (g, plot, learned, colour.brighter (subject ? 0.55f : 0.2f),
                          subject ? subjectLearned : contextLearned);
        }
    };

    switch (view)
    {
        case View::current:
            drawSignals (true);
            break;

        case View::reference:
            drawSignals (false);
            break;

        case View::eqCurve:
            // Both signals as context, fill only for the live traces: on this tab the
            // left scale is not the one in play, and an outlined trace crossing the
            // correction's own 0 dB line reads as if it were part of the curve.
            drawSpectrum (g, plot, liveCurrent, Theme::current(), faded (contextLive, liveCurrentFade));
            drawSpectrum (g, plot, liveReference, Theme::reference(), faded (contextLive, liveReferenceFade));
            drawSpectrum (g, plot, learnedCurrent, Theme::current(), contextLearned);
            drawSpectrum (g, plot, learnedReference, Theme::reference(), contextLearned);

            drawCorrection (g, plot, correctionLeft, Theme::correction(), true);

            // Unlinked, the two channels genuinely differ, so both are worth seeing.
            if (! linked)
                drawCorrection (g, plot, correctionRight, Theme::correction().withRotatedHue (0.08f), false);
            break;
    }

    drawBandShading (g, plot);
    drawReadout (g, plot);

    if (overlayMessage.isNotEmpty())
    {
        g.setColour (Theme::background().withAlpha (0.62f));
        g.fillRoundedRectangle (full, Theme::cornerRadius);

        g.setColour (Theme::text().withAlpha (0.75f));
        g.setFont (Fonts::bold (13.0f));
        g.drawText (overlayMessage, plot.bounds, juce::Justification::centred);
    }

}

void SpectrumDisplay::mouseMove (const juce::MouseEvent& event)
{
    mousePosition = event.position;
    mouseIsOver = true;
    hovered = handleAt (event.position);

    setMouseCursor (hovered.has_value() ? juce::MouseCursor::LeftRightResizeCursor
                                        : juce::MouseCursor::NormalCursor);
    repaint();
}

void SpectrumDisplay::mouseExit (const juce::MouseEvent&)
{
    mouseIsOver = false;
    hovered.reset();
    setMouseCursor (juce::MouseCursor::NormalCursor);
    repaint();
}

void SpectrumDisplay::mouseDown (const juce::MouseEvent& event)
{
    dragging = handleAt (event.position);

    if (dragging.has_value() && onBandGesture != nullptr)
        onBandGesture (*dragging, true);

    repaint();
}

void SpectrumDisplay::mouseDrag (const juce::MouseEvent& event)
{
    mousePosition = event.position;

    if (! dragging.has_value())
        return;

    const auto frequency = getPlot().xToFreq (event.position.x);

    // The two may not cross: an inverted band would mean the correction applies
    // nowhere, which looks like the plugin has stopped working. Only the edge being
    // dragged is reported — the other is read here purely as the limit on this one.
    const auto clamped = *dragging == BandEdge::low ? juce::jmin (frequency, bandHigh)
                                                    : juce::jmax (frequency, bandLow);

    if (onBandDragged != nullptr)
        onBandDragged (*dragging, clamped);

    repaint();
}

void SpectrumDisplay::mouseUp (const juce::MouseEvent& event)
{
    if (dragging.has_value() && onBandGesture != nullptr)
        onBandGesture (*dragging, false);

    dragging.reset();
    hovered = handleAt (event.position);
    repaint();
}
