# NoisiaX V1 Plan — Deterministic Scenario-First Core

## Summary
- We will ship a **domain-agnostic, deterministic v1** focused on strong scenario structure and reproducible execution, with **no AI integrations**.
- V1 will implement a strict authoring pipeline: **Goal → Assumptions → Entities → Variables → Dependencies → Constraints → Events → Evaluation Criteria** with hard validation gates.
- Authoring format will be **YAML + validator/compiler**, mapped into a static deterministic runtime (static dependency graph, event-driven activation only).

## Key Changes (Interfaces + Behavior)
1. Add a public scenario model layer:
- `ScenarioDefinition`, `EntityDescriptor`, `VariableDescriptor`, `DependencyEdge`, `ConstraintRule`, `EventDescriptor`, `EvaluationCriterion`, `ScenarioReport`.
- Required schema fields: `scenario_id`, `schema_version`, `master_seed`, `assumptions`, `entities`, `variables`, `dependency_edges`, `constraints`, `evaluation_criteria`.
- Optional fields for v1: `events`, `metadata`.
- Explicitly excluded in v1: `stochastic_overlays`, runtime dynamic edge creation, any AI connector APIs.

2. Add authoring + compilation pipeline:
- `ScenarioLoader` reads YAML and normalizes into canonical in-memory schema.
- `ScenarioValidator` enforces gate order and validation levels (`REJECT`, `WARN`, `AUTO_CORRECT` where safe, with full logs).
- `ScenarioCompiler` converts validated definitions into runtime artifacts (`ParameterHandle` map, static adjacency lists, event queue, constraint program).

3. Add deterministic runtime core:
- `SimulationState` with typed SoA buffers.
- `DependencyGraph` as static adjacency list with pre-registered propagation function IDs.
- Hybrid propagation semantics for v1 runtime: **push invalidation + pull recompute**.
- Deterministic `EventScheduler`: stable ordering by `(timestamp, priority, handle_lexicographic)`.

4. Add scenario-facing entrypoints:
- Library API: `validate_scenario(...)`, `compile_scenario(...)`, `run_scenario(...)`, `save_checkpoint(...)`, `load_checkpoint(...)`.
- CLI commands: `noisiax validate <file>`, `noisiax compile <file>`, `noisiax run <compiled_or_source>`.
- Artifacts: machine-readable validation report + human-readable summary.

5. Project structure update (implementation target):
- New modules under `/Users/taarn/Documents/PGM/Prog-Languages/NoisiaX/include/noisiax/` and `/Users/taarn/Documents/PGM/Prog-Languages/NoisiaX/src/` for schema, validation, compiler, engine, scheduler, and serialization.
- Build integration and tests extended from `/Users/taarn/Documents/PGM/Prog-Languages/NoisiaX/CMakeLists.txt`.

## Test Plan (Acceptance Criteria)
1. Schema/gate enforcement:
- Missing or out-of-order stages fail with `REJECT`.
- Unknown fields and anti-pattern fields are rejected.
- Required-field completeness test passes for valid scenario.

2. Validation correctness:
- Static cycle detection rejects cyclic dependency graphs.
- Type/unit incompatibility rejects invalid edges.
- Unsatisfiable initial constraints reject scenario before runtime.
- Orphan/fan-out/depth checks emit expected warnings/rejections by policy.

3. Determinism + replay:
- Same schema + seed + event stream must produce bitwise-identical end state in repeated runs.
- Checkpoint/load replay must match non-stop run state at same logical time.

4. Runtime semantics:
- Push invalidation marks downstream stale without eager recompute.
- Pull on stale node recomputes minimal upstream set and caches results.
- Event tie-break behavior is stable and reproducible.

5. End-to-end scenario tests:
- At least one canonical “happy path” YAML scenario and one failure scenario.
- CLI `validate/compile/run` smoke tests integrated into CTest.

## Assumptions and Defaults
- C++20 remains the baseline.
- YAML is the sole authoring format in v1 (JSON can be added later via transpile path).
- Domain remains generic (no built-in economic/risk domain package in v1).
- v1 runs on CPU in a single-process deterministic mode; parallel/distributed optimization is deferred.
- No AI model calls, inference adapters, or network-dependent intelligence modules are included.
