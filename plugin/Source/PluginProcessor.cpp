#include "PluginProcessor.h"
#include "PluginEditor.h"

#include <sonic/DspUtils.h>
#include <sonic/modules/EqModule.h>
#include <sonic/modules/TubeModule.h>

namespace
{
    constexpr int kDryDelaySize = 8192; // >= max chain latency

    std::unique_ptr<sonic::Module> makeModule (int index)
    {
        if (index == 0) return std::make_unique<sonic::TubeModule>();
        return std::make_unique<sonic::EqModule>();
    }
    constexpr int kNumModules = 2;

    // Text formatting shared by every parameter (also used by host automation lanes).
    juce::String formatWithUnit (float v, const juce::String& unit)
    {
        if (unit == "Hz")
            return v >= 1000.0f ? juce::String (v / 1000.0f, v >= 10000.0f ? 1 : 2) + " kHz"
                                : juce::String (v, v < 100.0f ? 1 : 0) + " Hz";
        if (unit == "dB")
            return (v > 0.0f ? "+" : "") + juce::String (v, 1) + " dB";
        if (unit == "deg")
            return juce::String (v, 0) + juce::String (juce::CharPointer_UTF8 ("\xc2\xb0"));
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

constexpr const char* AtelierProcessor::kModuleKeys[];

juce::String AtelierProcessor::paramId (int moduleIndex, const std::string& coreId)
{
    return juce::String (kModuleKeys[moduleIndex]) + "." + juce::String (coreId);
}

juce::String AtelierProcessor::enabledId (int moduleIndex)
{
    return juce::String (kModuleKeys[moduleIndex]) + ".enabled";
}

juce::AudioProcessorValueTreeState::ParameterLayout AtelierProcessor::createLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;
    using juce::ParameterID;

    // Global / routing
    layout.add (std::make_unique<juce::AudioParameterChoice> (ParameterID { "src.mode", 1 }, "Source",
                                                              juce::StringArray { "DAW Input", "Generator", "Audio File" }, 0));
    layout.add (std::make_unique<juce::AudioParameterBool> (ParameterID { "chain.bypass", 1 }, "A/B Bypass", false));
    layout.add (floatParam ("io.in_trim_db", "Input Trim", -24, 24, 0, 0, "dB"));
    layout.add (floatParam ("io.out_trim_db", "Output Trim", -24, 24, 0, 0, "dB"));
    layout.add (floatParam ("file.gain_db", "File Gain", -36, 12, 0, 0, "dB"));

    // Generator
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

    // Chain modules: parameters generated from the core ParamSpecs (single source of truth)
    for (int m = 0; m < kNumModules; ++m)
    {
        auto mod = makeModule (m);
        layout.add (std::make_unique<juce::AudioParameterBool> (ParameterID { enabledId (m), 1 },
                                                                juce::String (mod->displayName()) + " On", true));
        for (const auto& s : mod->params())
        {
            const auto id = paramId (m, s.id);
            const auto name = juce::String (mod->displayName()) + " " + juce::String (s.name);
            if (s.isChoice())
            {
                juce::StringArray items;
                for (auto& c : s.choices)
                    items.add (c);
                layout.add (std::make_unique<juce::AudioParameterChoice> (ParameterID { id, 1 }, name, items, (int) s.defaultValue));
            }
            else
            {
                layout.add (floatParam (id, name, s.minValue, s.maxValue, s.defaultValue, s.skewCentre, juce::String (s.unit)));
            }
        }
    }
    return layout;
}

AtelierProcessor::BusesProperties AtelierProcessor::makeBuses()
{
    return BusesProperties()
        .withInput ("Input", juce::AudioChannelSet::stereo(), true)
        .withOutput ("Output", juce::AudioChannelSet::stereo(), true)
        .withInput ("Sidechain (unprocessed reference)", juce::AudioChannelSet::stereo(), false);
}

AtelierProcessor::AtelierProcessor()
    : AudioProcessor (makeBuses()),
      apvts (*this, nullptr, "ATELIER", createLayout())
{
    for (int m = 0; m < kNumModules; ++m)
        processingChain.addModule (makeModule (m));

    bindParameters();

    formatManager.registerBasicFormats();
    readAheadThread.startThread (juce::Thread::Priority::normal);
    apvts.state.setProperty ("chainOrder", "0,1", nullptr);
}

AtelierProcessor::~AtelierProcessor()
{
    transportSource.setSource (nullptr);
    readAheadThread.stopThread (2000);
}

void AtelierProcessor::bindParameters()
{
    auto raw = [this] (const juce::String& id)
    {
        auto* p = apvts.getRawParameterValue (id);
        jassert (p != nullptr);
        return p;
    };

    pSource = raw ("src.mode");
    pBypass = raw ("chain.bypass");
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

    links.clear();
    enabledParams.clear();
    for (int m = 0; m < processingChain.size(); ++m)
    {
        auto& mod = processingChain.module (m);
        enabledParams.push_back (raw (enabledId (m)));
        for (int i = 0; i < mod.numParams(); ++i)
            links.push_back ({ raw (paramId (m, mod.params()[(size_t) i].id)), &mod, i });
    }
    syncParameters();
}

void AtelierProcessor::syncParameters()
{
    for (auto& l : links)
        l.module->setParam (l.index, l.raw->load (std::memory_order_relaxed));
    for (size_t m = 0; m < enabledParams.size(); ++m)
        processingChain.setEnabled ((int) m, enabledParams[m]->load() > 0.5f);

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
    preBuffer.setSize (1, maxBlock, false, true, false);

    syncParameters();
    gen.prepare (sampleRate);
    processingChain.prepare (sampleRate, maxBlock, 2);
    transportSource.prepareToPlay (maxBlock, sampleRate);

    for (auto* s : { &inGain, &outGain, &fileGain })
        s->reset (sampleRate, 0.02);
    inGain.setCurrentAndTargetValue (juce::Decibels::decibelsToGain (pInTrim->load()));
    outGain.setCurrentAndTargetValue (juce::Decibels::decibelsToGain (pOutTrim->load()));
    fileGain.setCurrentAndTargetValue (juce::Decibels::decibelsToGain (pFileGain->load()));

    latencyNow = processingChain.latencySamples();
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

    syncParameters();

    auto mainIn = getBusBuffer (buffer, true, 0);
    auto mainOut = getBusBuffer (buffer, false, 0);

    // Sidechain (unprocessed reference) channels, if the host enabled the bus.
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

    // The host may exceed the announced block size: process in chunks that fit
    // the buffers allocated in prepareToPlay (never reallocate here).
    const int chunk = work.getNumSamples();
    for (int start = 0; start < total; start += chunk)
    {
        const int n = juce::jmin (chunk, total - start);
        processChunk (mainIn, mainOut, scPtr, start, n);
    }

    for (int c = mainOut.getNumChannels(); c < buffer.getNumChannels(); ++c)
        buffer.clear (c, 0, total);

    const int latency = processingChain.latencySamples();
    if (latency != latencyNow.load())
    {
        latencyNow = latency;
        setLatencySamples (latency);
    }
}

void AtelierProcessor::processChunk (juce::AudioBuffer<float>& mainIn, juce::AudioBuffer<float>& mainOut,
                                     const float* const* scIn, int start, int n)
{
    // ---- Sidechain state + copy (copied because buffers can alias in some hosts) ----
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
        const double ms = sum / n;
        const double a = std::exp (-(double) n / (0.5 * sampleRateNow.load()));
        scEnvelope = ms + a * (scEnvelope - ms);
        scState = scEnvelope > 1e-10 ? (int) SidechainState::active : (int) SidechainState::silent; // -100 dBFS
        measure (sidechainMeter, s0, s1, n);
    }
    else
    {
        scState = (int) SidechainState::notConnected;
        scEnvelope = 0.0;
    }

    // ---- Source -> work buffer (processed as stereo internally) ----
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

    // ---- Input trim ----
    inGain.setTargetValue (juce::Decibels::decibelsToGain (pInTrim->load()));
    for (int i = 0; i < n; ++i)
    {
        const float g = inGain.getNextValue();
        l[i] *= g;
        r[i] *= g;
    }
    measure (inputMeter, l, r, n);

    // ---- Keep the pre-chain signal: for the scope and the latency-matched dry path ----
    float* pre = preBuffer.getWritePointer (0);
    juce::FloatVectorOperations::copy (pre, l, n);
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

    // ---- Chain, or bypass delayed by the chain latency (A/B stays time-aligned) ----
    if (pBypass->load() > 0.5f)
    {
        const int latency = processingChain.latencySamples();
        int pos = (dryDelayPos - latency + kDryDelaySize) & (kDryDelaySize - 1);
        for (int i = 0; i < n; ++i)
        {
            l[i] = dl[pos];
            r[i] = dr[pos];
            pos = (pos + 1) & (kDryDelaySize - 1);
        }
    }
    else
    {
        float* chans[2] = { l, r };
        processingChain.process (chans, 2, n);
    }
    dryDelayPos = (dryDelayPos + n) & (kDryDelaySize - 1);

    // ---- Output trim + non-finite guard (no clipping: the DAW owns the output stage) ----
    outGain.setTargetValue (juce::Decibels::decibelsToGain (pOutTrim->load()));
    int overs = 0;
    for (int i = 0; i < n; ++i)
    {
        const float g = outGain.getNextValue();
        float a = l[i] * g, b = r[i] * g;
        if (! std::isfinite (a)) a = 0.0f;
        if (! std::isfinite (b)) b = 0.0f;
        if (std::abs (a) > 1.0f || std::abs (b) > 1.0f)
            ++overs;
        l[i] = a;
        r[i] = b;
    }
    if (overs > 0)
        clipCount.fetch_add (overs);
    measure (outputMeter, l, r, n);

    // ---- Analysis scope (time-aligned frames) ----
    const float* scopeChans[numScopeChannels] = { pre, l, scL };
    scope.push (scopeChans, n);

    // ---- Main output ----
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

void AtelierProcessor::moveChainSlot (int from, int to)
{
    processingChain.moveSlot (from, to);
    juce::StringArray s;
    for (int i : processingChain.order())
        s.add (juce::String (i));
    apvts.state.setProperty ("chainOrder", s.joinIntoString (","), nullptr);
}

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

void AtelierProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto state = apvts.copyState();
    state.setProperty ("pluginVersion", JucePlugin_VersionString, nullptr);
    if (auto xml = state.createXml())
        copyXmlToBinary (*xml, destData);
}

void AtelierProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    auto xml = getXmlFromBinary (data, sizeInBytes);
    if (xml == nullptr || ! xml->hasTagName (apvts.state.getType()))
        return;

    apvts.replaceState (juce::ValueTree::fromXml (*xml));

    // Chain order
    std::vector<int> order;
    for (auto& t : juce::StringArray::fromTokens (apvts.state.getProperty ("chainOrder").toString(), ",", ""))
        if (t.isNotEmpty())
            order.push_back (t.getIntValue());
    processingChain.setOrder (order); // ignored if invalid

    // Reference file
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
