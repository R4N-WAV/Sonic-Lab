# Atelier: Architecture

## Overview

Atelier is a **Windows x64 VST3 audio effect**, built with JUCE 8. The DAW owns the audio devices, drivers, sample rate and buffers. Atelier owns:

- the DSP engine
- signal generation
- reference audio
- analysis
- the Bridge (node graph of blocks)
- blueprint export

```
repo/
├── core/          sonic_core: JUCE-free C++17 DSP library (the "truth")
│   ├── Module                   parameter specs (id, unit, range, skew, description, simple/advanced group)
│   ├── Graph                    Bridge DAG: nodes, edges, cycle check, latency-aligned fan-in sums, per-node mix
│   ├── ModuleRegistry           block catalogue (type id → factory)
│   ├── Convolver                partitioned convolution (linear-phase EQ)
│   ├── SignalGenerator          band-limited test signals
│   ├── Oversampler              linear-phase polyphase FIR, 1/2/4/8x, constant 64-sample latency
│   ├── Biquad                   RBJ filters, 64-bit, response = audio coefficients
│   ├── Analysis                 FFT, windows, calibrated spectrum, harmonics/THD/THD+N
│   ├── TransferAnalyzer         Welch H1 transfer function, coherence, PHAT delay estimate
│   └── modules/                 Tube, Saturation, Compressor (5 models), ParametricEq,
│                                Character (tape, transformer, console), Effects (delay, reverb, chorus,
│                                flanger, phaser, exciter, transient, stereo, filter, gain)
├── tests/         CoreTests / ModuleTests / PluginSmokeTest: measurable assertions (run in CI)
├── plugin/        JUCE VST3: processor, editor, analysis worker, Bridge UI, block editors,
│                  offline measurement (Measure) and Blueprint export
├── installer/     Inno Setup script + texts
└── docs/          DAW routing guide
```

## Key design rules

1. **One source of truth for parameters.** Each core module declares `ParamSpec`s (id, unit, range, default, skew, description). From these, the plugin generates:
   - its UI controls (SIMPLE = specs flagged `simple`, ADVANCED = all, grouped by `group`)
   - the Blueprint parameter tables

   Names, units and ranges therefore cannot drift between sound, UI and documentation.

2. **Graphs come from the DSP code.**
   - The EQ curve uses the exact coefficients the audio uses (`ParametricEqModule::responseDb`).
   - Saturation and compressor curves use `staticCurve` / `staticCurveDb` from the modules.
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
source: DAW input | generator | file  ->  input trim  ->  Bridge (Input -> blocks -> Output)
        -> output trim -> DAW
B = dry signal delayed by the bridge latency, loudness-matched to A (1.5 s power tracking)
```

### Buses

- Main input/output: mono or stereo, input must equal output.
- Optional **sidechain input**: mono or stereo, inactive by default.

The sidechain state is shown as not connected, silent or active.

## The Bridge

- A directed acyclic graph between Input (id 0) and Output (id 1). `connect` rejects cycles and duplicates.
- Several wires into one block are summed. Shorter paths are delayed so every join is sample-aligned.
- Each node: module, mix (dry is time-aligned to wet), output gain, enable.
- Edits build a new compiled plan on the message thread; the audio thread picks it up with one atomic swap. Old plans are freed later on the message thread (never on the audio thread).
- The graph is stored in the plugin state as a `BRIDGE` tree; undo / redo uses snapshots of that tree.
- Block parameters live in core atomics (not APVTS) for now, so they are not host-automatable yet.

## Blueprint export

`Blueprint::exportPackage` renders the bridge offline at the chosen rate (deterministic, separate instances) and writes `Documents/Atelier_Export/<Name>[_revNN]/`:

- `blueprint.json` (schema `atelier.blueprint/1.0`, `schema.json` alongside): graph, every parameter with unit / range / description, measurements, validation targets
- `blueprint.pdf`, `README.md`, `implementation/` (DSP requirements, validation tests)
- `plots/*.png`, `data/*.csv`: frequency response, THD vs level and frequency, step response, impulse response, stereo, noise, IMD
- `audio/*.wav`: reference input and processed renders

## Reference-plugin analysis (no internal VST3 host)

`source → [reference plugin] → Atelier main input`, with the unprocessed source sent to the Atelier sidechain.

In Reference mode:
- x = sidechain
- y = main input, taken *before* Atelier's chain

If the sidechain is not connected, Atelier only analyses the processed signal and says so. See `docs/DAW_ROUTING.md`.

**Decision:** no internal plugin hosting. DAW routing covers the need without crash-isolation and licensing risk. This will be revisited only if a needed feature cannot be done through routing, e.g. fully automated multi-level sweeps of a reference plugin.

## Dependencies and licensing

| Dependency | Version | Licence | Use |
|---|---|---|---|
| JUCE | 8.0.9 (CMake FetchContent) | AGPLv3 / commercial | Plugin framework, UI, audio formats |
| VST3 SDK | bundled with JUCE | MIT (since Steinberg's 2025 relicensing) | VST3 format |
| Inno Setup 6 | CI build tool only | free | Windows installer |
| pluginval | CI validation only | GPLv3 | Automated VST3 host tests |

This private project uses JUCE under AGPLv3. If Atelier is ever distributed commercially or closed-source, a JUCE commercial licence is needed.

## Build and CI

GitHub Actions on `windows-2022`:

1. Configure (VS 2022, x64).
2. Download the UI fonts (SIL OFL) into `plugin/Fonts/`.
3. Build `Atelier_VST3` and the three test programs, and run the tests.
4. Run **pluginval** at strictness 5.
5. Build the Inno Setup installer.
6. Upload the installer, portable ZIP and logs as artifacts.

Linux is used only for local compile checks of the same sources.
