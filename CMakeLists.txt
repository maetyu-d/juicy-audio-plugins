cmake_minimum_required(VERSION 3.22)
project(JuicySuite VERSION 0.1.0)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if (EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/JUCE/CMakeLists.txt")
    add_subdirectory(JUCE)
else()
    find_package(JUCE CONFIG REQUIRED)
endif()

set(JUICY_SHARED_SOURCES
    src/shared/JuicinessAnalyzer.cpp
    src/shared/JuicinessAnalyzer.h
    src/shared/JuicyMeterPanel.cpp
    src/shared/JuicyMeterPanel.h
    src/shared/JuicyPluginEditor.cpp
    src/shared/JuicyPluginEditor.h
)

function(add_juicy_plugin target name code)
    juce_add_plugin(${target}
        COMPANY_NAME "JuicyLabs"
        BUNDLE_ID "com.juicylabs.${target}"
        IS_SYNTH FALSE
        NEEDS_MIDI_INPUT FALSE
        NEEDS_MIDI_OUTPUT FALSE
        IS_MIDI_EFFECT FALSE
        EDITOR_WANTS_KEYBOARD_FOCUS FALSE
        COPY_PLUGIN_AFTER_BUILD FALSE
        PLUGIN_MANUFACTURER_CODE JuLc
        PLUGIN_CODE ${code}
        FORMATS AU VST3 Standalone
        PRODUCT_NAME "${name}"
    )

    target_sources(${target}
        PRIVATE
            ${JUICY_SHARED_SOURCES}
            src/plugins/${target}/PluginProcessor.cpp
            src/plugins/${target}/PluginProcessor.h
    )

    target_compile_definitions(${target}
        PRIVATE
            JUCE_WEB_BROWSER=0
            JUCE_USE_CURL=0
            JUCE_FORCE_USE_LEGACY_PARAM_IDS=1
    )

    target_link_libraries(${target}
        PRIVATE
            juce::juce_audio_utils
            juce::juce_dsp
        PUBLIC
            juce::juce_recommended_config_flags
            juce::juce_recommended_lto_flags
            juce::juce_recommended_warning_flags
    )
endfunction()

add_juicy_plugin(JuicyInfer "Juicy Infer" JINF)
add_juicy_plugin(JuicyPunch "Juicy Punch" JPCH)
add_juicy_plugin(JuicySaturator "Juicy Saturator" JSAT)
add_juicy_plugin(JuicyWidth "Juicy Width" JWID)
