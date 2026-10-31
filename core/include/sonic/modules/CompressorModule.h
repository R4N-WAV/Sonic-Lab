#pragma once
// Compressor with five distinct topologies. They differ in detector placement
// (feed-forward vs feedback), ballistics, ratio behaviour and the nonlinearity
// of the gain element, not just in default settings:
//   Opto      feedback; light-cell ballistics with programme-dependent two-stage release
//   FET       feedback; fast; gain element adds odd harmonics that grow with gain reduction
//   VCA       feed-forward; clean log-domain gain computer, exact static curve
//   Vari-Mu   feedback; ratio rises with gain reduction (very soft); even-harmonic tube colour
//   Diode     feed-forward; diode-bridge gain element adds mixed even/odd distortion with GR

#include "sonic/DspBlocks.h"
#include "sonic/Module.h"

#include <array>
#include <atomic>

namespace sonic
{

class CompressorModule final : public Module
{
public:
    enum class Model { opto = 0, fet, vca, varimu, diode };

    CompressorModule();

    const char* typeId() const override { return "dynamics.compressor"; }
    const char* displayName() const override { return "Compressor"; }
    std::string topology() const override;

    void prepare (double sampleRate, int maxBlockSize, int numChannels) override;
    void reset() override;
    void process (float* const* channels, int numChannels, int numSamples) override;
    float meter (int which) const override { return which == 0 ? grMeter.load() : 0.0f; }
    bool isParamRelevant (int index) const override;
    std::string irrelevantReason (int index) const override;

    // Static input->output curve in dB (steady state, including makeup).
    double staticCurveDb (double inputDb) const;

private:
    double computerDb (double levelDb, Model m, double ratio) const; // target GR (<= 0)

    struct Chan
    {
        Biquad hpf;
        double ms = 0.0;       // RMS state
        double gr = 0.0;       // smoothed GR (dB, <= 0)
        double grSlow = 0.0;   // slow release branch (opto)
        double grFast = 0.0;   // fast release branch (opto)
        double memory = 0.0;   // opto programme memory 0..1
        double prevOut = 0.0;  // feedback detector input
        DcBlocker dc;
    };
    std::array<Chan, 2> ch;
    double fs = 48000.0;
    std::atomic<float> grMeter { 0.0f };
};

} // namespace sonic
