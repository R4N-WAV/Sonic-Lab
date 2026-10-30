#include "AnalysisWorker.h"

#include <sonic/DspUtils.h>

namespace
{
    constexpr int kTransferSegment = 8192;
    constexpr double kSilenceDb = -100.0;

    double rmsDb (const std::vector<float>& v)
    {
        double s = 0.0;
        for (float x : v) s += (double) x * x;
        return sonic::gainToDb (std::sqrt (s / juce::jmax ((size_t) 1, v.size())), -200.0);
    }

    double peakDb (const std::vector<float>& v)
    {
        float p = 0.0f;
        for (float x : v) p = juce::jmax (p, std::abs (x));
        return sonic::gainToDb (p, -200.0);
    }

    // Finds the strongest spectral peak (20 Hz..20 kHz) with parabolic interpolation.
    double estimateFundamental (const sonic::Spectrum& s)
    {
        const int lo = juce::jmax (2, (int) (20.0 / s.binHz()));
        const int hi = juce::jmin ((int) s.amplitude.size() - 2, (int) (20000.0 / s.binHz()));
        int best = lo;
        for (int k = lo; k <= hi; ++k)
            if (s.amplitude[(size_t) k] > s.amplitude[(size_t) best])
                best = k;
        const double a = std::log (s.amplitude[(size_t) best - 1] + 1e-30);
        const double b = std::log (s.amplitude[(size_t) best] + 1e-30);
        const double c = std::log (s.amplitude[(size_t) best + 1] + 1e-30);
        const double denom = a - 2.0 * b + c;
        const double delta = std::abs (denom) > 1e-12 ? 0.5 * (a - c) / denom : 0.0;
        return (best + juce::jlimit (-0.5, 0.5, delta)) * s.binHz();
    }
}

AnalysisWorker::AnalysisWorker (AtelierProcessor& p) : juce::Thread ("ATELIER analysis"), proc (p)
{
    startThread (juce::Thread::Priority::low);
}

AnalysisWorker::~AnalysisWorker()
{
    stopThread (2000);
}

void AnalysisWorker::setMode (Mode m) { mode = (int) m; resetRequested = true; }
void AnalysisWorker::setFftSize (int size) { fftSize = size; }
void AnalysisWorker::setWindow (sonic::WindowType w) { window = (int) w; }
void AnalysisWorker::resetAveraging() { resetRequested = true; }

bool AnalysisWorker::getResults (Results& dest, int lastSerial) const
{
    const juce::ScopedLock sl (lock);
    if (latest.serial == lastSerial)
        return false;
    dest = latest;
    return true;
}

void AnalysisWorker::run()
{
    while (! threadShouldExit())
    {
        analyse();
        wait (60); // ~15 updates per second
    }
}

void AnalysisWorker::analyse()
{
    using SC = AtelierProcessor;
    const double fs = proc.currentSampleRate();
    const int nfft = fftSize.load();
    const auto win = (sonic::WindowType) window.load();
    const auto m = (Mode) mode.load();

    const size_t end = proc.scope.totalWritten();
    const int need = juce::jmax (nfft, kTransferSegment * 2);
    if (! proc.scope.read (end, need, frames))
        return;

    // x = "device input", y = "device output" for the chosen mode
    const auto& x = m == Mode::chain ? frames[SC::scopePre] : frames[SC::scopeSidechain];
    const auto& y = m == Mode::chain ? frames[SC::scopePost] : frames[SC::scopePre];

    Results r;
    r.sampleRate = fs;
    r.fftSize = nfft;
    r.window = win;
    r.mode = m;

    // ---- spectra (last nfft samples) ----
    const auto sx = sonic::computeSpectrum (x.data(), (int) x.size(), nfft, fs, win);
    const auto sy = sonic::computeSpectrum (y.data(), (int) y.size(), nfft, fs, win);

    const bool sizeChanged = avgX.size() != sx.amplitude.size();
    if (resetRequested.load() || sizeChanged || (int) m != lastMode)
    {
        avgX.assign (sx.amplitude.size(), -200.0f);
        avgY.assign (sy.amplitude.size(), -200.0f);
    }
    // Display averaging in the power domain (~0.3 s). Measurements below use raw data.
    const float k = 0.6f;
    auto blend = [k] (std::vector<float>& avg, const std::vector<double>& amp)
    {
        for (size_t i = 0; i < amp.size(); ++i)
        {
            const float p = (float) (amp[i] * amp[i]);
            const float old = avg[i] <= -199.0f ? p : std::pow (10.0f, avg[i] / 10.0f);
            avg[i] = 10.0f * std::log10 (juce::jmax (1e-20f, old * k + p * (1.0f - k)));
        }
    };
    blend (avgX, sx.amplitude);
    blend (avgY, sy.amplitude);
    r.spectrumX = avgX;
    r.spectrumY = avgY;

    const std::vector<float> yTail (y.end() - nfft, y.end()), xTail (x.end() - nfft, x.end());
    r.rmsYdB = rmsDb (yTail);
    r.peakYdB = peakDb (yTail);
    r.crestDb = r.peakYdB - r.rmsYdB;
    r.xHasSignal = rmsDb (xTail) > kSilenceDb;
    r.yHasSignal = r.rmsYdB > kSilenceDb;

    // ---- harmonics of y ----
    if (! r.yHasSignal)
    {
        r.harmonicsNote = "No signal at the measurement point.";
    }
    else
    {
        double f0 = 0.0;
        // Known fundamental only when the generator sine feeds the measured path.
        if (m == Mode::chain && proc.generatorIsRunningSine())
        {
            f0 = proc.generator().frequencyHz.load();
            r.f0Estimated = false;
        }
        else
        {
            f0 = estimateFundamental (sy);
            r.f0Estimated = true;
        }
        r.harmonics = sonic::analyseHarmonics (sy, f0, 10, juce::jmin (20000.0, fs * 0.49));
        r.harmonicsAvailable = r.harmonics.valid;
        if (! r.harmonics.valid)
            r.harmonicsNote = r.harmonics.invalidReason;
        else if (r.f0Estimated)
            r.harmonicsNote = "Fundamental estimated from the spectral peak. Meaningful only for a single steady tone.";
    }

    // ---- transfer function x -> y ----
    if (resetRequested.exchange (false) || (int) m != lastMode || transferRate != fs)
    {
        transfer.prepare (fs, kTransferSegment, 12.0);
        transferRate = fs;
        lastTransferIndex = end;
        lastMode = (int) m;
    }

    if (m == Mode::reference && proc.sidechainState() == AtelierProcessor::SidechainState::notConnected)
        r.transferNote = "Sidechain not connected: only the processed signal is available, so no input/output comparison is possible.";
    else if (! r.xHasSignal)
        r.transferNote = m == Mode::reference ? "Sidechain is silent: route the unprocessed signal to ATELIER's sidechain input."
                                              : "No signal entering the chain.";
    else
    {
        const size_t fresh = end - lastTransferIndex;
        if (fresh >= (size_t) kTransferSegment)
        {
            const int take = (int) juce::jmin (fresh, (size_t) need);
            const int offset = need - take;
            transfer.addBlock (x.data() + offset, y.data() + offset, take);
            lastTransferIndex = end;
        }
        r.transfer = transfer.result();
        r.transferAvailable = r.transfer.valid;
        if (! r.transferAvailable)
            r.transferNote = "Collecting data...";
        else if (! r.transfer.broadband)
            r.transferNote = "Narrowband excitation (" + juce::String (r.transfer.usableFraction * 100.0, 0)
                           + "% of the band excited): the curve is only valid where drawn, and the delay estimate is unreliable. "
                             "Use pink/white noise, a sweep or music for a full response.";
    }

    r.serial = ++serial;
    const juce::ScopedLock sl (lock);
    latest = std::move (r);
}
