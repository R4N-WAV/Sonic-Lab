#pragma once

#include "ScopeFifo.h"

#include <JuceHeader.h>
#include <sonic/Graph.h>
#include <sonic/SignalGenerator.h>

#include <atomic>

// Atelier by R4N - audio processor.
//
// Buses: main stereo (or mono) in/out + optional stereo sidechain input
// (reference-plugin analysis: unprocessed signal on the sidechain).
//
// Signal path:
//   source (DAW input | generator | file) -> input trim -> bridge (graph of blocks)
//   -> A/B (B = dry, delayed by the bridge latency, optionally loudness-matched)
//   -> output trim
//
// Host-automatable parameters: source, generator, trims, A/B, gain match.
// Block parameters live in the bridge and are saved with the session (v0.2:
// not exposed to host automation - see TODO.md).
class AtelierProcessor : public juce::AudioProcessor, private juce::Timer
{
public:
    enum class Source : int { dawInput = 0, generator, file };
    enum class SidechainState : int { notConnected = 0, silent, active };
    enum ScopeChannel { scopePre = 0, scopePostL, scopePostR, scopeSidechain, numScopeChannels };

    struct Meter
    {
        std::atomic<float> peak[2] { { 0.0f }, { 0.0f } };
        std::atomic<float> rms[2] { { 0.0f }, { 0.0f } };
    };

    AtelierProcessor();
    ~AtelierProcessor() override;

    // juce::AudioProcessor
    bool isBusesLayoutSupported (const BusesLayout&) const override;
    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }
    const juce::String getName() const override { return "Atelier"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 10.0; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return "Default"; }
    void changeProgramName (int, const juce::String&) override {}
    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    // ---- model ----
    juce::AudioProcessorValueTreeState apvts;
    sonic::Graph& graph() { return bridge; }
    sonic::SignalGenerator& generator() { return gen; }

    juce::ValueTree bridgeToTree() const;
    void bridgeFromTree (const juce::ValueTree& tree);
    static juce::ValueTree graphToTree (const sonic::Graph& g);
    static void treeToGraph (const juce::ValueTree& tree, sonic::Graph& g);

    // undo / redo of the bridge (snapshots)
    void checkpoint();
    bool undo();
    bool redo();
    bool canUndo() const { return undoPos > 0; }
    bool canRedo() const { return undoPos + 1 < (int) undoStack.size(); }

    // project identity (saved with the session)
    juce::String projectName() const { return apvts.state.getProperty ("projectName", "Untitled study").toString(); }
    void setProjectName (const juce::String& s) { apvts.state.setProperty ("projectName", s, nullptr); }
    juce::String objective() const { return apvts.state.getProperty ("objective", "").toString(); }
    void setObjective (const juce::String& s) { apvts.state.setProperty ("objective", s, nullptr); }

    // ---- file source ----
    bool loadFile (const juce::File& f, juce::String& error);
    juce::AudioTransportSource& transport() { return transportSource; }
    juce::AudioThumbnail& thumbnail() { return thumb; }
    juce::File currentFile() const { return loadedFile; }
    juce::AudioFormatManager& formats() { return formatManager; }

    // ---- status ----
    double currentSampleRate() const { return sampleRateNow.load(); }
    int currentBlockSize() const { return blockSizeNow.load(); }
    int currentLatency() const { return latencyNow.load(); }
    SidechainState sidechainState() const { return (SidechainState) scState.load(); }
    Source source() const;
    bool generatorIsRunningSine() const;
    bool isBypassed() const { return pBypass->load() > 0.5f; }
    float gainMatchDb() const { return matchDb.load(); }
    Meter inputMeter, outputMeter, sidechainMeter;
    ScopeFifo<numScopeChannels> scope;

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
    void processChunk (juce::AudioBuffer<float>& mainIn, juce::AudioBuffer<float>& mainOut,
                       const float* const* scIn, int start, int n);
    void measure (Meter& m, const float* l, const float* r, int n);
    void syncGenerator();
    void timerCallback() override;

    sonic::Graph bridge;
    sonic::SignalGenerator gen;

    std::atomic<float>* pSource = nullptr;
    std::atomic<float>* pGenRun = nullptr;
    std::atomic<float>* pGenWave = nullptr;
    std::atomic<float>* pGenFreq = nullptr;
    std::atomic<float>* pGenLevel = nullptr;
    std::atomic<float>* pGenPhase = nullptr;
    std::atomic<float>* pGenDc = nullptr;
    std::atomic<float>* pGenChannels = nullptr;
    std::atomic<float>* pSweepStart = nullptr;
    std::atomic<float>* pSweepEnd = nullptr;
    std::atomic<float>* pSweepTime = nullptr;
    std::atomic<float>* pBypass = nullptr;
    std::atomic<float>* pMatch = nullptr;
    std::atomic<float>* pInTrim = nullptr;
    std::atomic<float>* pOutTrim = nullptr;
    std::atomic<float>* pFileGain = nullptr;

    juce::AudioFormatManager formatManager;
    juce::TimeSliceThread readAheadThread { "Atelier file read-ahead" };
    std::unique_ptr<juce::AudioFormatReaderSource> readerSource;
    juce::AudioTransportSource transportSource;
    juce::AudioThumbnailCache thumbCache { 4 };
    juce::AudioThumbnail thumb { 512, formatManager, thumbCache };
    juce::File loadedFile;

    juce::AudioBuffer<float> work, dryDelay, scBuffer, preBuffer;
    int dryDelayPos = 0;
    juce::SmoothedValue<float> inGain, outGain, fileGain, bGain;
    double wetPow = 0.0, dryPow = 0.0;
    std::atomic<float> matchDb { 0.0f };

    std::atomic<double> sampleRateNow { 48000.0 };
    std::atomic<int> blockSizeNow { 512 };
    std::atomic<int> latencyNow { 0 };
    std::atomic<int> scState { 0 };
    double scEnvelope = 0.0;

    std::vector<juce::String> undoStack;
    int undoPos = -1;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AtelierProcessor)
};
