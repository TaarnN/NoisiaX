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
- `../include/noisiax/extensions/civic_resilience_extension.hpp` and `../src/extensions/civic_resilience_extension.cpp`: project-specific v5 extension linked into the default registry.

## Capability Coverage

- v3 typed runtime: components, entity types, relation types, initial relations, ticks, per-entity systems, pair systems, per-relation systems, writes, runtime relation creation with expiration, child events, causal event chains, deterministic `rng(...)`, and typed final-state metrics.
- v4 composition: imports population, infrastructure, and governance modules with namespaces.
- v4 experiments: runs 4 variants across 4 seeds with stochastic overlays and aggregate metrics.
- v5 extensions: declares `acme.market` and `civic.resilience`, lowers both custom authoring blocks, calls `acme.market::bid_price_v2(...)` plus `civic.resilience::resilience_index(...)`, `scarcity_pressure(...)`, and `triage_priority(...)` from typed expressions, and uses extension metrics from both namespaces.

## Verified Output

- Resolve: canonical output generated at `/private/tmp/project_civic_resilience_resolved.yaml`; authoring-only `extensions`, `acme.market`, and `civic.resilience` blocks are removed.
- Run: `processed_events=119`, `final_time=45.000000`, `state_fingerprint=584ff7cf269a98f6`.
- Experiment: 16/16 runs succeeded; `avg_population_resilience_pct` mean `58.0146`, min `56.1006`, max `60.4155`.

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
