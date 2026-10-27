// SPDX-License-Identifier: AGPL-3.0-only
#include "OutputSink.h"

#include "Hashing.h"
#include "Json.h"

#include <cstring>

namespace oracle
{
namespace
{
void putU16 (juce::MemoryOutputStream& out, uint32_t v)
{
    out.writeByte ((char) (v & 0xff));
    out.writeByte ((char) ((v >> 8) & 0xff));
}

void putU32 (juce::MemoryOutputStream& out, uint32_t v)
{
    putU16 (out, v & 0xffff);
    putU16 (out, v >> 16);
}

void putTag (juce::MemoryOutputStream& out, const char* tag)
{
    out.write (tag, 4);
}
} // namespace

OutputSink::OutputSink (juce::File outputRoot, juce::String scenarioDirectory)
    : root (std::move (outputRoot)), directory (std::move (scenarioDirectory))
{
}

bool OutputSink::writeBytes (const juce::String& name, const juce::MemoryBlock& bytes)
{
    const auto relative = directory + "/" + name;
    const auto target = root.getChildFile (relative);

    if (! target.getParentDirectory().createDirectory())
    {
        lastError = "cannot create " + target.getParentDirectory().getFullPathName();
        return false;
    }

    if (! target.replaceWithData (bytes.getData(), bytes.getSize()))
    {
        lastError = "cannot write " + target.getFullPathName();
        return false;
    }

    records.push_back ({ relative, sha256Hex (bytes), (juce::int64) bytes.getSize() });
    return true;
}

bool OutputSink::writeJson (const juce::String& name, const juce::var& value)
{
    const auto text = toCanonicalJson (value).toStdString();
    return writeBytes (name, juce::MemoryBlock (text.data(), text.size()));
}

bool OutputSink::writeWavFloat32 (const juce::String& name,
                                  const std::vector<std::vector<float>>& channels,
                                  int sampleRate)
{
    const auto numChannels = (uint32_t) channels.size();
    const auto numFrames = channels.empty() ? (uint32_t) 0 : (uint32_t) channels.front().size();

    for (const auto& c : channels)
    {
        if (c.size() != numFrames)
        {
            lastError = "WAV channels differ in length";
            return false;
        }
    }

    const uint32_t bytesPerSample = 4;
    const uint32_t blockAlign = numChannels * bytesPerSample;
    const uint32_t dataBytes = numFrames * blockAlign;

    juce::MemoryOutputStream out;
    putTag (out, "RIFF");
    putU32 (out, 4 + (8 + 18) + (8 + 4) + (8 + dataBytes));
    putTag (out, "WAVE");

    putTag (out, "fmt ");
    putU32 (out, 18);
    putU16 (out, 3); // IEEE float
    putU16 (out, numChannels);
    putU32 (out, (uint32_t) sampleRate);
    putU32 (out, (uint32_t) sampleRate * blockAlign);
    putU16 (out, blockAlign);
    putU16 (out, bytesPerSample * 8);
    putU16 (out, 0); // no extension

    putTag (out, "fact");
    putU32 (out, 4);
    putU32 (out, numFrames);

    putTag (out, "data");
    putU32 (out, dataBytes);

    for (uint32_t i = 0; i < numFrames; ++i)
    {
        for (const auto& c : channels)
        {
            uint32_t bits = 0;
            std::memcpy (&bits, &c[i], sizeof (bits));
            putU32 (out, bits);
        }
    }

    return writeBytes (name, out.getMemoryBlock());
}
} // namespace oracle
