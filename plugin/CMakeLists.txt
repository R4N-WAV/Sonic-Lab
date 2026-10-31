# ATELIER by R4N - VST3 plugin target (JUCE)

include(FetchContent)
# JUCE 8 is dual-licensed (AGPLv3 / commercial). This private project uses it under AGPLv3.
# To build offline, pass -DFETCHCONTENT_SOURCE_DIR_JUCE=/path/to/JUCE
FetchContent_Declare(JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG 8.0.9
    GIT_SHALLOW TRUE)
FetchContent_MakeAvailable(JUCE)

juce_add_plugin(Atelier
    PRODUCT_NAME "Atelier"
    COMPANY_NAME "R4N"
    COMPANY_WEBSITE ""
    BUNDLE_ID "com.r4n.atelier"
    PLUGIN_MANUFACTURER_CODE R4Nn
    PLUGIN_CODE Atlr
    FORMATS VST3
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    VST3_CATEGORIES "Fx" "Analyzer"
    COPY_PLUGIN_AFTER_BUILD FALSE
    DESCRIPTION "Atelier by R4N - Understand sound. Shape character.")

juce_generate_juce_header(Atelier)

target_sources(Atelier PRIVATE
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp
    Source/AnalysisWorker.cpp
    Source/MainPanels.cpp
    Source/Bridge.cpp
    Source/Inspector.cpp
    Source/BlockEditors.cpp
    Source/Measure.cpp
    Source/Blueprint.cpp
    Source/Templates.cpp)

# Fonts (SIL Open Font License) are downloaded by CI into plugin/Fonts; without
# them the UI falls back to system fonts.
file(GLOB ATELIER_FONTS "${CMAKE_CURRENT_SOURCE_DIR}/Fonts/*.ttf")
list(LENGTH ATELIER_FONTS ATELIER_FONT_COUNT)
if(ATELIER_FONT_COUNT EQUAL 8)
    juce_add_binary_data(AtelierFonts SOURCES ${ATELIER_FONTS})
    set_target_properties(AtelierFonts PROPERTIES POSITION_INDEPENDENT_CODE ON)
    target_link_libraries(Atelier PUBLIC AtelierFonts)
    message(STATUS "Atelier: embedding ${ATELIER_FONT_COUNT} fonts")
else()
    message(STATUS "Atelier: fonts not found (${ATELIER_FONT_COUNT}/8) - using system fonts")
endif()

target_compile_definitions(Atelier PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0
    JUCE_REPORT_APP_USAGE=0
    JUCE_USE_FLAC=1
    JUCE_USE_OGGVORBIS=1)

if(UNIX AND NOT APPLE)
    # Linux is only used for compile checks in CI/dev; keep system deps minimal.
    target_compile_definitions(Atelier PUBLIC
        JUCE_USE_XRANDR=0 JUCE_USE_XINERAMA=0 JUCE_USE_XCURSOR=0 JUCE_USE_XSHM=0
        JUCE_ALSA=0 JUCE_JACK=0)
endif()

set_target_properties(sonic_core PROPERTIES POSITION_INDEPENDENT_CODE ON)

target_link_libraries(Atelier
    PRIVATE
        sonic_core
        juce::juce_audio_utils
        juce::juce_audio_formats
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)

# Headless processor test: links the plugin's shared code (no DAW, no editor).
if(SONIC_BUILD_TESTS)
    add_executable(atelier_plugin_tests ${CMAKE_SOURCE_DIR}/tests/PluginSmokeTest.cpp)
    target_include_directories(atelier_plugin_tests PRIVATE $<TARGET_PROPERTY:Atelier,INCLUDE_DIRECTORIES>)
    target_compile_definitions(atelier_plugin_tests PRIVATE $<TARGET_PROPERTY:Atelier,COMPILE_DEFINITIONS>)
    target_compile_features(atelier_plugin_tests PRIVATE cxx_std_17)
    target_link_libraries(atelier_plugin_tests PRIVATE Atelier)
    add_test(NAME plugin_smoke COMMAND atelier_plugin_tests)

    # Development helper: renders the editor to PNGs (not run in CI)
    add_executable(atelier_snapshot EXCLUDE_FROM_ALL ${CMAKE_SOURCE_DIR}/tests/Snapshot.cpp)
    target_include_directories(atelier_snapshot PRIVATE $<TARGET_PROPERTY:Atelier,INCLUDE_DIRECTORIES>)
    target_compile_definitions(atelier_snapshot PRIVATE $<TARGET_PROPERTY:Atelier,COMPILE_DEFINITIONS>)
    target_compile_features(atelier_snapshot PRIVATE cxx_std_17)
    target_link_libraries(atelier_snapshot PRIVATE Atelier)
endif()
