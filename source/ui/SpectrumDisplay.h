#pragma once

#include "PlotGeometry.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <optional>
#include <vector>

/**
    The plugin's main curve view. It shows one of three things at a time, following
    the tab bar below it: the current signal, the reference signal, or the corrective
    EQ curve derived from the two.

    There are two dB scales. The left one is an absolute dBFS scale that the signal
    spectra are drawn against — nothing here is normalised, so a quiet signal reads as
    quiet and silence falls to the floor. The right one is the symmetric boost/cut
    scale the correction curve is drawn against. They are ranged as a 2:1 pair so that
    one set of horizontal gridlines serves both, and the grid therefore does not move
    when the tab does; see spectrumFloorDb.

    Whichever tab is selected, the correction is drawn faintly underneath, because it
    is the thing the plugin is doing and it is worth being able to see it while
    looking at the material it was derived from.

    All data is supplied from the message thread (the editor's timer). Spectra are
    linear magnitudes indexed by FFT bin (length == fftSize/2 + 1), on the analyzer's
    absolute scale where 1.0 is a full-scale sine; the correction curves are in dB
    over the same bins.
*/
class SpectrumDisplay : public juce::Component,
                        public juce::SettableTooltipClient
{
public:
    /** Which of the three tabs the view is showing. */
    enum class View
    {
        current,    // the signal going through the plugin now
        reference,  // the material being matched to
        eqCurve     // the correction the plugin is applying
    };

    SpectrumDisplay();

    void setSampleRate (double newSampleRate) noexcept { sampleRate = newSampleRate > 0.0 ? newSampleRate : sampleRate; }
    void setFftSize (int newFftSize) noexcept { fftSize = newFftSize > 0 ? newFftSize : fftSize; }

    void setView (View newView);
    View getView() const noexcept { return view; }

    /** Which curve a setCurve() call is supplying. */
    enum class Curve
    {
        liveCurrent,      // the moving trace of what is going through the plugin now
        liveReference,    // the moving trace of the reference / sidechain
        learnedCurrent,   // the settled average a Learn pass built up
        learnedReference
    };

    /** Hands the display one curve's worth of data for this frame. Copies rather than
        takes ownership: the editor refills the same scratch buffers every frame, and
        assigning into the vector already here reuses its storage instead of allocating
        a new one thirty times a second. Pass an empty span to clear a curve. */
    void setCurve (Curve, const std::vector<float>& mags);

    /** The correction the plugin is applying, per channel, in dB. When the channels
        are linked the two are identical and only one trace is drawn. */
    void setCorrection (const std::vector<float>& leftDb, const std::vector<float>& rightDb,
                        bool channelsAreLinked);

    /** How solidly each moving trace is drawn, 1 down to 0. The editor winds these
        down when its analyzer stops producing frames, so a stopped transport dissolves
        the live spectrum instead of leaving the last one standing there.

        Opacity rather than level: winding the magnitudes down instead just slides the
        trace onto the dB floor, where the scale clamps it, and leaves a bright line
        lying along the bottom of the graph that never goes away. */
    void setLiveFade (float currentLevel, float referenceLevel) noexcept
    {
        liveCurrentFade = juce::jlimit (0.0f, 1.0f, currentLevel);
        liveReferenceFade = juce::jlimit (0.0f, 1.0f, referenceLevel);
    }

    /** Shifts the whole correction curve vertically, so the output trim reads as
        part of the response the plugin is applying. */
    void setCorrectionOffsetDb (float offsetDb) noexcept { correctionOffsetDb = offsetDb; }

    void setBand (float lowHz, float highHz) noexcept { bandLow = lowHz; bandHigh = highHz; }

    /** Which end of the correction's band a drag is moving. */
    enum class BandEdge { low, high };

    /** Called while a band edge is being dragged on the graph, naming the edge and the
        frequency it has been dragged to. The editor turns that into a parameter
        change; the display does not own the value it is drawing.

        One edge, not both. It used to report the pair, which meant the editor wrote
        and opened a host gesture on both parameters however few of them had moved --
        so dragging the low edge with automation armed recorded a write for the high
        one too.

        Only live on the EQ Curve view, because the band is only about the correction
        — dragging an edge on a signal tab would be changing something the picture
        does not show. */
    std::function<void (BandEdge, float hz)> onBandDragged;

    /** Bracketing a drag, so the host records one gesture rather than a stream of
        unrelated writes. Names the same edge onBandDragged will. */
    std::function<void (BandEdge, bool starting)> onBandGesture;

    /** Dims the whole view and says why, e.g. while the plugin is bypassed. */
    void setOverlayMessage (const juce::String& message);

    void paint (juce::Graphics&) override;

    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;

    /** The colour the given view is drawn in. Also used by the tab bar, so a tab
        matches the curve it selects.

        A function rather than the three static Colour objects this used to be: those
        were initialised by copying the palette, which is a static in another
        translation unit, and nothing orders the two. Whenever the link order put
        these first they copied a default-constructed Colour — opaque black — and the
        entire graph and tab bar drew in greys. */
    static juce::Colour colourFor (View);

private:
    // How prominently one spectrum trace is drawn. A moving live trace and the settled
    // Learn curve share a colour, so weight is what tells them apart, and a trace that
    // is only there for context drops its outline entirely.
    struct TraceStyle
    {
        bool fill = false;
        bool stroke = true;
        float thickness = 1.3f;
        float alpha = 1.0f;
    };

    // Which of the two dB scales a curve's values belong to.
    enum class Scale { spectrum, correction };

    /** Where the curves are drawn, and how values map into it. */
    PlotGeometry getPlot() const;

    // Turns a per-bin curve into a screen path, collapsing every bin that lands in the
    // same pixel column down to one point. Above a couple of kHz there are hundreds of
    // bins per column, and stroking all of them cost more per frame than everything
    // else in the editor put together.
    juce::Path buildCurvePath (PlotGeometry, const std::vector<float>& values, Scale) const;

    // True while the correction is the view's subject rather than merely drawn behind
    // it. What it now governs is what you can do — the band edges are shown and
    // draggable, and the readout reports dB of correction rather than dBFS of signal.
    // It used to decide where the gridlines went as well; they no longer move.
    bool showingCorrectionScale() const noexcept { return view == View::eqCurve; }

    void drawGrid (juce::Graphics&, PlotGeometry, juce::Rectangle<float> full) const;
    void drawSpectrum (juce::Graphics&, PlotGeometry, const std::vector<float>& mags,
                       juce::Colour colour, TraceStyle) const;
    void drawCorrection (juce::Graphics&, PlotGeometry, const std::vector<float>& db,
                         juce::Colour colour, bool fill, float thickness = 2.0f) const;
    void drawBandShading (juce::Graphics&, PlotGeometry) const;

    /** Which band edge, if either, the pointer is close enough to take hold of.
        Empty rather than a "none" member, so the two states cannot be confused with
        the two edges the callbacks deal in. */
    std::optional<BandEdge> handleAt (juce::Point<float> position) const;
    void drawReadout (juce::Graphics&, PlotGeometry) const;

    float interpolateAt (const std::vector<float>& values, float freq) const;

    // The value the readout reports at the hovered frequency, and its units, for
    // whichever scale the current view is drawn against. Returns false when the view
    // has nothing to read there.
    bool readoutValueAt (float freq, juce::String& text) const;

    double sampleRate = 48000.0;
    int fftSize = 4096;

    View view = View::current;

    std::vector<float> liveCurrent, liveReference, learnedCurrent, learnedReference;
    std::vector<float> correctionLeft, correctionRight;
    float correctionOffsetDb = 0.0f;
    bool linked = true;

    float liveCurrentFade = 1.0f, liveReferenceFade = 1.0f;

    float bandLow = 20.0f;
    float bandHigh = 20000.0f;

    juce::String overlayMessage;

    juce::Point<float> mousePosition;
    bool mouseIsOver = false;

    // Which edge is being dragged, and which one the pointer is merely over — the
    // second so a grip lights up before you commit to it.
    std::optional<BandEdge> dragging, hovered;

    /** How close the pointer has to get, in pixels. Generous: the line itself is one
        pixel and nobody can hit that. */
    static constexpr float handleReach = 7.0f;


    static constexpr float axisBottom = 26.0f;        // frequency labels, plus the two unit captions
    static constexpr float axisTop = 9.0f;            // clearance for the topmost dB label
    static constexpr float axisLeft = 36.0f;          // room for the signal dB labels
    static constexpr float axisRight = 36.0f;         // room for the correction dB labels

    JUCE_LEAK_DETECTOR (SpectrumDisplay)
};
