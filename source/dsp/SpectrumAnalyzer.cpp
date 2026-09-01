#include "SpectrumAnalyzer.h"

#include <cmath>

SpectrumAnalyzer::SpectrumAnalyzer (int fftOrderToUse)
    : fft (fftOrderToUse),
      fftSize (1 << fftOrderToUse),
      numBins (fftSize / 2 + 1),
      hopSize (fftSize / 2),
      magnitudeScale (4.0f / (float) fftSize)
{
    window.resize ((size_t) fftSize);
    juce::dsp::WindowingFunction<float>::fillWindowingTables (
        window.data(), (size_t) fftSize, juce::dsp::WindowingFunction<float>::hann, false);

    fifo.assign ((size_t) fftSize, 0.0f);
    fftBuffer.assign ((size_t) fftSize * 2, 0.0f);
    powerSum.assign ((size_t) numBins, 0.0);
    emaPower.assign ((size_t) numBins, 0.0);
}

void SpectrumAnalyzer::setExponentialMode (bool shouldUseExponential, float decayPerFrame) noexcept
{
    const juce::SpinLock::ScopedLockType sl (accumulatorLock);
    exponentialMode = shouldUseExponential;
    emaAlpha = (double) juce::jlimit (0.001f, 1.0f, decayPerFrame);
}

void SpectrumAnalyzer::reset()
{
    {
        const juce::SpinLock::ScopedLockType sl (accumulatorLock);
        std::fill (powerSum.begin(), powerSum.end(), 0.0);
        std::fill (emaPower.begin(), emaPower.end(), 0.0);
        emaInitialised = false;
        frameCount.store (0);
    }

    // The input side belongs to the audio thread; it clears it itself. Set last, so a
    // block that starts between the two halves still sees a cleared average.
    inputResetPending.store (true);
}

void SpectrumAnalyzer::pushBlock (const float* data, int numSamples) noexcept
{
    if (inputResetPending.exchange (false))
    {
        std::fill (fifo.begin(), fifo.end(), 0.0f);
        fifoIndex = 0;
        samplesSinceHop = 0;
        samplesPushed = 0;
    }

    if (! capturing.load())
        return;

    // Copied in runs rather than sample by sample. The old loop did a modulo and a
    // branch per sample, on six analyzers at once, for a ring whose length is a power
    // of two -- the compiler could not know that, so the modulo was a real division.
    for (int offset = 0; offset < numSamples;)
    {
        // Stop at whichever comes first: the end of the block, the wrap of the ring,
        // or the next hop.
        const auto run = juce::jmin (numSamples - offset,
                                     fftSize - fifoIndex,
                                     hopSize - samplesSinceHop);

        std::copy (data + offset, data + offset + run, fifo.begin() + fifoIndex);

        offset += run;
        fifoIndex += run;
        samplesSinceHop += run;
        samplesPushed += run;

        if (fifoIndex == fftSize)
            fifoIndex = 0;

        if (samplesSinceHop == hopSize)
        {
            samplesSinceHop = 0;

            // Only produce frames once the ring holds a full window of real samples.
            if (samplesPushed >= fftSize)
                processFrame();
        }
    }
}

void SpectrumAnalyzer::processFrame() noexcept
{
    // Copy the most recent fftSize samples (oldest first) into the FFT scratch,
    // applying the Hann window as we go. The oldest sample sits at fifoIndex, so the
    // window is one run from there to the end of the ring and a second from its
    // start -- two straight passes rather than a modulo on every sample.
    const auto firstRun = fftSize - fifoIndex;

    for (int j = 0; j < firstRun; ++j)
        fftBuffer[(size_t) j] = fifo[(size_t) (fifoIndex + j)] * window[(size_t) j];

    for (int j = firstRun; j < fftSize; ++j)
        fftBuffer[(size_t) j] = fifo[(size_t) (j - firstRun)] * window[(size_t) j];

    std::fill (fftBuffer.begin() + fftSize, fftBuffer.end(), 0.0f);

    // Produces magnitudes in-place in the first fftSize entries.
    fft.performFrequencyOnlyForwardTransform (fftBuffer.data());

    // Try to accumulate; if the message thread is reading, skip this frame.
    const juce::SpinLock::ScopedTryLockType sl (accumulatorLock);
    if (! sl.isLocked())
        return;

    if (exponentialMode)
    {
        if (! emaInitialised)
        {
            for (int k = 0; k < numBins; ++k)
            {
                const auto mag = (double) fftBuffer[(size_t) k];
                emaPower[(size_t) k] = mag * mag;
            }
            emaInitialised = true;
        }
        else
        {
            const auto oneMinus = 1.0 - emaAlpha;
            for (int k = 0; k < numBins; ++k)
            {
                const auto mag = (double) fftBuffer[(size_t) k];
                emaPower[(size_t) k] = emaAlpha * (mag * mag) + oneMinus * emaPower[(size_t) k];
            }
        }
    }
    else
    {
        for (int k = 0; k < numBins; ++k)
        {
            const auto mag = (double) fftBuffer[(size_t) k];
            powerSum[(size_t) k] += mag * mag;
        }
    }

    frameCount.store (frameCount.load() + 1);
}

bool SpectrumAnalyzer::getAveragedMagnitudes (std::vector<float>& dest) const
{
    const juce::SpinLock::ScopedLockType sl (accumulatorLock);

    const auto count = frameCount.load();
    if (count <= 0)
        return false;

    dest.resize ((size_t) numBins);

    if (exponentialMode)
    {
        for (int k = 0; k < numBins; ++k)
            dest[(size_t) k] = magnitudeScale * (float) std::sqrt (emaPower[(size_t) k]);
    }
    else
    {
        const auto invCount = 1.0 / (double) count;
        for (int k = 0; k < numBins; ++k)
            dest[(size_t) k] = magnitudeScale * (float) std::sqrt (powerSum[(size_t) k] * invCount);
    }

    return true;
}
