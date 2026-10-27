// SPDX-License-Identifier: AGPL-3.0-only
// juce-oracle: runs SYRP parity scenarios against real JUCE and writes reference outputs.
#include "Json.h"
#include "Runner.h"
#include "ScenarioKind.h"

#include <iostream>

namespace
{
const char* usage = R"(usage:
  juce-oracle run <scenario.json>... --out <dir> [options]   run individual scenario files
  juce-oracle run-all <scenarios-dir> --out <dir> [options]  run every scenario under a directory
  juce-oracle hash <scenarios-dir>                            print the scenario-set hash and files
  juce-oracle kinds                                           list supported scenario kinds
  juce-oracle version                                         print oracle and JUCE versions

options:
  --strict               exit 4 if any scenario fails its expect block or is unsupported
  --replace              replace <dir> if it holds a previous oracle output
  --label <name>         platform label recorded in the manifest (default <os>-<arch>)
  --oracle-commit <sha>  this repository's commit, recorded in the manifest

exit codes: 0 ok, 1 scenario error, 2 usage, 3 environment, 4 strict mismatch
)";

void print (const juce::var& v)
{
    std::cout << oracle::toCanonicalJson (v) << std::flush;
}

int usageError (const juce::String& message)
{
    std::cerr << "juce-oracle: " << message << "\n\n" << usage;
    return oracle::exitUsage;
}
} // namespace

int main (int argc, char* argv[])
{
    juce::StringArray args;
    for (int i = 1; i < argc; ++i)
        args.add (juce::String::fromUTF8 (argv[i]));

    if (args.isEmpty() || args[0] == "--help" || args[0] == "-h" || args[0] == "help")
    {
        std::cout << usage;
        return args.isEmpty() ? oracle::exitUsage : oracle::exitOk;
    }

    const auto command = args[0];
    args.remove (0);

    if (command == "version")
    {
        print (oracle::describeBuild());
        return oracle::exitOk;
    }

    if (command == "kinds")
    {
        juce::Array<juce::var> list;
        for (const auto& k : oracle::makeAllKinds())
        {
            auto o = oracle::makeObject();
            oracle::set (o, "prefix", k->getPrefix());
            oracle::set (o, "description", k->getDescription());
            juce::Array<juce::var> outputs;
            for (const auto& name : k->getOutputs())
                outputs.add (name);
            oracle::set (o, "outputs", outputs);
            list.add (o);
        }
        print (list);
        return oracle::exitOk;
    }

    const auto cwd = juce::File::getCurrentWorkingDirectory();

    if (command == "hash")
    {
        if (args.size() != 1)
            return usageError ("hash takes one directory");

        const auto root = cwd.getChildFile (args[0]);
        if (! root.isDirectory())
            return usageError ("not a directory: " + args[0]);

        const auto files = oracle::collectScenarioFiles (root);
        auto o = oracle::makeObject();
        oracle::set (o, "hash", oracle::scenarioSetHash (files));
        juce::Array<juce::var> list;
        for (const auto& f : files)
        {
            auto e = oracle::makeObject();
            oracle::set (e, "file", f.relativePath);
            oracle::set (e, "sha256", f.sha256);
            list.add (e);
        }
        oracle::set (o, "files", list);
        print (o);
        return oracle::exitOk;
    }

    if (command != "run" && command != "run-all")
        return usageError ("unknown command '" + command + "'");

    oracle::RunOptions options;
    juce::StringArray positional;

    for (int i = 0; i < args.size(); ++i)
    {
        const auto& a = args[i];
        auto value = [&] (juce::String& target) -> bool
        {
            if (i + 1 >= args.size())
                return false;
            target = args[++i];
            return true;
        };

        if (a == "--out")
        {
            juce::String path;
            if (! value (path))
                return usageError ("--out needs a directory");
            options.outputDirectory = cwd.getChildFile (path);
        }
        else if (a == "--label")
        {
            if (! value (options.platformLabel))
                return usageError ("--label needs a value");
        }
        else if (a == "--oracle-commit")
        {
            if (! value (options.oracleCommit))
                return usageError ("--oracle-commit needs a value");
        }
        else if (a == "--strict")
            options.strict = true;
        else if (a == "--replace")
            options.replaceOutput = true;
        else if (a.startsWith ("--"))
            return usageError ("unknown option " + a);
        else
            positional.add (a);
    }

    if (options.outputDirectory == juce::File())
        return usageError ("--out is required");

    std::vector<oracle::ScenarioFile> files;

    if (command == "run-all")
    {
        if (positional.size() != 1)
            return usageError ("run-all takes one scenarios directory");

        const auto root = cwd.getChildFile (positional[0]);
        if (! root.isDirectory())
            return usageError ("not a directory: " + positional[0]);

        files = oracle::collectScenarioFiles (root);
    }
    else
    {
        if (positional.isEmpty())
            return usageError ("run needs at least one scenario file");

        for (const auto& p : positional)
        {
            const auto f = cwd.getChildFile (p);
            if (! f.existsAsFile())
                return usageError ("no such file: " + p);
            files.push_back ({ f, f.getFileName(), oracle::scenarioFileHash (f) });
        }
    }

    return oracle::runScenarioSet (files, options);
}
