# Atelier: Development plan

Work proceeds in small rounds. Each round ends with a CI build you can install and test in a DAW.

## History

- **v0.0** (SONIC LAB, standalone): DSP core started.
- **v0.1** VST3 foundation: buses + sidechain, state, latency, generator / file sources, Tube → EQ chain, spectrum / harmonics / transfer function, installer, pluginval.
- **v0.2 (this round): rounds 1 + 2 of the feedback.**
  - Bridge node graph with parallel paths and per-block mix
  - 17 blocks incl. 5 compressor topologies, harmonic-recipe saturation, tape / transformer / console, FX
  - Pro-Q-style EQ (24 bands, dynamic, M/S, linear phase, EQ Match, selectable dB range)
  - New dark UI, loudness-matched A/B, undo / redo
  - Blueprint export package for Claude

## Next rounds

| Round | Content |
|---|---|
| v0.3 | Fixes from R4N's DAW test. Fine-tuning of block sound. Host automation for block parameters. Presets. |
| v0.4 | Blueprint depth: per-block measurements at several settings, aliasing and latency reports, richer PDF. |
| v0.5 | FL Studio reference-analysis guide with screenshots; reference-plugin measurement sweeps through sidechain routing. |
| v0.6 | Validation workflow: load a Claude-built plugin as the reference and score it against a saved blueprint. |
| v1.0 | Hardening, performance, full DAW validation, documentation. |

## Testing policy

- Every DSP feature gets a numeric test in `tests/` (it measures the output, not just "it runs").
- pluginval at strictness 5 runs on every CI build.
- DAW compatibility is claimed only after testing in the DAW itself (FL Studio, Cubase, Reaper).
