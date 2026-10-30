#include "sonic/Chain.h"

#include <algorithm>

namespace sonic
{

uint64_t Chain::pack (const std::vector<int>& o)
{
    uint64_t v = (uint64_t) o.size() & 0xF;
    for (size_t i = 0; i < o.size(); ++i)
        v |= ((uint64_t) (o[i] & 0xF)) << (4 * (i + 1));
    return v;
}

std::vector<int> Chain::unpack (uint64_t v)
{
    const int n = (int) (v & 0xF);
    std::vector<int> o ((size_t) n);
    for (int i = 0; i < n; ++i)
        o[(size_t) i] = (int) ((v >> (4 * (i + 1))) & 0xF);
    return o;
}

void Chain::addModule (std::unique_ptr<Module> m)
{
    if ((int) modules.size() >= kMaxModules)
        return;
    modules.push_back (std::move (m));
    enabled.push_back (std::make_unique<std::atomic<bool>> (true));
    auto o = order();
    o.push_back ((int) modules.size() - 1);
    packedOrder.store (pack (o));
}

std::vector<int> Chain::order() const { return unpack (packedOrder.load()); }

void Chain::setOrder (const std::vector<int>& newOrder)
{
    // Must be a permutation of all module indices.
    if ((int) newOrder.size() != size())
        return;
    std::vector<int> sorted = newOrder;
    std::sort (sorted.begin(), sorted.end());
    for (int i = 0; i < size(); ++i)
        if (sorted[(size_t) i] != i)
            return;
    packedOrder.store (pack (newOrder));
}

void Chain::moveSlot (int from, int to)
{
    auto o = order();
    if (from < 0 || to < 0 || from >= (int) o.size() || to >= (int) o.size() || from == to)
        return;
    const int m = o[(size_t) from];
    o.erase (o.begin() + from);
    o.insert (o.begin() + to, m);
    setOrder (o);
}

void Chain::prepare (double sampleRate, int maxBlockSize, int numChannels)
{
    for (auto& m : modules)
        m->prepare (sampleRate, maxBlockSize, numChannels);
}

void Chain::reset()
{
    for (auto& m : modules)
        m->reset();
}

void Chain::process (float* const* ch, int numCh, int n)
{
    const uint64_t v = packedOrder.load (std::memory_order_acquire);
    const int count = (int) (v & 0xF);
    for (int i = 0; i < count; ++i)
    {
        const int idx = (int) ((v >> (4 * (i + 1))) & 0xF);
        if (idx < (int) modules.size() && enabled[(size_t) idx]->load (std::memory_order_relaxed))
            modules[(size_t) idx]->process (ch, numCh, n);
    }
}

int Chain::latencySamples() const
{
    int total = 0;
    for (size_t i = 0; i < modules.size(); ++i)
        if (enabled[i]->load())
            total += modules[i]->latencySamples();
    return total;
}

} // namespace sonic
