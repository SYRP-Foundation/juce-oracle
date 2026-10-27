# juce-oracle

Differential oracle for [SYRP](https://github.com/SYRP-Foundation/SYRP), a Rust reimplementation of JUCE.

This repository builds real JUCE 9.0.4 and runs SYRP's parity scenarios against it to produce reference
outputs (JSON dumps, audio, images) with a hash manifest. SYRP's CI downloads those outputs by hash.

## Licence boundary

- This repository is licensed **AGPL-3.0-only** because it links JUCE under JUCE's AGPL option.
- JUCE is **not vendored**. CMake fetches it at configure time from the official repository, pinned to
  tag `9.0.4`, commit `3fb5cbc0340758596dac39f7c8de47d562b4896e`; configure fails on any other commit.
- It is never distributed with SYRP, and SYRP implementers never read this repository. Only reference
  outputs (the files listed in a manifest) cross over, which are observations of JUCE's behaviour.
- The scenario runner is written against JUCE's public API and documentation. Do not copy JUCE source,
  comments or tables into this repository.

## Build

Requirements: CMake 3.22+, a C++20 compiler, git, Python 3.10+ (tests and scripts only), and on Linux
the freetype and fontconfig development headers (see below).

```sh
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release \
      -DJUCE_ORACLE_SCENARIOS_DIR=/path/to/SYRP/parity/scenarios   # optional, enables b2_syrp_scenarios_run
cmake --build build -j2
ctest --test-dir build --output-on-failure
cmake --install build --prefix dist      # dist/bin/juce-oracle
```

To reuse an existing JUCE clone, add `-DFETCHCONTENT_SOURCE_DIR_JUCE=/path/to/JUCE` (it must be at the
pinned commit).

The runner links `juce_core`, `juce_events`, `juce_data_structures` and `juce_cryptography` only. It is a
headless console app: curl, web views and the splash screen are disabled, and no GUI, audio-device or
graphics module is linked, so on Linux no ALSA, X11, GTK, WebKit or curl headers are needed (JUCE's
configure step probes for them and prints "not found"; that is harmless). On Linux, JUCE's build-time
helper tool (`juceaide`, built during configure) does need the freetype and fontconfig headers:
`sudo apt-get install libfreetype-dev libfontconfig1-dev`. Kinds that render images will add
`juce_graphics`, which needs the same two packages.

## Run

```sh
juce-oracle run-all <SYRP>/parity/scenarios --out out/   # every scenario under a directory
juce-oracle run a.json b.json --out out/                 # individual files
juce-oracle hash <SYRP>/parity/scenarios                 # scenario-set hash and per-file hashes
juce-oracle kinds                                        # supported scenario kinds
juce-oracle version                                      # oracle and linked JUCE versions
```

Options for `run`/`run-all`: `--strict` (exit 4 if a scenario fails its `expect` block or is
unsupported), `--replace` (overwrite a previous oracle output directory; a directory without a
`manifest.json` is never deleted), `--label <name>` (platform label, default `<os>-<arch>`),
`--oracle-commit <sha>`.

Exit codes: 0 every scenario ran (pass, fail or unsupported), 1 some scenario could not be run (error),
2 usage, 3 output directory unusable, 4 `--strict` mismatch.

### Output layout

```
out/
  manifest.json                         run description and one entry per scenario
  SHA256SUMS                            every output plus manifest.json, `sha256sum -c` format
  scenarios/<scenario id>/<file>        outputs of each scenario (e.g. result.json, signal.wav)
```

`manifest.json` (`"format": "juce-oracle-manifest/1"`) records `juce` (tag, commit, version string),
`oracle` (version, commit), `platform` (label, os, arch, compiler), `scenario_set` (hash, count),
`summary` (counts per status) and `scenarios`: for each, `file`, `sha256` (of the scenario file), `id`,
`kind`, `status` (`pass`, `fail`, `error`, `unsupported`), optional `message`, and `outputs` (`path`,
`sha256`, `bytes`).

`fail` means JUCE did not behave as the scenario's `expect` block says. That is a finding for SYRP's spec
room, not a harness error; the observed behaviour is in the outputs either way.

Outputs are written byte-for-byte identically on every platform for the same behaviour (fixed JSON
layout, `\n` line ends, no timestamps or paths), so their hashes can be compared across platforms.

### Scenario-set hash

The release for a set of scenarios is found by its hash, which SYRP CI computes the same way:

1. Files: every `*.json` under the scenarios directory, at any depth, except `*.schema.json`.
2. File hash: SHA-256 (lower-case hex) of the file's bytes after replacing every CR LF with LF.
3. Set hash: SHA-256 of the UTF-8 text made of one line `<file hash>␠␠<relative path>\n` per file,
   paths `/`-separated and sorted in byte order.

`scripts/scenario_set_hash.py` is a reference implementation; `b2_scenario_set_hash_matches_reference`
checks the runner against it.

## Supported scenario kinds

A scenario's kind is the registered prefix that matches its `id` (longest match at a `/` boundary), or
its explicit `"kind"` field if it has one.

| Kind (id prefix) | What it does | Outputs |
|---|---|---|
| `state/value_tree/listener` | Builds `ValueTree` objects with the `setup` ops, attaches recording `ValueTree::Listener`s, runs the `steps`, records every callback heard during the steps in order, and compares them with `expect.calls` (`sequence`, `set` or `multiset`). | `result.json` |
| `oracle/selftest` | Harness self-test (fixtures only): a triangle-wave WAV and a JSON summary. | `signal.wav`, `summary.json` |

`result.json` for `state/value_tree/listener` holds `observed` (the calls in the order JUCE made them;
tree, parent and child are given as the scenario name that referred to that data at the time, or a
sorted array of names when several did) and `check` (`status`, and the indices of `unmatched_expected`
calls and `unmatched_observed` calls). Every scenario in SYRP's `parity/scenarios/` today is of this kind.

Audio outputs are 32-bit float WAV (`WAVE_FORMAT_IEEE_FLOAT`, interleaved). PNG output is available
through `OutputSink::writeBytes` once a rendering kind exists.

## Adding a scenario kind

1. Create `src/kinds/<Name>Kind.cpp` with a class deriving from `oracle::ScenarioKind`
   (`src/ScenarioKind.h`): `getPrefix()` (the scenario-id prefix, e.g. `dsp/normalisable_range`),
   `getDescription()`, `getOutputs()`, and `run(scenario, sink)`, which drives JUCE and writes outputs with
   `sink.writeJson`, `sink.writeWavFloat32` or `sink.writeBytes`, then returns `Outcome::pass/fail/error`.
   Outputs must not depend on the platform beyond JUCE's own behaviour.
2. Add a factory `makeXxxKind()` next to it, declare it in `ScenarioKind.h`, and add one line to
   `makeAllKinds()` in `src/ScenarioKind.cpp`.
3. Add the source to `target_sources` in `CMakeLists.txt`, and any JUCE module it needs to
   `target_link_libraries` (keep Linux system dependencies in mind).
4. Add a fixture scenario under `tests/fixtures/scenarios/<prefix>/` and a `b2_...` test in
   `tests/oracle_tests.py` (listed in `cmake/Tests.cmake`).

## CI and releases

`.github/workflows/oracle.yml` builds on `ubuntu-24.04`, `macos-14` and `windows-2022`, runs ctest, runs
every scenario in SYRP's `parity/scenarios` (from `SYRP-Foundation/SYRP`, ref `main` or the
`workflow_dispatch` input `syrp_ref`), verifies the output, and uploads it as an artefact. While SYRP is
private, add a repository secret `SYRP_READ_TOKEN` (a fine-grained token with read access to SYRP's
contents).

On `main` (push, the daily schedule, or a manual run with `publish`), the `publish` job runs
`scripts/publish_release.py`, which creates or updates the release **`scenarios-<scenario-set hash>`**
with these assets (re-runs replace them):

| Asset | Contents |
|---|---|
| `juce-oracle-<label>.tar.gz` | the output directory of one platform (`linux-x64`, `macos-arm64`, `windows-x64`) |
| `manifest-<label>.json` | that platform's manifest |
| `cross-platform.json` | output files and statuses that differ between platforms (ideally none) |
| `release.json` | tag, scenario set, JUCE tag and commit, oracle commit, per-platform summary, asset list |

SYRP CI downloads by hash, for example:

```sh
gh release download "scenarios-$HASH" -R SYRP-Foundation/juce-oracle -p 'juce-oracle-linux-x64.tar.gz'
```

Only GitHub-owned actions are used, each pinned to a commit SHA; `scripts/lint_workflow.py`
(`b3_workflow_lint`) enforces that.

## Tests

`ctest` runs one test per behaviour, defined in `tests/oracle_tests.py`:

| Behaviour | Tests |
|---|---|
| B1 runner built against JUCE 9.0.4 | `b1_runner_reports_juce_9_0_4`, `b1_lists_supported_kinds`, `b1_cli_usage_errors_exit_2` |
| B2 scenarios in, outputs and hashed manifest out | `b2_value_tree_listener_fixtures_run`, `b2_selftest_writes_valid_wav`, `b2_manifest_hashes_match_outputs`, `b2_scenario_set_hash_matches_reference`, `b2_outputs_are_deterministic`, `b2_unsupported_kind_is_reported`, `b2_malformed_scenario_is_an_error`, `b2_refuses_non_empty_output_dir`, `b2_syrp_scenarios_run` (skipped without `JUCE_ORACLE_SCENARIOS_DIR`) |
| B3 publishing | `b3_workflow_lint` (skipped without PyYAML), `b3_release_dry_run` |
