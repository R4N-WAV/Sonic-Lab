# ATELIER by R4N: Architecture

## Overview

ATELIER is a **Windows x64 VST3 audio effect**, built with JUCE 8. The DAW owns the audio devices, drivers, sample rate and buffers. ATELIER owns:

- the DSP engine
- signal generation
- reference audio
- analysis
- (later) blueprint export

```
repo/
├── core/          sonic_core: JUCE-free C++17 DSP library (the "truth")
│   ├── Module / Chain           parameter specs, lock-free chain ordering
│   ├── SignalGenerator          band-limited test signals
│   ├── Oversampler              linear-phase polyphase FIR, 1/2/4/8x, constant 64-sample latency
│   ├── Biquad                   RBJ filters, 64-bit, response = audio coefficients
│   ├── Analysis                 FFT, windows, calibrated spectrum, harmonics/THD/THD+N
│   ├── TransferAnalyzer         Welch H1 transfer function, coherence, PHAT delay estimate
│   └── modules/                 TubeModule, EqModule
├── tests/         CoreTests.cpp: measurable DSP assertions (run in CI on every build)
├── plugin/        JUCE VST3: processor, editor, analysis worker, UI panels
├── installer/     Inno Setup script + texts
└── docs/          DAW routing guide
```

## Key design rules

1. **One source of truth for parameters.** Each core module declares `ParamSpec`s (id, unit, range, default, skew, description). From these, the plugin generates:
   - its host-automatable parameters (`<module>.<param>`, e.g. `tube.drive_db`)
   - its UI controls

   The blueprint exporter will read the same specs, so names, units and ranges cannot drift.

2. **Graphs come from the DSP code.**
   - The EQ curve uses the exact biquad coefficients the audio uses (`EqModule::design`).
   - The tube curve uses `TubeModule::staticCurve`.
   - Tests check that measured responses equal the displayed ones.

3. **Real-time safety.** The audio thread:
   - never allocates, locks or does file I/O
   - takes parameters from atomics (APVTS raw values, synced into core atomics each block)
   - switches oversampling factors without allocating (kernels are precomputed)
   - splits oversized host blocks into chunks

4. **Analysis off the audio thread.**
   - The processor pushes time-aligned frames (pre-chain, post-chain, sidechain) into a lock-free `ScopeFifo`.
   - `AnalysisWorker` (a background thread) computes spectra, harmonics and transfer functions.
   - The UI copies the latest results under a lock that the audio thread never touches.

5. **Honest measurement labels.**
   - **MEASURED**: known stimulus, e.g. the generator's sine frequency.
   - **ESTIMATED**: f0 taken from the spectral peak; transfer function by H1.
   - Plus the conditions: FFT size, window, sample rate, measurement point.
   - Black-box results never claim circuit topology.

## Signal path (plugin)

```
source: DAW input | generator | file  ->  input trim  ->  chain (Tube, EQ; reorderable, per-stage enable)
        -> output trim -> DAW
A/B bypass = dry signal delayed by the chain latency (time-aligned comparison)
```

### Buses

- Main input/output: mono or stereo, input must equal output.
- Optional **sidechain input**: mono or stereo, inactive by default.

The sidechain state is shown as not connected, silent or active.

## Reference-plugin analysis (no internal VST3 host)

`source → [reference plugin] → ATELIER main input`, with the unprocessed source sent to the ATELIER sidechain.

In Reference mode:
- x = sidechain
- y = main input, taken *before* ATELIER's chain

If the sidechain is not connected, ATELIER only analyses the processed signal and says so. See `docs/DAW_ROUTING.md`.

**Decision:** no internal plugin hosting. DAW routing covers the need without crash-isolation and licensing risk. This will be revisited only if a needed feature cannot be done through routing, e.g. fully automated multi-level sweeps of a reference plugin.

## Dependencies and licensing

| Dependency | Version | Licence | Use |
|---|---|---|---|
| JUCE | 8.0.9 (CMake FetchContent) | AGPLv3 / commercial | Plugin framework, UI, audio formats |
| VST3 SDK | bundled with JUCE | MIT (since Steinberg's 2025 relicensing) | VST3 format |
| Inno Setup 6 | CI build tool only | free | Windows installer |
| pluginval | CI validation only | GPLv3 | Automated VST3 host tests |

This private project uses JUCE under AGPLv3. If ATELIER is ever distributed commercially or closed-source, a JUCE commercial licence is needed.

## Build and CI

GitHub Actions on `windows-2022`:

1. Configure (VS 2022, x64).
2. Build `Atelier_VST3` and `sonic_core_tests`.
3. Run the DSP tests.
4. Run **pluginval** at strictness 5.
5. Build the Inno Setup installer.
6. Upload the installer, portable ZIP and logs as artifacts.

Linux is used only for local compile checks of the same sources.
