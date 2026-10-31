#pragma once
// Pictures of what each block does (measured offline on a copy of the block, see
// sonic/Preview.h), drawn in the Maison style.

#include "Ui.h"

#include <sonic/Preview.h>

// Background thread that renders block figures. The newest request always wins.
class PreviewEngine : private juce::Thread
{
public:
    PreviewEngine() : juce::Thread ("Atelier figures") { startThread (juce::Thread::Priority::low); }
    ~PreviewEngine() override
    {
        cancel = true;
        signalThreadShouldExit();
        notify();
        stopThread (4000);
    }

    void request (int nodeId, sonic::preview::Settings s, unsigned kinds, juce::uint64 stamp)
    {
        const juce::ScopedLock sl (lock);
        if (busyNode >= 0 && busyNode != nodeId)
            cancel = true;   // a different block was selected: drop the old render
        pending = { nodeId, std::move (s), kinds, stamp };
        hasPending = true;
        notify();
    }

    // Copies the newest finished result for nodeId if it differs from 'stamp'.
    bool fetch (int nodeId, juce::uint64& stamp, sonic::preview::Result& out)
    {
        const juce::ScopedLock sl (lock);
        if (done.nodeId != nodeId || ! done.valid || done.stamp == stamp)
            return false;
        out = done.result;
        stamp = done.stamp;
        return true;
    }

private:
    struct Job { int nodeId = -1; sonic::preview::Settings s; unsigned kinds = 0; juce::uint64 stamp = 0; };
    struct Done { int nodeId = -1; juce::uint64 stamp = 0; bool valid = false; sonic::preview::Result result; };

    void run() override
    {
        while (! threadShouldExit())
        {
            Job job;
            {
                const juce::ScopedLock sl (lock);
                if (hasPending)
                {
                    job = pending;
                    hasPending = false;
                    busyNode = job.nodeId;
                    cancel = false;
                }
            }
            if (job.nodeId < 0) { wait (-1); continue; }
            auto r = sonic::preview::render (job.s, job.kinds, &cancel);
            const juce::ScopedLock sl (lock);
            busyNode = -1;
            if (! cancel.load())
            {
                done.nodeId = job.nodeId;
                done.stamp = job.stamp;
                done.valid = true;
                done.result = std::move (r);
            }
        }
    }

    juce::CriticalSection lock;
    Job pending;
    bool hasPending = false;
    int busyNode = -1;
    Done done;
    std::atomic<bool> cancel { false };
};

// The figure of one block: requests renders when the block changes and draws them.
class BlockFigure : public juce::Component, private juce::Timer
{
public:
    BlockFigure (Ui& ui, int nodeId, PreviewEngine& engine, unsigned onlyKinds = ~0u);
    void paint (juce::Graphics&) override;
    bool hasResult() const { return have; }
    void poll() { timerCallback(); }   // tests / snapshots without a message loop
    const sonic::preview::Result& result() const { return res; }

private:
    void timerCallback() override;
    juce::uint64 stampNow() const;

    void drawResponse (juce::Graphics&, juce::Rectangle<float>, bool big);
    void drawHarmonics (juce::Graphics&, juce::Rectangle<float>);
    void drawTransfer (juce::Graphics&, juce::Rectangle<float>);
    void drawDynamics (juce::Graphics&, juce::Rectangle<float>);
    void drawEnvelope (juce::Graphics&, juce::Rectangle<float>);
    void drawImpulse (juce::Graphics&, juce::Rectangle<float>);
    void drawModMap (juce::Graphics&, juce::Rectangle<float>);
    void drawPitch (juce::Graphics&, juce::Rectangle<float>);
    void drawStereo (juce::Graphics&, juce::Rectangle<float>);

    Ui& ui;
    int nodeId;
    PreviewEngine& engine;
    std::string type;
    unsigned kinds = 0;
    juce::uint64 requested = 0, received = 0;
    double lastChange = 0.0;
    sonic::preview::Result res;
    bool have = false;
    std::vector<float> grHistory;   // live gain reduction (compressor)
};

namespace fig
{
    // A plot sheet with a mono caption above it; returns the inner plot area.
    juce::Rectangle<float> sheet (juce::Graphics&, juce::Rectangle<float> r, const juce::String& caption, const juce::String& note = {});
    void logFreqAxis (juce::Graphics&, juce::Rectangle<float> plot, bool labels = true, double fMin = 20.0, double fMax = 20000.0);
    void dbAxis (juce::Graphics&, juce::Rectangle<float> plot, double dbMin, double dbMax, double step, const juce::String& unit = {});
    void harmonicBar (juce::Graphics&, juce::Rectangle<float> bar, int k, float alpha = 1.0f);
}
