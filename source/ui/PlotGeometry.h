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
*/
struct PlotGeometry
{
    juce::Rectangle<float> bounds;

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

    float correctionDbToY (float db) const noexcept
    {
        const auto clamped = juce::jlimit (-correctionRangeDb, correctionRangeDb, db);
        return proportionToY ((clamped + correctionRangeDb) / (2.0f * correctionRangeDb));
    }

    float spectrumDbToY (float db) const noexcept
    {
        const auto clamped = juce::jlimit (spectrumFloorDb, spectrumTopDb, db);
        return proportionToY ((clamped - spectrumFloorDb) / (spectrumTopDb - spectrumFloorDb));
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
