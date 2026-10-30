#include "sonic/SignalGenerator.h"
#include "sonic/DspUtils.h"

#include <algorithm>
#include <iterator>
#include <cmath>

namespace sonic
{

namespace
{
    // Measured so that pink noise has ~unity RMS for unity-RMS white input.
    constexpr double kPinkGain = 0.327;
}

const char* SignalGenerator::waveformName (Waveform w)
{
    switch (w)
    {
        case Waveform::sine:       return "Sine";
        case Waveform::saw:        return "Sawtooth";
        case Waveform::square:     return "Square";
        case Waveform::triangle:   return "Triangle";
        case Waveform::impulse:    return "Impulse";
        case Waveform::whiteNoise: return "White Noise";
        case Waveform::pinkNoise:  return "Pink Noise";
        case Waveform::logSweep:   return "Log Sweep";
        case Waveform::imdSmpte:   return "IMD (SMPTE 60 Hz + 7 kHz)";
        default:                   return "?";
    }
}

bool SignalGenerator::isPeriodic (Waveform w)
{
    return w == Waveform::sine || w == Waveform::saw || w == Waveform::square
        || w == Waveform::triangle || w == Waveform::impulse;
}

void SignalGenerator::prepare (double sampleRate)
{
    fs = sampleRate;
    reset();
}

void SignalGenerator::reset()
{
    phase = phase2 = 0.0;
    triState = -1.0; // square starts high at phase 0, so the triangle ramps up from -1
    sweepTime = sweepPhase = 0.0;
    impulseCounter = 0.0;
    std::fill (std::begin (pink), std::end (pink), 0.0);
    lastPhaseDeg = phaseDeg.load();
    phase = lastPhaseDeg / 360.0;
    phase -= std::floor (phase);
    haveSpare = false;
}

double SignalGenerator::polyBlep (double t, double dt) const
{
    if (t < dt)
    {
        t /= dt;
        return t + t - t * t - 1.0;
    }
    if (t > 1.0 - dt)
    {
        t = (t - 1.0) / dt;
        return t * t + t + t + 1.0;
    }
    return 0.0;
}

double SignalGenerator::nextGaussian()
{
    if (haveSpare)
    {
        haveSpare = false;
        return spare;
    }
    auto uniform = [this]
    {
        // xorshift64*
        rng ^= rng >> 12;
        rng ^= rng << 25;
        rng ^= rng >> 27;
        const uint64_t r = rng * 0x2545F4914F6CDD1Dull;
        return ((double) (r >> 11) + 0.5) * (1.0 / 9007199254740992.0);
    };
    const double u1 = uniform(), u2 = uniform();
    const double mag = std::sqrt (-2.0 * std::log (u1));
    spare = mag * std::sin (kTwoPi * u2);
    haveSpare = true;
    return mag * std::cos (kTwoPi * u2);
}

void SignalGenerator::render (float* left, float* right, int n)
{
    const auto wf = (Waveform) waveform.load();
    const auto mode = (ChannelMode) channelMode.load();
    const double freq = std::clamp ((double) frequencyHz.load(), 0.1, fs * 0.49);
    const double dt = freq / fs;
    const double amp = dbToGain (levelDb.load());
    const double dc = dcOffset.load();

    if ((int) wf != lastWaveform)
    {
        lastWaveform = (int) wf;
        reset();
    }

    // Apply a phase-offset change as a jump of the oscillator phase.
    const float pd = phaseDeg.load();
    if (pd != lastPhaseDeg)
    {
        phase += (pd - lastPhaseDeg) / 360.0;
        phase -= std::floor (phase);
        lastPhaseDeg = pd;
    }

    const double f1 = std::clamp ((double) sweepStartHz.load(), 1.0, fs * 0.49);
    const double f2 = std::clamp ((double) sweepEndHz.load(), 1.0, fs * 0.49);
    const double T = std::max (0.1, (double) sweepSeconds.load());

    for (int i = 0; i < n; ++i)
    {
        double y = 0.0;

        switch (wf)
        {
            case Waveform::sine:
                y = std::sin (kTwoPi * phase);
                break;

            case Waveform::saw:
                y = 2.0 * phase - 1.0 - polyBlep (phase, dt);
                break;

            case Waveform::square:
            case Waveform::triangle:
            {
                double sq = phase < 0.5 ? 1.0 : -1.0;
                sq += polyBlep (phase, dt);
                double t2 = phase + 0.5;
                t2 -= std::floor (t2);
                sq -= polyBlep (t2, dt);

                if (wf == Waveform::square)
                    y = sq;
                else
                {
                    // Leaky integration of the band-limited square -> triangle
                    triState = dt * 4.0 * sq + (1.0 - 0.0005) * triState;
                    y = triState;
                }
                break;
            }

            case Waveform::impulse:
                impulseCounter -= 1.0;
                if (impulseCounter <= 0.0)
                {
                    y = 1.0;
                    impulseCounter += 1.0 / dt;
                }
                break;

            case Waveform::whiteNoise:
                y = nextGaussian();
                break;

            case Waveform::pinkNoise:
            {
                // Paul Kellet's refined pink filter
                const double w = nextGaussian();
                pink[0] = 0.99886 * pink[0] + w * 0.0555179;
                pink[1] = 0.99332 * pink[1] + w * 0.0750759;
                pink[2] = 0.96900 * pink[2] + w * 0.1538520;
                pink[3] = 0.86650 * pink[3] + w * 0.3104856;
                pink[4] = 0.55000 * pink[4] + w * 0.5329522;
                pink[5] = -0.7616 * pink[5] - w * 0.0168980;
                y = (pink[0] + pink[1] + pink[2] + pink[3] + pink[4] + pink[5] + pink[6] + w * 0.5362) * kPinkGain;
                pink[6] = w * 0.115926;
                break;
            }

            case Waveform::logSweep:
            {
                const double fNow = f1 * std::exp (std::log (f2 / f1) * sweepTime / T);
                y = std::sin (kTwoPi * sweepPhase);
                sweepPhase += fNow / fs;
                sweepPhase -= std::floor (sweepPhase);
                sweepTime += 1.0 / fs;
                if (sweepTime >= T)
                {
                    sweepTime = 0.0;
                    sweepPhase = 0.0;
                }
                if (i == 0)
                    sweepFreqNow.store ((float) fNow, std::memory_order_relaxed);
                break;
            }

            case Waveform::imdSmpte:
                // 60 Hz and 7 kHz at 4:1 amplitude ratio, combined peak = level
                y = 0.8 * std::sin (kTwoPi * phase) + 0.2 * std::sin (kTwoPi * phase2);
                phase2 += 7000.0 / fs;
                phase2 -= std::floor (phase2);
                break;

            default:
                break;
        }

        if (wf == Waveform::imdSmpte)
            phase += 60.0 / fs;
        else
            phase += dt;
        phase -= std::floor (phase);

        const float s = (float) (y * amp + dc);
        switch (mode)
        {
            case ChannelMode::both:              left[i] = s; right[i] = s; break;
            case ChannelMode::leftOnly:          left[i] = s; right[i] = 0.0f; break;
            case ChannelMode::rightOnly:         left[i] = 0.0f; right[i] = s; break;
            case ChannelMode::bothRightInverted: left[i] = s; right[i] = -s; break;
        }
    }
}

} // namespace sonic
