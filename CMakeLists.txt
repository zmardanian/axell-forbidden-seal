cmake_minimum_required(VERSION 3.22)
project(AxellForbiddenSeal VERSION 4.0.0)
# the version for the GUI (every target: the plugin, tests and tools draw the same header)
add_compile_definitions(AXELL_VERSION_STRING="${PROJECT_VERSION}")

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

include(FetchContent)
FetchContent_Declare(JUCE
  GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
  GIT_TAG        8.0.15
  GIT_SHALLOW    ON)
FetchContent_MakeAvailable(JUCE)

# Axell core (shared DSP + look): next to this project, or vendored into external/axell-core for releases
set(AXELL_CORE_DIR "${CMAKE_CURRENT_LIST_DIR}/external/axell-core" CACHE PATH "Axell core")
if(NOT EXISTS "${AXELL_CORE_DIR}/CMakeLists.txt")
  set(AXELL_CORE_DIR "${CMAKE_CURRENT_LIST_DIR}/../axell-core" CACHE PATH "Axell core" FORCE)
endif()
add_subdirectory(${AXELL_CORE_DIR} axell-core)

juce_add_plugin(AxellForbiddenSeal
  COMPANY_NAME              "Axell Yobag"
  COMPANY_EMAIL             "axellpetre@protonmail.com"
  BUNDLE_ID                 com.axellyobag.axellforbiddenseal
  PLUGIN_MANUFACTURER_CODE  Axyb
  PLUGIN_CODE               Fsl1
  FORMATS                   VST3
  PRODUCT_NAME              "Axell Forbidden Seal"
  IS_SYNTH                  FALSE
  NEEDS_MIDI_INPUT          FALSE
  NEEDS_MIDI_OUTPUT         FALSE
  IS_MIDI_EFFECT            FALSE
  EDITOR_WANTS_KEYBOARD_FOCUS FALSE
  COPY_PLUGIN_AFTER_BUILD   FALSE
  VST3_CATEGORIES           Fx Distortion)

set(SEAL_SOURCES Source/PluginProcessor.cpp Source/PluginEditor.cpp)
set(SEAL_DEFS JUCE_WEB_BROWSER=0 AXELL_THEME_HUE=0 JUCE_USE_CURL=0 JUCE_VST3_CAN_REPLACE_VST2=0 JUCE_DISPLAY_SPLASH_SCREEN=0)
target_sources(AxellForbiddenSeal PRIVATE ${SEAL_SOURCES})
target_compile_definitions(AxellForbiddenSeal PUBLIC ${SEAL_DEFS})
axell_core_add_gui(AxellForbiddenSeal)
target_link_libraries(AxellForbiddenSeal PRIVATE juce::juce_audio_utils
  PUBLIC juce::juce_recommended_config_flags juce::juce_recommended_lto_flags juce::juce_recommended_warning_flags)

option(SEAL_SANITIZE "AddressSanitizer + UBSan for the test programs" OFF)
function(seal_sanitize target)
  if(SEAL_SANITIZE)
    target_compile_options(${target} PRIVATE -fsanitize=address,undefined -fno-omit-frame-pointer -fno-sanitize-recover=undefined)
    target_link_options(${target} PRIVATE -fsanitize=address,undefined)
  endif()
endfunction()

# DSP suite (JUCE-free: the engine headers exactly as they ship)
enable_testing()
add_executable(SealDspTests tests/DspTests.cpp)
target_include_directories(SealDspTests PRIVATE Source)
target_link_libraries(SealDspTests PRIVATE axell_core_dsp)
target_compile_options(SealDspTests PRIVATE -Wall -Wextra -Wshadow -Wno-mismatched-new-delete $<$<CONFIG:Release>:-O3>)
seal_sanitize(SealDspTests)
add_test(NAME dsp_suite COMMAND SealDspTests WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})

# plugin suite + GUI snapshots (JUCE)
foreach(tool SealPluginTests SealGuiSnapshot)
  juce_add_console_app(${tool} PRODUCT_NAME "${tool}")
  target_compile_definitions(${tool} PRIVATE ${SEAL_DEFS} JUCE_MODAL_LOOPS_PERMITTED=1 JucePlugin_Name="Axell Forbidden Seal")
  target_link_libraries(${tool} PRIVATE juce::juce_audio_utils juce::juce_recommended_config_flags)
  axell_core_add_gui(${tool})
  seal_sanitize(${tool})
endforeach()
target_sources(SealPluginTests PRIVATE tests/PluginTests.cpp ${SEAL_SOURCES})
target_sources(SealGuiSnapshot PRIVATE tests/GuiSnapshot.cpp ${SEAL_SOURCES})
add_test(NAME plugin_suite COMMAND SealPluginTests WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})

# independent black-box audit host (from the core)
juce_add_console_app(SealAuditHost PRODUCT_NAME "SealAuditHost")
target_sources(SealAuditHost PRIVATE ${AXELL_CORE_DIR}/tools/AuditHost.cpp)
target_compile_definitions(SealAuditHost PRIVATE JUCE_PLUGINHOST_VST3=1 JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_MODAL_LOOPS_PERMITTED=1)
target_link_libraries(SealAuditHost PRIVATE juce::juce_audio_utils juce::juce_audio_processors juce::juce_recommended_config_flags)

# research / benchmark tools (JUCE-free; the shipping engine headers)
find_package(Threads REQUIRED)
foreach(tool SealRender SealCalibrate)
  add_executable(${tool} tools/${tool}.cpp)
  target_include_directories(${tool} PRIVATE Source)
  target_link_libraries(${tool} PRIVATE axell_core_dsp Threads::Threads)
  target_compile_options(${tool} PRIVATE -O3)
endforeach()
