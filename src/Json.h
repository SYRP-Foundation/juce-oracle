// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

#include <juce_core/juce_core.h>

namespace oracle
{
/** Serialises a var as JSON with a fixed layout (two-space indent, "\n" line ends, a final newline,
    object keys in insertion order, shortest round-trip doubles). The same var gives the same bytes on
    every platform, which is what lets output hashes be compared across runners. */
juce::String toCanonicalJson (const juce::var& value);

/** Reads and parses a JSON file. */
juce::Result readJsonFile (const juce::File& file, juce::var& result);

/** Small helpers for building and reading JSON objects. */
juce::var makeObject();
void set (juce::var& object, const juce::String& key, const juce::var& value);
bool has (const juce::var& object, const juce::String& key);
} // namespace oracle
