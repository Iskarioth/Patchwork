cmake_minimum_required(VERSION 3.22)
project(Patchwork VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# JUCE: downloaded automatically, or point PATCHWORK_JUCE_PATH at a local checkout.
set(PATCHWORK_JUCE_PATH "" CACHE PATH "Local JUCE checkout (leave empty to download JUCE 9.0.5)")
if(PATCHWORK_JUCE_PATH)
    add_subdirectory(${PATCHWORK_JUCE_PATH} juce)
else()
    include(FetchContent)
    FetchContent_Declare(JUCE
        GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
        GIT_TAG 9.0.5
        GIT_SHALLOW ON)
    FetchContent_MakeAvailable(JUCE)
endif()

set(ENGINE_SOURCES
    Source/engine/Patch.cpp
    Source/engine/ModuleRegistry.cpp)

set(COMMON_DEFINITIONS
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_STRICT_REFCOUNTEDPOINTER=1)

# ---------------------------------------------------------------- app
juce_add_gui_app(Patchwork
    PRODUCT_NAME "Patchwork"
    COMPANY_NAME "Marcus")

target_sources(Patchwork PRIVATE
    Source/Main.cpp
    Source/MainComponent.cpp
    Source/engine/AudioEngine.cpp
    Source/ui/NodeComponent.cpp
    Source/ui/PatchCanvas.cpp
    Source/ui/PianoRollEditor.cpp
    Source/ui/DrumGridEditor.cpp
    ${ENGINE_SOURCES})

target_compile_definitions(Patchwork PRIVATE
    ${COMMON_DEFINITIONS}
    JUCE_APPLICATION_NAME_STRING="$<TARGET_PROPERTY:Patchwork,JUCE_PRODUCT_NAME>"
    JUCE_APPLICATION_VERSION_STRING="$<TARGET_PROPERTY:Patchwork,JUCE_VERSION>")

target_link_libraries(Patchwork
    PRIVATE
        juce::juce_audio_utils
        juce::juce_audio_processors
        juce::juce_dsp
        juce::juce_gui_extra
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)

# ---------------------------------------------------------------- engine tests
option(PATCHWORK_BUILD_TESTS "Build the offline engine tests" ON)
if(PATCHWORK_BUILD_TESTS)
    juce_add_console_app(PatchworkTests PRODUCT_NAME "PatchworkTests")
    target_sources(PatchworkTests PRIVATE Tests/EngineTests.cpp ${ENGINE_SOURCES})
    target_compile_definitions(PatchworkTests PRIVATE ${COMMON_DEFINITIONS})
    target_link_libraries(PatchworkTests
        PRIVATE
            juce::juce_audio_processors
            juce::juce_audio_formats
            juce::juce_dsp
        PUBLIC
            juce::juce_recommended_config_flags
            juce::juce_recommended_warning_flags)
endif()
