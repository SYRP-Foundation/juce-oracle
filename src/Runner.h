// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

#include "Hashing.h"

namespace oracle
{
/** Exit codes of the juce-oracle CLI. */
enum ExitCode
{
    exitOk = 0,          // every scenario ran (pass, fail or unsupported, unless --strict)
    exitScenarioError = 1,
    exitUsage = 2,
    exitEnvironment = 3, // output directory not usable
    exitStrict = 4       // --strict, and some scenario failed or was unsupported
};

struct RunOptions
{
    juce::File outputDirectory;
    juce::String platformLabel; // e.g. "linux-x64"; recorded in the manifest only
    juce::String oracleCommit;  // this repository's commit, recorded in the manifest only
    bool strict = false;
    bool replaceOutput = false; // allow replacing an existing oracle output directory
};

/** Runs the given scenario files and writes outputs, manifest.json and SHA256SUMS. */
int runScenarioSet (const std::vector<ScenarioFile>& files, const RunOptions& options);

/** JSON describing this build: oracle and JUCE versions. */
juce::var describeBuild();
} // namespace oracle
