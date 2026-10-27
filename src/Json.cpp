// SPDX-License-Identifier: AGPL-3.0-only
#include "Json.h"

#include <charconv>
#include <cmath>

namespace oracle
{
namespace
{
void appendIndent (juce::String& out, int depth)
{
    out << juce::String::repeatedString ("  ", depth);
}

void appendString (juce::String& out, const juce::String& s)
{
    out << '"';
    for (auto p = s.getCharPointer(); ! p.isEmpty();)
    {
        const auto c = p.getAndAdvance();
        switch (c)
        {
            case '"':  out << "\\\""; break;
            case '\\': out << "\\\\"; break;
            case '\n': out << "\\n"; break;
            case '\r': out << "\\r"; break;
            case '\t': out << "\\t"; break;
            case '\b': out << "\\b"; break;
            case '\f': out << "\\f"; break;
            default:
                if (c < 0x20)
                    out << "\\u" << juce::String::toHexString ((int) c).paddedLeft ('0', 4);
                else
                    out << juce::String::charToString (c);
        }
    }
    out << '"';
}

void appendDouble (juce::String& out, double d)
{
    if (! std::isfinite (d))
    {
        out << "null";
        return;
    }

    char buffer[64] {};
    const auto r = std::to_chars (buffer, buffer + sizeof (buffer) - 1, d);
    juce::String text (buffer, (size_t) (r.ptr - buffer));

    if (! text.containsAnyOf (".eE"))
        text << ".0";

    out << text;
}

void appendValue (juce::String& out, const juce::var& v, int depth)
{
    if (v.isVoid() || v.isUndefined())
        out << "null";
    else if (v.isBool())
        out << (static_cast<bool> (v) ? "true" : "false");
    else if (v.isInt() || v.isInt64())
        out << juce::String (static_cast<juce::int64> (v));
    else if (v.isDouble())
        appendDouble (out, static_cast<double> (v));
    else if (v.isString())
        appendString (out, v.toString());
    else if (auto* array = v.getArray())
    {
        if (array->isEmpty())
        {
            out << "[]";
            return;
        }

        out << "[\n";
        for (int i = 0; i < array->size(); ++i)
        {
            appendIndent (out, depth + 1);
            appendValue (out, array->getReference (i), depth + 1);
            out << (i + 1 < array->size() ? ",\n" : "\n");
        }
        appendIndent (out, depth);
        out << "]";
    }
    else if (auto* object = v.getDynamicObject())
    {
        const auto& props = object->getProperties();
        if (props.isEmpty())
        {
            out << "{}";
            return;
        }

        out << "{\n";
        for (int i = 0; i < props.size(); ++i)
        {
            appendIndent (out, depth + 1);
            appendString (out, props.getName (i).toString());
            out << ": ";
            appendValue (out, props.getValueAt (i), depth + 1);
            out << (i + 1 < props.size() ? ",\n" : "\n");
        }
        appendIndent (out, depth);
        out << "}";
    }
    else
    {
        // Binary blobs, methods and other var kinds never appear in oracle outputs.
        out << "null";
    }
}
} // namespace

juce::String toCanonicalJson (const juce::var& value)
{
    juce::String out;
    appendValue (out, value, 0);
    out << "\n";
    return out;
}

juce::Result readJsonFile (const juce::File& file, juce::var& result)
{
    if (! file.existsAsFile())
        return juce::Result::fail ("no such file: " + file.getFullPathName());

    const auto parsed = juce::JSON::parse (file.loadFileAsString(), result);
    if (parsed.failed())
        return juce::Result::fail (file.getFileName() + ": invalid JSON: " + parsed.getErrorMessage());

    return juce::Result::ok();
}

juce::var makeObject()
{
    return juce::var (new juce::DynamicObject());
}

void set (juce::var& object, const juce::String& key, const juce::var& value)
{
    if (auto* o = object.getDynamicObject())
        o->setProperty (juce::Identifier (key), value);
}

bool has (const juce::var& object, const juce::String& key)
{
    if (auto* o = object.getDynamicObject())
        return o->hasProperty (juce::Identifier (key));

    return false;
}
} // namespace oracle
