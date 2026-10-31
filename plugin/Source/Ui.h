#pragma once
// Shared editor state passed to every panel.

#include "AnalysisWorker.h"
#include "Widgets.h"

#include <functional>

struct Ui
{
    Ui (AtelierProcessor& p, AnalysisWorker& w) : proc (p), worker (w) {}

    AtelierProcessor& proc;
    AnalysisWorker& worker;
    AnalysisWorker::Results res;   // latest analysis (updated by the editor timer)
    bool frozen = false;

    // selection: block id, or sonic::Graph::kInputId / kOutputId, or -1
    int selected = -1;
    std::function<void()> onSelectionChanged;
    std::function<void (int)> openFullEditor;   // block id
    std::function<void()> onBridgeChanged;      // structure changed (rebuild views)

    void select (int id)
    {
        selected = id;
        if (onSelectionChanged) onSelectionChanged();
    }

    sonic::Graph::Node* selectedNode() { return selected >= 2 ? proc.graph().node (selected) : nullptr; }

    // Sum of gain reduction meters of all active compressors (dB, <= 0)
    float totalGainReduction()
    {
        float gr = 0.0f;
        auto& g = proc.graph();
        for (int id : g.activeOrder())
            if (auto* n = g.node (id); n != nullptr && n->enabled.load() && n->type == "dynamics.compressor")
                gr += n->module->meter (0);
        return gr;
    }
};
