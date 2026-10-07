#include "SpectrumDisplay.h"

#include <CelineUI/Fonts.h>
#include <CelineUI/Theme.h>

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

    // The Preview button, as far in from the plot's right edge as it is from its top, so
    // it sits square in the corner. Ten, because the band's high edge stands on that
    // border with its grip at the top, and needs a clear run at it.
    constexpr int previewWidth = 70;
    constexpr int previewHeight = 22;
    constexpr int previewInset = 10;

    // The readout box's height, and how far below the plot's top its centre sits: on the
    // button's own centre line, so the two line up when both are showing.
    constexpr float readoutHeight = 20.0f;
    constexpr float readoutCentre = (float) previewInset + (float) previewHeight * 0.5f;
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

    previewButton.setClickingTogglesState (true);
    previewButton.setToggleState (true, juce::dontSendNotification);
    previewButton.setWantsKeyboardFocus (false);
    previewButton.setTooltip ("Draw the preview of the EQ curve.");

    previewButton.onClick = [this]
    {
        if (onPreviewShownChanged != nullptr)
            onPreviewShownChanged (previewButton.getToggleState());

        repaint();
    };

    applyColours();
    addAndMakeVisible (previewButton);
}

void SpectrumDisplay::applyColours()
{
    // On, it wears the orange the curve it controls is drawn in -- the same tint the
    // Match button takes when it is asking to be pressed, which is the same news. Off,
    // it steps back to an ordinary control. Composited onto the graph's ground rather
    // than left translucent, so the gridlines do not run through it.
    const auto ground = Theme::background();
    const auto orange = Theme::stale();

    previewButton.setColour (juce::TextButton::buttonColourId, Theme::surface());
    previewButton.setColour (juce::TextButton::buttonOnColourId, ground.overlaidWith (orange.withAlpha (0.24f)));
    previewButton.setColour (juce::TextButton::textColourOffId, Theme::textDim());
    previewButton.setColour (juce::TextButton::textColourOnId, orange.brighter (0.35f));
}

void SpectrumDisplay::setPreview (const std::vector<float>& leftDb, const std::vector<float>& rightDb)
{
    previewLeft = leftDb;
    previewRight = rightDb;
}

void SpectrumDisplay::setPreviewShown (bool shouldShow)
{
    if (previewButton.getToggleState() == shouldShow)
        return;

    previewButton.setToggleState (shouldShow, juce::dontSendNotification);
    repaint();
}

void SpectrumDisplay::resized()
{
    const auto plot = getPlot().bounds;

    previewButton.setBounds (juce::roundToInt (plot.getRight()) - previewInset - previewWidth,
                             juce::roundToInt (plot.getY()) + previewInset,
                             previewWidth, previewHeight);
}

Theme::Role SpectrumDisplay::roleFor (View v)
{
    switch (v)
    {
        case View::current:   return Theme::Role::current;
        case View::reference: return Theme::Role::reference;
        case View::eqCurve:   return Theme::Role::correction;
    }

    return Theme::Role::current;
}

juce::Colour SpectrumDisplay::colourFor (View v)
{
    return Theme::colour (roleFor (v));
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
    return { area, viewShiftDb };
}

void SpectrumDisplay::setViewShift (float correctionDb)
{
    const auto clamped = juce::jlimit (PlotGeometry::minShiftDb, PlotGeometry::maxShiftDb, correctionDb);

    if (juce::exactlyEqual (clamped, viewShiftDb))
        return;

    viewShiftDb = clamped;
    repaint();
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

    // The alphas are not the ones this used to carry. It drew in literal white, which
    // is the one thing nothing outside Theme.h is allowed to do -- and it meant the
    // theme editor's "Grid line" moved nothing at all. Reaching the same picture from
    // grid()'s mid-grey instead of from white takes about four times the alpha, since
    // the grey starts that much nearer the ground it is laid on.
    for (const auto& line : freqLines)
    {
        g.setColour (Theme::grid().withAlpha (line.label != nullptr ? 0.30f : 0.15f));
        hairline (plot.freqToX (line.hz), plot.getY(), 1.0f, plot.getHeight());
    }

    // One grid, and the same one on every tab. The horizontal lines used to belong to
    // whichever scale the view was about, which meant they jumped by half a division
    // every time you switched — the two scales are 96dB and 48dB, and the old spectrum
    // window put its lines half a step off the correction's. Both are now divided into
    // the same eight, so a single set of lines serves both and the picture holds still
    // while the subject changes. PlotGeometry::gridDivisions is what keeps that true: change either
    // range without keeping them a 2:1 pair and the labels stop landing on the lines.
    //
    // Placed by value rather than by fraction of the plot, so that when the view is slid
    // the lines travel with the curves instead of standing still while the curves move
    // past them. Every multiple of 6 dB of correction that is in view gets one.
    const auto step = PlotGeometry::correctionStepDb;
    const auto firstLine = (int) std::ceil (plot.correctionBottomDb() / step - 1.0e-3f);
    const auto lastLine = (int) std::floor (plot.correctionTopDb() / step + 1.0e-3f);

    for (int i = firstLine; i <= lastLine; ++i)
    {
        const auto y = plot.correctionDbToY ((float) i * step);

        // Zero is the correction's: no boost, no cut. Now that the correction is drawn
        // on every tab, that line means something on every tab -- and it is the line
        // that tells you where you are once the view has been moved.
        g.setColour (Theme::grid().withAlpha (i == 0 ? 0.90f : 0.23f));
        hairline (plot.getX(), y, plot.getWidth(), 1.0f);
    }

    g.setFont (Fonts::light (10.0f));
    g.setColour (Theme::graphText());

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
    const auto axisText = Theme::graphText().withAlpha (0.8f);

    g.setColour (axisText);

    const auto signedDb = [] (int db)
    {
        return db == 0 ? juce::String ("0") : (db > 0 ? "+" : "") + juce::String (db);
    };

    for (int i = firstLine; i <= lastLine; ++i)
    {
        const auto correctionDb = (float) i * step;
        const auto y = plot.correctionDbToY (correctionDb);

        // The signal scale labels every line, down to the last one clear of the foot: a
        // label on the bottom line would sit in the corner the 20Hz caption is clamped
        // into.
        if (y < plot.getBottom() - 7.0f)
        {
            const auto spectrumDb = juce::roundToInt (PlotGeometry::spectrumDbAtCorrection (correctionDb));
            g.drawText (juce::String (spectrumDb),
                        juce::Rectangle<float> (full.getX() + 2.0f, y - 7.0f, axisLeft - 8.0f, 14.0f),
                        juce::Justification::centredRight);
        }

        // The correction's every other line, 12 dB apart.
        if (i % 2 == 0)
            g.drawText (signedDb (juce::roundToInt (correctionDb)),
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
    // out of view instead of being re-scaled back up into it.
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
        g.setColour (Theme::graphShade().withAlpha (0.55f));
        g.fillRect (juce::Rectangle<float> (area.getX(), area.getY(), x - area.getX(), area.getHeight()));
    }

    if (bandHigh < PlotGeometry::maxFreq)
    {
        const auto x = area.freqToX (bandHigh);
        g.setColour (Theme::graphShade().withAlpha (0.55f));
        g.fillRect (juce::Rectangle<float> (x, area.getY(), area.getRight() - x, area.getHeight()));
    }

    // And the edges themselves, as grips you can take hold of: a line the full height
    // with a tab at the top, lit when the pointer is on it or dragging it.
    const auto drawEdge = [&] (float frequency, BandEdge which)
    {
        const auto x = area.freqToX (frequency);
        const auto live = dragging == which || (! dragging.has_value() && hovered == which);

        g.setColour (live ? Theme::correction() : Theme::graphLine().withAlpha (0.75f));
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
        const auto applied = correctionLeft.size() > 1;
        const auto previewed = drawsPreview();

        if (! applied && ! previewed)
            return false;

        const auto at = [&] (const std::vector<float>& db)
        {
            return juce::String (interpolateAt (db, freq) + correctionOffsetDb, 1);
        };

        // With both on screen, linked, the readout says where this frequency stands and
        // where Match would move it. In words rather than with an arrow, which Jura does
        // not have. Unlinked that would be four numbers, so it reports whichever curve
        // is the one being looked at: the pending one if there is one.
        if (linked && applied && previewed)
            text << at (correctionLeft) << " dB   preview " << at (previewLeft) << " dB";
        else if (linked)
            text << at (previewed ? previewLeft : correctionLeft) << " dB";
        else if (previewed)
            text << "L " << at (previewLeft) << "   R " << at (previewRight) << " dB";
        else
            text << "L " << at (correctionLeft) << "   R " << at (correctionRight) << " dB";

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

    // Kept clear of the Preview button when it is up, with the gap the button keeps
    // from the plot's edge, rather than sliding underneath it.
    const auto right = previewButton.isVisible()
                           ? juce::jmax (area.getX() + width, (float) (previewButton.getX() - previewInset))
                           : area.getRight();

    const auto box = juce::Rectangle<float> (width, readoutHeight)
                         .withCentre ({ juce::jlimit (area.getX() + width * 0.5f,
                                                      right - width * 0.5f, x),
                                        area.getY() + readoutCentre });

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

    // The curves are kept to the plot. A curve that runs off the top or the bottom of
    // the view -- louder than the window, or quieter, or a cut deeper than it -- is cut
    // off at that edge rather than drawn over the axis labels or pinned flat along it.
    // Everything after the curves -- the band, the readout -- stands outside this and
    // may overlap the edge as it always has.
    g.saveState();
    g.reduceClipRegion (plot.bounds.getSmallestIntegerContainer());

    // The correction, faint, underneath everything, on the two tabs where it is not
    // the subject. It is the one thing the plugin is actually doing, and having it
    // vanish the moment you looked at either signal meant you could not see what you
    // had built while judging the material it was built from. Drawn before the
    // spectra so it reads as something behind them rather than over them.
    if (view != View::eqCurve)
    {
        drawCorrection (g, plot, correctionLeft, Theme::correction().withAlpha (0.5f), false, 1.5f);

        // And what Match would make of it, on top, at the same weight. This is where you
        // are looking while a Learn runs -- pressing one brings its tab up -- so it is
        // where the preview can be watched settling as the take builds. A shade
        // stronger than the violet, because it is the one that is moving.
        if (drawsPreview())
            drawCorrection (g, plot, previewLeft, Theme::stale().withAlpha (0.7f), false, 1.5f);
    }

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

            // What Match would build, on top: it is the news. Filled only when there is
            // nothing applied for it to be compared with -- two fills over one another
            // muddy into a third colour that means neither.
            if (drawsPreview())
            {
                const auto alone = correctionLeft.size() < 2;

                drawCorrection (g, plot, previewLeft, Theme::stale(), alone);

                if (! linked)
                    drawCorrection (g, plot, previewRight, Theme::stale().withRotatedHue (0.04f), false, 1.5f);
            }
            break;
    }

    g.restoreState();

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

    // Anywhere else on the graph takes hold of the view itself, to slide it up or down.
    // A band edge wins where the two meet: it is the smaller target, and the one you
    // were aiming at if the pointer is that close to it.
    if (! dragging.has_value() && getPlot().contains (event.position))
    {
        viewDragFrom = viewShiftDb;
        setMouseCursor (juce::MouseCursor::UpDownResizeCursor);
    }

    repaint();
}

void SpectrumDisplay::mouseDrag (const juce::MouseEvent& event)
{
    mousePosition = event.position;

    if (viewDragFrom.has_value())
    {
        // Like taking hold of paper: drag down and the curves come down with the pointer,
        // which brings the higher values into view.
        const auto plot = getPlot();
        setViewShift (*viewDragFrom + (float) event.getDistanceFromDragStartY() * plot.correctionDbPerPixel());
        return;
    }

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

    // Reported once the view has settled, not on every move: what listens to this
    // writes it into the session.
    if (viewDragFrom.has_value())
    {
        if (! juce::exactlyEqual (*viewDragFrom, viewShiftDb) && onViewShiftChanged != nullptr)
            onViewShiftChanged (viewShiftDb);

        viewDragFrom.reset();
    }

    dragging.reset();
    hovered = handleAt (event.position);
    setMouseCursor (hovered.has_value() ? juce::MouseCursor::LeftRightResizeCursor
                                        : juce::MouseCursor::NormalCursor);
    repaint();
}

void SpectrumDisplay::mouseDoubleClick (const juce::MouseEvent& event)
{
    // Back to the window as designed. Not on a band edge, where a double-click is far
    // more likely the second half of a fumbled grab than a request to reset the view.
    if (handleAt (event.position).has_value() || juce::exactlyEqual (viewShiftDb, 0.0f))
        return;

    setViewShift (0.0f);

    if (onViewShiftChanged != nullptr)
        onViewShiftChanged (viewShiftDb);
}
