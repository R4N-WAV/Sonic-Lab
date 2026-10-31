#pragma once
// Offline "figure" renders of a single block.
//
// Every block view in Atelier draws a picture of what the block does. Those
// pictures are measured, not drawn from formulas: a fresh copy of the block gets
// the same parameters, a known test signal is played through it offline, and the
// result is analysed. So the picture always matches the sound, whatever the
// block's internals are.
//
// Runs on a background thread (not real-time safe).

#include <atomic>
#include <string>
#include <vector>

namespace sonic::preview
{

enum Kind : unsigned
{
    response  = 1u << 0,   // small-signal magnitude response (impulse at -30 dBFS)
    harmonics = 1u << 1,   // H2..H10 of a sine
    transfer  = 1u << 2,   // input/output of one 100 Hz cycle at full scale (shows the curve and hysteresis)
    dynamics  = 1u << 3,   // static curve + response to a level step
    envelope  = 1u << 4,   // drum-like hits: input vs output envelope
    impulse   = 1u << 5,   // echoes / reverb tail
    modmap    = 1u << 6,   // response over time (modulation effects)
    pitch     = 1u << 7,   // spectrum of a harmonic tone, before and after
    stereo    = 1u << 8    // vectorscope cloud + width per frequency
};

// Which figures a block type gets.
unsigned kindsFor (const std::string& typeId);

struct Settings
{
    std::string type;
    std::vector<float> params;   // in parameter-index order
    float mix = 1.0f;            // the block's Mix (0..1); figures show what you hear
    float outDb = 0.0f;          // the block's output gain
    double sampleRate = 48000.0;
    double bpm = 120.0;
};

struct Result
{
    unsigned kinds = 0;

    // response (log frequency grid)
    std::vector<float> respHz, respDb;
    std::vector<float> respLoDb, respHiDb;     // Filter with LFO: the swept extremes

    // harmonics
    double testHz = 1000.0, testDbfs = -12.0;
    std::vector<float> harmDbc;                // H2..H10 (dBc)
    double thdPct = 0.0, evenPct = 50.0;

    // transfer (one cycle, input normalised to +-1)
    std::vector<float> xyIn, xyOut;

    // dynamics
    std::vector<float> staticIn, staticOut;    // dB
    double thresholdDb = 0.0;
    std::vector<float> stepMs, stepInDb, stepOutDb, stepGrDb;
    double stepLowDb = -40.0, stepHighDb = -6.0;

    // envelope (transient shaper)
    std::vector<float> envMs, envInDb, envOutDb;

    // impulse (delay taps, reverb tail): peak level per time slice, dB
    std::vector<float> irMs, irL, irR;
    double rt60 = 0.0, rtLow = 0.0, rtMid = 0.0, rtHigh = 0.0;

    // modulation map: rows = time, cols = frequency (dB relative to the dry signal)
    int mapRows = 0, mapCols = 0;
    std::vector<float> mapDb, mapHz;
    double mapSeconds = 0.0;

    // pitch: spectra of a 220 Hz harmonic tone
    std::vector<float> specHz, specInDb, specOutDb;
    double pitchInHz = 220.0;

    // stereo: output cloud in mid/side coordinates (normalised) and width per band
    std::vector<float> scopeM, scopeS;
    std::vector<float> widthHz, widthPct;
    double correlation = 0.0;
};

// Renders the requested figures. 'cancel' (optional) aborts early when set.
Result render (const Settings& settings, unsigned kinds, const std::atomic<bool>* cancel = nullptr);

} // namespace sonic::preview
