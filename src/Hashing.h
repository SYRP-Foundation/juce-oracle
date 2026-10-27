// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

#include <juce_core/juce_core.h>

namespace oracle
{
/** Lower-case hex SHA-256 of raw bytes. */
juce::String sha256Hex (const void* data, size_t size);
juce::String sha256Hex (const juce::MemoryBlock& block);

/** SHA-256 of a scenario file's bytes after turning every CR LF into LF, so a Windows checkout
    with autocrlf hashes the same as a Linux one. */
juce::String scenarioFileHash (const juce::File& file);

/** One scenario file in a scenario set. */
struct ScenarioFile
{
    juce::File file;
    juce::String relativePath; // '/'-separated, relative to the set's root
    juce::String sha256;       // scenarioFileHash
};

/** Collects the scenario files under root: every *.json file at any depth, except *.schema.json,
    sorted by relativePath in byte order. */
std::vector<ScenarioFile> collectScenarioFiles (const juce::File& root);

/** The scenario-set hash: SHA-256 of the UTF-8 text made of one line per scenario file, in the
    sorted order, each line being "<sha256>  <relativePath>\n". */
juce::String scenarioSetHash (const std::vector<ScenarioFile>& files);
} // namespace oracle
