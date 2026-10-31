# sonic_core: JUCE-free DSP library (reusable inside stand-alone VST3 builds)
add_library(sonic_core STATIC
    src/Module.cpp
    src/Biquad.cpp
    src/Oversampler.cpp
    src/SignalGenerator.cpp
    src/Analysis.cpp
    src/Chain.cpp
    src/Convolver.cpp
    src/Graph.cpp
    src/ModuleRegistry.cpp
    src/TransferAnalyzer.cpp
    src/Preview.cpp
    src/EqMatch.cpp
    src/modules/TubeModule.cpp
    src/modules/EqModule.cpp
    src/modules/SaturationModule.cpp
    src/modules/CompressorModule.cpp
    src/modules/ParametricEqModule.cpp
    src/modules/CharacterModules.cpp
    src/modules/EffectModules.cpp
)
target_include_directories(sonic_core PUBLIC include)
target_compile_features(sonic_core PUBLIC cxx_std_17)
find_package(Threads REQUIRED)
target_link_libraries(sonic_core PUBLIC Threads::Threads)
if(MSVC)
    target_compile_options(sonic_core PRIVATE /W4 /fp:precise)
else()
    target_compile_options(sonic_core PRIVATE -Wall -Wextra)
endif()
