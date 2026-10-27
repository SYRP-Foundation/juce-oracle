// SPDX-License-Identifier: AGPL-3.0-only
#include "Hashing.h"

#include <juce_cryptography/juce_cryptography.h>

#include <algorithm>

namespace oracle
{
juce::String sha256Hex (const void* data, size_t size)
{
    return juce::SHA256 (data, size).toHexString().toLowerCase();
}

juce::String sha256Hex (const juce::MemoryBlock& block)
{
    return sha256Hex (block.getData(), block.getSize());
}

juce::String scenarioFileHash (const juce::File& file)
{
    juce::MemoryBlock raw;
    file.loadFileAsData (raw);

    const auto* in = static_cast<const char*> (raw.getData());
    const auto size = raw.getSize();

    juce::MemoryBlock normalised;
    normalised.ensureSize (size);
    size_t written = 0;

    for (size_t i = 0; i < size; ++i)
    {
        if (in[i] == '\r' && i + 1 < size && in[i + 1] == '\n')
            continue;

        normalised[written++] = in[i];
    }

    return sha256Hex (normalised.getData(), written);
}

std::vector<ScenarioFile> collectScenarioFiles (const juce::File& root)
{
    std::vector<ScenarioFile> files;

    for (const auto& f : root.findChildFiles (juce::File::findFiles, true, "*.json"))
    {
        if (f.getFileName().endsWithIgnoreCase (".schema.json"))
            continue;

        ScenarioFile sf;
        sf.file = f;
        sf.relativePath = f.getRelativePathFrom (root).replaceCharacter ('\\', '/');
        sf.sha256 = scenarioFileHash (f);
        files.push_back (std::move (sf));
    }

    std::sort (files.begin(), files.end(), [] (const ScenarioFile& a, const ScenarioFile& b)
    {
        return a.relativePath.toStdString() < b.relativePath.toStdString();
    });

    return files;
}

juce::String scenarioSetHash (const std::vector<ScenarioFile>& files)
{
    std::string text;
    for (const auto& f : files)
        text += (f.sha256 + "  " + f.relativePath + "\n").toStdString();

    return sha256Hex (text.data(), text.size());
}
} // namespace oracle
