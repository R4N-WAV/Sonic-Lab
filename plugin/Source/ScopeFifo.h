#pragma once
// Multi-channel single-producer/single-consumer sample ring used to hand audio
// from the real-time thread to the analysis worker. All channels share one
// write index, so readers always get time-aligned frames across channels.
// The audio thread never blocks or allocates.

#include <array>
#include <atomic>
#include <vector>

template <int NumChannels>
class ScopeFifo
{
public:
    explicit ScopeFifo (int sizePow2 = 1 << 18) : mask ((size_t) sizePow2 - 1)
    {
        for (auto& b : buffers)
            b.assign ((size_t) sizePow2, 0.0f);
    }

    // channelData[c] may be nullptr (writes silence for that channel).
    void push (const float* const* channelData, int n) noexcept
    {
        const size_t w = writeIndex.load (std::memory_order_relaxed);
        for (int c = 0; c < NumChannels; ++c)
        {
            auto& b = buffers[(size_t) c];
            const float* src = channelData[c];
            for (int i = 0; i < n; ++i)
                b[(w + (size_t) i) & mask] = src != nullptr ? src[i] : 0.0f;
        }
        writeIndex.store (w + (size_t) n, std::memory_order_release);
    }

    size_t totalWritten() const noexcept { return writeIndex.load (std::memory_order_acquire); }
    int capacity() const noexcept { return (int) mask + 1; }

    // Copies frames [endIndex - n, endIndex) of every channel. endIndex is a
    // value previously returned by totalWritten(). Returns false if unavailable.
    bool read (size_t endIndex, int n, std::array<std::vector<float>, NumChannels>& dest) const
    {
        if (endIndex < (size_t) n || n > capacity() / 2)
            return false;
        const size_t start = endIndex - (size_t) n;
        for (int c = 0; c < NumChannels; ++c)
        {
            auto& d = dest[(size_t) c];
            d.resize ((size_t) n);
            const auto& b = buffers[(size_t) c];
            for (int i = 0; i < n; ++i)
                d[(size_t) i] = b[(start + (size_t) i) & mask];
        }
        return true;
    }

private:
    std::array<std::vector<float>, NumChannels> buffers;
    size_t mask;
    std::atomic<size_t> writeIndex { 0 };
};
