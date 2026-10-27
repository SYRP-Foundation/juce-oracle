// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

#include <juce_core/juce_core.h>

#include <vector>

namespace oracle
{
/** One file a scenario produced, as listed in the manifest. */
struct OutputRecord
{
    juce::String path;   // '/'-separated, relative to the run's output directory
    juce::String sha256;
    juce::int64 bytes = 0;
};

/** Where a scenario kind writes its outputs. Every file goes under one directory per scenario
    (<out>/scenarios/<scenario id>/) and is hashed as it is written. Kinds must produce the same
    bytes on every platform for the same scenario: no timestamps, paths or platform names. */
class OutputSink
{
public:
    OutputSink (juce::File outputRoot, juce::String scenarioDirectory);

    /** Writes a JSON document (see toCanonicalJson). */
    bool writeJson (const juce::String& name, const juce::var& value);

    /** Writes a 32-bit float WAV file (WAVE_FORMAT_IEEE_FLOAT), channels interleaved.
        channels[c][i] is sample i of channel c; all channels must have the same length. */
    bool writeWavFloat32 (const juce::String& name,
                          const std::vector<std::vector<float>>& channels,
                          int sampleRate);

    /** Writes raw bytes, for formats (such as PNG) that a kind encodes itself. */
    bool writeBytes (const juce::String& name, const juce::MemoryBlock& bytes);

    const std::vector<OutputRecord>& getRecords() const noexcept { return records; }
    const juce::String& getLastError() const noexcept { return lastError; }

private:
    juce::File root;
    juce::String directory;
    std::vector<OutputRecord> records;
    juce::String lastError;
};
} // namespace oracle
