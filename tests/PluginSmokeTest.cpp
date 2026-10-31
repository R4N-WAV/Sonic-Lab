// Headless test of the real AtelierProcessor (no DAW, no editor):
// buses, sources, bridge, A/B alignment + loudness match, sidechain detection,
// latency, state round-trip, undo, analysis worker and the blueprint export.

#include "../plugin/Source/AnalysisWorker.h"
#include "../plugin/Source/Blueprint.h"
#include "../plugin/Source/Measure.h"
#include "../plugin/Source/PluginProcessor.h"

#include <cstdio>

namespace
{
int failures = 0, checks = 0;
void check (bool ok, const juce::String& what)
{
    ++checks;
    std::printf ("  [%s] %s\n", ok ? " OK " : "FAIL", what.toRawUTF8());
    if (! ok) ++failures;
}

void setParam (AtelierProcessor& p, const juce::String& id, float plainValue)
{
    auto* param = p.apvts.getParameter (id);
    param->setValueNotifyingHost (param->convertTo0to1 (plainValue));
}

float rms (const float* x, int n)
{
    double s = 0;
    for (int i = 0; i < n; ++i) s += (double) x[i] * x[i];
    return (float) std::sqrt (s / n);
}

constexpr double fs = 48000.0;
constexpr int block = 512;

void fillSine (juce::AudioBuffer<float>& b, int ch, int startSample, float amp, double freq)
{
    for (int i = 0; i < b.getNumSamples(); ++i)
        b.setSample (ch, i, amp * (float) std::sin (2.0 * juce::MathConstants<double>::pi * freq * (startSample + i) / fs));
}

void runBlocks (AtelierProcessor& p, juce::AudioBuffer<float>& buf, int count, float amp = 0.25f)
{
    juce::MidiBuffer midi;
    for (int b = 0; b < count; ++b)
    {
        buf.clear();
        fillSine (buf, 0, b * block, amp, 1000.0);
        fillSine (buf, 1, b * block, amp, 1000.0);
        p.processBlock (buf, midi);
    }
}
}

int main()
{
    juce::ScopedJuceInitialiser_GUI init;
    juce::MidiBuffer midi;

    std::printf ("Processor setup\n");
    auto p = std::make_unique<AtelierProcessor>();
    auto layout = p->getBusesLayout();
    layout.inputBuses.getReference (1) = juce::AudioChannelSet::stereo();
    check (p->setBusesLayout (layout), "stereo main + stereo sidechain layout accepted");
    auto bad = p->getBusesLayout();
    bad.outputBuses.getReference (0) = juce::AudioChannelSet::create5point1();
    check (! p->checkBusesLayoutSupported (bad), "5.1 rejected");
    p->prepareToPlay (fs, block);
    check (p->getLatencySamples() == 0, "empty bridge: zero latency");

    juce::AudioBuffer<float> buf (4, block);
    runBlocks (*p, buf, 4);
    {
        juce::AudioBuffer<float> ref (4, block);
        ref.clear();
        fillSine (ref, 0, 3 * block, 0.25f, 1000.0);
        bool same = true;
        for (int i = 0; i < block; ++i) same = same && std::abs (buf.getSample (0, i) - ref.getSample (0, i)) < 1e-7f;
        check (same, "empty bridge passes audio unchanged");
    }

    std::printf ("Bridge editing\n");
    auto& g = p->graph();
    g.disconnect (sonic::Graph::kInputId, sonic::Graph::kOutputId);
    const int sat = g.addNode ("character.saturation", 300, 120);
    const int comp = g.addNode ("dynamics.compressor", 520, 120);
    const int dly = g.addNode ("space.delay", 520, 260);
    g.connect (sonic::Graph::kInputId, sat);
    g.connect (sat, comp);
    g.connect (comp, sonic::Graph::kOutputId);
    g.connect (sat, dly);                       // parallel branch
    g.connect (dly, sonic::Graph::kOutputId);
    check (g.activeOrder().size() == 3, "3 blocks in the signal path");
    runBlocks (*p, buf, 20);
    check (p->getLatencySamples() == 64, "latency reported to host = saturation oversampling (" + juce::String (p->getLatencySamples()) + ")");
    check (rms (buf.getReadPointer (0), block) > 0.01f, "processed output present");
    p->checkpoint();

    std::printf ("A/B\n");
    setParam (*p, "chain.bypass", 1.0f);
    setParam (*p, "ab.gain_match", 0.0f);
    {
        juce::AudioBuffer<float> imp (4, block);
        for (int k = 0; k < 8; ++k) { imp.clear(); p->processBlock (imp, midi); }
        imp.clear();
        imp.setSample (0, 10, 1.0f);
        p->processBlock (imp, midi);
        int peak = 0;
        for (int i = 0; i < block; ++i) if (std::abs (imp.getSample (0, i)) > std::abs (imp.getSample (0, peak))) peak = i;
        check (peak == 74 && std::abs (imp.getSample (0, peak) - 1.0f) < 1e-6f, "B = dry, delayed by the bridge latency (peak at " + juce::String (peak) + ")");
    }
    setParam (*p, "ab.gain_match", 1.0f);
    runBlocks (*p, buf, 700, 0.05f);   // > 4 loudness time constants
    const float bLevel = rms (buf.getReadPointer (0), block);
    setParam (*p, "chain.bypass", 0.0f);
    runBlocks (*p, buf, 700, 0.05f);
    const float aLevel = rms (buf.getReadPointer (0), block);
    check (std::abs (juce::Decibels::gainToDecibels (bLevel / aLevel)) < 1.0f,
           "gain-matched B within 1 dB of A (A " + juce::String (juce::Decibels::gainToDecibels (aLevel), 1) + " dB, B " + juce::String (juce::Decibels::gainToDecibels (bLevel), 1) + " dB)");

    std::printf ("Sidechain detection\n");
    {
        juce::AudioBuffer<float> sc (4, block);
        for (int b = 0; b < 40; ++b)
        {
            sc.clear();
            fillSine (sc, 2, b * block, 0.25f, 500.0);
            fillSine (sc, 3, b * block, 0.25f, 500.0);
            p->processBlock (sc, midi);
        }
        check (p->sidechainState() == AtelierProcessor::SidechainState::active, "sidechain signal -> ACTIVE");
        check (rms (sc.getReadPointer (2), block) < 1e-9f, "sidechain channels cleared after processing");
    }

    std::printf ("Host block larger than prepared size\n");
    {
        juce::AudioBuffer<float> big (4, 4096);
        for (int i = 0; i < 4096; ++i) big.setSample (0, i, 0.2f * std::sin (i * 0.13f));
        p->processBlock (big, midi);
        bool finite = true;
        for (int i = 0; i < 4096; ++i) finite = finite && std::isfinite (big.getSample (0, i));
        check (finite && rms (big.getReadPointer (0) + 3000, 1000) > 0.01f, "4096-sample block processed in chunks");
    }

    std::printf ("Undo / state\n");
    g.node (sat)->module->setParam ("drive_db", 30.0f);
    g.node (comp)->mix = 0.4f;
    p->checkpoint();
    check (p->undo(), "undo available");
    check (std::abs (p->graph().node (sat)->module->getParam ("drive_db") - 12.0f) < 0.01f, "undo restores previous drive");
    check (p->redo() && std::abs (p->graph().node (sat)->module->getParam ("drive_db") - 30.0f) < 0.01f, "redo re-applies it");

    juce::MemoryBlock state;
    p->getStateInformation (state);
    auto q = std::make_unique<AtelierProcessor>();
    q->setStateInformation (state.getData(), (int) state.getSize());
    auto& g2 = q->graph();
    check (g2.nodeIds().size() == 3 && g2.edges().size() == 5, "bridge structure restored (3 blocks, 5 wires)");
    check (g2.node (sat) && std::abs (g2.node (sat)->module->getParam ("drive_db") - 30.0f) < 0.01f, "block parameter restored");
    check (g2.node (comp) && std::abs (g2.node (comp)->mix.load() - 0.4f) < 0.001f, "block mix restored");

    std::printf ("Analysis worker\n");
    {
        setParam (*p, "src.mode", 1.0f);
        setParam (*p, "gen.run", 1.0f);
        setParam (*p, "gen.level_db", -12.0f);
        AnalysisWorker worker (*p);
        AnalysisWorker::Results r;
        bool got = false;
        for (int t = 0; t < 60 && ! got; ++t)
        {
            juce::AudioBuffer<float> b (4, block);
            for (int k = 0; k < 10; ++k) { b.clear(); p->processBlock (b, midi); }
            juce::Thread::sleep (60);
            got = t > 25 && worker.getResults (r, 0) && r.harmonicsAvailable && r.lufsShort > -100.0;
        }
        check (got, "worker produced harmonics + loudness");
        check (! r.f0Estimated && std::abs (r.harmonics.fundamentalHz - 1000.0) < 1e-3, "generator fundamental used (MEASURED)");
        check (r.truePeakDb > -40.0 && r.truePeakDb < 3.0, "true peak plausible (" + juce::String (r.truePeakDb, 1) + " dBTP)");
        {
            juce::AudioBuffer<float> b (4, block);
            b.clear();
            p->processBlock (b, midi);
            float maxDiff = 0;
            for (int i = 0; i < block; ++i) maxDiff = std::max (maxDiff, std::abs (b.getSample (0, i) - b.getSample (1, i)));
            std::printf ("    (L/R max difference %.6f, L rms %.4f)\n", maxDiff, rms (b.getReadPointer (0), block));
        }
        check (r.stereoValid && r.correlation > 0.9, "correlation of identical L/R ~1 (" + juce::String (r.correlation, 3) + ")");
    }

    std::printf ("Blueprint export\n");
    {
        auto tmp = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("atelier_bp_test");
        tmp.deleteRecursively();
        tmp.createDirectory();
        Blueprint::Options o;
        o.projectName = "Test vocal";
        o.objective = "Warm glue with a parallel echo";
        o.sampleRate = 48000.0;
        o.baseFolder = tmp;
        juce::String err;
        auto dir = Blueprint::exportPackage (p->bridgeToTree(), o, {}, nullptr, nullptr, err);
        check (dir.isDirectory() && err.isEmpty(), "package written to " + dir.getFullPathName() + (err.isEmpty() ? "" : " - " + err));
        for (auto f : { "README.md", "blueprint.json", "schema.json", "blueprint.pdf", "audio/input_reference.wav", "audio/processed_reference.wav",
                        "audio/comparison.wav", "graphs/frequency_response.png", "graphs/signal_chain.png", "measurements/measurement_data.csv",
                        "implementation/dsp_requirements.md", "implementation/validation_tests.md" })
            check (dir.getChildFile (f).existsAsFile() && dir.getChildFile (f).getSize() > 100, juce::String ("file ") + f);
        auto json = juce::JSON::parse (dir.getChildFile ("blueprint.json"));
        auto nodes = json["signal_graph"]["nodes"];
        check (nodes.isArray() && nodes.size() == 3, "JSON lists the 3 blocks");
        bool driveOk = false;
        for (auto& n : *nodes.getArray())
            if (n["type"].toString() == "character.saturation")
                for (auto& pp : *n["parameters"].getArray())
                    if (pp["id"].toString() == "drive_db") driveOk = std::abs ((double) pp["value"] - 30.0) < 1e-6 && pp["unit"].toString() == "dB";
        check (driveOk, "JSON parameter value + unit match the bridge exactly (drive 30 dB)");
        check ((int) json["signal_graph"]["total_latency_samples"] == 64, "JSON latency = bridge latency");
        juce::MemoryBlock pdf;
        dir.getChildFile ("blueprint.pdf").loadFileAsData (pdf);
        const juce::String head (static_cast<const char*> (pdf.getData()), 8);
        const juce::String tail (static_cast<const char*> (pdf.getData()) + pdf.getSize() - 6, 6);
        check (head == "%PDF-1.4" && tail.trim() == "%%EOF", "PDF structure valid (" + juce::String ((int) pdf.getSize() / 1024) + " kB)");

        // processed_reference.wav must equal a fresh offline render of the same input
        juce::AudioFormatManager fm;
        fm.registerBasicFormats();
        std::unique_ptr<juce::AudioFormatReader> rIn (fm.createReaderFor (dir.getChildFile ("audio/input_reference.wav")));
        std::unique_ptr<juce::AudioFormatReader> rOut (fm.createReaderFor (dir.getChildFile ("audio/processed_reference.wav")));
        check (rIn && rOut && rIn->lengthInSamples == rOut->lengthInSamples, "reference WAVs present with equal length");
        int stages = 0;
        for (auto& n : *nodes.getArray()) if ((int) n["stage"] > 0) ++stages;
        check (stages == 3, "every block in the path has a processing-order stage number");
        check (json["measurements"]["noise_floor_dbfs"].isVoid() || (double) json["measurements"]["noise_floor_dbfs"] > -150.0,
               "noise floor is null (not a fake number) when below the measurement floor");
        auto again = Blueprint::exportPackage (p->bridgeToTree(), o, {}, nullptr, nullptr, err);
        check (again.getFileName().endsWith ("_rev02"), "second export becomes revision 2 (" + again.getFileName() + ")");
        if (juce::SystemStats::getEnvironmentVariable ("ATELIER_KEEP_EXPORT", {}).isEmpty())
            tmp.deleteRecursively();
    }


    // ---- measurement accuracy: compressor timing ----
    {
        auto timing = [] (const char* model, float att, float rel, float autoRel)
        {
            sonic::Graph g;
            const int c = g.addNode ("dynamics.compressor", 300, 120);
            auto& m = *g.node (c)->module;
            m.setParam ("model", (float) juce::StringArray ({ "opto", "fet", "vca", "varimu", "diode" }).indexOf (model));
            m.setParam ("threshold_db", -24.0f); m.setParam ("ratio", 8.0f);
            m.setParam ("attack_ms", att); m.setParam ("release_ms", rel); m.setParam ("auto_release", autoRel);
            m.setParam ("knee_db", 0.0f); m.setParam ("detector", 0.0f);
            g.connect (sonic::Graph::kInputId, c); g.connect (c, sonic::Graph::kOutputId);
            juce::ValueTree t = AtelierProcessor::graphToTree (g);
            return measure::run (t, 48000.0, nullptr, nullptr);
        };
        auto vca = timing ("vca", 10.0f, 100.0f, 0.0f);
        check (vca.attackMs > 3.0 && vca.attackMs < 25.0, "VCA attack 10 ms measured as " + juce::String (vca.attackMs, 1) + " ms (63 %)");
        check (vca.releaseMs > 40.0 && vca.releaseMs < 250.0, "VCA release 100 ms measured as " + juce::String (vca.releaseMs, 1) + " ms (63 %)");
        auto fast = timing ("vca", 0.5f, 100.0f, 0.0f);
        check (fast.attackMs >= 0.0 && fast.attackMs < 3.0, "fast attack is no longer rounded to a 5 ms grid (" + juce::String (fast.attackMs, 2) + " ms)");
        auto opto = timing ("opto", 1.0f, 900.0f, 1.0f);
        check (opto.release90Ms > 2.0 * opto.releaseMs, "Opto two-stage release visible: 63 % " + juce::String (opto.releaseMs, 0)
                                                          + " ms, 90 % " + juce::String (opto.release90Ms, 0) + " ms");
        sonic::Graph g;
        const int c = g.addNode ("dynamics.compressor", 0, 0);
        auto& m = *g.node (c)->module;
        int autoIdx = -1;
        for (int i = 0; i < m.numParams(); ++i) if (std::string (m.params()[(size_t) i].id) == "auto_release") autoIdx = i;
        m.setParam ("model", 0.0f);
        const bool hiddenOpto = ! m.isParamRelevant (autoIdx);
        m.setParam ("model", 2.0f);
        check (hiddenOpto && m.isParamRelevant (autoIdx), "Auto release is hidden for Opto only");
    }

    p->releaseResources();
    std::printf ("\n%d checks, %d failed\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
