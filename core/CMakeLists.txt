# sonic_core: JUCE-free DSP library (also intended for reuse inside VST3 builds)
add_library(sonic_core STATIC
    src/Module.cpp
    src/Biquad.cpp
    src/Oversampler.cpp
    src/SignalGenerator.cpp
    src/Analysis.cpp
    src/Chain.cpp
    src/TransferAnalyzer.cpp
    src/modules/TubeModule.cpp
    src/modules/EqModule.cpp
)
target_include_directories(sonic_core PUBLIC include)
target_compile_features(sonic_core PUBLIC cxx_std_17)
if(MSVC)
    target_compile_options(sonic_core PRIVATE /W4 /fp:precise)
else()
    target_compile_options(sonic_core PRIVATE -Wall -Wextra)
endif()
