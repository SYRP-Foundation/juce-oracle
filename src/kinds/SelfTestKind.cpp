// SPDX-License-Identifier: AGPL-3.0-only
// Scenario kind "oracle/selftest": exercises the output paths (JSON and WAV) with data that does not
// depend on JUCE behaviour, so the harness itself can be tested. Never used by SYRP scenarios.
#include "ScenarioKind.h"
#include "Json.h"

namespace oracle
{
namespace
{
class SelfTestKind final : public ScenarioKind
{
public:
    juce::String getPrefix() const override { return "oracle/selftest"; }
    juce::String getDescription() const override { return "Harness self-test: writes a triangle-wave WAV and a JSON summary"; }
    juce::StringArray getOutputs() const override { return { "signal.wav", "summary.json" }; }

    Outcome run (const juce::var& scenario, OutputSink& sink) override
    {
        const int sampleRate = scenario.getProperty ("sample_rate", 48000);
        const int samples = scenario.getProperty ("samples", 64);
        const int channels = scenario.getProperty ("channels", 2);
        const int period = scenario.getProperty ("period", 16);

        if (sampleRate <= 0 || samples < 0 || channels <= 0 || period <= 0 || period % 4 != 0)
            return Outcome::error ("sample_rate, channels and period must be positive, period a multiple of 4");

        // A triangle wave built from exact fractions, so every platform produces identical floats.
        std::vector<std::vector<float>> data ((size_t) channels, std::vector<float> ((size_t) samples));
        const int quarter = period / 4;
        for (int c = 0; c < channels; ++c)
        {
            for (int i = 0; i < samples; ++i)
            {
                const int phase = (i + c * quarter) % period;
                const int up = phase < 2 * quarter ? phase - quarter : 3 * quarter - phase;
                data[(size_t) c][(size_t) i] = (float) up / (float) quarter;
            }
        }

        if (! sink.writeWavFloat32 ("signal.wav", data, sampleRate))
            return Outcome::error (sink.getLastError());

        auto summary = makeObject();
        set (summary, "id", scenario["id"]);
        set (summary, "sample_rate", sampleRate);
        set (summary, "samples", samples);
        set (summary, "channels", channels);
        set (summary, "first_frame", [&]
        {
            juce::Array<juce::var> frame;
            for (int c = 0; c < channels && samples > 0; ++c)
                frame.add ((double) data[(size_t) c][0]);
            return frame;
        }());

        if (! sink.writeJson ("summary.json", summary))
            return Outcome::error (sink.getLastError());

        return Outcome::pass();
    }
};
} // namespace

std::unique_ptr<ScenarioKind> makeSelfTestKind()
{
    return std::make_unique<SelfTestKind>();
}
} // namespace oracle
