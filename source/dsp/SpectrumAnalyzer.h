#pragma once

#include <juce_dsp/juce_dsp.h>
#include <atomic>
#include <cstdint>
#include <vector>

/** The FFT order every analyzer in this plugin uses. Shared with PluginProcessor so
    that arrays of analyzers can be default-constructed at the right size. */
inline constexpr int spectrumFftOrder = 12; // 4096-point FFT

/**
    Accumulates a long-term average magnitude spectrum from a mono signal.

    The audio thread feeds samples via pushBlock(). Overlapping (50%) Hann-windowed
    frames are FFT'd and their power spectra summed into an accumulator. The message
    thread can read back the averaged magnitude spectrum via getAveragedMagnitudes().

    Magnitudes come back on an absolute scale: a full-scale sine sitting on a bin
    reads 1.0, i.e. 0 dBFS. That is what lets the display show real dB rather than a
    curve normalised to its own peak. The correction only ever looks at the ratio of
    two spectra, so the same scaling cancels out there.

    Realtime safety: pushBlock() never allocates and never blocks. Accumulation into
    the shared buffer uses a try-lock, so if the message thread happens to be reading,
    the audio thread simply skips accumulating that single frame.
*/
class SpectrumAnalyzer
{
public:
    explicit SpectrumAnalyzer (int fftOrderToUse = spectrumFftOrder);

    /** Selects how frames are combined:
          - cumulative (default): long-term running average, used for capturing the
            material a match is computed from.
          - exponential: a decaying real-time average for a live moving display.
        decayPerFrame is the weight (0..1) given to each new frame in exponential mode. */
    void setExponentialMode (bool shouldUseExponential, float decayPerFrame = 0.4f) noexcept;

    /** Clears the accumulated spectrum and the input FIFO.

        Safe to call from any thread at any time, including while audio is running.
        It has to be: the live analyzers are cleared when an editor opens, which is a
        message-thread event with no relation to the transport. The two halves are
        cleared by whichever thread owns them -- the averages here and now under the
        lock, the input side by the audio thread at the top of its next pushBlock --
        because the input side is otherwise untouched by any other thread and putting
        a lock around it would be a lock on the hot path for no reason. */
    void reset();

    /** Audio thread: push a block of mono samples. Only accumulates while capturing. */
    void pushBlock (const float* data, int numSamples) noexcept;

    void setCapturing (bool shouldCapture) noexcept { capturing.store (shouldCapture); }
    bool isCapturing() const noexcept { return capturing.load(); }

    /** Multiplies raw FFT magnitudes onto the 0 dBFS-sine scale: the Hann window's
        coherent gain is fftSize/2, and a real sine splits its energy across the
        positive and negative frequency, hence 4/fftSize. */
    float getMagnitudeScale() const noexcept { return magnitudeScale; }

    int getFftSize() const noexcept { return fftSize; }
    int getNumBins() const noexcept { return numBins; }
    std::int64_t getFrameCount() const noexcept { return frameCount.load(); }

    /** Message thread: copies the averaged magnitude spectrum (length == numBins)
        into dest. Returns false if no frames have been captured yet. */
    bool getAveragedMagnitudes (std::vector<float>& dest) const;

private:
    void processFrame() noexcept;

    juce::dsp::FFT fft;
    const int fftSize;
    const int numBins;
    const int hopSize;
    const float magnitudeScale;

    std::vector<float> window;     // Hann window, length fftSize
    std::vector<float> fifo;       // ring buffer of the most recent fftSize samples
    std::vector<float> fftBuffer;  // scratch for the FFT, length 2 * fftSize

    // Audio thread only, which is why none of it is behind the lock.
    int fifoIndex = 0;
    int samplesSinceHop = 0;
    std::int64_t samplesPushed = 0;

    // Set by reset() from any thread, honoured by the audio thread before it pushes
    // anything. Without it, reset() wrote fifo/fifoIndex underneath a pushBlock that
    // was reading them, which is a data race whatever the accumulator lock is doing.
    std::atomic<bool> inputResetPending { false };

    mutable juce::SpinLock accumulatorLock;
    std::vector<double> powerSum;              // length numBins, guarded by accumulatorLock
    std::vector<double> emaPower;              // length numBins, guarded by accumulatorLock
    bool exponentialMode = false;
    double emaAlpha = 0.4;
    bool emaInitialised = false;
    std::atomic<std::int64_t> frameCount { 0 };
    std::atomic<bool> capturing { false };

    JUCE_LEAK_DETECTOR (SpectrumAnalyzer)
};
