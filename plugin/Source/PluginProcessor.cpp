#include "PluginProcessor.h"
#include "PluginEditor.h"

#include <sonic/ModuleRegistry.h>

namespace
{
    constexpr int kDryDelaySize = 1 << 16; // >= max bridge latency

    juce::String formatWithUnit (float v, const juce::String& unit)
    {
        if (unit == "Hz")
            return v >= 1000.0f ? juce::String (v / 1000.0f, v >= 10000.0f ? 1 : 2) + " kHz"
                                : numText (v, v < 100.0f ? 1 : 0) + " Hz";
        if (unit == "dB")
            return (v > 0.0f ? "+" : "") + juce::String (v, 1) + " dB";
        if (unit == "deg")
            return juce::String (juce::roundToInt (v)) + juce::String (juce::CharPointer_UTF8 ("\xc2\xb0"));
        if (unit == "s")
            return juce::String (v, 1) + " s";
        return juce::String (v, 2) + (unit.isEmpty() ? "" : " " + unit);
    }

    std::unique_ptr<juce::AudioParameterFloat> floatParam (const juce::String& id, const juce::String& name,
                                                           float min, float max, float def, float skewCentre,
                                                           const juce::String& unit)
    {
        juce::NormalisableRange<float> range (min, max);
        if (skewCentre > min && skewCentre < max)
            range.setSkewForCentre (skewCentre);
        auto attrs = juce::AudioParameterFloatAttributes()
                         .withLabel (unit)
                         .withStringFromValueFunction ([unit] (float v, int) { return formatWithUnit (v, unit); })
                         .withValueFromStringFunction ([] (const juce::String& t)
                                                       {
                                                           auto v = t.retainCharacters ("-0123456789.").getFloatValue();
                                                           return t.containsIgnoreCase ("k") ? v * 1000.0f : v;
                                                       });
        return std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { id, 1 }, name, range, def, attrs);
    }
}

juce::AudioProcessorValueTreeState::ParameterLayout AtelierProcessor::createLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;
    using juce::ParameterID;

    layout.add (std::make_unique<juce::AudioParameterChoice> (ParameterID { "src.mode", 1 }, "Source",
                                                              juce::StringArray { "DAW Input", "Generator", "Audio File" }, 0));
    layout.add (std::make_unique<juce::AudioParameterBool> (ParameterID { "chain.bypass", 1 }, "A/B (B = dry)", false));
    layout.add (std::make_unique<juce::AudioParameterBool> (ParameterID { "ab.gain_match", 2 }, "Gain match", true));
    layout.add (floatParam ("io.in_trim_db", "Input Trim", -24, 24, 0, 0, "dB"));
    layout.add (floatParam ("io.out_trim_db", "Output Trim", -24, 24, 0, 0, "dB"));
    layout.add (floatParam ("file.gain_db", "File Gain", -36, 12, 0, 0, "dB"));

    juce::StringArray waves;
    for (int i = 0; i < (int) sonic::SignalGenerator::Waveform::numWaveforms; ++i)
        waves.add (sonic::SignalGenerator::waveformName ((sonic::SignalGenerator::Waveform) i));
    layout.add (std::make_unique<juce::AudioParameterBool> (ParameterID { "gen.run", 1 }, "Generator Run", false));
    layout.add (std::make_unique<juce::AudioParameterChoice> (ParameterID { "gen.waveform", 1 }, "Waveform", waves, 0));
    layout.add (floatParam ("gen.freq_hz", "Gen Frequency", 10, 20000, 1000, 1000, "Hz"));
    layout.add (floatParam ("gen.level_db", "Gen Level", -80, 0, -18, 0, "dB"));
    layout.add (floatParam ("gen.phase_deg", "Gen Phase", 0, 360, 0, 0, "deg"));
    layout.add (floatParam ("gen.dc", "Gen DC Offset", -0.5f, 0.5f, 0, 0, ""));
    layout.add (std::make_unique<juce::AudioParameterChoice> (ParameterID { "gen.channels", 1 }, "Gen Channels",
                                                              juce::StringArray { "Both", "Left only", "Right only", "Both (R inverted)" }, 0));
    layout.add (floatParam ("gen.sweep_start_hz", "Sweep Start", 10, 20000, 20, 1000, "Hz"));
    layout.add (floatParam ("gen.sweep_end_hz", "Sweep End", 10, 20000, 20000, 1000, "Hz"));
    layout.add (floatParam ("gen.sweep_s", "Sweep Time", 0.5f, 60, 10, 10, "s"));
    return layout;
}

AtelierProcessor::AtelierProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput ("Input", juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)
                          .withInput ("Sidechain", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "ATELIER", createLayout())
{
    auto raw = [this] (const char* id) { return apvts.getRawParameterValue (id); };
    pSource = raw ("src.mode");
    pBypass = raw ("chain.bypass");
    pMatch = raw ("ab.gain_match");
    pInTrim = raw ("io.in_trim_db");
    pOutTrim = raw ("io.out_trim_db");
    pFileGain = raw ("file.gain_db");
    pGenRun = raw ("gen.run");
    pGenWave = raw ("gen.waveform");
    pGenFreq = raw ("gen.freq_hz");
    pGenLevel = raw ("gen.level_db");
    pGenPhase = raw ("gen.phase_deg");
    pGenDc = raw ("gen.dc");
    pGenChannels = raw ("gen.channels");
    pSweepStart = raw ("gen.sweep_start_hz");
    pSweepEnd = raw ("gen.sweep_end_hz");
    pSweepTime = raw ("gen.sweep_s");

    formatManager.registerBasicFormats();
    readAheadThread.startThread (juce::Thread::Priority::normal);
    checkpoint();
    startTimerHz (4);
}

AtelierProcessor::~AtelierProcessor()
{
    stopTimer();
    transportSource.setSource (nullptr);
    readAheadThread.stopThread (2000);
}

void AtelierProcessor::timerCallback()
{
    bridge.collectGarbage();
}

// ------------------------------------------------------------------ bridge state

juce::ValueTree AtelierProcessor::graphToTree (const sonic::Graph& g)
{
    juce::ValueTree t ("BRIDGE");
    t.setProperty ("format", 1, nullptr);
    for (int id : g.nodeIds())
    {
        const auto* n = g.node (id);
        juce::ValueTree nt ("BLOCK");
        nt.setProperty ("id", id, nullptr);
        nt.setProperty ("type", juce::String (n->type), nullptr);
        nt.setProperty ("name", juce::String (n->name), nullptr);
        nt.setProperty ("x", n->x, nullptr);
        nt.setProperty ("y", n->y, nullptr);
        nt.setProperty ("mix", n->mix.load(), nullptr);
        nt.setProperty ("out_db", n->outGainDb.load(), nullptr);
        nt.setProperty ("enabled", n->enabled.load(), nullptr);
        juce::ValueTree params ("PARAMS");
        const auto& m = *n->module;
        for (int i = 0; i < m.numParams(); ++i)
            params.setProperty (juce::Identifier (juce::String (m.params()[(size_t) i].id)), m.getParam (i), nullptr);
        nt.appendChild (params, nullptr);
        t.appendChild (nt, nullptr);
    }
    for (auto& e : g.edges())
    {
        juce::ValueTree et ("WIRE");
        et.setProperty ("from", e.from, nullptr);
        et.setProperty ("to", e.to, nullptr);
        t.appendChild (et, nullptr);
    }
    return t;
}

void AtelierProcessor::treeToGraph (const juce::ValueTree& t, sonic::Graph& g)
{
    if (! t.isValid() || ! t.hasType ("BRIDGE"))
        return;
    g.clear();
    for (const auto& nt : t)
    {
        if (! nt.hasType ("BLOCK"))
            continue;
        const int id = g.addNode (nt["type"].toString().toStdString(), (float) nt["x"], (float) nt["y"], (int) nt["id"]);
        if (id < 0)
            continue;
        auto* n = g.node (id);
        n->name = nt.getProperty ("name", juce::String (n->module->displayName())).toString().toStdString();
        n->mix = (float) nt.getProperty ("mix", 1.0f);
        n->outGainDb = (float) nt.getProperty ("out_db", 0.0f);
        n->enabled = (bool) nt.getProperty ("enabled", true);
        const auto params = nt.getChildWithName ("PARAMS");
        for (int i = 0; i < n->module->numParams(); ++i)
        {
            const juce::Identifier pid (juce::String (n->module->params()[(size_t) i].id));
            if (params.hasProperty (pid))
                n->module->setParam (i, (float) params[pid]);
        }
    }
    for (const auto& et : t)
        if (et.hasType ("WIRE"))
            g.connect ((int) et["from"], (int) et["to"]);
}

juce::ValueTree AtelierProcessor::bridgeToTree() const { return graphToTree (bridge); }

void AtelierProcessor::bridgeFromTree (const juce::ValueTree& t)
{
    treeToGraph (t, bridge);
}

void AtelierProcessor::checkpoint()
{
    const auto xml = bridgeToTree().toXmlString();
    if (undoPos >= 0 && undoPos < (int) undoStack.size() && undoStack[(size_t) undoPos] == xml)
        return;
    undoStack.resize ((size_t) (undoPos + 1));
    undoStack.push_back (xml);
    if (undoStack.size() > 100)
        undoStack.erase (undoStack.begin());
    undoPos = (int) undoStack.size() - 1;
}

bool AtelierProcessor::undo()
{
    if (! canUndo())
        return false;
    --undoPos;
    bridgeFromTree (juce::ValueTree::fromXml (undoStack[(size_t) undoPos]));
    return true;
}

bool AtelierProcessor::redo()
{
    if (! canRedo())
        return false;
    ++undoPos;
    bridgeFromTree (juce::ValueTree::fromXml (undoStack[(size_t) undoPos]));
    return true;
}

// ------------------------------------------------------------------ audio

AtelierProcessor::Source AtelierProcessor::source() const
{
    return (Source) juce::jlimit (0, 2, (int) pSource->load());
}

bool AtelierProcessor::generatorIsRunningSine() const
{
    return source() == Source::generator && pGenRun->load() > 0.5f
        && (int) pGenWave->load() == (int) sonic::SignalGenerator::Waveform::sine;
}

bool AtelierProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto mainIn = layouts.getMainInputChannelSet();
    const auto mainOut = layouts.getMainOutputChannelSet();
    if (mainOut != juce::AudioChannelSet::mono() && mainOut != juce::AudioChannelSet::stereo())
        return false;
    if (mainIn != mainOut)
        return false;
    if (layouts.inputBuses.size() > 1)
    {
        const auto sc = layouts.getChannelSet (true, 1);
        if (! sc.isDisabled() && sc != juce::AudioChannelSet::mono() && sc != juce::AudioChannelSet::stereo())
            return false;
    }
    return true;
}

void AtelierProcessor::syncGenerator()
{
    gen.waveform = (int) pGenWave->load();
    gen.frequencyHz = pGenFreq->load();
    gen.levelDb = pGenLevel->load();
    gen.phaseDeg = pGenPhase->load();
    gen.dcOffset = pGenDc->load();
    gen.channelMode = (int) pGenChannels->load();
    gen.sweepStartHz = pSweepStart->load();
    gen.sweepEndHz = pSweepEnd->load();
    gen.sweepSeconds = pSweepTime->load();
}

void AtelierProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    sampleRateNow = sampleRate;
    blockSizeNow = samplesPerBlock;
    const int maxBlock = juce::jmax (samplesPerBlock, 32);

    work.setSize (2, maxBlock, false, true, false);
    dryDelay.setSize (2, kDryDelaySize, false, true, false);
    dryDelay.clear();
    dryDelayPos = 0;
    scBuffer.setSize (2, maxBlock, false, true, false);
    preBuffer.setSize (2, maxBlock, false, true, false);

    syncGenerator();
    gen.prepare (sampleRate);
    bridge.prepare (sampleRate, maxBlock);
    transportSource.prepareToPlay (maxBlock, sampleRate);

    for (auto* s : { &inGain, &outGain, &fileGain, &bGain })
        s->reset (sampleRate, 0.03);
    inGain.setCurrentAndTargetValue (juce::Decibels::decibelsToGain (pInTrim->load()));
    outGain.setCurrentAndTargetValue (juce::Decibels::decibelsToGain (pOutTrim->load()));
    fileGain.setCurrentAndTargetValue (juce::Decibels::decibelsToGain (pFileGain->load()));
    bGain.setCurrentAndTargetValue (1.0f);
    wetPow = dryPow = 0.0;

    latencyNow = bridge.latencySamples();
    setLatencySamples (latencyNow.load());
}

void AtelierProcessor::releaseResources()
{
    transportSource.releaseResources();
}

void AtelierProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    const int total = buffer.getNumSamples();
    if (total == 0)
        return;

    syncGenerator();
    sonic::HostInfo info;
    if (auto* ph = getPlayHead())
        if (auto pos = ph->getPosition())
        {
            if (auto bpm = pos->getBpm()) info.bpm = *bpm;
            info.playing = pos->getIsPlaying();
        }
    bridge.setHostInfo (info);

    auto mainIn = getBusBuffer (buffer, true, 0);
    auto mainOut = getBusBuffer (buffer, false, 0);

    const float* scPtr[2] = { nullptr, nullptr };
    if (getBusCount (true) > 1)
        if (auto* scBus = getBus (true, 1); scBus != nullptr && scBus->isEnabled())
        {
            auto sc = getBusBuffer (buffer, true, 1);
            if (sc.getNumChannels() > 0)
            {
                scPtr[0] = sc.getReadPointer (0);
                scPtr[1] = sc.getReadPointer (sc.getNumChannels() > 1 ? 1 : 0);
            }
        }

    const int chunk = work.getNumSamples();
    for (int start = 0; start < total; start += chunk)
        processChunk (mainIn, mainOut, scPtr, start, juce::jmin (chunk, total - start));

    for (int c = mainOut.getNumChannels(); c < buffer.getNumChannels(); ++c)
        buffer.clear (c, 0, total);

    const int latency = bridge.latencySamples();
    if (latency != latencyNow.load())
    {
        latencyNow = latency;
        setLatencySamples (latency);
    }
}

void AtelierProcessor::processChunk (juce::AudioBuffer<float>& mainIn, juce::AudioBuffer<float>& mainOut,
                                     const float* const* scIn, int start, int n)
{
    // ---- sidechain ----
    const float* scL = nullptr;
    if (scIn[0] != nullptr)
    {
        float* s0 = scBuffer.getWritePointer (0);
        float* s1 = scBuffer.getWritePointer (1);
        juce::FloatVectorOperations::copy (s0, scIn[0] + start, n);
        juce::FloatVectorOperations::copy (s1, scIn[1] + start, n);
        scL = s0;
        double sum = 0.0;
        for (int i = 0; i < n; ++i)
            sum += (double) s0[i] * s0[i];
        const double a = std::exp (-(double) n / (0.5 * sampleRateNow.load()));
        scEnvelope = sum / n + a * (scEnvelope - sum / n);
        scState = scEnvelope > 1e-10 ? (int) SidechainState::active : (int) SidechainState::silent;
        measure (sidechainMeter, s0, s1, n);
    }
    else
    {
        scState = (int) SidechainState::notConnected;
        scEnvelope = 0.0;
    }

    // ---- source ----
    float* l = work.getWritePointer (0);
    float* r = work.getWritePointer (1);
    switch (source())
    {
        case Source::generator:
            if (pGenRun->load() > 0.5f)
                gen.render (l, r, n);
            else
            {
                juce::FloatVectorOperations::clear (l, n);
                juce::FloatVectorOperations::clear (r, n);
            }
            break;
        case Source::file:
        {
            float* chans[2] = { l, r };
            juce::AudioBuffer<float> view (chans, 2, n);
            juce::AudioSourceChannelInfo info (&view, 0, n);
            transportSource.getNextAudioBlock (info);
            fileGain.setTargetValue (juce::Decibels::decibelsToGain (pFileGain->load()));
            for (int i = 0; i < n; ++i)
            {
                const float g = fileGain.getNextValue();
                l[i] *= g;
                r[i] *= g;
            }
            break;
        }
        case Source::dawInput:
        default:
            if (mainIn.getNumChannels() > 0)
            {
                juce::FloatVectorOperations::copy (l, mainIn.getReadPointer (0) + start, n);
                juce::FloatVectorOperations::copy (r, mainIn.getReadPointer (mainIn.getNumChannels() > 1 ? 1 : 0) + start, n);
            }
            else
            {
                juce::FloatVectorOperations::clear (l, n);
                juce::FloatVectorOperations::clear (r, n);
            }
            break;
    }

    // ---- input trim ----
    inGain.setTargetValue (juce::Decibels::decibelsToGain (pInTrim->load()));
    for (int i = 0; i < n; ++i)
    {
        const float g = inGain.getNextValue();
        l[i] *= g;
        r[i] *= g;
    }
    measure (inputMeter, l, r, n);

    float* preL = preBuffer.getWritePointer (0);
    juce::FloatVectorOperations::copy (preL, l, n);

    // dry history (for B and loudness matching)
    float* dl = dryDelay.getWritePointer (0);
    float* dr = dryDelay.getWritePointer (1);
    {
        int pos = dryDelayPos;
        for (int i = 0; i < n; ++i)
        {
            dl[pos] = l[i];
            dr[pos] = r[i];
            pos = (pos + 1) & (kDryDelaySize - 1);
        }
    }

    // ---- bridge ----
    float* chans[2] = { l, r };
    const float* key[2] = { scBuffer.getReadPointer (0), scBuffer.getReadPointer (1) };
    bridge.process (chans, 2, n, scL != nullptr ? key : nullptr);   // blocks with Key = Sidechain listen to it
    const int latency = bridge.latencySamples();

    // ---- loudness tracking (A vs aligned dry) for gain-matched A/B ----
    float* dryAl = preBuffer.getWritePointer (1); // reuse as aligned dry (left)
    {
        int pos = (dryDelayPos - latency + kDryDelaySize) & (kDryDelaySize - 1);
        double wp = 0.0, dp = 0.0;
        for (int i = 0; i < n; ++i)
        {
            const float a = dl[pos], b = dr[pos];
            dryAl[i] = a;
            wp += 0.5 * ((double) l[i] * l[i] + (double) r[i] * r[i]);
            dp += 0.5 * ((double) a * a + (double) b * b);
            pos = (pos + 1) & (kDryDelaySize - 1);
        }
        const double k = std::exp (-(double) n / (1.5 * sampleRateNow.load()));
        wetPow = wp / n + k * (wetPow - wp / n);
        dryPow = dp / n + k * (dryPow - dp / n);
        const double ratioDb = (wetPow > 1e-12 && dryPow > 1e-12) ? 10.0 * std::log10 (wetPow / dryPow) : 0.0;
        matchDb = (float) juce::jlimit (-24.0, 24.0, ratioDb);
    }

    // ---- B (dry, latency-aligned, optionally loudness matched) ----
    if (pBypass->load() > 0.5f)
    {
        bGain.setTargetValue (pMatch->load() > 0.5f ? juce::Decibels::decibelsToGain (matchDb.load()) : 1.0f);
        int pos = (dryDelayPos - latency + kDryDelaySize) & (kDryDelaySize - 1);
        for (int i = 0; i < n; ++i)
        {
            const float g = bGain.getNextValue();
            l[i] = dl[pos] * g;
            r[i] = dr[pos] * g;
            pos = (pos + 1) & (kDryDelaySize - 1);
        }
    }
    dryDelayPos = (dryDelayPos + n) & (kDryDelaySize - 1);

    // ---- output trim ----
    outGain.setTargetValue (juce::Decibels::decibelsToGain (pOutTrim->load()));
    for (int i = 0; i < n; ++i)
    {
        const float g = outGain.getNextValue();
        l[i] = std::isfinite (l[i]) ? l[i] * g : 0.0f;
        r[i] = std::isfinite (r[i]) ? r[i] * g : 0.0f;
    }
    measure (outputMeter, l, r, n);

    const float* scopeChans[numScopeChannels] = { preL, l, r, scL };
    scope.push (scopeChans, n);

    const int numOut = mainOut.getNumChannels();
    if (numOut >= 1)
        juce::FloatVectorOperations::copy (mainOut.getWritePointer (0) + start, l, n);
    if (numOut >= 2)
        juce::FloatVectorOperations::copy (mainOut.getWritePointer (1) + start, r, n);
}

void AtelierProcessor::measure (Meter& m, const float* l, const float* r, int n)
{
    const float* ch[2] = { l, r };
    const float a = std::exp (-(float) n / (0.3f * (float) sampleRateNow.load()));
    for (int c = 0; c < 2; ++c)
    {
        float pk = 0.0f;
        double sum = 0.0;
        for (int i = 0; i < n; ++i)
        {
            const float v = std::abs (ch[c][i]);
            pk = juce::jmax (pk, v);
            sum += (double) v * v;
        }
        float prev = m.peak[c].load (std::memory_order_relaxed);
        while (pk > prev && ! m.peak[c].compare_exchange_weak (prev, pk)) {}
        const float blockRms = (float) std::sqrt (sum / n);
        const float old = m.rms[c].load (std::memory_order_relaxed);
        m.rms[c].store (blockRms + a * (old - blockRms), std::memory_order_relaxed);
    }
}

// ------------------------------------------------------------------ file

bool AtelierProcessor::loadFile (const juce::File& f, juce::String& error)
{
    std::unique_ptr<juce::AudioFormatReader> reader (formatManager.createReaderFor (f));
    if (reader == nullptr)
    {
        error = "Unsupported or unreadable audio file: " + f.getFileName();
        return false;
    }
    const double fileRate = reader->sampleRate;
    auto newSource = std::make_unique<juce::AudioFormatReaderSource> (reader.release(), true);
    newSource->setLooping (true);
    transportSource.stop();
    transportSource.setSource (newSource.get(), 65536, &readAheadThread, fileRate, 2);
    readerSource = std::move (newSource);
    loadedFile = f;
    thumb.setSource (new juce::FileInputSource (f));
    apvts.state.setProperty ("filePath", f.getFullPathName(), nullptr);
    return true;
}

// ------------------------------------------------------------------ state

void AtelierProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto state = apvts.copyState();
    state.setProperty ("pluginVersion", JucePlugin_VersionString, nullptr);
    state.removeChild (state.getChildWithName ("BRIDGE"), nullptr);
    state.appendChild (bridgeToTree(), nullptr);
    if (auto xml = state.createXml())
        copyXmlToBinary (*xml, destData);
}

void AtelierProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    auto xml = getXmlFromBinary (data, sizeInBytes);
    if (xml == nullptr || ! xml->hasTagName (apvts.state.getType()))
        return;
    auto tree = juce::ValueTree::fromXml (*xml);
    const auto b = tree.getChildWithName ("BRIDGE");
    apvts.replaceState (tree);
    if (b.isValid())
        bridgeFromTree (b);
    undoStack.clear();
    undoPos = -1;
    checkpoint();

    const juce::File f (apvts.state.getProperty ("filePath").toString());
    if (f.existsAsFile())
    {
        juce::String err;
        loadFile (f, err);
    }
}

juce::AudioProcessorEditor* AtelierProcessor::createEditor()
{
    return new AtelierEditor (*this);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new AtelierProcessor();
}
