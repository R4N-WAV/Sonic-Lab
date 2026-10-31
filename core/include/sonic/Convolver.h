#pragma once
// Uniformly partitioned overlap-save FFT convolution (real-time safe after
// prepare). Two impulse-response slots allow a new filter to be loaded by a
// worker thread and switched in at a block boundary without allocation.
// Latency: exactly one partition (P samples).

#include <atomic>
#include <complex>
#include <vector>

namespace sonic
{

class PartitionedConvolver
{
public:
    // Allocates. maxIrLength must be a multiple of partitionSize (power of two).
    void prepare (int partitionSize, int maxIrLength);
    void reset();

    int latency() const { return P; }
    int maxIrLength() const { return maxLen; }

    // Worker thread: writes an IR into the inactive slot. Returns false if the
    // audio thread has not yet picked up the previously loaded IR.
    bool loadIr (const std::vector<double>& ir);

    // Audio thread.
    float processSample (float x);

private:
    void processBlock();

    int P = 256, maxLen = 4096, numParts = 16;
    using Spectrum = std::vector<std::complex<double>>;
    std::vector<Spectrum> irSlots[2];   // [slot][partition] -> 2P bins
    int partsInSlot[2] = { 0, 0 };
    std::atomic<int> ready { 0 }, active { 0 };

    std::vector<Spectrum> fdl;           // frequency-domain delay line of input blocks
    int fdlPos = 0;
    std::vector<double> inBlock, prevBlock;
    std::vector<float> outBlock;
    Spectrum work, acc;
    int pos = 0;
};

} // namespace sonic
