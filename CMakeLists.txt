cmake_minimum_required(VERSION 3.22)

project(GigRig VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Link the Microsoft runtime into the .exe so it runs on a laptop with no
# Visual C++ redistributable installed (download, double-click, done).
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

# Shown in the app's title bar and at the top of every report, so a pasted
# report says exactly which build produced it. CI passes the run number.
set(GIGRIG_BUILD_LABEL "local" CACHE STRING "Build label shown in the app")

# ---------------------------------------------------------------------------
# JUCE (audio devices, MIDI, GUI). Pinned to a release tag.
# To use a copy already on disk: -DFETCHCONTENT_SOURCE_DIR_JUCE=/path/to/JUCE
# ---------------------------------------------------------------------------
include(FetchContent)
FetchContent_Declare(
    JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG        9.0.5
    GIT_SHALLOW    TRUE)
FetchContent_MakeAvailable(JUCE)

# ---------------------------------------------------------------------------
# The app. Stage 0 was a device check; stage 1 adds the audio engine with
# play-through and a round-trip latency meter. Later stages keep growing it.
# ---------------------------------------------------------------------------
juce_add_gui_app(GigRig
    PRODUCT_NAME "GigRig"
    COMPANY_NAME "Gig Rig"
    BUNDLE_ID    "au.gigrig.app")

target_sources(GigRig PRIVATE
    src/Main.cpp
    src/MainComponent.cpp
    src/AudioEngine.cpp
    src/AudioPanel.cpp
    src/DeviceCheckPanel.cpp
    src/DeviceReport.cpp
    src/EffectsPanel.cpp
    src/EffectsRack.cpp
    src/EffectsSelfTest.cpp
    src/GlitchWatch.cpp
    src/LatencyProbe.cpp
    src/Looper.cpp
    src/LooperPanel.cpp
    src/LooperSelfTest.cpp
    src/MidiMonitor.cpp
    src/Pedal.cpp
    src/PedalPanel.cpp
    src/SelfTest.cpp
    src/Style.cpp
    src/TestSongs.cpp
    # The effects: one folder each (see docs/effects.md). A new effect adds its .cpp files here.
    src/effects/Effect.cpp
    src/effects/Chain.cpp
    src/effects/Registry.cpp
    src/effects/EffectChecks.cpp
    src/effects/tubescreamer/TubeScreamer.cpp
    src/effects/bluesdriver/BluesDriver.cpp
    src/effects/klon/Klon.cpp
    src/effects/marshall/MarshallBox.cpp
    src/effects/fuzz/Fuzz.cpp
    src/effects/boost/CleanBoost.cpp
    src/effects/bass9/BassMachine.cpp
    src/effects/bass9/Bass9.cpp
    src/effects/bass9/Bass9Checks.cpp)

target_compile_definitions(GigRig PRIVATE
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_APPLICATION_NAME_STRING="$<TARGET_PROPERTY:GigRig,JUCE_PRODUCT_NAME>"
    JUCE_APPLICATION_VERSION_STRING="$<TARGET_PROPERTY:GigRig,JUCE_VERSION>"
    GIGRIG_BUILD_LABEL="${GIGRIG_BUILD_LABEL}")

if(WIN32)
    # ASIO is how the Allen & Heath CQ driver exposes all of its channels at
    # low latency. JUCE bundles the ASIO headers, so nothing else to download.
    target_compile_definitions(GigRig PRIVATE JUCE_ASIO=1)
endif()

if(UNIX AND NOT APPLE)
    option(GIGRIG_ENABLE_JACK "List JACK / PipeWire-JACK devices on Linux" ON)
    if(GIGRIG_ENABLE_JACK)
        target_compile_definitions(GigRig PRIVATE JUCE_JACK=1)
    endif()
endif()

target_link_libraries(GigRig
    PRIVATE
        juce::juce_audio_devices
        juce::juce_audio_formats
        juce::juce_gui_basics
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)
