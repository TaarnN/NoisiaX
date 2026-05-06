# Scenario Fixtures

This folder contains small scenario fixtures for validation, compilation, and
runtime regression tests.

## v2 Agent-Layer Fixtures

- `v2_minimal_purchase.yaml`: smallest successful v2 path; one agent moves,
  queues, buys, and records final state.
- `v2_social_influence.yaml`: two agents share a location so one purchase can
  create an `OBSERVE_PURCHASE` event and influence counters.
- `v2_budget_stock_pressure.yaml`: traces rejected candidates when one item is
  unaffordable and the affordable item becomes depleted.
- `v2_queue_capacity.yaml`: sends three agents to a slow capacity-one shop to
  expose queue penalties and retry behavior.
- `v2_max_events_limit.yaml`: valid schema that intentionally hits the runtime
  `max_events` guard.
- `v2_invalid_agent_layer_reference.yaml`: invalid schema fixture for broken
  agent-layer references.

`market_butterfly_v2.yaml` remains the larger end-to-end demonstration fixture.

## v3 Typed-Layer Fixtures

- `v3_small_town_internet_100p_90d.yaml`: large deterministic typed-layer
  simulation of a 100-resident town over 90 days, with ward network hubs,
  social relations, adoption, misinformation, outages, repair events,
  service-slowdown child events, help tickets, and final state snapshots for
  every resident and hub.

## v4 Experiment + Composition Fixtures

- `v4_composition_base.yaml` (+ `v4_composition_fragment_a.yaml`,
  `v4_composition_fragment_b.yaml`): scenario composition via top-level
  `imports` with namespacing.
- `v4_circular_a.yaml` / `v4_circular_b.yaml`: circular import rejection.
- `v4_duplicate_ids_base.yaml`: duplicate resolved id rejection without
  namespacing.
- `v4_rng_value.yaml`: tiny typed-layer scenario that stores an RNG draw into a
  typed field for experiment determinism checks.
- `v4_experiment_basic.yaml`: minimal experiment definition driving
  `v4_rng_value.yaml` across a fixed seed list and aggregating a typed-field
  metric.

## Project-Scale v3/v4/v5 Scenario

- `project_civic_resilience_v5.yaml`: larger authoring scenario that combines
  v3 typed systems, v4 namespaced imports, and v5 `acme.market` extension
  lowering/expression symbols. The scenario imports population,
  infrastructure, and governance fragments, then simulates a 45-day disaster
  resilience response with scheduled shocks, causal child events, deterministic
  RNG, runtime relation creation, and typed final-state metrics.
- `project_civic_resilience_experiment_v4_v5.yaml`: 16-run experiment over
  four variants and four seeds with stochastic overlays, typed metrics, and the
  v5 extension metric `acme.market::scaled_typed_field_final`.
- `project_civic_resilience.md`: runbook and capability map for the full suite.
