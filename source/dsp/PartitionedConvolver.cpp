#include "PartitionedConvolver.h"

#include <algorithm>

namespace
{
    /** Dot product of two runs of floats.

        Four running totals rather than one, which is not micro-optimisation but the
        whole performance of this file. Float addition is not associative, so a
        single-accumulator reduction is a serial dependency chain that the compiler is
        *forbidden* to reorder -- and therefore forbidden to vectorise -- without
        -ffast-math, which this build does not use and should not. Four independent
        chains it can vectorise, because each one keeps its own summation order.

        Measured over a 512-sample stereo block with a 256-tap head: 140us before,
        which was three quarters of everything the plugin did on the audio thread. */
    float dotProduct (const float* a, const float* b, int numValues) noexcept
    {
        float acc0 = 0.0f, acc1 = 0.0f, acc2 = 0.0f, acc3 = 0.0f;

        int k = 0;

        for (; k + 4 <= numValues; k += 4)
        {
            acc0 += a[k]     * b[k];
            acc1 += a[k + 1] * b[k + 1];
            acc2 += a[k + 2] * b[k + 2];
            acc3 += a[k + 3] * b[k + 3];
        }

        // partitionSize is a power of two so this never runs, but the function should
        // not quietly drop values if that ever stops being true.
        float remainder = 0.0f;

        for (; k < numValues; ++k)
            remainder += a[k] * b[k];

        return (acc0 + acc1) + (acc2 + acc3) + remainder;
    }
}

void PartitionedConvolver::prepare (int channelCount, int irLength)
{
    numChannels = juce::jmax (1, channelCount);

    // The head takes the first partition; everything past it goes to the tail.
    const auto tailLength = juce::jmax (0, irLength - partitionSize);
    numPartitions = (tailLength + partitionSize - 1) / partitionSize;

    // Overlap-save: a frame of partitionSize new samples is transformed together with
    // the partitionSize before it, and the second half of the result is the part free
    // of wrap-around.
    fftSize = partitionSize * 2;
    fft = std::make_unique<juce::dsp::FFT> (juce::roundToInt (std::log2 ((double) fftSize)));

    channels.clear();
    channels.resize ((size_t) numChannels);

    for (auto& channel : channels)
    {
        // Twice the length, holding each sample in both halves, so the most recent
        // partitionSize samples are always one contiguous run.
        channel.ring.assign ((size_t) partitionSize * 2, 0.0f);
        channel.previous.assign ((size_t) partitionSize, 0.0f);
        channel.tailFifo.assign ((size_t) partitionSize, 0.0f);
        channel.fdl.assign ((size_t) juce::jmax (1, numPartitions) * (size_t) fftSize, Complex {});
    }

    timeScratch.assign ((size_t) fftSize, Complex {});
    freqScratch.assign ((size_t) fftSize, Complex {});
    accumulator.assign ((size_t) fftSize, Complex {});

    const auto headSize = (size_t) (numChannels * partitionSize);
    headActive.assign (headSize, 0.0f);
    headTarget.assign (headSize, 0.0f);
    headPending.assign (headSize, 0.0f);

    const auto tailSize = (size_t) numChannels * (size_t) numPartitions * (size_t) fftSize;
    tailActive.assign (tailSize, Complex {});
    tailTarget.assign (tailSize, Complex {});
    tailPending.assign (tailSize, Complex {});

    pendingReady.store (false);
    loaded.store (false);

    rampFramesLeft = 0;
    applied = false;
    writeIndex = 0;
    framePosition = 0;
}

void PartitionedConvolver::reset()
{
    for (auto& channel : channels)
    {
        std::fill (channel.ring.begin(), channel.ring.end(), 0.0f);
        std::fill (channel.previous.begin(), channel.previous.end(), 0.0f);
        std::fill (channel.tailFifo.begin(), channel.tailFifo.end(), 0.0f);
        std::fill (channel.fdl.begin(), channel.fdl.end(), Complex {});
    }

    writeIndex = 0;
    framePosition = 0;
}

void PartitionedConvolver::setImpulseResponse (const juce::AudioBuffer<float>& ir)
{
    if (fft == nullptr || ir.getNumChannels() <= 0)
        return;

    std::vector<Complex> time ((size_t) fftSize), freq ((size_t) fftSize);

    // Held for the whole write, not just the publish at the end. The audio thread only
    // ever try-locks, so it never waits on this -- it keeps running the filter it has
    // and picks the new one up a block later. Publishing first and writing after left a
    // window in which a second call could be filling the pending buffers while the
    // audio thread swapped them out from under it. Throttling made that vanishingly
    // unlikely rather than impossible, which is not the same thing.
    const juce::SpinLock::ScopedLockType sl (swapLock);

    for (int channel = 0; channel < numChannels; ++channel)
    {
        // A mono impulse response is used for every channel.
        const auto* taps = ir.getReadPointer (juce::jmin (channel, ir.getNumChannels() - 1));
        const auto numTaps = ir.getNumSamples();

        const auto tapAt = [&] (int index) { return index < numTaps ? taps[index] : 0.0f; };

        // Head, time-reversed so the sliding dot product in process() reads forwards.
        auto* head = headPending.data() + (size_t) (channel * partitionSize);

        for (int i = 0; i < partitionSize; ++i)
            head[i] = tapAt (partitionSize - 1 - i);

        // Tail, starting one partition in: those are the taps the overlap-save
        // engine's own one-frame delay lines up with.
        for (int partition = 0; partition < numPartitions; ++partition)
        {
            std::fill (time.begin(), time.end(), Complex {});

            // Zero-padded to the full frame so the circular convolution behaves like
            // a linear one.
            for (int i = 0; i < partitionSize; ++i)
                time[(size_t) i] = Complex { tapAt (partitionSize + partition * partitionSize + i), 0.0f };

            fft->perform (time.data(), freq.data(), false);

            auto* dest = tailPending.data()
                       + (size_t) ((channel * numPartitions + partition) * fftSize);
            std::copy (freq.begin(), freq.end(), dest);
        }
    }

    // Publish. The audio thread picks this up at its next frame boundary; if it is
    // mid-frame the flag simply survives until then.
    pendingReady.store (true);
    loaded.store (true);
}

void PartitionedConvolver::processFrame (int channelIndex) noexcept
{
    if (numPartitions <= 0)
        return;

    auto& channel = channels[(size_t) channelIndex];

    // [previous frame | this frame], the window overlap-save needs. "This frame" is
    // the ring's first half, which is where convolveHead() has just finished laying
    // down exactly these partitionSize samples in order.
    for (int i = 0; i < partitionSize; ++i)
    {
        timeScratch[(size_t) i] = Complex { channel.previous[(size_t) i], 0.0f };
        timeScratch[(size_t) (partitionSize + i)] = Complex { channel.ring[(size_t) i], 0.0f };
    }

    std::copy (channel.ring.begin(), channel.ring.begin() + partitionSize,
               channel.previous.begin());

    fft->perform (timeScratch.data(), freqScratch.data(), false);

    // Newest spectrum goes in at writeIndex; partition k pairs with the spectrum k
    // frames older, which is what makes this a convolution across frames.
    std::copy (freqScratch.begin(), freqScratch.end(),
               channel.fdl.begin() + (std::ptrdiff_t) writeIndex * fftSize);

    // Both operands are transforms of real sequences, so both are conjugate-symmetric
    // and so is their product. Only the lower half is worth multiplying; the rest is
    // filled in by reflection afterwards, which halves the work here.
    const auto half = fftSize / 2;

    std::fill (accumulator.begin(), accumulator.begin() + half + 1, Complex {});

    const auto* filter = tailActive.data() + (size_t) (channelIndex * numPartitions * fftSize);

    for (int partition = 0; partition < numPartitions; ++partition)
    {
        const auto slot = (writeIndex - partition + numPartitions) % numPartitions;
        const auto* history = channel.fdl.data() + (size_t) (slot * fftSize);
        const auto* taps = filter + (size_t) (partition * fftSize);

        for (int bin = 0; bin <= half; ++bin)
            accumulator[(size_t) bin] += history[bin] * taps[bin];
    }

    for (int bin = 1; bin < half; ++bin)
        accumulator[(size_t) (fftSize - bin)] = std::conj (accumulator[(size_t) bin]);

    fft->perform (accumulator.data(), freqScratch.data(), true);

    // The first half is contaminated by wrap-around; the second half is the answer.
    for (int i = 0; i < partitionSize; ++i)
        channel.tailFifo[(size_t) i] = freqScratch[(size_t) (partitionSize + i)].real();
}

void PartitionedConvolver::pickUpPendingFilter() noexcept
{
    if (! pendingReady.load())
        return;

    // A try-lock, so a message thread mid-update costs nothing but a retry later.
    const juce::SpinLock::ScopedTryLockType sl (swapLock);

    if (! sl.isLocked())
        return;

    headTarget.swap (headPending);
    tailTarget.swap (tailPending);
    pendingReady.store (false);

    if (applied)
    {
        rampFramesLeft = rampFrames;
        return;
    }

    // The very first filter arrives over silence, so there is nothing to walk away
    // from and every reason to be exact from the very first sample. Swapping rather
    // than copying keeps that off the audio thread's budget; target is left holding
    // the old coefficients, which nothing reads while the ramp is idle.
    headActive.swap (headTarget);
    tailActive.swap (tailTarget);
    rampFramesLeft = 0;
    applied = true;
}

void PartitionedConvolver::advanceRamp() noexcept
{
    if (rampFramesLeft <= 0)
        return;

    const auto step = 1.0f / (float) rampFramesLeft;

    for (size_t i = 0; i < headActive.size(); ++i)
        headActive[i] += (headTarget[i] - headActive[i]) * step;

    for (size_t i = 0; i < tailActive.size(); ++i)
        tailActive[i] += (tailTarget[i] - tailActive[i]) * step;

    --rampFramesLeft;
}

void PartitionedConvolver::convolveHead (int channelIndex, float* samples, int numSamples) noexcept
{
    auto& channel = channels[(size_t) channelIndex];

    auto* ring = channel.ring.data();
    const auto* tail = channel.tailFifo.data();
    const auto* taps = headActive.data() + (size_t) (channelIndex * partitionSize);

    for (int i = 0; i < numSamples; ++i)
    {
        const auto position = framePosition + i;
        const auto input = samples[i];

        // Written to both halves, so the window below is always contiguous.
        ring[position] = input;
        ring[position + partitionSize] = input;

        // The head, convolved here and now: this is the part that costs no latency.
        // The window runs oldest to newest and the taps are stored reversed to match,
        // so a plain dot product is the convolution.
        samples[i] = dotProduct (ring + position + 1, taps, partitionSize) + tail[position];
    }
}

void PartitionedConvolver::process (juce::AudioBuffer<float>& buffer) noexcept
{
    if (fft == nullptr || ! loaded.load())
        return;

    // Before the first sample, not only at frame boundaries: the head is convolved
    // as the samples arrive, so a filter that has been waiting since the last block
    // has to be in place for this one's first sample rather than its 256th.
    pickUpPendingFilter();

    const auto numSamples = buffer.getNumSamples();
    const auto channelsToDo = juce::jmin (numChannels, buffer.getNumChannels());

    for (int start = 0; start < numSamples;)
    {
        // Runs stop at frame boundaries. Inside one, the coefficients cannot change
        // and every channel walks the same positions, which is what lets the channel
        // loop sit outside the sample loop: each channel gets one straight pass along
        // its own ring rather than the two being interleaved sample by sample.
        const auto run = juce::jmin (numSamples - start, partitionSize - framePosition);

        for (int ch = 0; ch < channelsToDo; ++ch)
            convolveHead (ch, buffer.getWritePointer (ch) + start, run);

        start += run;
        framePosition += run;

        if (framePosition < partitionSize)
            continue;

        // A frame has completed: take any filter that has been waiting, step the walk
        // towards it, and transform the frame the head has just finished laying down.
        framePosition = 0;

        pickUpPendingFilter();
        advanceRamp();

        if (numPartitions > 0)
        {
            writeIndex = (writeIndex + 1) % numPartitions;

            for (int ch = 0; ch < channelsToDo; ++ch)
                processFrame (ch);
        }
    }
}
