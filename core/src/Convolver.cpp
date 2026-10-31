#include "sonic/Convolver.h"
#include "sonic/Analysis.h"

#include <algorithm>

namespace sonic
{

void PartitionedConvolver::prepare (int partitionSize, int maxIrLength)
{
    P = partitionSize;
    maxLen = ((maxIrLength + P - 1) / P) * P;
    numParts = maxLen / P;
    for (auto& slot : irSlots)
        slot.assign ((size_t) numParts, Spectrum ((size_t) (2 * P)));
    partsInSlot[0] = partsInSlot[1] = 0;
    fdl.assign ((size_t) numParts, Spectrum ((size_t) (2 * P)));
    inBlock.assign ((size_t) P, 0.0);
    prevBlock.assign ((size_t) P, 0.0);
    outBlock.assign ((size_t) P, 0.0f);
    work.assign ((size_t) (2 * P), {});
    acc.assign ((size_t) (2 * P), {});
    ready = 0;
    active = 0;
    reset();
}

void PartitionedConvolver::reset()
{
    for (auto& s : fdl)
        std::fill (s.begin(), s.end(), std::complex<double> {});
    std::fill (inBlock.begin(), inBlock.end(), 0.0);
    std::fill (prevBlock.begin(), prevBlock.end(), 0.0);
    std::fill (outBlock.begin(), outBlock.end(), 0.0f);
    fdlPos = 0;
    pos = 0;
}

bool PartitionedConvolver::loadIr (const std::vector<double>& ir)
{
    if (ready.load() != active.load())
        return false; // previous IR not yet taken
    const int slot = 1 - active.load();
    const int len = std::min ((int) ir.size(), maxLen);
    const int parts = (len + P - 1) / P;
    std::vector<std::complex<double>> buf ((size_t) (2 * P));
    for (int p = 0; p < parts; ++p)
    {
        std::fill (buf.begin(), buf.end(), std::complex<double> {});
        for (int i = 0; i < P; ++i)
        {
            const int idx = p * P + i;
            if (idx < len)
                buf[(size_t) i] = ir[(size_t) idx];
        }
        fft (buf);
        irSlots[slot][(size_t) p] = buf;
    }
    partsInSlot[slot] = parts;
    ready.store (slot);
    return true;
}

float PartitionedConvolver::processSample (float x)
{
    inBlock[(size_t) pos] = x;
    const float y = outBlock[(size_t) pos];
    if (++pos == P)
    {
        pos = 0;
        processBlock();
    }
    return y;
}

void PartitionedConvolver::processBlock()
{
    // Switch IR at a block boundary if a new one is ready.
    const int r = ready.load();
    if (r != active.load())
        active.store (r);
    const int slot = active.load();
    const int parts = partsInSlot[slot];

    // Input spectrum of [previous block | current block]
    for (int i = 0; i < P; ++i)
    {
        work[(size_t) i] = prevBlock[(size_t) i];
        work[(size_t) (i + P)] = inBlock[(size_t) i];
    }
    std::copy (inBlock.begin(), inBlock.end(), prevBlock.begin());
    fft (work);
    fdlPos = (fdlPos + numParts - 1) % numParts;
    fdl[(size_t) fdlPos] = work;

    std::fill (acc.begin(), acc.end(), std::complex<double> {});
    for (int p = 0; p < parts; ++p)
    {
        const auto& X = fdl[(size_t) ((fdlPos + p) % numParts)];
        const auto& H = irSlots[slot][(size_t) p];
        for (int k = 0; k < 2 * P; ++k)
            acc[(size_t) k] += X[(size_t) k] * H[(size_t) k];
    }

    // Inverse FFT via conjugation; keep the last P samples (overlap-save)
    for (auto& v : acc)
        v = std::conj (v);
    fft (acc);
    const double scale = 1.0 / (2.0 * P);
    for (int i = 0; i < P; ++i)
        outBlock[(size_t) i] = (float) (acc[(size_t) (i + P)].real() * scale);
}

} // namespace sonic
