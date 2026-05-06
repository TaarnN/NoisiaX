# NoisiaX v5 Platform RFC + Implementation Plan

## 1. v5 Definition & Vision
v5 makes NoisiaX an extension-friendly deterministic simulation platform without relaxing the strict scenario contract.

It solves two problems:
- **Authoring ergonomics:** users can write natural higher-level YAML through declared extensions, custom blocks, hookable names, and better typed expressions.
- **Developer extensibility:** developers can add DSL transforms, expression functions, propagation functions, typed-system behavior, v2 policies, experiment metrics, and overlays through public registries instead of editing core schemas.

The invariant remains: **same canonical scenario + extension set + seed + runtime options => identical output, trace, manifest, and fingerprint**.

## 2. Architecture
Pipeline:
`YAML authoring -> import resolve -> extension discovery/compat check -> extension pre-validation -> extension transforms -> canonicalize -> strict core deserialize/validate -> compile with registries -> run -> manifest/trace`

Key rules:
- `imports` stay first-class and run before extension transforms so fragments can declare `extensions`, `hooks`, and custom extension blocks.
- Extension blocks are only legal in the authoring boundary. They must be removed or lowered before the final core schema boundary.
- The final canonical core YAML must contain only strict core fields plus normalized hook references that the v5 schema explicitly allows.
- Unknown top-level or nested fields still fail at final schema parse/validation.
- Canonicalization sorts maps, normalizes numeric/string output, resolves imports/namespaces, normalizes symbol IDs, records extension versions, and emits stable `canonical_yaml` plus `canonical_hash`.

Determinism rules:
- Extension hooks receive only NoisiaX-provided context, typed inputs, scenario config, and deterministic RNG streams.
- No wall-clock time, filesystem, network, global mutable state, threads, unordered iteration, or external random sources.
- Any sandboxed external input, if ever allowed later, must be explicitly captured in the canonical manifest and hash input.

## 3. DSL / Schema Proposal
Add authoring-only top-level sections:
```yaml
schema_version: "5.0.0"
extensions:
  - id: "acme.market"
    version: "1.2.0"
    compatibility: ">=1.2,<2.0"
    config:
      currency: "THB"

hooks:
  defaults:
    expression_namespace: "std"
    propagation_namespace: "core"

acme.market:
  market:
    agents: 100
    shops: [...]
```

Rules:
- `extensions` declares allowed extension namespaces, versions, compatibility ranges, and optional typed config.
- Extension-owned custom blocks use the extension namespace as a top-level key, e.g. `acme.market:`.
- Each extension validates its own custom block before transform.
- Each extension transform lowers custom YAML into canonical core `variables`, `typed_layer`, `agent_layer`, `experiment`, or hook references.
- Extension transforms must be deterministic, ordered by resolved import order then `extensions` order, and must emit transform provenance into the canonicalization report.
- Extension IDs use reverse-DNS or domain-like namespaces where practical, but symbol IDs use `namespace::name`.

## 4. Hookable Names & Registries
Unified **Symbol ID** format:
- `core::linear_scale`
- `std::sqrt`
- `std::rng_uniform`
- `acme.market::bid_price_v2`
- `acme.market@1::bid_price` is allowed only in manifests and diagnostics; YAML normally selects versions through `extensions`.

Collision rules:
- Fully qualified IDs are required in canonical YAML.
- Short names may be accepted only as authoring sugar when unambiguous under declared defaults.
- Extensions cannot override `core::*` or `std::*`.
- Scenario-local aliases are allowed only if canonicalization expands them.

Before:
```yaml
dependency_edges:
  - edge_id: demand_to_price
    source_variable: demand
    target_variable: price
    propagation_function_id: linear_scale
    weight: 1.2
```

After canonical lowering:
```yaml
dependency_edges:
  - edge_id: demand_to_price
    source_variable: demand
    target_variable: price
    behavior:
      id: "core::linear_scale"
      config: {}
    weight: 1.2
```

Typed system example:
```yaml
systems:
  - system_id: price_update
    kind: "acme.market::auction_round"
    impl:
      id: "acme.market::clearing_price_v2"
      config:
        spread: 0.04
```

Expression example:
```yaml
expr: 'std::clamp(acme.market::bid_price_v2(self.Price.base), 0, 100)'
```

Registry API sketch:
```cpp
noisiax::ExtensionRegistry registry;
registry.register_extension(acme_market_extension());

registry.expression_functions().register_function({
    .id = "acme.market::bid_price_v2",
    .signature = "(FLOAT) -> FLOAT",
    .config_schema = {},
    .implementation = bid_price_fn
});

registry.propagation_functions().register_function({
    .id = "core::linear_scale",
    .signature = "(target: FLOAT, source: FLOAT, weight: FLOAT, config: OBJECT) -> FLOAT"
});

registry.system_kinds().register_kind({
    .id = "acme.market::auction_round",
    .compile = compile_auction_round,
    .execute = execute_auction_round
});
```

Validation:
- Unknown hook IDs fail validation unless registered by a declared extension.
- Hook configs are typed-validated during extension pre-validation or compile.
- Hook input/output signatures are checked during compile.
- Every hook invocation has trace metadata: `symbol_id`, resolved extension version, config hash, inputs, outputs, RNG draws, and deterministic error if it fails.

Migration:
- Existing built-ins map to canonical IDs:
  - `linear_scale` -> `core::linear_scale`
  - `additive` -> `core::additive`
  - `min`, `max`, `sqrt`, `clamp`, `abs` -> `std::*`
  - `rng` -> `std::rng_uniform`
  - v2 default policies -> `core.agent::default_*`
  - metric kinds `runtime_stat`, `typed_field_final` -> `core.experiment::*`
- Old fields remain accepted in v5 authoring with warnings and are auto-lowered to hook form.

## 5. Type System Improvements
Add deterministic, serializable typed-layer field/value types:
- `INTEGER`, `FLOAT`, `BOOLEAN`, `STRING` remain.
- `ENTITY_REF<T?>`: references a known entity, optionally constrained by entity type.
- `RELATION_REF<T?>`: references a relation instance/type when persisted.
- `TIME`: fixed-point simulation timestamp, serialized as decimal string or integer ticks.
- `DURATION`: fixed-point non-negative interval.
- `ENUM<name>`: declared enum with canonical string values.
- `LIST<T>`: bounded list for canonical data, not arbitrary runtime mutation.
- `SYMBOL_ID`: validated hook/function/system/metric symbol.

Rules:
- Prefer fixed-point internal representation for `TIME`/`DURATION` to avoid floating drift.
- Numeric conversions: `INTEGER -> FLOAT` allowed; `FLOAT -> INTEGER` requires explicit function.
- `ENTITY_REF`, `ENUM`, `SYMBOL_ID`, and typed configs validate against known registries/schemas at compile time.
- Serialization must be stable and round-trippable through canonical YAML.

## 6. Expression Language v5
Roadmap:
- Parse expressions into a bounded AST during compile instead of repeatedly parsing strings at runtime.
- Resolve function names through `ExpressionFunctionRegistry`.
- Add standard functions under `std::*`: math, comparisons, bounded string helpers, enum equality, safe casts, `count`, `exists`, `relation_count`, `has_relation`, and relation payload accessors.
- Keep expressions side-effect free except explicit RNG functions supplied by NoisiaX.

RNG rules:
- RNG APIs require a string stream key and typed distribution function, e.g. `std::rng_uniform("adoption", 0.0, 1.0)`.
- Effective stream key is canonicalized as `scenario_hash/system_id/event_id/symbol_id/user_key`.
- Draw order must be stable under deterministic system iteration.
- Traces include stream key, draw index, raw value, distribution, interpreted result, and hook call site.

Error model:
- Compile-time errors for unknown identifiers/functions, invalid arity, invalid hook config, missing fields, unsafe conversions, and impossible target writes.
- Runtime deterministic errors for domain violations dependent on state, such as division by zero or relation bounds.
- Errors include stable path/call-site diagnostics.

## 7. Extension API: C++ Surface
Primary public API:
```cpp
struct ExtensionDescriptor {
    SymbolId id;
    SemVer version;
    std::string noisiax_compat;
};

class INoisiaXExtension {
public:
    virtual ExtensionDescriptor descriptor() const = 0;
    virtual void register_symbols(ExtensionRegistry&) = 0;
    virtual void validate_authoring_block(const YamlNode&, DiagnosticSink&) const = 0;
    virtual TransformResult transform(const TransformContext&, const YamlNode&) const = 0;
};
```

Registries:
- `ExtensionRegistry`
- `ExpressionFunctionRegistry`
- `PropagationFunctionRegistry`
- `TypedSystemRegistry`
- `AgentPolicyRegistry`
- `ExperimentMetricRegistry`
- `OverlaySamplerRegistry`
- `AuthoringTransformRegistry`

Loading options:
- MVP: compile-time linked extensions registered by C++ code. Safest, deterministic, easiest to test.
- Later: runtime shared-library plugins behind an explicit loader flag. More flexible, higher ABI/security complexity.
- Later still: manifest-only extension packages for pure YAML transforms, no native code.

Safety constraints:
- Extension APIs expose deterministic context objects, stable allocators/iteration helpers, typed RNG, and trace sinks.
- Native plugins must declare capabilities and pass conformance tests.
- Unbounded loops/recursion are disallowed through bounded callbacks, max operation budgets, and runtime guards.

## 8. Experiments v5
Changes:
- Experiment parsing also uses the extension registry.
- Metrics become hookable names:
```yaml
metrics:
  - metric_id: revenue_p95
    kind: "acme.market::percentile_metric"
    config:
      source: "shop.revenue"
      percentile: 95
```

Rules:
- Metric kinds must declare typed config, input needs, output type, aggregation behavior, and trace/report serialization.
- Overlays can use registered sampler kinds with typed params.
- The experiment manifest records canonical experiment YAML hash, canonical base scenario hash, extension set, hook symbol table, seeds, overlays, sampled values, run options, metric outputs, and final fingerprints.
- A run is reproducible from manifest plus extension binaries matching declared IDs/versions.

## 9. Migration Plan
Compatibility:
- v1/v2/v3/v4 scenarios continue to validate, compile, and run.
- v4 `imports` remain supported and become the first stage of v5 authoring resolution.
- Existing propagation IDs, expression function names, system kinds, policy refs, and metric kinds are accepted as legacy short names.
- Canonical v5 output expands legacy names to `core::*`/`std::*` symbols.

Deprecations:
- Warn on unqualified hook names in `schema_version >= 5.0.0`.
- Warn on legacy `propagation_function_id` once `behavior: { id, config }` exists.
- Keep compatibility shims through v5; consider removal no earlier than v7.

## 10. Milestones
MVP:
- Add `SymbolId`, version parsing, extension descriptors, and central registries.
- Move current built-ins into `core::*`/`std::*` registrations.
- Add canonicalization pipeline: resolve imports, lower legacy names, emit canonical YAML/hash.
- Add `extensions` section and authoring-only custom block validation/transform.
- Add expression function registry and deterministic RNG tracing.
- Acceptance: all existing tests pass; legacy scenarios canonicalize to v5 symbols; one sample extension adds a function, propagation function, custom block, and metric.

Milestone 2:
- Add hook config schemas and compile-time signature checking.
- Add `behavior: { id, config }`, `impl: { id, config }`, and metric hook configs.
- Add typed `TIME`, `DURATION`, `ENTITY_REF`, `ENUM`, and `SYMBOL_ID`.
- Acceptance: invalid hook IDs/configs fail before runtime; canonical hashes stable across repeated resolve.

Milestone 3:
- Add registered typed system kinds/operators and v2 policy hooks.
- Compile expressions to AST bytecode-like bounded plans, not a general VM.
- Add richer relation query standard library.
- Acceptance: extension system kind runs deterministically with full trace and operation budget enforcement.

Milestone 4:
- Optional runtime plugin loader with explicit enable flag and conformance suite.
- Extension package manifests and ABI/version checks.
- Acceptance: incompatible or nondeterministic plugins fail conformance before use.

## 11. Risks & Mitigations
- Scope creep: make MVP registry/canonicalization first; defer dynamic loading.
- Plugin nondeterminism: restrict extension context, provide deterministic RNG only, require conformance tests and trace hooks.
- Validation complexity: keep authoring validation separate from final strict core schema validation.
- Performance regressions: compile expressions and hook configs once; avoid per-event YAML/registry lookup.
- Canonical hash churn: define one canonical emitter and golden snapshot tests before adding many transforms.
- Namespace confusion: require fully qualified canonical IDs and make short names authoring-only.

## 12. Acceptance Criteria
Concrete gates:
- Determinism: repeated runs of the same canonical scenario, extension set, seed, and options produce identical traces, manifests, metrics, and fingerprints.
- Seed sensitivity: changing only seed changes at least one RNG-dependent trace in fixtures designed for stochastic behavior.
- Canonical hash stability: repeated resolve/canonicalize produces byte-identical YAML and hash across runs.
- Strict validation: unknown final-schema fields fail; unknown extension blocks fail unless declared; unknown hook IDs fail unless registered.
- Extension conformance: sample extension passes tests for custom block lowering, expression function, propagation function, system kind/operator, metric kind, RNG tracing, and invalid config diagnostics.
- Migration: all existing v1/v2/v3/v4 fixtures pass unchanged; legacy names lower to `core::*` or `std::*` with warnings.
- Performance budgets: canonicalization overhead under 10% of compile time for medium fixtures; runtime hook dispatch overhead under 5% versus equivalent built-in baseline; no unbounded extension execution.
