# juce-oracle: runs SYRP parity scenarios against real JUCE 9.0.4.
# SPDX-License-Identifier: AGPL-3.0-only
cmake_minimum_required(VERSION 3.22)

# Floating-point std::to_chars needs macOS 13.3 or later.
if(APPLE AND NOT CMAKE_OSX_DEPLOYMENT_TARGET)
    set(CMAKE_OSX_DEPLOYMENT_TARGET "13.3" CACHE STRING "Minimum macOS version")
endif()

project(juce_oracle VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

set(JUCE_ORACLE_SCENARIOS_DIR "" CACHE PATH
    "Optional: a SYRP parity/scenarios directory; enables the tests that run the real scenario set")

include(cmake/FetchJUCE.cmake)

juce_add_console_app(juce-oracle
    PRODUCT_NAME "juce-oracle"
    VERSION ${PROJECT_VERSION})

target_sources(juce-oracle PRIVATE
    src/Main.cpp
    src/Hashing.cpp
    src/Json.cpp
    src/OutputSink.cpp
    src/Runner.cpp
    src/ScenarioKind.cpp
    src/kinds/SelfTestKind.cpp
    src/kinds/ValueTreeListenerKind.cpp)

target_include_directories(juce-oracle PRIVATE src)

target_compile_definitions(juce-oracle PRIVATE
    JUCE_ORACLE_VERSION="${PROJECT_VERSION}"
    JUCE_ORACLE_JUCE_TAG="${JUCE_ORACLE_JUCE_TAG}"
    JUCE_ORACLE_JUCE_COMMIT="${JUCE_ORACLE_JUCE_COMMIT}"
    # Headless console tool: no network stack, no web views, no splash or GUI.
    JUCE_USE_CURL=0
    JUCE_WEB_BROWSER=0
    JUCE_LOAD_CURL_SYMBOLS_LAZILY=0
    JUCE_STANDALONE_APPLICATION=1
    JUCE_DISPLAY_SPLASH_SCREEN=0
    JUCE_MODAL_LOOPS_PERMITTED=0)

target_link_libraries(juce-oracle
    PRIVATE
        juce::juce_core
        juce::juce_events
        juce::juce_data_structures
        juce::juce_cryptography
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags)

install(TARGETS juce-oracle RUNTIME DESTINATION bin)

enable_testing()
include(cmake/Tests.cmake)
