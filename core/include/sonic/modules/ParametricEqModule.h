#pragma once
// Parametric EQ: up to 24 bands, each with shape, slope, stereo placement
// (Stereo / Left / Right / Mid / Side) and optional dynamic gain.
// Phase modes:
//   Zero latency  - bilinear (RBJ) IIR filters at the host rate
//   Natural       - the same filters run at 2x the host rate (removes the
//                   high-frequency "cramping" of the bilinear transform), 64 samples latency
//   Linear        - FIR designed from the static magnitude response (exact
//                   linear phase), partitioned FFT convolution. Dynamic bands are
//                   applied statically in this mode; Left/Right bands are
//                   applied to both channels when Mid/Side bands are present.

#include "sonic/Convolver.h"
#include "sonic/DspBlocks.h"
#include "sonic/Module.h"
#include "sonic/Oversampler.h"

#include <array>
#include <atomic>
#include <condition_variable>
#include <mutex>
#include <thread>

namespace sonic
{

class ParametricEqModule final : public Module
{
public:
    static constexpr int kMaxBands = 24;
    static constexpr int kParamsPerBand = 12;
    static constexpr int kMaxSections = 8;

    enum class Shape { bell = 0, lowShelf, highShelf, lowCut, highCut, notch, bandPass, tilt };
    enum class Place { stereo = 0, left, right, mid, side };
    enum class Phase { zeroLatency = 0, natural, linear };

    ParametricEqModule();
    ~ParametricEqModule() override;

    const char* typeId() const override { return "tone.eq"; }
    const char* displayName() const override { return "Parametric EQ"; }
    std::string topology() const override;

    void prepare (double sampleRate, int maxBlockSize, int numChannels) override;
    void reset() override;
    void process (float* const* channels, int numChannels, int numSamples) override;
    int latencySamples() const override;

    // ---- band helpers (parameter index arithmetic) ----
    static int bandParam (int band, int field) { return kGlobalParams + band * kParamsPerBand + field; }
    enum Field { fOn = 0, fShape, fFreq, fGain, fQ, fSlope, fPlace, fDyn, fThr, fRange, fAtt, fRel };
    int firstFreeBand() const;
    bool bandOn (int b) const { return getParam (bandParam (b, fOn)) > 0.5f; }

    // ---- response for UI / blueprint (static gains) ----
    // view: -1 = all bands, 0 = Left, 1 = Right, 2 = Mid, 3 = Side
    double responseDb (double freq, int view = -1) const;
    double bandResponseDb (int band, double freq) const;
    float dynamicGainDb (int band) const { return dynGain[(size_t) band].load (std::memory_order_relaxed); }
    static const char* slopeName (int index);

    // Offline rendering: designs the linear-phase filter synchronously (no
    // background thread latency). Call after prepare().
    void prepareOffline();

private:
    static constexpr int kGlobalParams = 3; // phase, linear quality, output

    struct Design
    {
        BiquadCoeffs s[kMaxSections];
        int n = 0;
    };
    Design designBand (int b, double rate, double gainOverrideDb, bool useOverride) const;

    struct Band
    {
        std::array<std::array<Biquad, kMaxSections>, 2> f; // two signal paths (L/R or M/S)
        std::array<Biquad, 2> det;
        std::array<double, 2> env { -120.0, -120.0 };
        double amount = 0.0;
        unsigned lastVersion = ~0u;
        double lastGain = 1e9;
        int nSections = 0;
    };

    void processIir (double* l, double* r, int n, double rate);
    void designLinearIr();
    void workerLoop();

    std::array<Band, kMaxBands> bands;
    std::array<std::atomic<float>, kMaxBands> dynGain {};
    double fs = 48000.0;
    int maxBlock = 512;
    std::array<Oversampler, 2> os;
    std::vector<double> bufL, bufR;
    std::vector<float> tmpL, tmpR;

    // linear phase
    std::array<PartitionedConvolver, 2> conv;
    std::atomic<int> linearMs { 0 };   // 1 = linear filters are in M/S domain
    int linearLen = 4096;
    std::thread worker;
    std::atomic<bool> quit { false };
    std::atomic<bool> linearActive { false };
    std::atomic<unsigned> designedVersion { ~0u };
    std::atomic<double> workerFs { 48000.0 };
    std::mutex designMutex;
};

} // namespace sonic
