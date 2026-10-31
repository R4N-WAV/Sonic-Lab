#include "sonic/Graph.h"
#include "sonic/DspUtils.h"
#include "sonic/ModuleRegistry.h"

#include <algorithm>
#include <cmath>
#include <functional>
#include <set>

namespace sonic
{

namespace
{
    constexpr int kMask = Graph::kMaxDelay - 1;

    // Stereo delay line: writes the input and adds the signal delayed by d samples.
    struct StereoDelay
    {
        std::vector<float> l, r;
        int w = 0;

        StereoDelay() : l ((size_t) Graph::kMaxDelay, 0.0f), r ((size_t) Graph::kMaxDelay, 0.0f) {}

        void processAdd (const float* inL, const float* inR, float* outL, float* outR, int n, int d) noexcept
        {
            d = std::clamp (d, 0, Graph::kMaxDelay - 1);
            for (int i = 0; i < n; ++i)
            {
                l[(size_t) w] = inL[i];
                r[(size_t) w] = inR[i];
                const int rp = (w - d) & kMask;
                outL[i] += l[(size_t) rp];
                outR[i] += r[(size_t) rp];
                w = (w + 1) & kMask;
            }
        }
    };

    float dbToGainF (float db) { return std::pow (10.0f, db / 20.0f); }
}

struct Graph::Compiled
{
    struct Entry
    {
        Node* node = nullptr;
        std::vector<int> preds;      // indices into entries, -1 = Input
        std::vector<int> predDelay;  // index into delays for each pred
        int dryDelay = 0;
        std::vector<float> outL, outR;
        float prevMix = 1.0f, prevGain = 1.0f;
        int outLat = 0;
    };

    std::vector<std::shared_ptr<Node>> keepAlive;
    std::vector<Entry> entries;
    std::vector<int> outPreds, outPredDelay;
    std::vector<StereoDelay> delays;
    std::vector<float> inL, inR, accL, accR, dryL, dryR, monoR;
    int maxBlock = 0;
};

Graph::Graph()
{
    resetToDefault();
}

Graph::~Graph()
{
    delete current.exchange (nullptr);
    for (auto* c : graveyard)
        delete c;
}

// ------------------------------------------------------------------ structure

int Graph::addNode (const std::string& type, float x, float y, int forcedId)
{
    std::lock_guard<std::recursive_mutex> sl (structureLock);
    auto mod = createModule (type);
    if (mod == nullptr)
        return -1;

    int id = forcedId >= 2 && nodes.find (forcedId) == nodes.end() ? forcedId : nextId;
    nextId = std::max (nextId, id + 1);

    auto n = std::make_shared<Node>();
    n->id = id;
    n->type = type;
    n->name = mod->displayName();
    n->x = x;
    n->y = y;
    n->mix = mod->defaultMix();
    if (fs > 0.0)
        mod->prepare (fs, maxBlock, 2);
    n->module = std::move (mod);
    nodes[id] = n;
    commit();
    return id;
}

bool Graph::removeNode (int id)
{
    std::lock_guard<std::recursive_mutex> sl (structureLock);
    if (nodes.erase (id) == 0)
        return false;
    edgeList.erase (std::remove_if (edgeList.begin(), edgeList.end(),
                                    [id] (const Edge& e) { return e.from == id || e.to == id; }),
                    edgeList.end());
    commit();
    return true;
}

bool Graph::wouldCreateCycle (int from, int to) const
{
    std::lock_guard<std::recursive_mutex> sl (structureLock);
    // Cycle if 'from' is reachable from 'to'.
    std::set<int> seen;
    std::function<bool (int)> reach = [&] (int v)
    {
        if (v == from)
            return true;
        if (! seen.insert (v).second)
            return false;
        for (auto& e : edgeList)
            if (e.from == v && reach (e.to))
                return true;
        return false;
    };
    return reach (to);
}

bool Graph::connect (int from, int to)
{
    std::lock_guard<std::recursive_mutex> sl (structureLock);
    auto exists = [this] (int id) { return id == kInputId || id == kOutputId || nodes.count (id) > 0; };
    if (from == to || from == kOutputId || to == kInputId || ! exists (from) || ! exists (to))
        return false;
    if (std::find (edgeList.begin(), edgeList.end(), Edge { from, to }) != edgeList.end())
        return false;
    if (wouldCreateCycle (from, to))
        return false;
    edgeList.push_back ({ from, to });
    commit();
    return true;
}

bool Graph::disconnect (int from, int to)
{
    std::lock_guard<std::recursive_mutex> sl (structureLock);
    auto it = std::find (edgeList.begin(), edgeList.end(), Edge { from, to });
    if (it == edgeList.end())
        return false;
    edgeList.erase (it);
    commit();
    return true;
}

void Graph::clear()
{
    std::lock_guard<std::recursive_mutex> sl (structureLock);
    nodes.clear();
    edgeList.clear();
    nextId = 2;
    commit();
}

void Graph::resetToDefault()
{
    std::lock_guard<std::recursive_mutex> sl (structureLock);
    nodes.clear();
    edgeList.clear();
    nextId = 2;
    edgeList.push_back ({ kInputId, kOutputId });
    commit();
}

Graph::Node* Graph::node (int id)
{
    std::lock_guard<std::recursive_mutex> sl (structureLock);
    auto it = nodes.find (id);
    return it == nodes.end() ? nullptr : it->second.get();
}

const Graph::Node* Graph::node (int id) const
{
    std::lock_guard<std::recursive_mutex> sl (structureLock);
    auto it = nodes.find (id);
    return it == nodes.end() ? nullptr : it->second.get();
}

std::vector<int> Graph::nodeIds() const
{
    std::lock_guard<std::recursive_mutex> sl (structureLock);
    std::vector<int> ids;
    for (auto& [id, n] : nodes)
        ids.push_back (id);
    return ids;
}

std::vector<Graph::Edge> Graph::edges() const
{
    std::lock_guard<std::recursive_mutex> sl (structureLock);
    return edgeList;
}

std::vector<int> Graph::activeOrder() const
{
    std::lock_guard<std::recursive_mutex> sl (structureLock);

    // Forward reachability from Input, backward from Output.
    std::set<int> fwd, bwd;
    std::function<void (int)> f = [&] (int v)
    {
        if (! fwd.insert (v).second) return;
        for (auto& e : edgeList) if (e.from == v) f (e.to);
    };
    std::function<void (int)> b = [&] (int v)
    {
        if (! bwd.insert (v).second) return;
        for (auto& e : edgeList) if (e.to == v) b (e.from);
    };
    f (kInputId);
    b (kOutputId);

    std::set<int> active;
    for (auto& [id, n] : nodes)
        if (fwd.count (id) && bwd.count (id))
            active.insert (id);

    // Kahn's algorithm restricted to active nodes (Input counts as already done).
    std::map<int, int> indeg;
    for (int id : active)
    {
        indeg[id] = 0;
        for (auto& e : edgeList)
            if (e.to == id && active.count (e.from))
                ++indeg[id];
    }
    std::vector<int> order, ready;
    for (auto& [id, d] : indeg)
        if (d == 0) ready.push_back (id);
    while (! ready.empty())
    {
        std::sort (ready.begin(), ready.end());
        const int v = ready.front();
        ready.erase (ready.begin());
        order.push_back (v);
        for (auto& e : edgeList)
            if (e.from == v && active.count (e.to) && --indeg[e.to] == 0)
                ready.push_back (e.to);
    }
    return order;
}

// ------------------------------------------------------------------ compile / swap

void Graph::commit()
{
    auto* c = new Compiled();
    c->maxBlock = std::max (32, maxBlock);
    const size_t mb = (size_t) c->maxBlock;
    for (auto* v : { &c->inL, &c->inR, &c->accL, &c->accR, &c->dryL, &c->dryR, &c->monoR })
        v->assign (mb, 0.0f);

    const auto order = activeOrder();
    std::map<int, int> indexOf;
    for (int id : order)
    {
        auto n = nodes.at (id);
        c->keepAlive.push_back (n);
        Compiled::Entry e;
        e.node = n.get();
        e.outL.assign (mb, 0.0f);
        e.outR.assign (mb, 0.0f);
        e.prevMix = n->mix.load();
        e.prevGain = dbToGainF (n->outGainDb.load());
        indexOf[id] = (int) c->entries.size();
        c->entries.push_back (std::move (e));
    }

    int delayCount = 0;
    for (auto& e : c->entries)
    {
        for (auto& ed : edgeList)
        {
            if (ed.to != e.node->id)
                continue;
            if (ed.from == kInputId)
                e.preds.push_back (-1);
            else if (indexOf.count (ed.from))
                e.preds.push_back (indexOf[ed.from]);
            else
                continue;
            e.predDelay.push_back (delayCount++);
        }
        e.dryDelay = delayCount++;
    }
    for (auto& ed : edgeList)
    {
        if (ed.to != kOutputId)
            continue;
        if (ed.from == kInputId)
            c->outPreds.push_back (-1);
        else if (indexOf.count (ed.from))
            c->outPreds.push_back (indexOf[ed.from]);
        else
            continue;
        c->outPredDelay.push_back (delayCount++);
    }
    c->delays.resize ((size_t) delayCount);

    if (auto* old = current.exchange (c))
        graveyard.push_back (old);
    structVersion.fetch_add (1);
    collectGarbage();
}

void Graph::collectGarbage()
{
    std::lock_guard<std::recursive_mutex> sl (structureLock);
    for (auto it = graveyard.begin(); it != graveyard.end();)
    {
        if (inUse.load() != *it)
        {
            delete *it;
            it = graveyard.erase (it);
        }
        else
            ++it;
    }
}

void Graph::prepare (double sampleRate, int maxBlockSize)
{
    std::lock_guard<std::recursive_mutex> sl (structureLock);
    fs = sampleRate;
    maxBlock = std::max (32, maxBlockSize);
    for (auto& [id, n] : nodes)
        n->module->prepare (fs, maxBlock, 2);
    commit();
}

// ------------------------------------------------------------------ audio

void Graph::process (float* const* io, int numChannels, int numSamples)
{
    Compiled* c = nullptr;
    do
    {
        c = current.load();
        inUse.store (c);
    } while (c != current.load());

    if (c == nullptr || numChannels < 1)
    {
        inUse.store (nullptr);
        return;
    }

    for (int start = 0; start < numSamples; start += c->maxBlock)
    {
        const int n = std::min (c->maxBlock, numSamples - start);
        float* l = io[0] + start;
        float* r = numChannels > 1 ? io[1] + start : c->monoR.data();
        if (numChannels == 1)
            std::copy (l, l + n, c->monoR.begin());
        processChunk (*c, l, r, n);
    }

    inUse.store (nullptr);
}

void Graph::processChunk (Compiled& c, float* l, float* r, int n)
{
    std::copy (l, l + n, c.inL.begin());
    std::copy (r, r + n, c.inR.begin());

    auto srcL = [&c] (int p) -> const float* { return p < 0 ? c.inL.data() : c.entries[(size_t) p].outL.data(); };
    auto srcR = [&c] (int p) -> const float* { return p < 0 ? c.inR.data() : c.entries[(size_t) p].outR.data(); };
    auto srcLat = [&c] (int p) { return p < 0 ? 0 : c.entries[(size_t) p].outLat; };

    for (auto& e : c.entries)
    {
        Node& nd = *e.node;

        // ---- sum aligned inputs ----
        int arrival = 0;
        for (int p : e.preds)
            arrival = std::max (arrival, srcLat (p));
        std::fill (c.accL.begin(), c.accL.begin() + n, 0.0f);
        std::fill (c.accR.begin(), c.accR.begin() + n, 0.0f);
        for (size_t k = 0; k < e.preds.size(); ++k)
        {
            const int p = e.preds[k];
            c.delays[(size_t) e.predDelay[k]].processAdd (srcL (p), srcR (p), c.accL.data(), c.accR.data(), n, arrival - srcLat (p));
        }

        // ---- process ----
        const bool on = nd.enabled.load (std::memory_order_relaxed);
        const int lat = on ? std::clamp (nd.module->latencySamples(), 0, kMaxDelay - 1) : 0;
        std::copy (c.accL.begin(), c.accL.begin() + n, e.outL.begin());
        std::copy (c.accR.begin(), c.accR.begin() + n, e.outR.begin());

        // dry path delayed by the block's latency (always written to keep history)
        std::fill (c.dryL.begin(), c.dryL.begin() + n, 0.0f);
        std::fill (c.dryR.begin(), c.dryR.begin() + n, 0.0f);
        c.delays[(size_t) e.dryDelay].processAdd (c.accL.data(), c.accR.data(), c.dryL.data(), c.dryR.data(), n, lat);

        if (on)
        {
            nd.module->setHostInfo (host);
            float* chans[2] = { e.outL.data(), e.outR.data() };
            nd.module->process (chans, 2, n);

            const float mixT = std::clamp (nd.mix.load (std::memory_order_relaxed), 0.0f, 1.0f);
            const float gainT = dbToGainF (nd.outGainDb.load (std::memory_order_relaxed));
            const float dm = (mixT - e.prevMix) / (float) n, dg = (gainT - e.prevGain) / (float) n;
            float m = e.prevMix, g = e.prevGain;
            for (int i = 0; i < n; ++i)
            {
                m += dm;
                g += dg;
                float a = (c.dryL[(size_t) i] + (e.outL[(size_t) i] - c.dryL[(size_t) i]) * m) * g;
                float b = (c.dryR[(size_t) i] + (e.outR[(size_t) i] - c.dryR[(size_t) i]) * m) * g;
                e.outL[(size_t) i] = std::isfinite (a) ? a : 0.0f;
                e.outR[(size_t) i] = std::isfinite (b) ? b : 0.0f;
            }
            e.prevMix = mixT;
            e.prevGain = gainT;
        }
        e.outLat = arrival + lat;
    }

    // ---- output: aligned sum ----
    int arrival = 0;
    for (int p : c.outPreds)
        arrival = std::max (arrival, srcLat (p));
    std::fill (l, l + n, 0.0f);
    std::fill (r, r + n, 0.0f);
    for (size_t k = 0; k < c.outPreds.size(); ++k)
    {
        const int p = c.outPreds[k];
        c.delays[(size_t) c.outPredDelay[k]].processAdd (srcL (p), srcR (p), l, r, n, arrival - srcLat (p));
    }
    latency.store (arrival, std::memory_order_relaxed);
}

} // namespace sonic
