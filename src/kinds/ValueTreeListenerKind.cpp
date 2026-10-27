// SPDX-License-Identifier: AGPL-3.0-only
// Scenario kind "state/value_tree/listener": builds ValueTree objects with setup ops, attaches recording
// listeners, runs the step ops, and records every listener callback heard during the steps.
// Written against the public ValueTree / ValueTree::Listener API and its documentation only.
#include "ScenarioKind.h"
#include "Json.h"

#include <juce_data_structures/juce_data_structures.h>

#include <functional>
#include <limits>
#include <map>

namespace oracle
{
namespace
{
const juce::StringArray treeFields { "tree", "parent", "child" };

class Session;

/** A listener that reports every callback to the session, and can run ops when it hears a property. */
class RecordingListener final : public juce::ValueTree::Listener
{
public:
    RecordingListener (Session& s, juce::String n, juce::var reactionsIn)
        : session (s), name (std::move (n)), reactions (std::move (reactionsIn)) {}

    void valueTreePropertyChanged (juce::ValueTree& tree, const juce::Identifier& property) override;
    void valueTreeChildAdded (juce::ValueTree& parent, juce::ValueTree& child) override;
    void valueTreeChildRemoved (juce::ValueTree& parent, juce::ValueTree& child, int index) override;
    void valueTreeChildOrderChanged (juce::ValueTree& parent, int oldIndex, int newIndex) override;
    void valueTreeParentChanged (juce::ValueTree& tree) override;
    void valueTreeRedirected (juce::ValueTree& tree) override;

private:
    Session& session;
    juce::String name;
    juce::var reactions;
};

class Session
{
public:
    ~Session()
    {
        // Trees first, so no tree outlives a listener it still points at. Destroying shared data can
        // itself send callbacks, so listeners are muted and the map is emptied before anything dies.
        tearingDown = true;
        auto doomed = std::move (trees);
        trees.clear();
        doomed.clear();
        listeners.clear();
    }

    /** False while the session is being destroyed: callbacks then do nothing at all. */
    bool isLive() const noexcept { return ! tearingDown; }

    bool recording = false;
    juce::Array<juce::var> observed;
    juce::String error;

    /** The scenario names whose ValueTree object refers to the same data as t, at this moment. One
        name gives a string, several give a sorted array, none gives null. */
    juce::var namesFor (const juce::ValueTree& t) const
    {
        juce::StringArray names;
        for (const auto& [n, tree] : trees) // std::map: already sorted by name
            if (*tree == t)
                names.add (n);

        if (names.isEmpty())
            return {};
        if (names.size() == 1)
            return names[0];

        juce::Array<juce::var> list;
        for (const auto& n : names)
            list.add (n);
        return list;
    }

    void record (const juce::String& listener, const juce::String& callback, const juce::var& fields)
    {
        if (! recording)
            return;

        auto call = makeObject();
        set (call, "listener", listener);
        set (call, "callback", callback);
        if (auto* o = fields.getDynamicObject())
            for (const auto& p : o->getProperties())
                set (call, p.name.toString(), p.value);
        observed.add (call);
    }

    bool apply (const juce::var& op)
    {
        if (error.isNotEmpty())
            return false;

        if (++depth > maxDepth)
        {
            fail ("listener reactions nested deeper than " + juce::String (maxDepth));
            --depth;
            return false;
        }

        applyOne (op);
        --depth;
        return error.isEmpty();
    }

private:
    static constexpr int maxDepth = 64;
    int depth = 0;
    bool tearingDown = false;
    std::map<juce::String, std::unique_ptr<juce::ValueTree>> trees;
    std::map<juce::String, std::unique_ptr<RecordingListener>> listeners;

    void fail (const juce::String& message)
    {
        if (error.isEmpty())
            error = message;
    }

    juce::String str (const juce::var& op, const char* key)
    {
        if (! has (op, key) || ! op[key].isString())
        {
            fail ("op " + op["op"].toString() + ": missing string field '" + key + "'");
            return {};
        }
        return op[key].toString();
    }

    int integer (const juce::var& op, const char* key)
    {
        const auto v = op[key];
        if (! (v.isInt() || v.isInt64()))
        {
            fail ("op " + op["op"].toString() + ": missing integer field '" + key + "'");
            return 0;
        }
        return narrowToInt (v, "op " + op["op"].toString() + ": field '" + key + "'", 0);
    }

    /** Integers are read as int; a value outside the int range is an error, never a wrapped value. */
    int narrowToInt (const juce::var& v, const juce::String& what, int fallback)
    {
        const auto wide = static_cast<juce::int64> (v);
        if (wide < static_cast<juce::int64> (std::numeric_limits<int>::min())
            || wide > static_cast<juce::int64> (std::numeric_limits<int>::max()))
        {
            fail (what + ": integer " + juce::String (wide) + " is outside the supported 32-bit range");
            return fallback;
        }
        return static_cast<int> (wide);
    }

    juce::ValueTree* tree (const juce::String& name)
    {
        if (error.isNotEmpty())
            return nullptr;

        const auto it = trees.find (name);
        if (it == trees.end())
        {
            fail ("unknown tree '" + name + "'");
            return nullptr;
        }
        return it->second.get();
    }

    /** Scenario values are JSON numbers, strings or booleans; integers become int vars. */
    juce::var value (const juce::var& v)
    {
        if (v.isInt64())
            return narrowToInt (v, "set_property: value", 0);
        if (v.isInt() || v.isDouble() || v.isBool() || v.isString())
            return v;

        fail ("set_property: value must be a number, string or boolean");
        return {};
    }

    void applyOne (const juce::var& op)
    {
        const auto kind = op["op"].toString();

        if (kind == "create")
        {
            const auto name = str (op, "tree");
            const auto type = str (op, "type");
            if (error.isEmpty() && trees.count (name) != 0)
                fail ("create: tree '" + name + "' already exists");
            if (error.isEmpty())
                trees[name] = std::make_unique<juce::ValueTree> (juce::Identifier (type));
        }
        else if (kind == "copy")
        {
            const auto name = str (op, "tree");
            if (auto* source = tree (str (op, "source")))
            {
                if (trees.count (name) != 0)
                    fail ("copy: tree '" + name + "' already exists");
                else
                    trees[name] = std::make_unique<juce::ValueTree> (*source);
            }
        }
        else if (kind == "destroy")
        {
            const auto name = str (op, "tree");
            if (tree (name) != nullptr)
            {
                // Take the object out of the map before destroying it: its destruction can send
                // callbacks, and those look names up in the map.
                auto doomed = std::move (trees[name]);
                trees.erase (name);
                doomed.reset();
            }
        }
        else if (kind == "assign")
        {
            auto* target = tree (str (op, "tree"));
            auto* source = tree (str (op, "source"));
            if (target != nullptr && source != nullptr)
                *target = *source;
        }
        else if (kind == "set_property")
        {
            auto* t = tree (str (op, "tree"));
            const auto name = str (op, "name");
            const auto v = value (op["value"]);
            if (t != nullptr && error.isEmpty())
                t->setProperty (juce::Identifier (name), v, nullptr);
        }
        else if (kind == "remove_property")
        {
            auto* t = tree (str (op, "tree"));
            const auto name = str (op, "name");
            if (t != nullptr && error.isEmpty())
                t->removeProperty (juce::Identifier (name), nullptr);
        }
        else if (kind == "add_child")
        {
            auto* parent = tree (str (op, "parent"));
            auto* child = tree (str (op, "child"));
            const auto index = integer (op, "index");
            if (parent != nullptr && child != nullptr && error.isEmpty())
                parent->addChild (*child, index, nullptr);
        }
        else if (kind == "remove_child")
        {
            auto* parent = tree (str (op, "parent"));
            auto* child = tree (str (op, "child"));
            if (parent != nullptr && child != nullptr)
                parent->removeChild (*child, nullptr);
        }
        else if (kind == "move_child")
        {
            auto* parent = tree (str (op, "parent"));
            const auto from = integer (op, "from_index");
            const auto to = integer (op, "to_index");
            if (parent != nullptr && error.isEmpty())
                parent->moveChild (from, to, nullptr);
        }
        else if (kind == "add_listener")
        {
            auto* t = tree (str (op, "tree"));
            const auto name = str (op, "listener");
            if (t == nullptr || error.isNotEmpty())
                return;

            auto& l = listeners[name];
            if (l == nullptr)
                l = std::make_unique<RecordingListener> (*this, name, op["on_property_changed"]);
            t->addListener (l.get());
        }
        else
        {
            fail ("unknown op '" + kind + "'");
        }
    }
};

juce::var fields (std::initializer_list<std::pair<const char*, juce::var>> items)
{
    auto o = makeObject();
    for (const auto& [k, v] : items)
        set (o, k, v);
    return o;
}

void RecordingListener::valueTreePropertyChanged (juce::ValueTree& tree, const juce::Identifier& property)
{
    if (! session.isLive())
        return;

    session.record (name, "valueTreePropertyChanged",
                    fields ({ { "tree", session.namesFor (tree) }, { "property", property.toString() } }));

    if (auto* list = reactions.getArray())
        for (const auto& r : *list)
            if (r["when_property"].toString() == property.toString())
                session.apply (r["do"]);
}

void RecordingListener::valueTreeChildAdded (juce::ValueTree& parent, juce::ValueTree& child)
{
    if (! session.isLive())
        return;

    session.record (name, "valueTreeChildAdded",
                    fields ({ { "parent", session.namesFor (parent) }, { "child", session.namesFor (child) } }));
}

void RecordingListener::valueTreeChildRemoved (juce::ValueTree& parent, juce::ValueTree& child, int index)
{
    if (! session.isLive())
        return;

    session.record (name, "valueTreeChildRemoved",
                    fields ({ { "parent", session.namesFor (parent) },
                              { "child", session.namesFor (child) },
                              { "index", index } }));
}

void RecordingListener::valueTreeChildOrderChanged (juce::ValueTree& parent, int oldIndex, int newIndex)
{
    if (! session.isLive())
        return;

    session.record (name, "valueTreeChildOrderChanged",
                    fields ({ { "parent", session.namesFor (parent) },
                              { "old_index", oldIndex },
                              { "new_index", newIndex } }));
}

void RecordingListener::valueTreeParentChanged (juce::ValueTree& tree)
{
    if (! session.isLive())
        return;

    session.record (name, "valueTreeParentChanged", fields ({ { "tree", session.namesFor (tree) } }));
}

void RecordingListener::valueTreeRedirected (juce::ValueTree& tree)
{
    if (! session.isLive())
        return;

    session.record (name, "valueTreeRedirected", fields ({ { "tree", session.namesFor (tree) } }));
}

//==============================================================================
// Comparing observed calls with the scenario's expect block.

bool fieldMatches (const juce::String& key, const juce::var& expected, const juce::var& observed)
{
    if (treeFields.contains (key) && expected.isString())
    {
        if (observed.isString())
            return observed.toString() == expected.toString();
        if (auto* names = observed.getArray())
            return names->contains (expected);
        return false;
    }

    if ((expected.isInt() || expected.isInt64()) && (observed.isInt() || observed.isInt64()))
        return static_cast<juce::int64> (expected) == static_cast<juce::int64> (observed);

    return expected == observed;
}

/** Every field the expected call names must match; fields it leaves out are not compared. */
bool callMatches (const juce::var& expected, const juce::var& observed)
{
    auto* e = expected.getDynamicObject();
    if (e == nullptr)
        return false;

    for (const auto& p : e->getProperties())
    {
        const auto key = p.name.toString();
        if (! has (observed, key) || ! fieldMatches (key, p.value, observed[p.name]))
            return false;
    }
    return true;
}

struct Check
{
    bool pass = false;
    juce::Array<juce::var> unmatchedExpected; // indices into expect.calls
    juce::Array<juce::var> unmatchedObserved; // indices into observed
};

Check compareSequence (const juce::Array<juce::var>& expected, const juce::Array<juce::var>& observed)
{
    Check c;
    const auto n = juce::jmax (expected.size(), observed.size());
    for (int i = 0; i < n; ++i)
    {
        const bool both = i < expected.size() && i < observed.size();
        if (both && callMatches (expected[i], observed[i]))
            continue;
        if (i < expected.size())
            c.unmatchedExpected.add (i);
        if (i < observed.size())
            c.unmatchedObserved.add (i);
    }
    c.pass = c.unmatchedExpected.isEmpty() && c.unmatchedObserved.isEmpty();
    return c;
}

/** Order ignored, repeats counted: a maximum bipartite matching between expected and observed. */
Check compareMultiset (const juce::Array<juce::var>& expected, const juce::Array<juce::var>& observed)
{
    const int ne = expected.size(), no = observed.size();
    std::vector<std::vector<bool>> ok ((size_t) ne, std::vector<bool> ((size_t) no));
    for (int i = 0; i < ne; ++i)
        for (int j = 0; j < no; ++j)
            ok[(size_t) i][(size_t) j] = callMatches (expected[i], observed[j]);

    std::vector<int> ownerOfObserved ((size_t) no, -1);

    std::function<bool (int, std::vector<bool>&)> augment = [&] (int i, std::vector<bool>& seen)
    {
        for (int j = 0; j < no; ++j)
        {
            if (! ok[(size_t) i][(size_t) j] || seen[(size_t) j])
                continue;
            seen[(size_t) j] = true;
            if (ownerOfObserved[(size_t) j] < 0 || augment (ownerOfObserved[(size_t) j], seen))
            {
                ownerOfObserved[(size_t) j] = i;
                return true;
            }
        }
        return false;
    };

    Check c;
    for (int i = 0; i < ne; ++i)
    {
        std::vector<bool> seen ((size_t) no, false);
        if (! augment (i, seen))
            c.unmatchedExpected.add (i);
    }
    for (int j = 0; j < no; ++j)
        if (ownerOfObserved[(size_t) j] < 0)
            c.unmatchedObserved.add (j);

    c.pass = c.unmatchedExpected.isEmpty() && c.unmatchedObserved.isEmpty();
    return c;
}

/** Order and repeats ignored: each expected call is heard at least once, and each heard call is expected. */
Check compareSet (const juce::Array<juce::var>& expected, const juce::Array<juce::var>& observed)
{
    Check c;
    for (int i = 0; i < expected.size(); ++i)
    {
        bool found = false;
        for (const auto& o : observed)
            found = found || callMatches (expected[i], o);
        if (! found)
            c.unmatchedExpected.add (i);
    }
    for (int j = 0; j < observed.size(); ++j)
    {
        bool found = false;
        for (const auto& e : expected)
            found = found || callMatches (e, observed[j]);
        if (! found)
            c.unmatchedObserved.add (j);
    }
    c.pass = c.unmatchedExpected.isEmpty() && c.unmatchedObserved.isEmpty();
    return c;
}

class ValueTreeListenerKind final : public ScenarioKind
{
public:
    juce::String getPrefix() const override { return "state/value_tree/listener"; }

    juce::String getDescription() const override
    {
        return "ValueTree::Listener callbacks heard during the steps, compared with expect.calls";
    }

    juce::StringArray getOutputs() const override { return { "result.json" }; }

    Outcome run (const juce::var& scenario, OutputSink& sink) override
    {
        Session session;

        for (const auto* section : { "setup", "steps" })
        {
            session.recording = juce::String (section) == "steps";
            auto* ops = scenario[section].getArray();
            if (ops == nullptr)
                return Outcome::error (juce::String ("missing array '") + section + "'");

            for (int i = 0; i < ops->size(); ++i)
                if (! session.apply (ops->getReference (i)))
                    return Outcome::error (juce::String (section) + "[" + juce::String (i) + "]: " + session.error);
        }
        session.recording = false;

        const auto expect = scenario["expect"];
        const auto compare = expect["compare"].toString();
        auto* expectedCalls = expect["calls"].getArray();
        if (expectedCalls == nullptr)
            return Outcome::error ("missing array 'expect.calls'");

        Check check;
        if (compare == "sequence")
            check = compareSequence (*expectedCalls, session.observed);
        else if (compare == "multiset")
            check = compareMultiset (*expectedCalls, session.observed);
        else if (compare == "set")
            check = compareSet (*expectedCalls, session.observed);
        else
            return Outcome::error ("unknown expect.compare '" + compare + "'");

        auto checkJson = makeObject();
        set (checkJson, "compare", compare);
        set (checkJson, "status", check.pass ? "pass" : "fail");
        set (checkJson, "unmatched_expected", check.unmatchedExpected);
        set (checkJson, "unmatched_observed", check.unmatchedObserved);

        auto result = makeObject();
        set (result, "id", scenario["id"]);
        set (result, "kind", getPrefix());
        set (result, "juce_tag", JUCE_ORACLE_JUCE_TAG);
        set (result, "record_order", scenario["record_order"]);
        set (result, "observed", session.observed);
        set (result, "check", checkJson);

        if (! sink.writeJson ("result.json", result))
            return Outcome::error (sink.getLastError());

        if (check.pass)
            return Outcome::pass();

        return Outcome::fail (juce::String (check.unmatchedExpected.size()) + " expected call(s) not heard, "
                              + juce::String (check.unmatchedObserved.size()) + " heard call(s) not expected");
    }
};
} // namespace

std::unique_ptr<ScenarioKind> makeValueTreeListenerKind()
{
    return std::make_unique<ValueTreeListenerKind>();
}
} // namespace oracle
