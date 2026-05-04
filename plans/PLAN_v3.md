# NoisiaX Plan v3: Generic Typed Simulation Layer

## Summary
v3 will generalize NoisiaX beyond the v2 market-specific `agent_layer` by adding a new optional `typed_layer` for user-defined simulation types. Users will be able to define their own entity types, component schemas, relations, events, and deterministic systems, so scenarios can model domains like particles, atoms, materials, organisms, markets, networks, or other small-world simulations without pretending everything is a person/shop/item.

v3 keeps the v2 strengths: deterministic event ordering, `master_seed` randomness, traceable random draws, causal trace output, validation-first YAML, compiled integer handles, and bounded runtime safety limits.

## Key Changes
- Add `typed_layer` as the v3 runtime extension. A scenario may use either `typed_layer` or `agent_layer`, but not both.
- Keep v1 and v2 compatibility unchanged. Runtime routing becomes: `typed_layer` -> v3 runtime, else `agent_layer` -> v2 runtime, else v1 runtime.
- Add generic schema sections inside `typed_layer`:
  - `world`: `duration`, `time_unit`, `max_event_count`, optional `tick_interval`.
  - `component_types`: reusable data shapes with typed fields.
  - `entity_types`: user-defined types composed from components.
  - `entities`: instances of user-defined types with initial component values.
  - `relation_types`: typed links between entities, optionally directed and bounded.
  - `relations`: initial relation instances.
  - `event_types`: declared runtime event names and payload fields.
  - `initial_events`: events scheduled at simulation start.
  - `systems`: deterministic rules that read/write components, create/remove relations, and emit events.
- Add generic final output:
  - `TypedFinalStateSnapshot`: final entity components, relation records, world summary, and state fingerprint.
  - `SystemTrace`: system executions, matched entities/relations, reads, writes, emitted events, and random draws.
  - Existing `RunResult` gains optional typed-layer trace/final-state fields while preserving existing v1/v2 members.

## Implementation Changes
- Validation:
  - Reject duplicate IDs across each typed-layer namespace.
  - Verify all component, entity type, entity, relation type, relation, event type, and system references.
  - Verify field values match declared field types: `INTEGER`, `FLOAT`, `BOOLEAN`, `STRING`.
  - Reject writes to undeclared fields and relation creation beyond configured bounds.
  - Reject scenarios containing both `agent_layer` and `typed_layer`.
- Compilation:
  - Compile all string IDs into stable integer handles.
  - Store component fields in SoA-style buffers grouped by component type and field.
  - Compile systems into read/write handle lists plus parsed expression programs.
  - Compile relation stores with stable source/target indexes and optional expiration time.
- Runtime:
  - Reuse the deterministic scheduler model from v2: timestamp, priority, event handle, generated ordinal.
  - Execute systems triggered by each event in declaration order.
  - Iterate entities and relations by compiled index order.
  - Apply writes after each system execution so system order is explicit and deterministic.
  - Support deterministic expression evaluation with arithmetic, comparisons, boolean logic, `min`, `max`, `clamp`, `abs`, `sqrt`, distance helpers, event payload refs, component refs, relation refs, and traceable `rng(stream_key, min, max)`.
  - Support bounded dynamic overlays through relation creation, relation expiration, event emission, and component writes; do not allow arbitrary schema mutation at runtime.

## Test Plan
- Add v3 validation fixtures:
  - Valid custom particle scenario.
  - Invalid unknown component reference.
  - Invalid field type mismatch.
  - Invalid system write to undeclared field.
  - Invalid scenario with both `agent_layer` and `typed_layer`.
- Add v3 runtime fixtures:
  - `v3_particle_motion.yaml`: particles with position/velocity updated by a tick system.
  - `v3_atom_bonding.yaml`: atom entities form bounded bond relations when distance/charge rules match.
  - `v3_relation_decay.yaml`: temporary relations expire deterministically.
  - `v3_seeded_branching.yaml`: same seed produces identical trace/fingerprint; different seed changes at least one branch.
  - `v3_event_emission.yaml`: a system emits child events and preserves deterministic causal links.
- Regression tests:
  - v1 scenarios still validate/compile/run unchanged.
  - v2 `agent_layer` scenarios still validate/compile/run unchanged.
  - v3 detailed output includes events, system traces, state changes, random draws, final typed snapshot, and state fingerprint.
  - `max_events` and relation bounds halt cleanly with clear runtime errors.

## Assumptions
- v3 remains C++20, single-process, CPU-only, YAML-authored, and deterministic by default.
- v3 is not a full scripting VM, physics engine, AI connector, internet data layer, or large distributed simulator.
- The first v3 goal is expressive generic modeling for small-to-medium deterministic simulations, not maximum performance at city/world scale.
- `agent_layer` remains as a specialized v2 layer; v3 does not rewrite it immediately.
- Runtime schema mutation is out of scope. Users can create entities/relations/events at runtime only within types declared up front.
