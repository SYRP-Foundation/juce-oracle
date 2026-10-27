# Fetches JUCE at build time, pinned to the 9.0.4 release commit. JUCE is never vendored.
# To reuse a local clone, pass -DFETCHCONTENT_SOURCE_DIR_JUCE=/path/to/JUCE (the commit is still checked).
# SPDX-License-Identifier: AGPL-3.0-only
include(FetchContent)

set(JUCE_ORACLE_JUCE_TAG "9.0.4")
set(JUCE_ORACLE_JUCE_COMMIT "3fb5cbc0340758596dac39f7c8de47d562b4896e")

FetchContent_Declare(JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG        ${JUCE_ORACLE_JUCE_TAG}
    GIT_SHALLOW    TRUE
    GIT_PROGRESS   TRUE)

FetchContent_MakeAvailable(JUCE)

# A shallow tag clone is fast, but a tag can move: refuse anything but the pinned commit.
find_package(Git REQUIRED)
execute_process(
    COMMAND "${GIT_EXECUTABLE}" rev-parse HEAD
    WORKING_DIRECTORY "${juce_SOURCE_DIR}"
    OUTPUT_VARIABLE _juce_head
    OUTPUT_STRIP_TRAILING_WHITESPACE
    RESULT_VARIABLE _juce_rev_result)
if(NOT _juce_rev_result EQUAL 0 OR NOT _juce_head STREQUAL JUCE_ORACLE_JUCE_COMMIT)
    message(FATAL_ERROR
        "JUCE at ${juce_SOURCE_DIR} is at '${_juce_head}', expected ${JUCE_ORACLE_JUCE_COMMIT} (tag ${JUCE_ORACLE_JUCE_TAG}).")
endif()
message(STATUS "juce-oracle: JUCE ${JUCE_ORACLE_JUCE_TAG} at ${_juce_head}")
