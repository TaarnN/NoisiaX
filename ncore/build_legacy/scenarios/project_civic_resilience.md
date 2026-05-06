# Project Scenario: Civic Resilience v3/v4/v5

This suite models a 45-day district disaster-response program at medium scale:

- 60 residents with household, preparedness, resource, trust, stress, rumor, and resilience state.
- 12 infrastructure facilities covering substations, water pumps, clinics, shelters, logistics, and communications.
- 6 command cells with budgets, public messaging, field teams, and legitimacy.
- 312 authored relations before runtime relation creation.
- 45 deterministic ticks plus scheduled storm, rumor, repair, supply, evacuation, and coordination events.

## Files

- `project_civic_resilience_population.yaml`: v3 typed population module.
- `project_civic_resilience_infrastructure.yaml`: v3 typed infrastructure module.
- `project_civic_resilience_governance.yaml`: v3 typed governance module.
- `project_civic_resilience_v5.yaml`: v5 authoring scenario that v4-imports the three modules.
- `project_civic_resilience_experiment_v4_v5.yaml`: v4 experiment using seeds, variants, overlays, typed metrics, and a v5 extension metric.
- `../tools/generate_project_civic_resilience_scenario.js`: deterministic generator for the suite.

## Capability Coverage

- v3 typed runtime: components, entity types, relation types, initial relations, ticks, per-entity systems, pair systems, per-relation systems, writes, runtime relation creation with expiration, child events, causal event chains, deterministic `rng(...)`, and typed final-state metrics.
- v4 composition: imports population, infrastructure, and governance modules with namespaces.
- v4 experiments: runs 4 variants across 4 seeds with stochastic overlays and aggregate metrics.
- v5 extensions: declares `acme.market`, lowers its custom authoring block, calls `acme.market::bid_price_v2(...)` from typed expressions, and uses `acme.market::scaled_typed_field_final` as an experiment metric.

## Run

Resolve the authoring scenario:

```sh
./build/noisiax resolve scenarios/project_civic_resilience_v5.yaml --output /tmp/project_civic_resilience_resolved.yaml
```

Run the resolved scenario:

```sh
./build/noisiax run /tmp/project_civic_resilience_resolved.yaml --trace none --output /tmp/project_civic_resilience_run.json
```

Run the experiment:

```sh
./build/noisiax experiment scenarios/project_civic_resilience_experiment_v4_v5.yaml --output-dir /tmp/project_civic_resilience_exp --trace none
```
