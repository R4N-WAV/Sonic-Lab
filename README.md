# Atelier

A private Windows x64 VST3 for sound design, measurement and DSP development. You build a sound from blocks on a node graph (the Bridge) and export a **Blueprint**: a measured, documented package that Claude can turn into a new, stand-alone plugin.

## Install

1. Open the latest successful run under **Actions → Build Atelier**.
2. Download the artifact `Atelier_Setup_<version>`.
3. Unzip it and run the installer. It replaces any older version (including v0.1 "ATELIER").
4. Rescan plugins in your DAW. See `installer/after_install.txt`.

## Docs

- [ARCHITECTURE.md](ARCHITECTURE.md): how it is built and why
- [DEVELOPMENT_PLAN.md](DEVELOPMENT_PLAN.md): roadmap
- [TODO.md](TODO.md): current status and known limitations
- [docs/DAW_ROUTING.md](docs/DAW_ROUTING.md): analysing another plugin through sidechain routing

## Build locally (Windows)

```
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release --target Atelier_VST3 sonic_core_tests atelier_module_tests atelier_plugin_tests
build\Release\sonic_core_tests.exe
build\Release\atelier_module_tests.exe
build\plugin\Release\atelier_plugin_tests.exe
```

UI fonts (SIL Open Font License) are downloaded by CI into `plugin/Fonts/`. Without them the plugin uses system fonts.
