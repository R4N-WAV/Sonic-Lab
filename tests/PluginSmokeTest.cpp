// Headless test of the real AtelierProcessor (no DAW, no editor):
// buses, sources, A/B alignment, sidechain detection, latency, state round-trip.

#include "../plugin/Source/AnalysisWorker.h"
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
    jassert (param != nullptr);
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

// buffer layout: ch0,1 = main in/out, ch2,3 = sidechain
void fillSine (juce::AudioBuffer<float>& b, int ch, int startSample, float amp, double freq)
{
    for (int i = 0; i < b.getNumSamples(); ++i)
        b.setSample (ch, i, amp * (float) std::sin (2.0 * juce::MathConstants<double>::pi * freq * (startSample + i) / fs));
}
}

int main()
{
    juce::ScopedJuceInitialiser_GUI init;
    juce::MidiBuffer midi;

    std::printf ("Processor setup\n");
    auto p = std::make_unique<AtelierProcessor>();
    auto layout = p->getBusesLayout();
    layout.inputBuses.getReference (1) = juce::AudioChannelSet::stereo(); // enable sidechain
    check (p->setBusesLayout (layout), "stereo main + stereo sidechain layout accepted");
    check (p->getTotalNumInputChannels() == 4, "4 input channels (main + sidechain)");
    auto mono = p->getBusesLayout();
    mono.inputBuses.getReference (0) = juce::AudioChannelSet::mono();
    mono.outputBuses.getReference (0) = juce::AudioChannelSet::mono();
    check (p->checkBusesLayoutSupported (mono), "mono main layout supported");
    auto bad = p->getBusesLayout();
    bad.outputBuses.getReference (0) = juce::AudioChannelSet::create5point1();
    check (! p->checkBusesLayoutSupported (bad), "5.1 rejected");

    p->prepareToPlay (fs, block);
    check (p->getLatencySamples() == 64, "reports 64 samples latency (tube 4x oversampling)");

    std::printf ("DAW input through chain\n");
    juce::AudioBuffer<float> buf (4, block);
    for (int b = 0; b < 40; ++b)
    {
        buf.clear();
        fillSine (buf, 0, b * block, 0.25f, 1000.0);
        fillSine (buf, 1, b * block, 0.25f, 1000.0);
        p->processBlock (buf, midi);
    }
    check (rms (buf.getReadPointer (0), block) > 0.05f, "output carries the processed signal");
    check (p->sidechainState() == AtelierProcessor::SidechainState::silent, "sidechain enabled but silent -> SILENT");

    std::printf ("Sidechain detection\n");
    for (int b = 0; b < 40; ++b)
    {
        buf.clear();
        fillSine (buf, 0, b * block, 0.25f, 1000.0);
        fillSine (buf, 2, b * block, 0.25f, 500.0);
        fillSine (buf, 3, b * block, 0.25f, 500.0);
        p->processBlock (buf, midi);
    }
    check (p->sidechainState() == AtelierProcessor::SidechainState::active, "sidechain signal -> ACTIVE");
    check (rms (buf.getReadPointer (2), block) < 1e-9f, "sidechain channels cleared after processing (no leakage)");

    std::printf ("A/B bypass is time-aligned with the chain latency\n");
    setParam (*p, "chain.bypass", 1.0f);
    {
        juce::AudioBuffer<float> imp (4, block);
        for (int b = 0; b < 4; ++b) { imp.clear(); p->processBlock (imp, midi); } // flush
        imp.clear();
        imp.setSample (0, 10, 1.0f);
        p->processBlock (imp, midi);
        int peak = 0;
        for (int i = 0; i < block; ++i)
            if (std::abs (imp.getSample (0, i)) > std::abs (imp.getSample (0, peak))) peak = i;
        check (peak == 10 + 64 && std::abs (imp.getSample (0, peak) - 1.0f) < 1e-6f,
               "bypassed impulse appears unchanged after exactly 64 samples (at " + juce::String (peak) + ")");
    }
    setParam (*p, "chain.bypass", 0.0f);

    std::printf ("Disabling the tube removes its latency\n");
    setParam (*p, "tube.enabled", 0.0f);
    buf.clear();
    p->processBlock (buf, midi);
    check (p->getLatencySamples() == 0, "latency now 0");
    setParam (*p, "tube.enabled", 1.0f);
    p->processBlock (buf, midi);
    check (p->getLatencySamples() == 64, "latency back to 64");

    std::printf ("Generator source\n");
    setParam (*p, "src.mode", 1.0f);
    setParam (*p, "gen.run", 1.0f);
    setParam (*p, "gen.level_db", -6.0f);
    for (int b = 0; b < 10; ++b) { buf.clear(); p->processBlock (buf, midi); }
    check (rms (buf.getReadPointer (0), block) > 0.1f, "generator produces output with silent DAW input");
    check (p->generatorIsRunningSine(), "generator sine state reported for exact harmonic analysis");


    std::printf ("Analysis worker (background thread)\n");
    {
        // Generator sine 1 kHz at -6 dBFS, tube drive 18 dB -> harmonics must be measured exactly
        setParam (*p, "tube.drive_db", 18.0f);
        setParam (*p, "tube.sag", 0.0f);
        for (int b = 0; b < 200; ++b) { buf.clear(); p->processBlock (buf, midi); }
        AnalysisWorker worker (*p);
        AnalysisWorker::Results r;
        bool got = false;
        for (int t = 0; t < 40 && ! got; ++t)
        {
            // keep "audio" flowing like a host would (~100 ms per iteration)
            for (int b = 0; b < 10; ++b) { buf.clear(); p->processBlock (buf, midi); }
            juce::Thread::sleep (100);
            got = worker.getResults (r, 0) && r.harmonicsAvailable && r.transferAvailable;
        }
        check (got, "worker produced harmonic + transfer results");
        std::printf ("    (f0 %.3f Hz, estimated=%d, mode=%d)\n", r.harmonics.fundamentalHz, (int) r.f0Estimated, (int) r.mode);
        check (! r.f0Estimated && std::abs (r.harmonics.fundamentalHz - 1000.0) < 1e-3, "fundamental taken from generator (MEASURED), 1000 Hz");
        check (r.harmonics.levelDbfs[0] < -6.0 && r.harmonics.levelDbfs[0] > -30.0, "saturated H1 below the -6 dBFS input (got " + juce::String (r.harmonics.levelDbfs[0], 2) + ")");
        check (r.harmonics.levelDbc[1] > -40.0, "driven triode shows H2 (" + juce::String (r.harmonics.levelDbc[1], 1) + " dBc)");
        check (! r.transfer.broadband && r.transferNote.containsIgnoreCase ("narrowband"), "sine excitation: transfer flagged narrowband, delay not claimed");

        // Broadband excitation: pink noise, light drive -> delay estimate = chain latency
        setParam (*p, "gen.waveform", (float) sonic::SignalGenerator::Waveform::pinkNoise);
        setParam (*p, "tube.drive_db", 0.0f);
        worker.resetAveraging();
        got = false;
        for (int t = 0; t < 50 && ! got; ++t)
        {
            for (int b = 0; b < 10; ++b) { buf.clear(); p->processBlock (buf, midi); }
            juce::Thread::sleep (100);
            got = worker.getResults (r, 0) && r.transferAvailable && r.transfer.broadband && r.transfer.segmentsAveraged >= 4;
        }
        check (got && r.transfer.estimatedDelaySamples == 64, "pink noise: delay estimate = chain latency 64 (" + juce::String (r.transfer.estimatedDelaySamples) + ")");
        check (r.transfer.meanCoherence > 0.9, "near-linear chain: coherence " + juce::String (r.transfer.meanCoherence, 3));
        worker.setMode (AnalysisWorker::Mode::reference);
        for (int t = 0; t < 4; ++t) { for (int b = 0; b < 10; ++b) { buf.clear(); p->processBlock (buf, midi); } juce::Thread::sleep (100); }
        worker.getResults (r, 0);
        check (! r.transferAvailable && r.transferNote.isNotEmpty(), "reference mode without sidechain signal: no fake comparison (" + r.transferNote + ")");
    }

    std::printf ("Host block larger than prepared size\n");
    {
        juce::AudioBuffer<float> big (4, 4096);
        big.clear();
        p->processBlock (big, midi);
        check (rms (big.getReadPointer (0) + 3000, 1000) > 0.1f, "4096-sample block processed in chunks without reallocation");
    }

    std::printf ("State save / restore\n");
    setParam (*p, "tube.drive_db", 21.0f);
    setParam (*p, "eq.bell_gain_db", -5.5f);
    p->moveChainSlot (1, 0);
    juce::MemoryBlock state;
    p->getStateInformation (state);

    auto q = std::make_unique<AtelierProcessor>();
    q->setStateInformation (state.getData(), (int) state.getSize());
    check (std::abs (*q->apvts.getRawParameterValue ("tube.drive_db") - 21.0f) < 0.01f, "tube drive restored");
    check (std::abs (*q->apvts.getRawParameterValue ("eq.bell_gain_db") + 5.5f) < 0.01f, "EQ bell gain restored");
    check (q->chainOrder() == std::vector<int> ({ 1, 0 }), "chain order restored (EQ before Tube)");
    check ((int) q->source() == 1, "source restored (generator)");

    p->releaseResources();
    std::printf ("\n%d checks, %d failed\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
