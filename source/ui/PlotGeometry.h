#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <cmath>

/**
    The mapping between the graph's rectangle and what is drawn in it: frequency along
    a logarithmic x axis, and two independent dB scales sharing the y axis.

    A type rather than a set of loose functions because everything that draws on the
    graph needs all of it, and passing the rectangle to each one separately meant every
    caller re-derived the same three mappings from it and could disagree about them.

    The two dB ranges are a deliberate 2:1 pair -- 96 dB of signal against 48 dB of
    correction -- so that dividing both into `gridDivisions` puts their gridlines in
    exactly the same places. That is what lets one fixed grid serve whichever tab is
    showing, instead of the lines jumping by half a division when the subject changes.
    Break the ratio and the labels stop landing on the lines.

    The window onto both can be slid up and down -- see shiftDb -- without changing
    either span. They move together and in the same 2:1 proportion, so a gridline at a
    round number of one scale is still at a round number of the other however far the
    view has been moved.
*/
struct PlotGeometry
{
    juce::Rectangle<float> bounds;

    /** How far the view has been slid, in dB of correction: positive shows higher values,
        negative lower. The spectrum scale moves by twice this, which keeps the pair 2:1.
        Zero is the window as designed: ±24 dB of correction, 0 to -96 dBFS. */
    float shiftDb = 0.0f;

    //==========================================================================
    static constexpr float minFreq = 20.0f;
    static constexpr float maxFreq = 20000.0f;

    /** Symmetric boost/cut range the correction curve is drawn against. */
    static constexpr float correctionRangeDb = 24.0f;

    /** Absolute dBFS window for the signal spectra. Low enough for the top octave of
        real material to stay on screen at a 4096-point FFT, and topping out at full
        scale, which is the usual analyser convention. */
    static constexpr float spectrumTopDb = 0.0f;
    static constexpr float spectrumFloorDb = -96.0f;

    /** Horizontal divisions of the plot: 12 dB of signal, 6 dB of correction. */
    static constexpr int gridDivisions = 8;

    /** One division, in dB of correction. The gridlines are placed at multiples of it,
        which is what keeps them on the values rather than on the plot once it moves. */
    static constexpr float correctionStepDb = 2.0f * correctionRangeDb / (float) gridDivisions;

    /** How far the view may be slid. Down until the floor is the deepest cut the
        correction can make (FilterDesigner's 60 dB), and up by half a scale -- enough to
        get the top of a loud signal off the edge without slipping into empty space. */
    static constexpr float minShiftDb = -36.0f;
    static constexpr float maxShiftDb = 12.0f;

    //==========================================================================
    // The rectangle's own scalar accessors, forwarded, so drawing code can ask this
    // one object where the plot is as well as what a value means in it.
    float getX() const noexcept       { return bounds.getX(); }
    float getY() const noexcept       { return bounds.getY(); }
    float getRight() const noexcept   { return bounds.getRight(); }
    float getBottom() const noexcept  { return bounds.getBottom(); }
    float getWidth() const noexcept   { return bounds.getWidth(); }
    float getHeight() const noexcept  { return bounds.getHeight(); }
    float getCentreX() const noexcept { return bounds.getCentreX(); }
    float getCentreY() const noexcept { return bounds.getCentreY(); }

    bool contains (juce::Point<float> point) const noexcept { return bounds.contains (point); }

    //==========================================================================
    float freqToX (float hz) const noexcept
    {
        return bounds.getX() + freqProportion (hz) * bounds.getWidth();
    }

    float xToFreq (float x) const noexcept
    {
        if (bounds.getWidth() <= 0.0f)
            return minFreq;

        const auto proportion = juce::jlimit (0.0f, 1.0f, (x - bounds.getX()) / bounds.getWidth());
        return std::pow (10.0f, logMin() + proportion * (logMax() - logMin()));
    }

    // The two windows as they stand, after the shift.
    float correctionBottomDb() const noexcept { return shiftDb - correctionRangeDb; }
    float correctionTopDb() const noexcept    { return shiftDb + correctionRangeDb; }
    float spectrumBottomDb() const noexcept   { return spectrumFloorDb + 2.0f * shiftDb; }
    float spectrumTopDbNow() const noexcept   { return spectrumTopDb + 2.0f * shiftDb; }

    /** What a correction value reads as on the spectrum scale: the label on the other
        side of the same gridline. Independent of the shift, since both scales move. */
    static float spectrumDbAtCorrection (float correctionDb) noexcept
    {
        return spectrumFloorDb + (correctionDb + correctionRangeDb) * (spectrumTopDb - spectrumFloorDb)
                                     / (2.0f * correctionRangeDb);
    }

    // Not held to either edge. A value outside the window maps outside the plot, and the
    // display clips every curve to the plot, so a curve leaving the view is cut off at
    // the edge it leaves by, top or bottom alike. Holding it to the floor, as this once
    // did, drew a flat line along the bottom wherever a trace was quieter than the
    // window or a cut deeper than it -- a line that looked like part of the picture, and
    // that the top edge, which had already stopped doing it, no longer drew.
    float correctionDbToY (float db) const noexcept
    {
        return proportionToY ((db - correctionBottomDb()) / (2.0f * correctionRangeDb));
    }

    float spectrumDbToY (float db) const noexcept
    {
        return proportionToY ((db - spectrumBottomDb()) / (spectrumTopDb - spectrumFloorDb));
    }

    /** How many dB of correction a vertical distance on screen spans, for dragging. */
    float correctionDbPerPixel() const noexcept
    {
        return bounds.getHeight() > 0.0f ? 2.0f * correctionRangeDb / bounds.getHeight() : 0.0f;
    }

    /** 0 at the bottom of the plot, 1 at the top. What the gridlines are placed by,
        since they belong to both scales at once. */
    float proportionToY (float proportion) const noexcept
    {
        return bounds.getBottom() - proportion * bounds.getHeight();
    }

private:
    static float logMin() noexcept { return std::log10 (minFreq); }
    static float logMax() noexcept { return std::log10 (maxFreq); }

    static float freqProportion (float hz) noexcept
    {
        const auto clamped = juce::jlimit (minFreq, maxFreq, hz);
        return (std::log10 (clamped) - logMin()) / (logMax() - logMin());
    }
};
