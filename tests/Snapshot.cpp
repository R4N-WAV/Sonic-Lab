// Renders the editor to PNG files without a window (for visual checks during development).
#include "../plugin/Source/PluginEditor.h"
#include "../plugin/Source/PluginProcessor.h"

#include <thread>

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI gui;
    const juce::File outDir (argc > 1 ? juce::String (argv[1]) : juce::File::getCurrentWorkingDirectory().getFullPathName());
    auto proc = std::make_unique<AtelierProcessor>();
    proc->setPlayConfigDetails (2, 2, 48000.0, 512);
    proc->prepareToPlay (48000.0, 512);
    auto set = [&] (const char* id, float v) { if (auto* p = proc->apvts.getParameter (id)) p->setValueNotifyingHost (p->convertTo0to1 (v)); };

    // a small bridge: tube -> eq -> compressor, with a parallel delay
    auto& g = proc->graph();
    const int tube = g.addNode ("tube.stage", 220, 132);
    const int eq = g.addNode ("tone.eq", 456, 132);
    const int comp = g.addNode ("dynamics.compressor", 692, 132);
    const int dly = g.addNode ("space.delay", 692, 290);
    const int pit = g.addNode ("pitch.shift", 456, -10);
    g.disconnect (sonic::Graph::kInputId, sonic::Graph::kOutputId);
    g.connect (sonic::Graph::kInputId, tube); g.connect (tube, eq); g.connect (eq, comp); g.connect (comp, sonic::Graph::kOutputId);
    g.connect (eq, dly); g.connect (dly, sonic::Graph::kOutputId);
    juce::ignoreUnused (pit);
    if (auto* n = g.node (eq))
    {
        auto& m = *n->module;
        auto band = [&] (int b, int shape, float f, float gain, float q, bool dyn = false)
        {
            using EQ = sonic::ParametricEqModule;
            m.setParam (EQ::bandParam (b, EQ::fOn), 1); m.setParam (EQ::bandParam (b, EQ::fShape), (float) shape);
            m.setParam (EQ::bandParam (b, EQ::fFreq), f); m.setParam (EQ::bandParam (b, EQ::fGain), gain); m.setParam (EQ::bandParam (b, EQ::fQ), q);
            if (dyn) { m.setParam (EQ::bandParam (b, EQ::fDyn), 1); m.setParam (EQ::bandParam (b, EQ::fRange), -6); m.setParam (EQ::bandParam (b, EQ::fThr), -40); }
        };
        band (0, 0, 120, 2.5f, 1.4f); band (1, 0, 400, -3, 1.8f); band (2, 0, 2500, 2, 0.9f); band (3, 0, 6300, -2.5f, 1.2f, true); band (4, 2, 12000, 3.5f, 0.7f);
    }
    g.prepare (48000.0, 512);
    set ("src.mode", 1.0f);      // generator
    set ("gen.run", 1.0f);
    set ("gen.level_db", -14.0f);
    if (auto* p = proc->apvts.getParameter ("gen.waveform")) p->setValueNotifyingHost (p->convertTo0to1 (6.0f));

    std::atomic<bool> run { true };
    std::thread audio ([&]
    {
        juce::AudioBuffer<float> b (4, 512);
        juce::MidiBuffer midi;
        while (run)
        {
            b.clear();
            proc->processBlock (b, midi);
            std::this_thread::sleep_for (std::chrono::milliseconds (10));
        }
    });

    std::unique_ptr<juce::AudioProcessorEditor> ed (proc->createEditor());
    ed->setSize (1600, 1000);
    auto shot = [&] (const juce::String& name)
    {
        for (int i = 0; i < 70; ++i)
        {
            juce::Thread::sleep (40);
            if (auto* e = dynamic_cast<AtelierEditor*> (ed.get())) e->pumpAnalysis();
            if (i % 10 == 0) ed->createComponentSnapshot ({ 0, 0, 8, 8 }, true, 1.0f);   // lets paint-time state settle
        }
        auto img = ed->createComponentSnapshot (ed->getLocalBounds(), true, 1.0f);
        juce::FileOutputStream os (outDir.getChildFile (name));
        os.setPosition (0); os.truncate();
        juce::PNGImageFormat().writeImageToStream (img, os);
        std::printf ("wrote %s\n", outDir.getChildFile (name).getFullPathName().toRawUTF8());
    };
    auto* e = dynamic_cast<AtelierEditor*> (ed.get());
    const juce::String only = argc > 2 ? juce::String (argv[2]) : juce::String();
    auto want = [&] (const char* n) { return only.isEmpty() || only == n; };
    e->selectBlock (-1);
    if (want ("main")) shot ("main.png");
    e->selectBlock (comp);
    if (want ("comp")) shot ("comp.png");
    e->selectBlock (eq);
    if (want ("eq")) shot ("eq.png");
    for (auto [type, name] : { std::pair<const char*, const char*> { "character.saturation", "saturation" }, { "character.tape", "tape" },
                               { "space.reverb", "reverb" }, { "mod.phaser", "phaser" }, { "dynamics.transient", "transient" },
                               { "pitch.shift", "pitch" }, { "utility.stereo", "stereo" }, { "tone.filter", "filter" } })
    {
        if (! want (name)) continue;
        const int id = g.addNode (type, 900, 300);
        if (juce::String (name) == "pitch") g.node (id)->module->setParam ("semitones", 7.0f);
        if (juce::String (name) == "filter") { g.node (id)->module->setParam ("lfo_rate_hz", 0.5f); g.node (id)->module->setParam ("lfo_depth_oct", 1.5f); }
        e->selectBlock (id);
        shot (juce::String (name) + ".png");
    }
    e->selectBlock (dly);
    if (want ("delay")) shot ("delay.png");
    e->selectBlock (sonic::Graph::kInputId);
    if (want ("input")) shot ("input.png");
    e->selectBlock (-1);
    if (want ("export")) { e->openExportDialog(); shot ("export.png"); }
    run = false;
    audio.join();
    ed.reset();
    proc->releaseResources();
    return 0;
}
