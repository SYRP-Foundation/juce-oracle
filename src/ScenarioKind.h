// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

#include "OutputSink.h"

#include <memory>
#include <vector>

namespace oracle
{
/** What happened when a scenario ran. */
struct Outcome
{
    enum class Status
    {
        pass,        // ran, and JUCE agreed with the scenario's expect block (or it has none)
        fail,        // ran, and JUCE disagreed with the expect block: an observation, not a harness bug
        error,       // the scenario could not be run (malformed, unknown op, output not writable...)
        unsupported  // no registered kind handles this scenario
    };

    Status status = Status::error;
    juce::String message;

    static Outcome pass (juce::String m = {}) { return { Status::pass, std::move (m) }; }
    static Outcome fail (juce::String m)      { return { Status::fail, std::move (m) }; }
    static Outcome error (juce::String m)     { return { Status::error, std::move (m) }; }
};

juce::String toString (Outcome::Status);

/** A family of scenarios, identified by a scenario-id prefix such as "state/value_tree/listener".
    A kind reads the scenario JSON, drives JUCE through its public API, and writes its observations
    with the OutputSink. To add one, see README.md ("Adding a scenario kind"). */
class ScenarioKind
{
public:
    virtual ~ScenarioKind() = default;

    /** The scenario-id prefix this kind handles, without a trailing slash. */
    virtual juce::String getPrefix() const = 0;

    /** One line for `juce-oracle kinds`. */
    virtual juce::String getDescription() const = 0;

    /** The output files this kind writes for each scenario, for `juce-oracle kinds`. */
    virtual juce::StringArray getOutputs() const = 0;

    virtual Outcome run (const juce::var& scenario, OutputSink& sink) = 0;
};

/** Every kind the oracle knows. Registration is one line in ScenarioKind.cpp. */
std::vector<std::unique_ptr<ScenarioKind>> makeAllKinds();

/** Finds the kind for a scenario: the one named by an explicit "kind" field if present, otherwise
    the kind with the longest prefix that matches the id at a '/' boundary. Null if none. */
ScenarioKind* findKind (const std::vector<std::unique_ptr<ScenarioKind>>& kinds,
                        const juce::String& scenarioId,
                        const juce::String& explicitKind);

// Factories, one per kind (each defined next to its kind).
std::unique_ptr<ScenarioKind> makeValueTreeListenerKind();
std::unique_ptr<ScenarioKind> makeSelfTestKind();
} // namespace oracle
