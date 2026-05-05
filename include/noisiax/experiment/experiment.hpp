#pragma once

#include "noisiax/noisiax.hpp"

#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <variant>
#include <vector>

namespace noisiax::experiment {

struct ResolveOptions {
    bool validate_resolved = true;
};

struct ImportSpec {
    std::string path;
    std::optional<std::string> namespace_prefix;

    bool operator==(const ImportSpec& other) const = default;
};

struct CompositionReport {
    bool success = true;
    std::vector<std::string> errors;
    std::vector<std::string> warnings;
    std::vector<ImportSpec> imports;
    std::vector<std::string> resolved_files;
    std::string source_hash;
    std::string resolved_hash;

    bool operator==(const CompositionReport& other) const = default;
};

struct ResolvedScenario {
    schema::ScenarioDefinition scenario;
    std::string canonical_yaml;
    std::string source_hash;
    std::string resolved_hash;
    CompositionReport report;

    bool operator==(const ResolvedScenario& other) const = default;
};

ResolvedScenario resolve_scenario(const std::string& path, const ResolveOptions& options = {});

struct JsonValue {
    using Array = std::vector<JsonValue>;
    using Object = std::map<std::string, JsonValue>;
    std::variant<std::monostate, bool, int64_t, double, std::string, Array, Object> data;

    JsonValue() : data(std::monostate{}) {}
    JsonValue(bool v) : data(v) {}
    JsonValue(int64_t v) : data(v) {}
    JsonValue(double v) : data(v) {}
    JsonValue(std::string v) : data(std::move(v)) {}
    JsonValue(const char* v) : data(std::string(v)) {}
    JsonValue(Array v) : data(std::move(v)) {}
    JsonValue(Object v) : data(std::move(v)) {}

    bool operator==(const JsonValue& other) const = default;
};

enum class OverrideOp {
    REPLACE,
    APPEND,
    MERGE
};

struct TypedFieldTarget {
    std::string entity_id;
    std::string component_type_id;
    std::string field_name;

    bool operator==(const TypedFieldTarget& other) const = default;
};

struct ScenarioOverrideTarget {
    std::optional<std::string> json_pointer;
    std::optional<TypedFieldTarget> typed_field;

    bool operator==(const ScenarioOverrideTarget& other) const = default;
};

struct ScenarioOverride {
    std::string override_id;
    OverrideOp op = OverrideOp::REPLACE;
    ScenarioOverrideTarget target;
    JsonValue value;

    bool operator==(const ScenarioOverride& other) const = default;
};

struct SeedPlan {
    std::vector<uint64_t> seeds;
    bool operator==(const SeedPlan& other) const = default;
};

enum class SamplerType {
    UNIFORM_INT,
    UNIFORM_FLOAT,
    BERNOULLI,
    CHOICE,
    WEIGHTED_CHOICE
};

struct StochasticOverlay {
    std::string overlay_id;
    SamplerType sampler = SamplerType::UNIFORM_FLOAT;
    ScenarioOverrideTarget target;
    JsonValue params;

    bool operator==(const StochasticOverlay& other) const = default;
};

struct ExperimentMetric {
    std::string metric_id;
    std::string kind;  // runtime_stat | typed_field_final
    std::string key;
    std::optional<TypedFieldTarget> typed_field;

    bool operator==(const ExperimentMetric& other) const = default;
};

struct ExperimentRunSpec {
    std::string run_id;
    std::string variant_id;
    uint64_t seed = 0;

    bool operator==(const ExperimentRunSpec& other) const = default;
};

struct RunManifest {
    std::string run_id;
    std::string variant_id;
    uint64_t seed = 0;
    std::string base_scenario_hash;
    std::string resolved_scenario_hash;
    TraceLevel trace_level = TraceLevel::NONE;
    double max_time = -1.0;
    std::size_t max_events = 0;
    bool include_final_state = true;
    bool include_causal_graph = true;
    std::vector<ScenarioOverride> overrides;
    bool success = false;
    std::vector<std::string> errors;
    std::map<std::string, std::string> metrics;
    std::map<std::string, JsonValue> overlay_samples;
    std::string final_fingerprint;

    bool operator==(const RunManifest& other) const = default;
};

struct AggregateMetric {
    std::string metric_id;
    std::size_t count = 0;
    double min = 0.0;
    double max = 0.0;
    double mean = 0.0;
    double stddev = 0.0;

    bool operator==(const AggregateMetric& other) const = default;
};

struct ExperimentResult {
    std::string experiment_id;
    std::string noisiax_version;
    std::string base_scenario_path;
    std::string base_scenario_hash;
    std::vector<RunManifest> runs;
    std::vector<AggregateMetric> aggregates;
    std::string output_dir;

    bool operator==(const ExperimentResult& other) const = default;
};

struct ExperimentDefinition {
    std::string experiment_id;
    std::string base_scenario;
    std::vector<ScenarioOverride> global_overrides;

    struct VariantDefinition {
        std::string variant_id;
        std::vector<ScenarioOverride> overrides;
        std::vector<StochasticOverlay> overlays;

        bool operator==(const VariantDefinition& other) const = default;
    };

    std::vector<VariantDefinition> variants;
    SeedPlan seed_plan;
    std::vector<StochasticOverlay> overlays;
    std::vector<ExperimentMetric> metrics;
    bool fail_fast = false;
    bool write_run_details = true;

    bool operator==(const ExperimentDefinition& other) const = default;
};

struct ExperimentOptions {
    std::string output_dir;
    RunOptions run_options;
};

ExperimentResult run_experiment(const std::string& path, const ExperimentOptions& options);
ExperimentResult run_experiment(const ExperimentDefinition& definition, const ExperimentOptions& options);

}  // namespace noisiax::experiment
