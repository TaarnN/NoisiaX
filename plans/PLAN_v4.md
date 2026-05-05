# NoisiaX v4 Plan: Experiments, Composition, and Reproducible Research Runs

## Summary
v4 will turn the existing v1/v2/v3 runtimes into a repeatable research workflow. It will add scenario composition, parameterized variants, deterministic seed ensembles, experiment-level stochastic overlays, and aggregate comparison reports while preserving the current deterministic runtime contract.

v4 will not add AI connectors, internet data fetching, distributed execution, or a new simulation runtime layer.

## Public Interfaces
- Add `noisiax::experiment` APIs:
  - `resolve_scenario(path, ResolveOptions) -> ResolvedScenario`
  - `run_experiment(path, ExperimentOptions) -> ExperimentResult`
  - `run_experiment(ExperimentDefinition, ExperimentOptions) -> ExperimentResult`
- Add core types:
  - `ExperimentDefinition`, `ExperimentRunSpec`, `ScenarioOverride`, `SeedPlan`, `StochasticOverlay`, `ExperimentMetric`, `ExperimentResult`, `RunManifest`, `AggregateMetric`, `CompositionReport`.
- Add CLI commands:
  - `noisiax resolve <scenario.yaml> --output canonical.yaml`
  - `noisiax experiment <experiment.yaml> --output-dir <dir> --trace none|events|decisions|full`
- Keep existing `validate`, `compile`, `run`, `run_scenario_detailed`, v1, v2, and v3 behavior backward-compatible.

## Key Changes
- Scenario composition:
  - Add optional root-level `imports` for scenario fragments.
  - Imports require explicit `path` and optional `namespace`.
  - Fragment IDs are namespace-prefixed before merge.
  - Duplicate resolved IDs, unresolved references, and circular imports are hard validation failures.
  - Resolved scenarios produce canonical YAML plus source/resolved hashes.

- Parameterized variants:
  - Add explicit override support for `replace`, `append`, and `merge`.
  - v4 supports two override targets: canonical JSON Pointer paths and typed-layer component fields by `{ entity_id, component_type_id, field_name }`.
  - No wildcard selector language in v4.

- Experiment runner:
  - Experiment YAML references one base scenario, optional composition parameters, variants, seed plan, overlays, metrics, and output settings.
  - Each run materializes a concrete scenario, validates it, compiles it, runs it with existing runtime APIs, and records a manifest.
  - Execution is sequential and deterministic in v4.

- Deterministic stochastic overlays:
  - Overlays are experiment-time materialization rules, not hidden runtime mutation.
  - Supported samplers: `uniform_int`, `uniform_float`, `bernoulli`, `choice`, and `weighted_choice`.
  - Sampling uses the existing deterministic RNG pattern with stream keys like `overlay:<experiment_id>:<variant_id>:<overlay_id>`.
  - Materialized sampled values become normal scenario values before validation/compile.

- Reporting:
  - Write `experiment_manifest.json`, `summary.json`, and optional per-run detailed JSON files.
  - Record NoisiaX version, experiment ID, run ID, base scenario hash, resolved scenario hash, seed, overrides, overlay samples, runtime options, success/failure, final fingerprint, and selected metrics.
  - Aggregate numeric metrics with count, min, max, mean, and standard deviation.

## Test Plan
- Composition tests:
  - Resolve a multi-fragment typed scenario with namespaces.
  - Reject circular imports.
  - Reject duplicate resolved IDs.
  - Reject invalid override paths and typed-field targets.
  - Confirm canonical resolved hash is stable across repeated resolves.

- Experiment tests:
  - Run an explicit seed list and confirm identical repeated experiment summaries.
  - Change only seed plan and confirm at least one RNG-dependent branch changes.
  - Apply overlays before validation and prove invalid sampled/overridden values fail cleanly.
  - Aggregate typed final-state metrics across runs.
  - Confirm failed runs are reported without aborting the whole experiment unless `fail_fast=true`.

- Regression tests:
  - Existing v1, v2, and v3 fixtures still validate, compile, and run unchanged.
  - Existing CLI tests still pass.
  - New CLI tests cover `resolve` and `experiment`.

## Assumptions
- v4 focus is experiment orchestration and composition.
- C++20, YAML authoring, single-process CPU execution, and strict determinism remain the defaults.
- Scenario runtime semantics do not change; v4 builds concrete scenario variants before handing them to existing validation/compile/run paths.
- Parallel experiment execution, AI/data connectors, visual editors, Bayesian calibration, and distributed simulation are deferred.
