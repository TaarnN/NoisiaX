# NoisiaX

NoisiaX is a deterministic, scenario-first C++20 simulation core.

It executes structured YAML scenarios through a strict pipeline:

```text
YAML -> validate -> compile -> run
```

Version: `1.0.0`

The current implementation includes the original variable-dependency runtime, the v2 market-style `agent_layer`, and the plan v3 `typed_layer` for generic typed simulations.

## What NoisiaX Is For

NoisiaX is designed for small-to-medium deterministic simulations where repeatability, validation, traceability, and clear runtime boundaries matter more than raw scale.

Good fits include:

- Dependency-driven scenario evaluation
- Market or shop simulations with deterministic agents
- Generic small-world simulations with user-defined entities, components, relations, and events
- Regression fixtures where the same seed must produce the same trace and final fingerprint
- Explainable simulation runs with event, decision, state-change, system, and random-draw traces

NoisiaX is not currently a physics engine, scripting VM, distributed simulator, AI connector, or internet data layer.

## Runtime Layers

NoisiaX routes each scenario to one runtime layer:

```text
typed_layer present  -> v3 typed runtime
agent_layer present  -> v2 agent runtime
otherwise            -> v1 variable-dependency runtime
```

A scenario may use `typed_layer` or `agent_layer`, but not both.

### v1 Core Runtime

The base runtime models a scenario as typed variables connected by dependency edges.

It supports:

- Strict top-level YAML validation
- Strong variable typing
- Static dependency graph validation
- Cycle rejection
- Deterministic compilation into parameter handles and adjacency lists
- Scheduled event ordering by timestamp, priority, and handle
- Runtime constraint checks
- Checkpoint save/load helpers

### v2 Agent Layer

The optional `agent_layer` adds a deterministic market-style simulation runtime.

It models:

- World duration, map size, walking speed, and event limits
- Locations
- Items and tags
- Shops, inventory, service time, and queue capacity
- Agents with budget, hunger, traits, preferences, memory, and policy references
- Movement, queueing, purchase attempts, observation, and social influence

Detailed v2 runs can emit:

- Event traces
- Decision traces with candidate scoring
- State changes
- Random draws
- Final agent and shop snapshots

### v3 Typed Layer

The plan v3 `typed_layer` generalizes NoisiaX beyond the market-specific agent model.

Instead of forcing every domain into people, shops, and items, v3 lets a scenario define its own simulation vocabulary:

- `world`: duration, time unit, max event count, and optional tick interval
- `component_types`: reusable typed data shapes
- `entity_types`: user-defined entity kinds composed from components
- `entities`: initial entity instances and component values
- `relation_types`: typed links between entities, with optional direction and bounds
- `relations`: initial relation instances with optional expiration time and payload
- `event_types`: runtime event names and typed payload fields
- `initial_events`: events scheduled at simulation start
- `systems`: deterministic rules triggered by events

The runtime supports three system kinds:

- `per_entity`: iterate entities of a declared type
- `pair`: iterate deterministic pairs of entities of a declared type
- `per_relation`: iterate active relations of a declared relation type

Systems can:

- Read component fields, event payload values, relation payload values, and runtime event metadata
- Write component fields
- Create bounded relations
- Emit child events with causal parent links
- Use `where` and `when` expressions to guard execution
- Use deterministic `rng(key, min, max)` draws that are recorded in traces

v3 final output includes:

- `typed_final_state`
- Final typed entities and components
- Final active relations
- Summary fields
- A deterministic state fingerprint
- `typed_system_traces` with matched entities, writes, created relations, emitted events, and random draws

## Determinism

NoisiaX is deterministic by default.

Determinism is based on:

- `master_seed` in the scenario
- Optional runtime seed override
- Stable compiled integer handles
- Stable entity, relation, system, and event iteration order
- Scheduler ordering by timestamp, priority, event handle, and generated ordinal
- Traceable random streams keyed by string

For the same scenario and seed, a deterministic fixture should produce the same traces and final fingerprint. Changing the seed can change branches that use `rng(...)`.

## Build

Requirements:

- CMake `3.21` or newer
- A C++20 compiler
- `yaml-cpp`

Configure and build:

```sh
cmake -S . -B build
cmake --build build
```

`yaml-cpp` resolution is automatic:

- An installed `yaml-cpp` CMake package is used when available.
- If no package is found, CMake fetches `yaml-cpp` `0.8.0` during configure.

## Test

Run the test suite:

```sh
ctest --test-dir build --output-on-failure
```

The tests cover:

- v1 validation, compilation, runtime behavior, and checkpoint helpers
- Strict unknown-field rejection
- Dependency cycle rejection
- Initial and runtime constraint enforcement
- v2 market fixtures, including social influence, budget pressure, queue pressure, deterministic seed behavior, and max-events limits
- v3 typed-layer validation fixtures
- v3 particle motion
- v3 atom bonding through pair systems and bounded relation creation
- v3 relation expiration
- v3 deterministic seeded branching
- v3 child event emission and causal links
- v3 relation-bound and max-events runtime failures

## CLI

The build produces a `noisiax` executable.

Validate a scenario:

```sh
./build/noisiax validate path.yaml
```

Compile a scenario:

```sh
./build/noisiax compile path.yaml
```

Run a scenario with the default report:

```sh
./build/noisiax run path.yaml
```

Run with detailed trace output:

```sh
./build/noisiax run path.yaml --trace full --output result.json
```

### v4 Composition and Experiments

Resolve a scenario that uses v4 `imports` composition into a canonical YAML file:

```sh
./build/noisiax resolve scenario.yaml --output canonical.yaml
```

Run a v4 experiment (seeds, variants, overrides, overlays, and aggregate metrics):

```sh
./build/noisiax experiment experiment.yaml --output-dir out --trace none
```

Supported `run` options:

```text
--trace none|events|decisions|full
--output <result.json>
--max-time <float>
--max-events <int>
--seed <u64>
```

Detailed JSON output includes the validation/runtime report, events, decisions, state changes, final v2 agent/shop state, and v3 typed traces/final state when available.

## Public C++ API

Include the umbrella header:

```cpp
#include <noisiax/noisiax.hpp>
```

Validate, compile, and run:

```cpp
auto report = noisiax::validate_scenario("path.yaml");

auto compiled = noisiax::compile_scenario("path.yaml");

noisiax::RunOptions options;
options.trace_level = noisiax::TraceLevel::FULL;
options.max_events = 1000;

auto result = noisiax::run_scenario_detailed(compiled, options);
```

Useful API entrypoints:

- `validate_scenario(filepath)`
- `validate_scenario_from_string(yaml_content)`
- `compile_scenario(filepath)`
- `compile_scenario(definition)`
- `run_scenario(filepath)`
- `run_scenario(compiled)`
- `run_scenario_detailed(filepath, options)`
- `run_scenario_detailed(compiled, options)`
- `save_checkpoint(state, scenario_id, filepath)`
- `load_checkpoint(filepath, state)`
- `version()`
- `name()`

## v3 YAML Example

This minimal v3 scenario defines particles with `Position` and `Velocity` components. A tick-driven system updates each particle position deterministically.

```yaml
scenario_id: "v3_particle_motion"
schema_version: "1.0.0"
master_seed: 123
goal_statement: "Move particles with deterministic ticks."
assumptions:
  - assumption_id: "assume_motion"
    category: "test"
    description: "Particles update position each tick."
    rationale: "Exercise typed_layer component writes."
    confidence_level: REJECT

typed_layer:
  world:
    duration: 2.0
    time_unit: "ticks"
    max_event_count: 1000
    tick_interval: 1.0

  component_types:
    - component_type_id: "Position"
      fields:
        x: FLOAT
        y: FLOAT
    - component_type_id: "Velocity"
      fields:
        vx: FLOAT
        vy: FLOAT

  entity_types:
    - entity_type_id: "particle"
      components: ["Position", "Velocity"]

  event_types:
    - event_type_id: "tick"
      payload_fields:
        dt: FLOAT

  entities:
    - entity_id: "p1"
      entity_type: "particle"
      components:
        Position:
          x: 0.0
          y: 0.0
        Velocity:
          vx: 1.0
          vy: 0.5

  systems:
    - system_id: "move_particles"
      triggered_by: ["tick"]
      kind: "per_entity"
      entity_type: "particle"
      writes:
        - target: "Position.x"
          expr: "Position.x + Velocity.vx * event.dt"
        - target: "Position.y"
          expr: "Position.y + Velocity.vy * event.dt"
```

## v3 Expression Model

Typed-layer expressions are intentionally bounded and deterministic.

They currently support:

- Numeric arithmetic
- Comparisons
- Boolean logic
- Component field references such as `Position.x`
- Pair-system references such as `other.Position.x`
- Event metadata and payload references such as `event.timestamp`, `event.priority`, and `event.dt`
- Runtime functions: `min`, `max`, `clamp`, `abs`, `sqrt`, and `rng`

Example:

```yaml
where: "sqrt((Position.x - other.Position.x) * (Position.x - other.Position.x)) < 1.0"
```

Example deterministic random branch:

```yaml
writes:
  - target: "Branch.flag"
    expr: "rng(\"branch\", 0, 1) > 0.5"
```

## Project Layout

```text
include/noisiax/                 Public headers
src/                             Library, runtime, compiler, validator, serializer, and CLI implementation
tests/                           Integration and regression tests
plans/                           Historical implementation plans
cmake/                           CMake package configuration
```

Important headers:

- `include/noisiax/noisiax.hpp`: public umbrella API
- `include/noisiax/schema/scenario_schema.hpp`: v1, v2, and v3 schema structs
- `include/noisiax/validation/scenario_validator.hpp`: validation API
- `include/noisiax/compiler/scenario_compiler.hpp`: compiled runtime artifacts
- `include/noisiax/engine/simulation_engine.hpp`: core runtime
- `include/noisiax/engine/agent_runtime.hpp`: v2 runtime
- `include/noisiax/engine/typed_runtime.hpp`: v3 runtime
- `include/noisiax/serialization/yaml_serializer.hpp`: YAML and report serialization

## Current Status

Plan v3 is represented in the codebase as the optional `typed_layer`.

Implemented pieces include:

- Generic typed schema definitions
- YAML parsing and serialization for typed-layer sections
- Validation for references, duplicate IDs, typed field values, invalid writes, layer exclusivity, relation bounds, and event payload fields
- Compilation into stable typed-layer handles and buffers
- Deterministic v3 runtime dispatch
- Tick scheduling through `world.tick_interval`
- Initial events
- Per-entity, pair, and per-relation systems
- Component writes
- Relation creation and expiration
- Event emission with causal parent links
- Deterministic RNG and random-draw tracing
- Typed final-state snapshots and state fingerprints
- CLI reporting for typed-layer compile/run status
- Regression fixtures for the main v3 behavior promised by the plan

Known boundaries:

- Runtime schema mutation is not supported.
- Entities, components, relation types, event types, and payload shapes must be declared up front.
- The expression language is deliberately limited.
- The runtime is single-process and CPU-only.
- v3 does not replace the v2 `agent_layer`; both layers coexist and are routed independently.
