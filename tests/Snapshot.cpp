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
    if (auto* e = dynamic_cast<AtelierEditor*> (ed.get())) e->selectBlock (comp);
    shot ("main.png");
    if (auto* e = dynamic_cast<AtelierEditor*> (ed.get())) e->openBlock (eq);
    shot ("eq.png");
    if (auto* e = dynamic_cast<AtelierEditor*> (ed.get())) { e->closeBlock(); e->openBlock (g.addNode ("character.saturation", 900, -10)); }
    shot ("saturation.png");
    run = false;
    audio.join();
    ed.reset();
    proc->releaseResources();
    return 0;
}
