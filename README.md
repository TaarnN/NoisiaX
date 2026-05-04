# NoisiaX

NoisiaX is a deterministic, scenario-first C++20 runtime for executing structured YAML scenarios through a strict pipeline:

`YAML -> validate -> compile -> run`

Version: `1.0.0`

## Features

- Strong scenario schema and validation gates
- Deterministic compilation into runtime artifacts
- Event-driven runtime with push invalidation + pull recompute
- Optional v2 `agent_layer` runtime for deterministic market-style agent simulation
- Deterministic seed streams with traceable draws for movement, decisions, social effects, and service jitter
- Detailed run API with event/decision/state-change traces and final state snapshots
- Runtime constraint enforcement
- Checkpoint save/load with scenario metadata
- Public API and CLI entrypoints

## Build

```sh
cmake -S . -B build
cmake --build build
```

`yaml-cpp` is resolved automatically:

- Uses an installed `yaml-cpp` package when available
- Falls back to fetching `yaml-cpp` (`yaml-cpp-0.8.0`) during configure

## Test

```sh
ctest --test-dir build --output-on-failure
```

## CLI

The build produces a `noisiax` executable with:

```sh
noisiax validate <scenario.yaml>
noisiax compile <scenario.yaml>
noisiax run <scenario.yaml>
noisiax run <scenario.yaml> --trace full --output result.json
```

## Project Layout

- `include/noisiax/` public headers (schema, validation, compiler, engine, scheduler, serialization, API)
- `src/` implementation
- `scenarios/` fixture scenarios (`happy_path.yaml`, `failure_cycle.yaml`)
- `tests/` integration and regression tests
