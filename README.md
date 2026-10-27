# juce-oracle

Differential oracle for [SYRP](https://github.com/SYRP-Foundation/SYRP), a Rust reimplementation of JUCE.

This repository builds real JUCE 9.0.4 and runs SYRP's parity scenarios against it to produce reference outputs (audio, JSON dumps, images) with a hash manifest. SYRP's CI downloads those outputs by hash.

It is licensed **AGPL-3.0-only** because it links JUCE under JUCE's AGPL option. It is never distributed with SYRP, and SYRP implementers never read it: only reference outputs cross over.
