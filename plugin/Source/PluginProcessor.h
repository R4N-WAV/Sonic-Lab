#pragma once

#include "ScopeFifo.h"

#include <JuceHeader.h>
#include <sonic/Chain.h>
#include <sonic/SignalGenerator.h>

#include <atomic>

// ATELIER by R4N - audio processor.
//
// Buses: main stereo (or mono) in/out + optional stereo sidechain input.
// Reference-plugin analysis: DAW routes  source -> reference plugin -> ATELIER main
// input, and the unprocessed source -> ATELIER sidechain.
//
// Signal path:  source (DAW input | generator | file) -> input trim
//               -> chain (or latency-matched bypass for A/B) -> output trim
class AtelierProcessor : public juce::AudioProcessor
{
public:
    enum class Source : int { dawInput = 0, generator, file };
    enum class SidechainState : int { notConnected = 0, silent, active };

    // Scope channels handed to the analysis worker (left channel of each point)
    enum ScopeChannel { scopePre = 0, scopePost, scopeSidechain, numScopeChannels };

    struct Meter
    {
        std::atomic<float> peak[2] { { 0.0f }, { 0.0f } };
        std::atomic<float> rms[2] { { 0.0f }, { 0.0f } };
    };

    static constexpr const char* kModuleKeys[] = { "tube", "eq" };

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

    const juce::String getName() const override { return "ATELIER"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return "Default"; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    // --- model access ---
    juce::AudioProcessorValueTreeState apvts;
    sonic::Chain& chain() { return processingChain; }
    sonic::SignalGenerator& generator() { return gen; }
    static juce::String paramId (int moduleIndex, const std::string& coreId);
    static juce::String enabledId (int moduleIndex);

    void moveChainSlot (int from, int to);
    std::vector<int> chainOrder() const { return processingChain.order(); }

    // --- file source ---
    bool loadFile (const juce::File& f, juce::String& error);
    juce::AudioTransportSource& transport() { return transportSource; }
    juce::AudioThumbnail& thumbnail() { return thumb; }
    juce::File currentFile() const { return loadedFile; }

    // --- status for UI / analysis ---
    double currentSampleRate() const { return sampleRateNow.load(); }
    int currentBlockSize() const { return blockSizeNow.load(); }
    int currentLatency() const { return latencyNow.load(); }
    SidechainState sidechainState() const { return (SidechainState) scState.load(); }
    Source source() const;
    bool generatorIsRunningSine() const;
    Meter inputMeter, outputMeter, sidechainMeter;
    std::atomic<int> clipCount { 0 };
    ScopeFifo<numScopeChannels> scope;

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
    static BusesProperties makeBuses();
    void bindParameters();
    void syncParameters();
    void processChunk (juce::AudioBuffer<float>& mainIn, juce::AudioBuffer<float>& mainOut,
                       const float* const* scIn, int start, int n);
    void measure (Meter& m, const float* l, const float* r, int n);

    sonic::SignalGenerator gen;
    sonic::Chain processingChain;

    struct ParamLink
    {
        std::atomic<float>* raw;
        sonic::Module* module;
        int index;
    };
    std::vector<ParamLink> links;
    std::vector<std::atomic<float>*> enabledParams;
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
    std::atomic<float>* pInTrim = nullptr;
    std::atomic<float>* pOutTrim = nullptr;
    std::atomic<float>* pFileGain = nullptr;

    // file player
    juce::AudioFormatManager formatManager;
    juce::TimeSliceThread readAheadThread { "ATELIER file read-ahead" };
    std::unique_ptr<juce::AudioFormatReaderSource> readerSource;
    juce::AudioTransportSource transportSource;
    juce::AudioThumbnailCache thumbCache { 4 };
    juce::AudioThumbnail thumb { 512, formatManager, thumbCache };
    juce::File loadedFile;

    // buffers (allocated in prepareToPlay only)
    juce::AudioBuffer<float> work, dryDelay, scBuffer, preBuffer;
    int dryDelayPos = 0;
    juce::SmoothedValue<float> inGain, outGain, fileGain;

    std::atomic<double> sampleRateNow { 48000.0 };
    std::atomic<int> blockSizeNow { 512 };
    std::atomic<int> latencyNow { 0 };
    std::atomic<int> scState { 0 };
    double scEnvelope = 0.0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AtelierProcessor)
};
