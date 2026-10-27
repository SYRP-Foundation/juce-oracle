// SPDX-License-Identifier: AGPL-3.0-only
#include "ScenarioKind.h"

namespace oracle
{
juce::String toString (Outcome::Status s)
{
    switch (s)
    {
        case Outcome::Status::pass:        return "pass";
        case Outcome::Status::fail:        return "fail";
        case Outcome::Status::error:       return "error";
        case Outcome::Status::unsupported: return "unsupported";
    }
    return "error";
}

std::vector<std::unique_ptr<ScenarioKind>> makeAllKinds()
{
    std::vector<std::unique_ptr<ScenarioKind>> kinds;
    kinds.push_back (makeValueTreeListenerKind());
    kinds.push_back (makeSelfTestKind());
    // New kinds: add one line here and the source file to CMakeLists.txt.
    return kinds;
}

ScenarioKind* findKind (const std::vector<std::unique_ptr<ScenarioKind>>& kinds,
                        const juce::String& scenarioId,
                        const juce::String& explicitKind)
{
    if (explicitKind.isNotEmpty())
    {
        for (const auto& k : kinds)
            if (k->getPrefix() == explicitKind)
                return k.get();

        return nullptr;
    }

    ScenarioKind* best = nullptr;
    for (const auto& k : kinds)
    {
        const auto prefix = k->getPrefix();
        if (scenarioId.startsWith (prefix + "/")
            && (best == nullptr || prefix.length() > best->getPrefix().length()))
            best = k.get();
    }
    return best;
}
} // namespace oracle
