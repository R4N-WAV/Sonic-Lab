#pragma once
// Background analysis thread. Pulls time-aligned audio from the processor's
// scope FIFO and computes spectra, harmonics and transfer functions off the
// audio thread. The UI copies the latest Results under a lock (the audio
// thread never touches this lock).

#include "PluginProcessor.h"

#include <sonic/Analysis.h>
#include <sonic/TransferAnalyzer.h>
#include <sonic/Biquad.h>
#include <sonic/Oversampler.h>

class AnalysisWorker : private juce::Thread
{
public:
    enum class Mode : int
    {
        chain = 0,      // ATELIER chain: pre-chain (x) -> post-chain (y)
        reference       // Reference plugin: sidechain (x, unprocessed) -> main input (y, processed)
    };

    struct Results
    {
        double sampleRate = 48000.0;
        int fftSize = 8192;
        sonic::WindowType window = sonic::WindowType::blackmanHarris4;
        Mode mode = Mode::chain;

        // Display spectra (dBFS per bin, display-averaged)
        std::vector<float> spectrumX, spectrumY;
        bool xHasSignal = false, yHasSignal = false;

        // Harmonics of the measured signal (y), unaveraged
        sonic::HarmonicResult harmonics;
        bool f0Estimated = false;   // true = fundamental found from the spectrum peak
        bool harmonicsAvailable = false;
        juce::String harmonicsNote;

        // Linear transfer estimate x -> y
        sonic::TransferAnalyzer::Result transfer;
        bool transferAvailable = false;
        juce::String transferNote;

        double rmsYdB = -200.0, peakYdB = -200.0, crestDb = 0.0;
        double lufsShort = -200.0;      // ITU-R BS.1770 K-weighted, 3 s, output L+R
        double truePeakDb = -200.0;     // 4x oversampled peak of the output
        double correlation = 0.0;       // output L/R, last 8192 samples
        bool stereoValid = false;
        int serial = 0;
    };

    explicit AnalysisWorker (AtelierProcessor& p);
    ~AnalysisWorker() override;

    void setMode (Mode m);
    void setFftSize (int size);
    void setWindow (sonic::WindowType w);
    void resetAveraging();

    // Copies the latest results; returns false if nothing new since lastSerial.
    bool getResults (Results& dest, int lastSerial) const;

private:
    void run() override;
    void analyse();

    AtelierProcessor& proc;
    mutable juce::CriticalSection lock;
    Results latest;

    std::atomic<int> mode { (int) Mode::chain };
    std::atomic<int> fftSize { 8192 };
    std::atomic<int> window { (int) sonic::WindowType::blackmanHarris4 };
    std::atomic<bool> resetRequested { true };

    // worker-thread state
    sonic::TransferAnalyzer transfer;
    sonic::Oversampler tpOs;
    double tpRate = 0.0;
    double transferRate = 0.0;
    int lastMode = -1;
    size_t lastTransferIndex = 0;
    std::vector<float> avgX, avgY;
    std::array<std::vector<float>, AtelierProcessor::numScopeChannels> frames;
    int serial = 0;
};
