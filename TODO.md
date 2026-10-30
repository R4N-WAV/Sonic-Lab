# TODO

## v0.1 (current)
- [x] Core DSP: generator, oversampler, tube, EQ, chain, analysis, transfer estimator
- [x] 37 numeric core DSP checks + 27 headless plugin-processor checks (buses, sidechain, A/B alignment, latency, state, analysis worker)
- [x] VST3 processor: buses (main + sidechain), APVTS from ParamSpecs, state save/restore, latency reporting
- [x] Sources: DAW input / generator / audio file
- [x] Chain UI: reorder, enable, time-aligned A/B, trims, meters (in / out / sidechain)
- [x] Module editor with DSP-derived graphs (tube transfer curve, EQ magnitude + phase)
- [x] Analysis worker thread: spectrum, harmonics/THD/THD+N, transfer function + coherence + delay
- [x] Inno Setup installer, pluginval, CI artifacts
- [ ] First DAW test by R4N (FL Studio, Cubase): load, automation, save/reload, sidechain routing

## Known limitations (v0.1)
- Chain modules are fixed (Tube, EQ). Add/remove comes in v0.2.
- A/B is time-aligned but not loudness-matched yet.
- Analysis uses the left channel only.
- The generator replaces the DAW input while Source = Generator, even when the generator is stopped (silence).
- At 44.1 kHz the oversampler's anti-alias filter starts rolling off around 18 kHz (inside the tube stage only).
- DAW routing steps are written from each DAW's standard workflow and are not yet confirmed with ATELIER.

## Backlog
See DEVELOPMENT_PLAN.md (v0.2 onwards).
