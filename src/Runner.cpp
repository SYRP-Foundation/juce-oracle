// SPDX-License-Identifier: AGPL-3.0-only
#include "Runner.h"

#include "Json.h"
#include "ScenarioKind.h"

#include <algorithm>
#include <iostream>
#include <map>
#include <set>

namespace oracle
{
namespace
{
bool isSafeScenarioId (const juce::String& id)
{
    if (id.isEmpty() || id.startsWith ("/") || id.endsWith ("/"))
        return false;

    if (! id.containsOnly ("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_-./"))
        return false;

    for (const auto& segment : juce::StringArray::fromTokens (id, "/", ""))
        if (segment.isEmpty() || segment == "." || segment == "..")
            return false;

    return true;
}

juce::String compilerName()
{
#if defined(__clang__)
    return "clang " __clang_version__;
#elif defined(__GNUC__)
    return "gcc " __VERSION__;
#elif defined(_MSC_VER)
    return "msvc " + juce::String (_MSC_FULL_VER);
#else
    return "unknown";
#endif
}

juce::String archName()
{
#if defined(__aarch64__) || defined(_M_ARM64)
    return "arm64";
#elif defined(__x86_64__) || defined(_M_X64)
    return "x64";
#else
    return "unknown";
#endif
}

juce::String osName()
{
#if JUCE_LINUX
    return "linux";
#elif JUCE_MAC
    return "macos";
#elif JUCE_WINDOWS
    return "windows";
#else
    return "other";
#endif
}

bool prepareOutputDirectory (const RunOptions& options, juce::String& error)
{
    const auto& out = options.outputDirectory;

    if (out.exists() && ! out.isDirectory())
    {
        error = out.getFullPathName() + " exists and is not a directory";
        return false;
    }

    if (out.isDirectory() && out.getNumberOfChildFiles (juce::File::findFilesAndDirectories) > 0)
    {
        // Only ever delete a directory that is recognisably a previous oracle run.
        if (! options.replaceOutput || ! out.getChildFile ("manifest.json").existsAsFile())
        {
            error = out.getFullPathName() + " is not empty (pass --replace to overwrite a previous oracle output)";
            return false;
        }

        if (! out.deleteRecursively())
        {
            error = "cannot remove " + out.getFullPathName();
            return false;
        }
    }

    if (! out.createDirectory())
    {
        error = "cannot create " + out.getFullPathName();
        return false;
    }

    return true;
}
} // namespace

juce::var describeBuild()
{
    auto juce = makeObject();
    set (juce, "tag", JUCE_ORACLE_JUCE_TAG);
    set (juce, "commit", JUCE_ORACLE_JUCE_COMMIT);
    set (juce, "version", juce::SystemStats::getJUCEVersion());

    auto build = makeObject();
    set (build, "oracle_version", JUCE_ORACLE_VERSION);
    set (build, "juce", juce);
    return build;
}

int runScenarioSet (const std::vector<ScenarioFile>& files, const RunOptions& options)
{
    juce::String error;
    if (! prepareOutputDirectory (options, error))
    {
        std::cerr << "juce-oracle: " << error << std::endl;
        return exitEnvironment;
    }

    const auto kinds = makeAllKinds();
    const auto& out = options.outputDirectory;

    juce::Array<juce::var> entries;
    std::vector<OutputRecord> allOutputs;
    std::set<juce::String> seenIds;
    std::map<juce::String, int> counts { { "pass", 0 }, { "fail", 0 }, { "error", 0 }, { "unsupported", 0 } };

    for (const auto& sf : files)
    {
        auto entry = makeObject();
        set (entry, "file", sf.relativePath);
        set (entry, "sha256", sf.sha256);

        Outcome outcome;
        std::vector<OutputRecord> outputs;

        juce::var scenario;
        const auto parsed = readJsonFile (sf.file, scenario);
        const auto id = scenario["id"].toString();
        set (entry, "id", id);

        if (parsed.failed())
            outcome = Outcome::error (parsed.getErrorMessage());
        else if (! isSafeScenarioId (id))
            outcome = Outcome::error ("missing or unsafe 'id' (letters, digits, _ - . and / only)");
        else if (! seenIds.insert (id).second)
            outcome = Outcome::error ("duplicate scenario id");
        else if (auto* kind = findKind (kinds, id, scenario["kind"].toString()))
        {
            set (entry, "kind", kind->getPrefix());
            OutputSink sink (out, "scenarios/" + id);
            outcome = kind->run (scenario, sink);
            outputs = sink.getRecords();
        }
        else
        {
            outcome = { Outcome::Status::unsupported, "no scenario kind handles this id" };
        }

        const auto status = toString (outcome.status);
        ++counts[status];
        set (entry, "status", status);
        if (outcome.message.isNotEmpty())
            set (entry, "message", outcome.message);

        juce::Array<juce::var> outputList;
        for (const auto& r : outputs)
        {
            auto o = makeObject();
            set (o, "path", r.path);
            set (o, "sha256", r.sha256);
            set (o, "bytes", r.bytes);
            outputList.add (o);
            allOutputs.push_back (r);
        }
        set (entry, "outputs", outputList);
        entries.add (entry);

        std::cerr << status.paddedRight (' ', 12) << (id.isNotEmpty() ? id : sf.relativePath)
                  << (outcome.message.isNotEmpty() ? "  (" + outcome.message + ")" : juce::String()) << std::endl;
    }

    auto set_ = makeObject();
    set (set_, "hash", scenarioSetHash (files));
    set (set_, "count", (int) files.size());

    auto summary = makeObject();
    for (const auto& [k, v] : counts)
        set (summary, k, v);

    auto oracleInfo = makeObject();
    set (oracleInfo, "version", JUCE_ORACLE_VERSION);
    set (oracleInfo, "commit", options.oracleCommit);

    auto juceInfo = makeObject();
    set (juceInfo, "tag", JUCE_ORACLE_JUCE_TAG);
    set (juceInfo, "commit", JUCE_ORACLE_JUCE_COMMIT);
    set (juceInfo, "version", juce::SystemStats::getJUCEVersion());

    auto platform = makeObject();
    set (platform, "label", options.platformLabel.isNotEmpty() ? options.platformLabel : osName() + "-" + archName());
    set (platform, "os", osName());
    set (platform, "arch", archName());
    set (platform, "compiler", compilerName());

    auto manifest = makeObject();
    set (manifest, "format", "juce-oracle-manifest/1");
    set (manifest, "juce", juceInfo);
    set (manifest, "oracle", oracleInfo);
    set (manifest, "platform", platform);
    set (manifest, "scenario_set", set_);
    set (manifest, "summary", summary);
    set (manifest, "scenarios", entries);

    OutputSink rootSink (out.getParentDirectory(), out.getFileName());
    if (! rootSink.writeJson ("manifest.json", manifest))
    {
        std::cerr << "juce-oracle: " << rootSink.getLastError() << std::endl;
        return exitEnvironment;
    }

    // SHA256SUMS, in the format `sha256sum -c` reads, paths relative to the output directory.
    allOutputs.push_back ({ "manifest.json", rootSink.getRecords().front().sha256, rootSink.getRecords().front().bytes });
    std::sort (allOutputs.begin(), allOutputs.end(), [] (const OutputRecord& a, const OutputRecord& b)
    {
        return a.path.toStdString() < b.path.toStdString();
    });

    std::string sums;
    for (const auto& r : allOutputs)
        sums += (r.sha256 + "  " + r.path + "\n").toStdString();

    if (! out.getChildFile ("SHA256SUMS").replaceWithData (sums.data(), sums.size()))
    {
        std::cerr << "juce-oracle: cannot write SHA256SUMS" << std::endl;
        return exitEnvironment;
    }

    std::cerr << "scenario set " << scenarioSetHash (files) << ": " << counts["pass"] << " pass, " << counts["fail"]
              << " fail, " << counts["error"] << " error, " << counts["unsupported"] << " unsupported" << std::endl;

    if (counts["error"] > 0)
        return exitScenarioError;
    if (options.strict && (counts["fail"] > 0 || counts["unsupported"] > 0))
        return exitStrict;
    return exitOk;
}
} // namespace oracle
