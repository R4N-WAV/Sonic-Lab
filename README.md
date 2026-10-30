# ATELIER by R4N

*Understand sound. Shape character.*

ATELIER is a private Windows x64 VST3 for sound design, analog modelling, measurement and DSP development. It also produces engineering blueprints that Claude can turn into new plugins.

## Install

1. Open the latest successful run under **Actions → Build ATELIER**.
2. Download the artifact `ATELIER_by_R4N_Setup_<version>`.
3. Unzip it and run the installer.
4. Rescan plugins in your DAW. See `installer/after_install.txt`.

## Docs

- [ARCHITECTURE.md](ARCHITECTURE.md): how it is built and why
- [DEVELOPMENT_PLAN.md](DEVELOPMENT_PLAN.md): roadmap
- [TODO.md](TODO.md): current status and known limitations
- [docs/DAW_ROUTING.md](docs/DAW_ROUTING.md): analysing another plugin through sidechain routing

## Build locally (Windows)

```
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release --target Atelier_VST3 sonic_core_tests
build\Release\sonic_core_tests.exe
```
