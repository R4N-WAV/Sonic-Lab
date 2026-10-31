// Headless test of the real AtelierProcessor (no DAW, no editor):
// buses, sources, bridge, A/B alignment + loudness match, sidechain detection,
// latency, state round-trip, undo, analysis worker and the blueprint export.

#include "../plugin/Source/AnalysisWorker.h"
#include "../plugin/Source/Blueprint.h"
#include "../plugin/Source/Measure.h"
#include "../plugin/Source/Widgets.h"
#include "../plugin/Source/Templates.h"
#include "../plugin/Source/PluginProcessor.h"
#include "../plugin/Source/PluginEditor.h"
#include "../plugin/Source/BlockPresets.h"
#include "../plugin/Source/ParamHelp.h"

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
        AnalysisWorker::Results a, b2;
        worker.getResults (a, -1);
        juce::Thread::sleep (500);
        worker.getResults (b2, -1);
        const double fps = (b2.serial - a.serial) / 0.5;
        check (fps > 40.0, "spectrum refreshes smoothly (" + juce::String (fps, 0) + " frames/s)");
        check (b2.fftSize <= 4096, "display FFT is short for low latency (" + juce::String (b2.fftSize) + ")");
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


    // ---- templates ----
    {
        int ok = 0, total = 0;
        juce::String bad;
        for (auto& t : templates::factory())
        {
            ++total;
            auto tree = templates::bridgeFor (t);
            sonic::Graph g;
            AtelierProcessor::treeToGraph (tree, g);
            g.prepare (48000.0, 512);
            const bool hasPath = ! g.activeOrder().empty();
            std::vector<float> l (48000), r (48000);
            for (size_t i = 0; i < l.size(); ++i) l[i] = r[i] = 0.25f * (float) std::sin (2.0 * 3.14159265 * 220.0 * (double) i / 48000.0);
            bool finite = true;
            double energy = 0;
            for (size_t i = 0; i < l.size(); i += 512)
            {
                float* ch[2] = { l.data() + i, r.data() + i };
                g.process (ch, 2, (int) juce::jmin ((size_t) 512, l.size() - i));
            }
            for (size_t i = 24000; i < l.size(); ++i) { finite = finite && std::isfinite (l[i]); energy += l[i] * (double) l[i]; }
            if (hasPath && finite && energy > 1e-3) ++ok; else bad << t.name << "; ";
        }
        check (ok == total && total >= 30, juce::String (total) + " factory templates build, connect and play clean audio" + (bad.isEmpty() ? "" : " (failed: " + bad + ")"));

        juce::String err;
        const juce::String name = "Atelier test template " + juce::String (juce::Random::getSystemRandom().nextInt (100000));
        const bool saved = templates::save (name, "Tests", "temporary", p->bridgeToTree(), err);
        bool found = false, removed = false, sameBridge = false;
        for (auto& t : templates::personal())
            if (t.name == name)
            {
                found = true;
                sameBridge = templates::bridgeFor (t).isEquivalentTo (p->bridgeToTree());
                removed = templates::remove (t);
            }
        check (saved && found && sameBridge && removed, "personal template saves, lists, loads identically and deletes" + (err.isEmpty() ? "" : " (" + err + ")"));
    }

    // ---- typed values ----
    check (std::abs (parseTypedValue ("2k", "Hz", 0) - 2000.0) < 1e-9 && std::abs (parseTypedValue ("2.5 kHz", "Hz", 0) - 2500.0) < 1e-9,
           "typing '2k' / '2.5 kHz' gives 2000 / 2500 Hz");
    check (std::abs (parseTypedValue ("-3,5 dB", "dB", 0) + 3.5) < 1e-9 && std::abs (parseTypedValue ("4:1", ":1", 0) - 4.0) < 1e-9
           && std::abs (parseTypedValue ("0.5 s", "ms", 0) - 500.0) < 1e-9 && std::abs (parseTypedValue ("abc", "dB", 7.0) - 7.0) < 1e-9,
           "typing '-3,5 dB', '4:1', '0.5 s' parses; nonsense keeps the old value");

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


    // ================================================================ v0.4
    std::printf ("\nv0.4\n");

    // ---- sidechain: on by default, and it keys a compressor ----
    {
        AtelierProcessor sp;
        check (sp.getBusCount (true) == 2 && sp.getBus (true, 1)->isEnabled(), "sidechain input is enabled by default (FL Studio offers it without extra steps)");
        sp.prepareToPlay (fs, block);
        auto& g = sp.graph();
        g.clear();
        const int c = g.addNode ("dynamics.compressor", 300, 120);
        auto& m = *g.node (c)->module;
        m.setParam ("model", 2.0f); m.setParam ("threshold_db", -30.0f); m.setParam ("ratio", 10.0f); m.setParam ("key", 1.0f);
        g.connect (sonic::Graph::kInputId, c); g.connect (c, sonic::Graph::kOutputId);
        g.prepare (fs, block);
        juce::AudioBuffer<float> buf (4, block);
        juce::MidiBuffer midi;
        auto level = [&] (float keyAmp)
        {
            float out = 0.0f;
            for (int b = 0; b < 60; ++b)
            {
                buf.clear();
                fillSine (buf, 0, b * block, 0.02f, 300.0);
                fillSine (buf, 1, b * block, 0.02f, 300.0);
                fillSine (buf, 2, b * block, keyAmp, 60.0);
                fillSine (buf, 3, b * block, keyAmp, 60.0);
                sp.processBlock (buf, midi);
                out = rms (buf.getReadPointer (0), block);
            }
            return out;
        };
        const float quietKey = level (0.0f), loudKey = level (0.7f);
        check (sp.sidechainState() == AtelierProcessor::SidechainState::active, "sidechain signal detected");
        check (juce::Decibels::gainToDecibels (loudKey / quietKey) < -10.0f,
               "a loud sidechain ducks the track through Key = Sidechain (" + juce::String (juce::Decibels::gainToDecibels (loudKey / quietKey), 1) + " dB)");
        sp.releaseResources();
    }

    // ---- block presets ----
    {
        bool idsOk = true, soundOk = true;
        int count = 0;
        for (auto& info : sonic::availableModules())
            for (auto& pr : blockpresets::factory (info.typeId))
            {
                ++count;
                sonic::Graph g;
                g.clear();
                const int id = g.addNode (info.typeId, 0, 0);
                for (auto& [k, v] : pr.params)
                    if (g.node (id)->module->paramIndex (k) < 0) { idsOk = false; std::printf ("    unknown id %s in %s\n", k.c_str(), pr.name.toRawUTF8()); }
                blockpresets::apply (pr, *g.node (id));
                g.connect (sonic::Graph::kInputId, id); g.connect (id, sonic::Graph::kOutputId);
                g.prepare (fs, block);
                juce::AudioBuffer<float> b (2, block);
                float energy = 0.0f;
                bool finite = true;
                for (int k = 0; k < 40; ++k)
                {
                    fillSine (b, 0, k * block, 0.25f, 440.0);
                    fillSine (b, 1, k * block, 0.25f, 440.0);
                    float* ch[2] = { b.getWritePointer (0), b.getWritePointer (1) };
                    g.process (ch, 2, block);
                    for (int i = 0; i < block; ++i) finite &= std::isfinite (ch[0][i]);
                    energy += rms (ch[0], block);
                }
                if (! finite || energy < 1e-4f) { soundOk = false; std::printf ("    preset %s: finite %d energy %f\n", pr.name.toRawUTF8(), finite, energy); }
            }
        check (count >= 60, "factory block presets: " + juce::String (count));
        check (idsOk, "every factory preset only names real parameters");
        check (soundOk, "every factory preset plays clean, audible audio");

        sonic::Graph g;
        const int id = g.addNode ("dynamics.compressor", 0, 0);
        auto& n = *g.node (id);
        n.module->setParam ("ratio", 7.5f);
        n.mix = 0.4f;
        juce::String err;
        const bool saved = blockpresets::save (n, "Smoke test preset", err);
        auto mine = blockpresets::personal ("dynamics.compressor");
        auto it = std::find_if (mine.begin(), mine.end(), [] (const blockpresets::Preset& x) { return x.name == "Smoke test preset"; });
        bool loaded = false;
        if (it != mine.end())
        {
            n.module->setParam ("ratio", 2.0f);
            n.mix = 1.0f;
            blockpresets::apply (*it, n);
            loaded = std::abs (n.module->getParam ("ratio") - 7.5f) < 1e-4f && std::abs (n.mix.load() - 0.4f) < 1e-4f;
        }
        check (saved && loaded, "personal block preset: save, load (incl. Mix) " + err);
        check (it != mine.end() && blockpresets::remove (*it) && blockpresets::personal ("dynamics.compressor").size() + 1 == mine.size(), "personal block preset: delete");
    }

    // ---- hover help: every control explains itself ----
    {
        int missing = 0, total = 0;
        for (auto& info : sonic::availableModules())
        {
            auto m = sonic::createModule (info.typeId);
            for (int i = 0; i < m->numParams(); ++i)
            {
                ++total;
                if (help::forParam (info.typeId, m->params()[(size_t) i].id).empty())
                {
                    ++missing;
                    std::printf ("    no help: %s/%s\n", info.typeId.c_str(), m->params()[(size_t) i].id.c_str());
                }
            }
        }
        check (missing == 0, "plain-language help for all " + juce::String (total) + " block controls");
    }

    // ---- the editor: block in the centre, EQ editing, controls ----
    {
        p->prepareToPlay (fs, block);
        auto& g = p->graph();
        g.clear();
        const int a = g.addNode ("character.tape", 200, 132);
        const int eqId = g.addNode ("tone.eq", 440, 132);
        const int par = g.addNode ("space.reverb", 440, 132);
        g.connect (sonic::Graph::kInputId, a); g.connect (a, eqId); g.connect (eqId, sonic::Graph::kOutputId);
        g.connect (a, par); g.connect (par, sonic::Graph::kOutputId);
        std::unique_ptr<juce::AudioProcessorEditor> ed (p->createEditor());
        auto* e = dynamic_cast<AtelierEditor*> (ed.get());
        e->selectBlock (a);
        check (e->centreView().isVisible() && e->centreView().shownId() == a, "selecting a block puts it in the centre view");
        e->selectBlock (-1);
        check (! e->centreView().isVisible(), "nothing selected: the spectrum is back");

        e->selectBlock (eqId);
        auto* eqv = dynamic_cast<EqView*> (e->centreView().body());
        check (eqv != nullptr, "the EQ opens in the centre view");
        if (eqv != nullptr)
        {
            eqv->addBandAt (200.0, 3.0);
            eqv->addBandAt (2000.0, -2.0);
            eqv->addBandAt (8000.0, 1.0);
            check (eqv->numBands() == 3, "EQ: bands added");
            eqv->selectAllBands();
            eqv->deleteSelectedBands();
            check (eqv->numBands() == 0, "EQ: select all + delete removes every band");
            sonic::eqmatch::Spectrum flat, bump;
            flat.binHz = bump.binHz = fs / 4096.0;
            flat.frames = bump.frames = 1;
            for (int k = 0; k <= 2048; ++k)
            {
                const double f = juce::jmax (20.0, k * flat.binHz);
                flat.power.push_back (1.0 / f);
                bump.power.push_back (1.0 / f * (f > 3000.0 && f < 6000.0 ? 4.0 : 1.0));
            }
            eqv->applyMatch (flat, bump);
            const int first = eqv->numBands();
            eqv->applyMatch (flat, bump);
            check (first > 0 && eqv->numBands() == first, "EQ match adds bands, and applying again replaces them (" + juce::String (first) + " bands)");
            auto& eq = static_cast<sonic::ParametricEqModule&> (*g.node (eqId)->module);
            using EQ = sonic::ParametricEqModule;
            int b0 = -1;
            for (int b = 0; b < EQ::kMaxBands; ++b) if (eq.bandOn (b)) { b0 = b; break; }
            check (b0 >= 0 && ! eq.isParamRelevant (EQ::bandParam (b0, EQ::fThr)), "EQ: dynamic controls are greyed out while Dynamic is off");
            if (b0 >= 0) eq.setParam (EQ::bandParam (b0, EQ::fDyn), 1.0f);
            check (b0 >= 0 && eq.isParamRelevant (EQ::bandParam (b0, EQ::fThr)), "EQ: and active once it is on");
        }

        // fine mouse wheel: one notch moves a control by under 1 % of its travel
        {
            AtelierSlider sl;
            sl.setRange (0.0, 100.0, 0.0);
            sl.setValue (50.0);
            juce::MouseWheelDetails w {};
            w.deltaY = 0.234f;   // one notch on Windows
            auto ev = juce::MouseEvent (juce::Desktop::getInstance().getMainMouseSource(), juce::Point<float>(), juce::ModifierKeys(), 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, &sl, &sl,
                                        juce::Time::getCurrentTime(), juce::Point<float>(), juce::Time::getCurrentTime(), 1, false);
            sl.mouseWheelMove (ev, w);
            const double step = sl.getValue() - 50.0;
            check (step > 0.2 && step < 1.0, "one wheel notch moves a control by " + juce::String (step, 2) + " % of its travel (was ~3.5 %)");
            sl.setDefault (20.0);
            sl.mouseDoubleClick (ev);
            check (std::abs (sl.getValue() - 20.0) < 1e-9, "double-click resets a control to its default");
        }

        // TIDY keeps the parallel reverb off the main line
        e->selectBlock (-1);
        BridgePanel bp (e->uiState());
        bp.setBounds (0, 0, 1552, 448);
        bp.tidyLayout();
        check (std::abs (g.node (a)->y - g.node (eqId)->y) < 1.0f && std::abs (g.node (par)->y - g.node (eqId)->y) > 50.0f,
               "TIDY: the main line stays in line, the parallel block gets its own lane");
        ed.reset();
    }

    p->releaseResources();
    std::printf ("\n%d checks, %d failed\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
