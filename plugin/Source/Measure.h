#pragma once
// Offline, deterministic measurements of a bridge (or a single block) for the
// blueprint. Everything is rendered from a fresh copy of the saved bridge state,
// so results never depend on what is currently playing.

#include <JuceHeader.h>

#include <vector>

namespace measure
{
    struct LevelPoint
    {
        double inDb = 0, outDb = 0, thdPct = 0, thdDb = -200;
        std::vector<double> hDbc; // H2..H10
    };

    struct Result
    {
        double fs = 48000.0;
        int latency = 0;

        // linear frequency response (H1 estimate, white noise -18 dBFS RMS)
        std::vector<double> frF, frMag, frPhaseDeg, frCoh;
        double meanCoherence = 0.0;

        // harmonics vs level (1 kHz) and vs frequency (-12 dBFS)
        std::vector<LevelPoint> levels;
        std::vector<double> thdFreqF, thdFreqDb;
        std::vector<float> spectrumDb;   // output spectrum, 1 kHz at -12 dBFS
        double spectrumBinHz = 0.0;

        // dynamics (1 kHz step -30 -> -6 -> -30 dBFS)
        std::vector<float> envIn, envOut; // dB per 5 ms frame (for plots)
        // Timing from a synchronous 1 kHz demodulator (2 ms Hann window, 0.1 ms hop).
        // attack/release = 63 % of the gain change; release90 = 90 % (shows slow, two-stage release tails).
        double attackMs = -1, releaseMs = -1, release90Ms = -1, grStepDb = 0;
        double timingResolutionMs = 0.5;

        std::vector<float> ir;           // impulse response (latency compensated)

        double crosstalkDb = -200, corrMono = 1, corrWide = 0, sideToMidDb = -200;
        std::vector<float> scopeL, scopeR;
        double noiseFloorDb = -200;      // <= kNoiseFloorLimit means "below the measurement floor"
        static constexpr double kNoiseFloorLimit = -150.0;
        double imdPct = 0;
    };

    // Renders input through the bridge described by tree (latency compensated, same length as input).
    void render (const juce::ValueTree& bridgeTree, double fs, const std::vector<float>& inL, const std::vector<float>& inR,
                 std::vector<float>& outL, std::vector<float>& outR, int* latencyOut = nullptr);

    // Full measurement set. progress (0..1) may be nullptr. shouldStop returns true to abort.
    Result run (const juce::ValueTree& bridgeTree, double fs, std::function<void (double)> progress, std::function<bool()> shouldStop);

    // Single-block bridge: Input -> block -> Output.
    juce::ValueTree isolateBlock (const juce::ValueTree& bridgeTree, int blockId);

    // Deterministic reference program used for the audio exports (~17 s).
    void referenceProgram (double fs, std::vector<float>& l, std::vector<float>& r);
}
