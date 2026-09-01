#pragma once

#include <juce_dsp/juce_dsp.h>

#include <atomic>
#include <vector>

/**
    FIR convolution with no latency of its own, whose filter can be replaced while
    audio is running without discarding the input it has already heard.

    Both of those matter here. juce::dsp::Convolution builds a fresh engine for every
    loadImpulseResponse(), and a fresh engine has an empty input history — so for the
    first impulse-response length of samples its output is missing everything that
    came before the swap. Crossfading into that is audible as a click, and it happens
    even when the response handed over is identical to the one already loaded. A
    matching EQ reshapes its curve from five different knobs, so it reloads
    constantly, and every reload clicked.

    The response is split in two. The first partition is convolved directly, sample by
    sample, which costs nothing in latency. The rest goes through a uniformly-
    partitioned overlap-save engine, which is inherently one partition behind — which
    is exactly the delay those taps needed anyway. Summed, the two reproduce the whole
    response with the first tap landing on the same sample that produced it.

    The input history lives in a frequency-domain delay line and a sample ring that
    the filter swap never touches; only the coefficients are exchanged, at a frame
    boundary, under a try-lock the audio thread will never wait on. And they are
    walked to rather than jumped to: convolution is linear and both filters share one
    history, so blending the coefficients is exactly a crossfade of the two outputs.
*/
class PartitionedConvolver
{
public:
    /** Frame size, and the length of the directly-convolved head. */
    static constexpr int partitionSize = 256;

    PartitionedConvolver() = default;

    /** Sizes everything for the given channel count and response length. Allocates;
        call it from prepareToPlay and never from the audio thread. */
    void prepare (int numChannels, int irLength);

    /** Clears the input history and any part-finished output. Keeps the filter. */
    void reset();

    /** Message thread: hands over a new filter. One channel of the buffer per channel
        prepared; a mono buffer is used for every channel. Takes effect at the next
        frame boundary, walked to over roughly 40 ms. */
    void setImpulseResponse (const juce::AudioBuffer<float>& ir);

    /** True once a filter has been handed over. */
    bool hasImpulseResponse() const noexcept { return loaded.load(); }

    /** Audio thread: filters in place. A no-op until a filter has been set. */
    void process (juce::AudioBuffer<float>& buffer) noexcept;

    /** Zero. The response's own group delay is the caller's to report. */
    int getLatencySamples() const noexcept { return 0; }

private:
    using Complex = juce::dsp::Complex<float>;

    void processFrame (int channel) noexcept;
    void convolveHead (int channel, float* samples, int numSamples) noexcept;
    void pickUpPendingFilter() noexcept;
    void advanceRamp() noexcept;

    struct Channel
    {
        // The frame being filled, held twice over so that the last partitionSize
        // samples are always one contiguous run whatever the position in the frame.
        // Its first half is also the completed frame that processFrame() transforms:
        // there is no separate copy of that, because the ring already is one.
        std::vector<float> ring;        // 2 * partitionSize
        std::vector<float> previous;    // the frame before it
        std::vector<float> tailFifo;    // the frame of tail output being drained
        std::vector<Complex> fdl;       // numPartitions spectra of fftSize
    };

    std::unique_ptr<juce::dsp::FFT> fft;

    int fftSize = 0;
    int numPartitions = 0;
    int numChannels = 0;

    std::vector<Channel> channels;

    // Audio thread only, so process() never allocates.
    std::vector<Complex> timeScratch, freqScratch, accumulator;

    // The directly-convolved head, numChannels * partitionSize, stored time-reversed
    // so the sliding dot product runs forwards over contiguous memory.
    std::vector<float> headActive, headTarget, headPending;

    // The partitioned tail, numChannels * numPartitions * fftSize.
    std::vector<Complex> tailActive, tailTarget, tailPending;

    // Frames left in the walk from active to target. Roughly 40 ms: long enough that a
    // step in the curve is inaudible, short enough to finish inside the throttle that
    // paces rebuilds.
    static constexpr int rampFrames = 8;
    int rampFramesLeft = 0;

    // Audio thread only: whether the active coefficients are a real filter yet.
    bool applied = false;

    juce::SpinLock swapLock;
    std::atomic<bool> pendingReady { false };
    std::atomic<bool> loaded { false };

    int writeIndex = 0;

    // How far into the current frame we are, 0 to partitionSize - 1. This was two
    // separate counters, one for the ring and one for the frame; they were incremented
    // together from the same start and so could never differ.
    int framePosition = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PartitionedConvolver)
};
