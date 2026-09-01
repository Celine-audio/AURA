#include "FilterDesigner.h"

#include <juce_dsp/juce_dsp.h>

#include <algorithm>
#include <cmath>
#include <complex>

namespace FilterDesigner
{
    static float linToDb (float lin) noexcept
    {
        constexpr float floor = 1.0e-9f;
        return 20.0f * std::log10 (std::max (lin, floor));
    }

    static float dbToLin (float db) noexcept
    {
        return std::pow (10.0f, db / 20.0f);
    }

    static float smoothstep (float x) noexcept
    {
        x = std::clamp (x, 0.0f, 1.0f);
        return x * x * (3.0f - 2.0f * x);
    }

    // 1 inside [low, high], ramping smoothly to 0 across transOct octaves at each edge.
    // A low bound only applies when lowHz is above the 20 Hz minimum.
    static float bandWeight (float freq, float lowHz, float highHz, float transOct) noexcept
    {
        const bool hasLowBound = lowHz > Params::noLowBound;
        const bool hasHighBound = highHz < Params::noHighBound;

        if (transOct <= 0.0f)
        {
            if (hasLowBound && freq < lowHz)   return 0.0f;
            if (hasHighBound && freq > highHz) return 0.0f;
            return 1.0f;
        }

        if (freq <= 0.0f)               // DC: only cut it if a low bound is active
            return hasLowBound ? 0.0f : 1.0f;

        const auto logF = std::log2 (freq);
        float w = 1.0f;

        if (hasLowBound)
        {
            const auto lo0 = std::log2 (lowHz) - transOct; // fully attenuated below here
            w *= smoothstep ((logF - lo0) / transOct);
        }

        if (hasHighBound)
        {
            const auto hi1 = std::log2 (highHz) + transOct; // fully attenuated above here
            w *= smoothstep ((hi1 - logF) / transOct);
        }

        return std::clamp (w, 0.0f, 1.0f);
    }

    void smoothOctaves (std::vector<float>& db, double sampleRate, float octaves)
    {
        const auto numBins = db.size();

        if (octaves <= 0.0f || numBins < 4 || sampleRate <= 0.0)
            return;

        // Smoothing is only meaningful on a log-frequency axis: a fixed-width window
        // in linear bins would be microscopic in the bass and enormous in the treble.
        // So resample onto a log grid, convolve with a fixed-width Gaussian there, and
        // map back. That also keeps the cost independent of the smoothing width.
        const double binHz = sampleRate / (double) ((numBins - 1) * 2);
        const double minHz = binHz;                              // first non-DC bin
        const double maxHz = binHz * (double) (numBins - 1);     // Nyquist

        if (! (maxHz > minHz))
            return;

        constexpr int gridSize = 1024;
        const double logMin = std::log2 (minHz);
        const double logSpan = std::log2 (maxHz) - logMin;
        const double pointsPerOctave = (double) (gridSize - 1) / logSpan;

        // 1) Linear bins -> log grid.
        std::vector<float> grid ((size_t) gridSize, 0.0f);
        for (int i = 0; i < gridSize; ++i)
        {
            const auto freq = std::exp2 (logMin + logSpan * (double) i / (double) (gridSize - 1));
            const auto pos = std::clamp (freq / binHz, 0.0, (double) (numBins - 1));
            const auto k0 = (size_t) pos;
            const auto k1 = std::min (k0 + 1, numBins - 1);
            const auto frac = (float) (pos - (double) k0);
            grid[(size_t) i] = db[k0] + frac * (db[k1] - db[k0]);
        }

        // 2) Gaussian blur, done as three box blurs. Convolving the kernel directly
        //    costs O(gridSize * radius), and at the widest settings the radius runs to
        //    several hundred points — enough that re-deriving the curve blew a whole
        //    UI frame every time a knob moved. Three passes of a running-sum box blur
        //    cost O(gridSize) whatever the width. Away from the ends of the grid it
        //    tracks the true Gaussian to within about 0.05 dB, rising to ~0.3 dB at
        //    the widest setting; the two differ more at the very edges, where both are
        //    guessing past the end of the data and the band-limit fade attenuates the
        //    result anyway.
        const double sigma = std::max (0.5, (double) octaves * 0.5 * pointsPerOctave);

        // Three boxes of width w have variance 3 * (w^2 - 1) / 12, so matching sigma
        // means w = sqrt(4 * sigma^2 + 1). Odd widths keep the result centred.
        const auto boxRadius = std::min (gridSize - 1,
                                         (int) std::lround ((std::sqrt (4.0 * sigma * sigma + 1.0) - 1.0) * 0.5));

        std::vector<float> smoothed (grid);

        if (boxRadius > 0)
        {
            std::vector<float> scratch ((size_t) gridSize, 0.0f);
            const double inverseWidth = 1.0 / (double) (2 * boxRadius + 1);

            for (int pass = 0; pass < 3; ++pass)
            {
                // Clamping at the edges holds the end values rather than fading them
                // towards zero, which would introduce a spurious roll-off.
                auto at = [&] (int i) { return (double) smoothed[(size_t) std::clamp (i, 0, gridSize - 1)]; };

                double sum = 0.0;
                for (int j = -boxRadius; j <= boxRadius; ++j)
                    sum += at (j);

                scratch[0] = (float) (sum * inverseWidth);

                for (int i = 1; i < gridSize; ++i)
                {
                    sum += at (i + boxRadius) - at (i - boxRadius - 1);
                    scratch[(size_t) i] = (float) (sum * inverseWidth);
                }

                smoothed.swap (scratch);
            }
        }

        // 3) Log grid -> linear bins.
        for (size_t k = 0; k < numBins; ++k)
        {
            const auto freq = (double) k * binHz;
            if (freq <= minHz)
            {
                db[k] = smoothed[0]; // DC and the first bin follow the bottom of the grid
                continue;
            }

            const auto pos = std::clamp ((std::log2 (freq) - logMin) * (double) (gridSize - 1) / logSpan,
                                         0.0, (double) (gridSize - 1));
            const auto i0 = (size_t) pos;
            const auto i1 = std::min (i0 + 1, (size_t) (gridSize - 1));
            const auto frac = (float) (pos - (double) i0);
            db[k] = smoothed[i0] + frac * (smoothed[i1] - smoothed[i0]);
        }
    }

    std::vector<float> computeCorrectionDb (const std::vector<float>& sourceMag,
                                            const std::vector<float>& referenceMag,
                                            double sampleRate,
                                            const Params& params)
    {
        const auto numBins = std::min (sourceMag.size(), referenceMag.size());
        std::vector<float> correctionDb (numBins, 0.0f);

        constexpr float epsilon = 1.0e-9f;
        // Negative amounts are useful too: they push the source further away from the
        // reference, which is how you exaggerate a difference instead of removing it.
        const auto amount = std::clamp (params.amount, -1.0f, 1.0f);

        for (size_t k = 0; k < numBins; ++k)
        {
            const auto ratio = (referenceMag[k] + epsilon) / (sourceMag[k] + epsilon);
            auto db = linToDb (ratio);
            db = std::clamp (db, -params.maxGainDb, params.maxGainDb);
            correctionDb[k] = db * amount;
        }

        smoothOctaves (correctionDb, sampleRate, params.smoothingOctaves);

        // Restrict the correction to [lowFreqHz, highFreqHz] with a smooth roll-off,
        // so the extreme lows/highs (often noisy in the capture) stay flat.
        if (numBins > 1)
        {
            const auto binHz = sampleRate / (double) ((numBins - 1) * 2);
            for (size_t k = 0; k < numBins; ++k)
            {
                const auto freq = (float) ((double) k * binHz);
                correctionDb[k] *= bandWeight (freq, params.lowFreqHz, params.highFreqHz, params.transitionOctaves);
            }
        }

        return correctionDb;
    }

    std::vector<float> dbToMagnitudes (const std::vector<float>& db)
    {
        std::vector<float> mag (db.size(), 1.0f);
        for (size_t k = 0; k < db.size(); ++k)
            mag[k] = dbToLin (db[k]);
        return mag;
    }

    std::vector<float> computeCorrectionMagnitudes (const std::vector<float>& sourceMag,
                                                    const std::vector<float>& referenceMag,
                                                    double sampleRate,
                                                    const Params& params)
    {
        return dbToMagnitudes (computeCorrectionDb (sourceMag, referenceMag, sampleRate, params));
    }

    void applyLink (std::vector<float>& leftDb, std::vector<float>& rightDb, float link)
    {
        const auto n = std::min (leftDb.size(), rightDb.size());
        const auto amount = std::clamp (link, 0.0f, 1.0f);

        if (amount <= 0.0f)
            return;

        for (size_t k = 0; k < n; ++k)
        {
            const auto average = 0.5f * (leftDb[k] + rightDb[k]);

            if (amount >= 1.0f)
            {
                // Assign rather than interpolate, so fully linked really is one
                // shared curve rather than two that agree to within a rounding error.
                leftDb[k] = average;
                rightDb[k] = average;
            }
            else
            {
                leftDb[k]  += amount * (average - leftDb[k]);
                rightDb[k] += amount * (average - rightDb[k]);
            }
        }
    }

    std::vector<float> averageDb (const std::vector<float>& leftDb, const std::vector<float>& rightDb)
    {
        const auto n = std::min (leftDb.size(), rightDb.size());
        std::vector<float> out (n, 0.0f);
        for (size_t k = 0; k < n; ++k)
            out[k] = 0.5f * (leftDb[k] + rightDb[k]);
        return out;
    }

    // The correction is computed on the analysis FFT's grid, which need not be the
    // length of the response we are about to build. Straight indexing would leave the
    // top of a longer response's spectrum at zero, so read it by position instead.
    static float magnitudeAt (const std::vector<float>& mag, int bin, int numBins)
    {
        if (mag.empty())
            return 1.0f;

        if (mag.size() == 1 || numBins <= 1)
            return mag.front();

        const auto position = (double) bin * (double) (mag.size() - 1) / (double) (numBins - 1);
        const auto lower = (size_t) std::min ((double) (mag.size() - 1), std::floor (position));
        const auto upper = std::min (lower + 1, mag.size() - 1);
        const auto fraction = (float) (position - (double) lower);

        return mag[lower] + fraction * (mag[upper] - mag[lower]);
    }

    std::vector<float> buildLinearPhaseIR (const std::vector<float>& correctionMag, int fftSize)
    {
        const int order = (int) std::round (std::log2 ((double) fftSize));
        juce::dsp::FFT fft (order);

        const int numBins = fftSize / 2 + 1;

        // Build a Hermitian-symmetric, zero-phase spectrum so the IFFT is real.
        std::vector<std::complex<float>> spectrum ((size_t) fftSize, { 0.0f, 0.0f });

        for (int k = 0; k < numBins; ++k)
            spectrum[(size_t) k] = { magnitudeAt (correctionMag, k, numBins), 0.0f };

        for (int k = 1; k < fftSize / 2; ++k)
            spectrum[(size_t) (fftSize - k)] = spectrum[(size_t) k];

        std::vector<std::complex<float>> timeDomain ((size_t) fftSize, { 0.0f, 0.0f });
        fft.perform (spectrum.data(), timeDomain.data(), true);

        // fftshift: centre the zero-lag so the filter is linear phase (causal).
        std::vector<float> ir ((size_t) fftSize, 0.0f);
        const int half = fftSize / 2;
        for (int i = 0; i < fftSize; ++i)
            ir[(size_t) i] = timeDomain[(size_t) ((i + half) % fftSize)].real();

        // Taper the truncated IR to suppress ripple.
        std::vector<float> win ((size_t) fftSize);
        juce::dsp::WindowingFunction<float>::fillWindowingTables (
            win.data(), (size_t) fftSize, juce::dsp::WindowingFunction<float>::hann, false);

        double sum = 0.0;
        for (int i = 0; i < fftSize; ++i)
        {
            ir[(size_t) i] *= win[(size_t) i];
            sum += ir[(size_t) i];
        }

        // Normalise so the IR's DC gain matches the target, independent of the
        // FFT's inverse-scaling convention.
        const float targetDc = correctionMag.empty() ? 1.0f : correctionMag.front();
        if (std::abs (sum) > 1.0e-12)
        {
            const auto scale = (float) ((double) targetDc / sum);
            for (auto& s : ir)
                s *= scale;
        }

        return ir;
    }

    std::vector<float> buildMinimumPhaseIR (const std::vector<float>& correctionMag, int irLength)
    {
        // Work at four times the length we want to keep. The cepstrum is computed on a
        // circle, so it wraps; the further the working length runs past the response,
        // the less of that wrap lands in the taps we keep.
        const int workSize = juce::nextPowerOfTwo (irLength * 4);
        const int order = (int) std::round (std::log2 ((double) workSize));
        juce::dsp::FFT fft (order);

        const int numBins = workSize / 2 + 1;

        // 1) The log magnitude, over the whole circle. The floor keeps a band the
        //    correction has driven to nothing from taking the log to negative
        //    infinity and the exponential below to zero everywhere.
        constexpr float floorMag = 1.0e-6f;

        std::vector<std::complex<float>> spectrum ((size_t) workSize, { 0.0f, 0.0f });

        for (int k = 0; k < numBins; ++k)
        {
            const auto mag = std::max (magnitudeAt (correctionMag, k, numBins), floorMag);
            spectrum[(size_t) k] = { std::log (mag), 0.0f };
        }

        for (int k = 1; k < workSize / 2; ++k)
            spectrum[(size_t) (workSize - k)] = spectrum[(size_t) k];

        // 2) Into the cepstral domain.
        std::vector<std::complex<float>> cepstrum ((size_t) workSize, { 0.0f, 0.0f });
        fft.perform (spectrum.data(), cepstrum.data(), true);

        // 3) Fold onto the causal half. A minimum-phase signal is exactly one whose
        //    cepstrum is causal, so this is the step that chooses the phase: keep
        //    n == 0 and the midpoint, double the causal half, discard the rest.
        for (int n = 1; n < workSize / 2; ++n)
        {
            cepstrum[(size_t) n] *= 2.0f;
            cepstrum[(size_t) (workSize - n)] = { 0.0f, 0.0f };
        }

        // 4) Back to a spectrum, and exponentiate: exp of the folded log-spectrum is
        //    the same magnitude carrying minimum phase.
        std::vector<std::complex<float>> folded ((size_t) workSize, { 0.0f, 0.0f });
        fft.perform (cepstrum.data(), folded.data(), false);

        for (auto& bin : folded)
            bin = std::exp (bin);

        // 5) And into the time domain, where it is already causal.
        std::vector<std::complex<float>> timeDomain ((size_t) workSize, { 0.0f, 0.0f });
        fft.perform (folded.data(), timeDomain.data(), true);

        std::vector<float> ir ((size_t) irLength, 0.0f);

        for (int i = 0; i < irLength; ++i)
            ir[(size_t) i] = timeDomain[(size_t) i].real();

        // Taper only the tail. The head is the part that matters and must not be
        // touched; the last eighth is faded out so truncation does not leave a step.
        const int fadeLength = irLength / 8;

        for (int i = 0; i < fadeLength; ++i)
        {
            const auto position = (float) i / (float) fadeLength;
            const auto gain = 0.5f + 0.5f * std::cos (juce::MathConstants<float>::pi * position);
            ir[(size_t) (irLength - fadeLength + i)] *= gain;
        }

        // Same DC normalisation as the linear-phase build, and for the same reason.
        double sum = 0.0;
        for (auto tap : ir)
            sum += (double) tap;

        const float targetDc = correctionMag.empty() ? 1.0f : correctionMag.front();

        if (std::abs (sum) > 1.0e-12)
        {
            const auto scale = (float) ((double) targetDc / sum);

            for (auto& tap : ir)
                tap *= scale;
        }

        return ir;
    }
}
