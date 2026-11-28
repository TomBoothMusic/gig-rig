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
# Neural Amp Modeler core (MIT licence) plays the amp models trained from the
# Amp capture tab. It needs Eigen (MPL2), pinned to the commit NAM itself uses.
# Local copies: -DFETCHCONTENT_SOURCE_DIR_NAMCORE=... -DFETCHCONTENT_SOURCE_DIR_EIGEN=...
# ---------------------------------------------------------------------------
FetchContent_Declare(
    namcore
    GIT_REPOSITORY https://github.com/sdatkinson/NeuralAmpModelerCore.git
    GIT_TAG        v0.3.0
    GIT_SHALLOW    TRUE
    GIT_SUBMODULES ""
    SOURCE_SUBDIR  no-cmake-here)    # only the sources are used: their own CMake projects are not run
FetchContent_Declare(
    eigen
    URL https://gitlab.com/libeigen/eigen/-/archive/87300c93cae6a8afd9a4f8aa8d9d5c5324cf02e1/eigen-87300c93cae6a8afd9a4f8aa8d9d5c5324cf02e1.tar.gz
    SOURCE_SUBDIR  no-cmake-here)
FetchContent_MakeAvailable(namcore eigen)

# An object library, not a static one: NAM registers its model types from static
# initialisers, which a linker would otherwise drop as unused.
add_library(namcore OBJECT
    ${namcore_SOURCE_DIR}/NAM/activations.cpp
    ${namcore_SOURCE_DIR}/NAM/convnet.cpp
    ${namcore_SOURCE_DIR}/NAM/dsp.cpp
    ${namcore_SOURCE_DIR}/NAM/get_dsp.cpp
    ${namcore_SOURCE_DIR}/NAM/lstm.cpp
    ${namcore_SOURCE_DIR}/NAM/util.cpp
    ${namcore_SOURCE_DIR}/NAM/wavenet.cpp)
target_include_directories(namcore SYSTEM PUBLIC
    ${namcore_SOURCE_DIR}
    ${namcore_SOURCE_DIR}/NAM
    ${namcore_SOURCE_DIR}/Dependencies/nlohmann
    ${eigen_SOURCE_DIR})
target_compile_definitions(namcore PUBLIC NAM_SAMPLE_FLOAT)
set_target_properties(namcore PROPERTIES POSITION_INDEPENDENT_CODE ON)
if(MSVC)
    target_compile_options(namcore PRIVATE /bigobj /W0)
else()
    target_compile_options(namcore PRIVATE -w)
endif()

# The amp models trained from Tom's captures, built into the app. On start the app
# copies any that are missing into Documents/GigRig/Amp models. The starter presets go to
# Documents/GigRig/Effect presets the same way, but only if no file of that name is there
# (so Tom's own changes to one are never overwritten).
juce_add_binary_data(GigRigData SOURCES
    resources/amp-models/dream65-breakup.nam
    resources/amp-models/dream65-clean.nam
    resources/amp-models/dream65-driven.nam
    resources/presets/starter-acoustic.txt
    resources/presets/starter-vocal.txt
    resources/presets/starter-bass.txt
    resources/presets/vocal-for-the-mixer.txt
    # The remote screen: the page a phone or tablet opens (docs/remote.md). RemoteServer hands these out.
    remote/index.html
    remote/app.css
    remote/core.js
    remote/demo.js
    remote/screens.js
    remote/eq-curve.js
    remote/builder.js
    remote/demo-data.js)

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
    src/AmpModels.cpp
    src/AudioEngine.cpp
    src/AudioPanel.cpp
    src/BreakMusic.cpp
    src/BreakMusicPanel.cpp
    src/BreakMusicSelfTest.cpp
    src/DeviceCheckPanel.cpp
    src/DeviceReport.cpp
    src/EffectsPanel.cpp
    src/EffectsRack.cpp
    src/Responses.cpp
    src/EffectsSelfTest.cpp
    src/ExpressionPedal.cpp
    src/Spectrum.cpp
    src/Tuner.cpp
    src/StrobeTuner.cpp
    src/TunerStrip.cpp
    src/TunerSelfTest.cpp
    src/ExpressionSelfTest.cpp
    src/GlitchWatch.cpp
    src/KickPad.cpp
    src/KickPadPanel.cpp
    src/KickPadSelfTest.cpp
    src/LatencyProbe.cpp
    src/Looper.cpp
    src/LooperLayout.cpp
    src/LooperPanel.cpp
    src/LooperSelfTest.cpp
    src/MidiMonitor.cpp
    src/Mixer.cpp
    src/MixerPanel.cpp
    src/MixerSelfTest.cpp
    src/MixRecorder.cpp
    src/RecordingPanel.cpp
    src/RecordingLibrary.cpp
    src/RecordingsPanel.cpp
    src/RecordingsSelfTest.cpp
    src/Soundcheck.cpp
    src/LoopReplay.cpp
    src/Pedal.cpp
    src/RemoteApi.cpp
    src/RemotePanel.cpp
    src/RemoteSelfTest.cpp
    src/RemoteServer.cpp
    src/PedalPanel.cpp
    src/SelfTest.cpp
    src/SongBook.cpp
    src/SongsPanel.cpp
    src/SongSelfTest.cpp
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
    src/effects/gate/NoiseGate.cpp
    src/effects/comp/Comp1176.cpp
    src/effects/eq/GraphicEq.cpp
    src/effects/analogdelay/AnalogDelay.cpp
    src/effects/digitaldelay/DigitalDelay.cpp
    src/effects/tapedelay/TapeDelay.cpp
    src/effects/spring/SpringReverb.cpp
    src/effects/tremolo/Tremolo.cpp
    src/effects/vibrato/Vibrato.cpp
    src/effects/chorus/Chorus.cpp
    src/effects/flanger/Flanger.cpp
    src/effects/univibe/UniVibe.cpp
    src/effects/plate/PlateReverb.cpp
    src/effects/room/RoomReverb.cpp
    src/effects/peq/ChannelEq.cpp
    src/effects/body/AcousticBody.cpp
    src/effects/ir/IrTools.cpp
    src/effects/ir/ResponsePlayer.cpp
    src/effects/ir/ResponseChecks.cpp
    src/effects/harmony/Harmony.cpp
    src/effects/harmony/HarmonyChecks.cpp
    src/effects/whammy/Whammy.cpp
    src/effects/wah/Wah.cpp
    src/effects/volume/VolumePedal.cpp
    src/effects/la2a/OptoComp.cpp
    src/effects/enhancer/SourceEnhancer.cpp
    src/effects/warmth/Warmth.cpp
    src/effects/tape/TapeMachine.cpp
    src/effects/buscomp/BusComp.cpp
    src/effects/mixer/MixerBlocks.cpp
    src/effects/mixer/MixerBlockChecks.cpp
    src/effects/amp/NamAmp.cpp
    src/effects/bass9/BassMachine.cpp
    src/effects/bass9/Bass9.cpp
    src/effects/bass9/Bass9Checks.cpp)

# What the app and the board's controller say to each other about the kick pad: one file,
# built into both programs (the controller's is firmware/bench-test).
target_include_directories(GigRig PRIVATE firmware/shared)

target_compile_definitions(GigRig PRIVATE
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_USE_MP3AUDIOFORMAT=1      # break music: Tom's mixes may be MP3 files
    JUCE_APPLICATION_NAME_STRING="$<TARGET_PROPERTY:GigRig,JUCE_PRODUCT_NAME>"
    JUCE_APPLICATION_VERSION_STRING="$<TARGET_PROPERTY:GigRig,JUCE_VERSION>"
    GIGRIG_BUILD_LABEL="${GIGRIG_BUILD_LABEL}")

if(WIN32)
    # ASIO is how the Allen & Heath CQ driver exposes all of its channels at
    # low latency. JUCE bundles the ASIO headers, so nothing else to download.
    target_compile_definitions(GigRig PRIVATE JUCE_ASIO=1)
endif()

if(MSVC)
    # Windows gives the main thread 1 MB of stack where Linux gives 8 MB. The self-test's long
    # check functions (each check builds its own engine) outgrew 1 MB on 28 Nov 2026 and the
    # Windows self-test stopped with a stack overflow, so Windows gets the same 8 MB.
    target_link_options(GigRig PRIVATE /STACK:8388608)
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
        namcore
        GigRigData
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)
