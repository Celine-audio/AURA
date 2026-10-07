#pragma once

#include <juce_dsp/juce_dsp.h>

#include <initializer_list>
#include <memory>
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
        /** How far the correction may go per bin, asymmetrically, because boosting
            and cutting are not the same risk.

            A boost amplifies whatever is in the capture at that frequency, and where
            the source has rolled off -- a guitar cab above 5 kHz, say -- what is there
            is the noise floor and the analysis window's leakage rather than signal. So
            boosts stay at 24 dB.

            A cut only ever removes, so it can go further, and 60 dB is where the two
            phase modes still agree about what the curve does. Past roughly 96 dB the
            minimum-phase build stops being monotonic -- its cepstrum floors the
            magnitude at 1e-6 -- and asking for a deeper notch starts returning a
            shallower one. 60 also keeps a single dead bin in a capture from gouging
            its neighbours when the smoother below spreads it. */
        float maxBoostDb = 24.0f;
        float maxCutDb = 60.0f;

        /** Taken off the raw difference before anything else is done to it, in dB: the
            overall level difference between the two takes, as levelDifferenceDb()
            measures it. Zero leaves the curve carrying it. MatchEngine sets it so the
            correction matches tone rather than loudness -- see levelDifferenceDb. */
        float levelOffsetDb = 0.0f;
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

    /** One channel's source and reference spectra, for levelDifferenceDb(). */
    struct ChannelSpectra
    {
        const std::vector<float>& source;
        const std::vector<float>& reference;
    };

    /** How much louder the reference is than the source overall, in dB, pooled across
        every channel given: the weighted median of the per-bin difference over the
        audible range, 20 Hz to 20 kHz.

        The whole range rather than the band the correction is confined to, and that is
        deliberate: the band edges are something you drag while listening, and the level
        taken out has to hold still while you do. Measured inside the band, every move of
        an edge moved the whole curve up or down with it. The level difference is a
        property of the two takes, not of how much of the correction you choose to apply.

        Taken off the correction (see Params::levelOffsetDb) so that it matches the two
        takes' tone and not their loudness, the way Logic's Match EQ does. Left in, a
        reference 15 dB hotter than the source -- a mastered track against a mix -- was
        15 dB of gain across the whole curve: it pressed against the boost ceiling, and
        where the band eases the correction back to 0 dB at its edges it did so down a
        15 dB slope.

        One figure for all the channels rather than one each, so that a reference whose
        left side is louder than its right, relative to the source, still asks for that.
        Only the level they share comes out.

        Weighted by each bin's share of a log-frequency axis, so every octave has an
        equal say -- unweighted, the top octave alone holds half the bins. A median rather
        than a mean, because the bins that disagree most say least about level: a
        reference encoded to stop at 16 kHz, a source with nothing under 40 Hz, a hum. */
    float levelDifferenceDb (std::initializer_list<ChannelSpectra> channels, double sampleRate);

    /** Computes the per-bin correction in dB (referenceMag / sourceMag) less
        params.levelOffsetDb, clamped,
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

    /**
        Builds the same two responses, but keeps the transform between calls.

        Which is the whole reason it exists. Constructing the FFT the minimum-phase
        build works at -- 32768 points, four times the response, so the cepstrum's wrap
        lands outside the taps that are kept -- costs about fifteen milliseconds, and
        the transforms themselves take a fifth of one. The free functions above build
        that object and throw it away on every call, so a rebuild spent nine tenths of
        its time on setup it had already done. Under a drag, where a rebuild runs every
        120 ms, that was a third of a core going on nothing, on the message thread,
        which is also the thread that has to draw.

        Not thread-safe, and not meant to be: one lives in MatchEngine, and rebuilds
        happen on the message thread.
    */
    class IrBuilder
    {
    public:
        IrBuilder();
        ~IrBuilder();

        /** @see buildLinearPhaseIR */
        std::vector<float> buildLinearPhase (const std::vector<float>& correctionMag, int irLength);

        /** @see buildMinimumPhaseIR */
        std::vector<float> buildMinimumPhase (const std::vector<float>& correctionMag, int irLength);

    private:
        /** The transform for a given size, made on the first call that wants it and
            kept until one wants a different size -- which in practice never happens,
            since the length is fixed at compile time. */
        juce::dsp::FFT& transformOfSize (int size);

        std::unique_ptr<juce::dsp::FFT> fft;
        int fftSize = 0;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (IrBuilder)
    };
}
