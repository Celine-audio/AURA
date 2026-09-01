#pragma once

#include <vector>

/**
    Turns a captured source spectrum and a reference spectrum into a matching EQ
    curve, and from that curve into a linear-phase FIR impulse response.

    Curves are carried in the dB domain right up until the IR is built, because
    that is where averaging (L/R linking) and smoothing behave musically.
*/
namespace FilterDesigner
{
    struct Params
    {
        float amount = 1.0f;                  // -1..1, how much of the correction to apply
                                              // (negative inverts it)
        float maxGainDb = 24.0f;              // per-bin boost/cut limit
        float smoothingOctaves = 1.0f / 3.0f; // width of the Gaussian window; 0 = no smoothing
        float lowFreqHz = 20.0f;              // correction fades out below this
        float highFreqHz = 20000.0f;          // correction fades out above this
        float transitionOctaves = 0.5f;       // roll-off width at each band edge

        /** At these two values the band is open on that side: no roll-off at all, and
            the correction runs to DC and to Nyquist respectively.

            They are the ends of the corresponding parameters' own ranges, so a control
            sitting at its limit means "do not limit this end" rather than "limit it
            here". Without that, the top end could never be switched off: the roll-off
            begins at highFreqHz and takes transitionOctaves to complete, so a limit of
            20 kHz put the correction only halfway down by the time it ran into Nyquist
            at 48 kHz -- and only a quarter of the way at 44.1. The low end has always
            worked this way; the high end did not, which is the asymmetry this names. */
        static constexpr float noLowBound = 20.0f;
        static constexpr float noHighBound = 20000.0f;
    };

    /** Computes the per-bin correction in dB (referenceMag / sourceMag), clamped,
        amount-scaled, smoothed over a fractional-octave Gaussian window and faded
        out beyond the band limits.
        sourceMag and referenceMag must be the same length (numBins == fftSize/2 + 1). */
    std::vector<float> computeCorrectionDb (const std::vector<float>& sourceMag,
                                            const std::vector<float>& referenceMag,
                                            double sampleRate,
                                            const Params& params);

    /** As computeCorrectionDb(), but returns linear magnitude gains. */
    std::vector<float> computeCorrectionMagnitudes (const std::vector<float>& sourceMag,
                                                    const std::vector<float>& referenceMag,
                                                    double sampleRate,
                                                    const Params& params);

    /** Converts a dB curve to linear magnitude gains. */
    std::vector<float> dbToMagnitudes (const std::vector<float>& db);

    /** Blends two per-channel dB curves towards their common average.
        link == 0 leaves each channel with its own correction; link == 1 gives both
        channels the average of the two. Both vectors must be the same length. */
    void applyLink (std::vector<float>& leftDb, std::vector<float>& rightDb, float link);

    /** Returns the element-wise average of two dB curves (the mono / "L+R" curve). */
    std::vector<float> averageDb (const std::vector<float>& leftDb, const std::vector<float>& rightDb);

    /** Smooths a dB curve with a Gaussian window of the given width in octaves,
        evaluated on a log-frequency grid. A width of 0 leaves the curve untouched. */
    void smoothOctaves (std::vector<float>& db, double sampleRate, float octaves);

    /** Builds a linear-phase FIR impulse response of the given length whose magnitude
        response follows correctionMag, which may be on a shorter grid — it is
        resampled onto the response's own. The result is symmetric about its centre,
        so its group delay is irLength / 2, and Hann-tapered to suppress the ripple
        truncation would otherwise leave. */
    std::vector<float> buildLinearPhaseIR (const std::vector<float>& correctionMag, int irLength);

    /** Builds a minimum-phase FIR with the same magnitude response.

        Same magnitude, least possible group delay: all the energy is pulled to the
        front of the response, so the filter can run with no reported latency and
        without the pre-ringing a symmetric response smears ahead of a transient. The
        cost is that phase is no longer flat, which is the trade the mode exists to
        offer.

        Derived through the real cepstrum: a minimum-phase signal's log-spectrum is
        causal in the cepstral domain, so folding the cepstrum onto its causal half
        and exponentiating back is the phase that goes with this magnitude. */
    std::vector<float> buildMinimumPhaseIR (const std::vector<float>& correctionMag, int irLength);
}
