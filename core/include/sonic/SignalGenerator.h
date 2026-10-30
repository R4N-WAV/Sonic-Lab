#pragma once
// Test signal generator. Settings are atomics so the UI can change them while
// the audio thread renders. Saw/square/triangle are band-limited (PolyBLEP)
// so their own aliasing does not pollute distortion measurements.

#include <atomic>
#include <cstdint>

namespace sonic
{

class SignalGenerator
{
public:
    enum class Waveform : int
    {
        sine = 0, saw, square, triangle, impulse, whiteNoise, pinkNoise, logSweep, imdSmpte, numWaveforms
    };
    enum class ChannelMode : int { both = 0, leftOnly, rightOnly, bothRightInverted };

    static const char* waveformName (Waveform w);
    static bool isPeriodic (Waveform w);

    // --- settings (thread-safe) ---
    std::atomic<int> waveform { (int) Waveform::sine };
    std::atomic<int> channelMode { (int) ChannelMode::both };
    std::atomic<float> frequencyHz { 1000.0f };
    std::atomic<float> levelDb { -18.0f };   // peak dBFS for tones, RMS dBFS for noise
    std::atomic<float> phaseDeg { 0.0f };
    std::atomic<float> dcOffset { 0.0f };    // linear, added after level
    std::atomic<float> sweepStartHz { 20.0f };
    std::atomic<float> sweepEndHz { 20000.0f };
    std::atomic<float> sweepSeconds { 10.0f };

    void prepare (double sampleRate);
    void reset();
    void render (float* left, float* right, int numSamples);

    // For the UI: instantaneous sweep frequency (Hz), updated by render().
    float currentSweepFrequency() const { return sweepFreqNow.load (std::memory_order_relaxed); }

private:
    double polyBlep (double t, double dt) const;
    double nextGaussian();

    double fs = 48000.0;
    double phase = 0.0;      // 0..1 cycles
    double phase2 = 0.0;     // second tone for IMD
    double triState = 0.0;
    double sweepTime = 0.0;
    double sweepPhase = 0.0;
    double impulseCounter = 0.0;
    float lastPhaseDeg = 0.0f;
    int lastWaveform = -1;
    double pink[7] {};
    uint64_t rng = 0x9E3779B97F4A7C15ull;
    bool haveSpare = false;
    double spare = 0.0;
    std::atomic<float> sweepFreqNow { 0.0f };
};

} // namespace sonic
