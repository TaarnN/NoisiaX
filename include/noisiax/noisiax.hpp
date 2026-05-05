#pragma once

#include "noisiax/schema/scenario_schema.hpp"
#include "noisiax/validation/scenario_validator.hpp"
#include "noisiax/compiler/scenario_compiler.hpp"
#include "noisiax/engine/simulation_engine.hpp"
#include "noisiax/scheduler/event_scheduler.hpp"
#include "noisiax/serialization/yaml_serializer.hpp"

#include <string>
#include <memory>
#include <optional>
#include <vector>
#include <map>
#include <cstdint>

namespace noisiax {

enum class TraceLevel {
    NONE,
    EVENTS,
    DECISIONS,
    FULL
};

struct RunOptions {
    double max_time = -1.0;
    std::size_t max_events = 100000;
    TraceLevel trace_level = TraceLevel::NONE;
    bool include_final_state = true;
    bool include_causal_graph = true;
    std::optional<uint64_t> seed_override;
};

struct RandomDrawTrace {
    std::string stream_key;
    uint64_t draw_index = 0;
    uint64_t raw_u64 = 0;
    double normalized = 0.0;
    std::string interpreted_result;

    bool operator==(const RandomDrawTrace& other) const = default;
};

struct EventTrace {
    uint64_t event_id = 0;
    std::string event_type;
    std::string event_handle;
    double timestamp = 0.0;
    int priority = 0;
    std::string actor_id;
    std::string shop_id;
    std::string item_id;
    std::vector<uint64_t> causal_parent_event_ids;
    std::map<std::string, std::string> payload;
    std::vector<RandomDrawTrace> random_draws;

    bool operator==(const EventTrace& other) const = default;
};

struct DecisionCandidateTrace {
    std::string item_id;
    std::string shop_id;
    double weight = 0.0;
    double base_appeal = 0.0;
    double preference = 0.0;
    double hunger_bonus = 0.0;
    double social_signal = 0.0;
    double tag_association = 0.0;
    double random_jitter = 0.0;
    double price_penalty = 0.0;
    double distance_penalty = 0.0;
    double queue_penalty = 0.0;
    bool affordable = false;
    bool in_stock = false;

    bool operator==(const DecisionCandidateTrace& other) const = default;
};

struct DecisionTrace {
    uint64_t decision_id = 0;
    uint64_t event_id = 0;
    std::string decision_type;
    std::string agent_id;
    std::vector<DecisionCandidateTrace> candidates;
    double random_draw = 0.0;
    double random_draw_scaled = 0.0;
    std::string selected_item_id;
    std::string selected_shop_id;
    std::vector<uint64_t> causal_parent_event_ids;
    std::vector<RandomDrawTrace> random_draws;

    bool operator==(const DecisionTrace& other) const = default;
};

struct StateChangeTrace {
    uint64_t event_id = 0;
    std::string entity_type;
    std::string entity_id;
    std::string field_name;
    std::string old_value;
    std::string new_value;
    std::vector<uint64_t> causal_parent_event_ids;

    bool operator==(const StateChangeTrace& other) const = default;
};

struct AgentFinalState {
    std::string agent_id;
    std::vector<std::string> visited_locations;
    std::vector<std::string> purchases;
    double money_spent = 0.0;
    double time_walking = 0.0;
    double time_talking = 0.0;
    double time_queueing = 0.0;
    std::size_t observed_purchases = 0;
    double influence_received = 0.0;
    std::vector<uint64_t> causal_parent_event_ids;

    bool operator==(const AgentFinalState& other) const = default;
};

struct ShopFinalState {
    std::string shop_id;
    std::map<std::string, int64_t> remaining_stock;
    std::size_t queue_size = 0;

    bool operator==(const ShopFinalState& other) const = default;
};

struct FinalStateSnapshot {
    std::vector<AgentFinalState> agents;
    std::vector<ShopFinalState> shops;
    std::map<std::string, std::string> summary;

    bool operator==(const FinalStateSnapshot& other) const = default;
};

struct TypedEntityComponentFinalState {
    std::string component_type_id;
    std::map<std::string, schema::TypedScalarValue> fields;

    bool operator==(const TypedEntityComponentFinalState& other) const = default;
};

struct TypedEntityFinalState {
    std::string entity_id;
    std::string entity_type_id;
    std::vector<TypedEntityComponentFinalState> components;

    bool operator==(const TypedEntityFinalState& other) const = default;
};

struct TypedRelationFinalState {
    std::string relation_type_id;
    std::string source_entity_id;
    std::string target_entity_id;
    std::optional<double> expires_at;
    std::map<std::string, schema::TypedScalarValue> payload;

    bool operator==(const TypedRelationFinalState& other) const = default;
};

struct TypedFinalStateSnapshot {
    std::vector<TypedEntityFinalState> entities;
    std::vector<TypedRelationFinalState> relations;
    std::map<std::string, std::string> summary;
    std::string state_fingerprint;

    bool operator==(const TypedFinalStateSnapshot& other) const = default;
};

struct TypedSystemWriteTrace {
    std::string entity_id;
    std::string component_type_id;
    std::string field_name;
    std::string old_value;
    std::string new_value;

    bool operator==(const TypedSystemWriteTrace& other) const = default;
};

struct TypedSystemCreateRelationTrace {
    std::string relation_type_id;
    std::string source_entity_id;
    std::string target_entity_id;

    bool operator==(const TypedSystemCreateRelationTrace& other) const = default;
};

struct TypedSystemEmitEventTrace {
    std::string event_type_id;
    uint64_t event_id = 0;

    bool operator==(const TypedSystemEmitEventTrace& other) const = default;
};

struct TypedSystemTrace {
    uint64_t event_id = 0;
    std::string system_id;
    double timestamp = 0.0;
    std::vector<std::string> matched_entity_ids;
    std::vector<TypedSystemWriteTrace> writes;
    std::vector<TypedSystemCreateRelationTrace> created_relations;
    std::vector<TypedSystemEmitEventTrace> emitted_events;
    std::vector<RandomDrawTrace> random_draws;

    bool operator==(const TypedSystemTrace& other) const = default;
};

struct RunResult {
    schema::ScenarioReport report;
    FinalStateSnapshot final_state;
    std::vector<EventTrace> events;
    std::vector<DecisionTrace> decisions;
    std::vector<StateChangeTrace> state_changes;
    std::optional<TypedFinalStateSnapshot> typed_final_state;
    std::vector<TypedSystemTrace> typed_system_traces;

    bool operator==(const RunResult& other) const = default;
};

/**
 * @brief Validate a scenario definition from YAML file
 * @param filepath Path to the YAML scenario file
 * @return Validation report with errors, warnings, and info messages
 */
schema::ScenarioReport validate_scenario(const std::string& filepath);

/**
 * @brief Validate a scenario definition from string content
 * @param yaml_content The YAML content to validate
 * @return Validation report with errors, warnings, and info messages
 */
schema::ScenarioReport validate_scenario_from_string(const std::string& yaml_content);

/**
 * @brief Compile a validated scenario into runtime artifacts
 * @param filepath Path to the YAML scenario file (must be valid)
 * @return Compiled scenario artifact, or throws on validation failure
 */
compiler::CompiledScenario compile_scenario(const std::string& filepath);

/**
 * @brief Compile a scenario from already-parsed definition
 * @param scenario The validated scenario definition
 * @return Compiled scenario artifact
 */
compiler::CompiledScenario compile_scenario(const schema::ScenarioDefinition& scenario);

/**
 * @brief Run a compiled scenario to completion
 * @param compiled The compiled scenario
 * @return Runtime report with execution results
 */
schema::ScenarioReport run_scenario(const compiler::CompiledScenario& compiled);

/**
 * @brief Run a scenario from YAML file (validates, compiles, runs)
 * @param filepath Path to the YAML scenario file
 * @return Runtime report with execution results
 */
schema::ScenarioReport run_scenario(const std::string& filepath);

/**
 * @brief Run a compiled scenario and collect detailed trace output.
 */
RunResult run_scenario_detailed(const compiler::CompiledScenario& compiled,
                                const RunOptions& options = {});

/**
 * @brief Run a scenario from YAML file and collect detailed trace output.
 */
RunResult run_scenario_detailed(const std::string& filepath,
                                const RunOptions& options = {});

/**
 * @brief Save a checkpoint of simulation state
 * @param state The current simulation state
 * @param scenario_id The scenario identifier
 * @param filepath Output checkpoint file path
 * @return true if successful
 */
bool save_checkpoint(const engine::SimulationState& state, 
                     const std::string& scenario_id,
                     const std::string& filepath);

/**
 * @brief Load a checkpoint and restore simulation state
 * @param filepath Input checkpoint file path
 * @param state The simulation state to restore into
 * @return Scenario ID from checkpoint
 */
std::string load_checkpoint(const std::string& filepath, engine::SimulationState& state);

/**
 * @brief Library version information
 */
[[nodiscard]] constexpr std::string_view version() noexcept {
    return "1.0.0";
}

/**
 * @brief Library name
 */
[[nodiscard]] std::string_view name() noexcept;

// ============================================================================
// V4 Experiment APIs and Types
// ============================================================================

namespace experiment {

/**
 * @brief Options for resolving a scenario with imports and composition.
 */
struct ResolveOptions {
    std::vector<std::string> import_paths;  // Additional search paths for imports
    bool validate_after_resolve = true;     // Run validation after composition
    bool output_canonical = true;           // Include canonical YAML in result
};

/**
 * @brief Result of scenario resolution/composition.
 */
struct ResolvedScenario {
    schema::ScenarioDefinition definition;
    std::string canonical_yaml;
    std::string source_hash;       // Hash of original source files
    std::string resolved_hash;     // Hash of resolved/merged scenario
    std::vector<std::string> imported_files;
    std::vector<std::string> errors;
    bool success = false;
};

/**
 * @brief Override mode for parameterized variants.
 */
enum class OverrideMode {
    REPLACE,   // Replace value entirely
    APPEND,    // Append to list/string
    MERGE      // Merge maps/objects
};

/**
 * @brief A parameterized override for experiment variants.
 * 
 * Supports two target types:
 * 1. JSON Pointer path (e.g., "/variables/0/default_value")
 * 2. Typed-layer component field by {entity_id, component_type_id, field_name}
 */
struct ScenarioOverride {
    std::string override_id;
    
    // Target specification (mutually exclusive)
    std::string json_pointer_path;  // JSON Pointer RFC 6901
    std::string entity_id;          // For typed-layer overrides
    std::string component_type_id;  // For typed-layer overrides
    std::string field_name;         // For typed-layer overrides
    
    OverrideMode mode = OverrideMode::REPLACE;
    schema::TypedScalarValue value;
};

/**
 * @brief Seed plan for deterministic ensembles.
 */
struct SeedPlan {
    enum class PlanType {
        EXPLICIT_LIST,   // Use provided seeds exactly
        RANGE,           // Generate seeds in range
        HASH_DERIVED     // Derive from base seed + index
    };
    
    PlanType plan_type = PlanType::EXPLICIT_LIST;
    std::vector<uint64_t> explicit_seeds;
    uint64_t base_seed = 0;
    std::size_t num_runs = 1;
    std::size_t seed_stride = 1;
};

/**
 * @brief Stochastic sampler types for experiment overlays.
 */
enum class SamplerType {
    UNIFORM_INT,
    UNIFORM_FLOAT,
    BERNOULLI,
    CHOICE,
    WEIGHTED_CHOICE
};

/**
 * @brief A stochastic overlay sampler definition.
 */
struct StochasticSampler {
    std::string sampler_id;
    SamplerType type = SamplerType::UNIFORM_FLOAT;
    
    // Parameters based on type
    int64_t int_min = 0;
    int64_t int_max = 1;
    double float_min = 0.0;
    double float_max = 1.0;
    double probability = 0.5;  // For bernoulli
    std::vector<schema::TypedScalarValue> choices;  // For choice/weighted_choice
    std::vector<double> weights;  // For weighted_choice
    
    // Target for applying sampled value
    std::string json_pointer_path;
    std::string entity_id;
    std::string component_type_id;
    std::string field_name;
};

/**
 * @brief Experiment-level stochastic overlay.
 */
struct StochasticOverlay {
    std::string overlay_id;
    std::vector<StochasticSampler> samplers;
};

/**
 * @brief Metric extraction configuration.
 */
struct ExperimentMetric {
    std::string metric_id;
    std::string description;
    
    // Extraction target
    std::string source = "final_state";  // final_state, events, decisions, summary
    std::string field_path;              // Path to extract within source
    std::string aggregation = "mean";    // mean, sum, min, max, count, stddev
    
    // Optional filtering
    std::string filter_expression;       // Simple expression to filter values
};

/**
 * @brief Complete experiment definition.
 */
struct ExperimentDefinition {
    std::string experiment_id;
    std::string schema_version = "4.0.0";
    
    // Base scenario reference
    std::string base_scenario_path;
    std::optional<schema::ScenarioDefinition> inline_base_scenario;
    
    // Composition parameters (imports, namespaces)
    std::vector<std::string> imports;
    std::map<std::string, std::string> import_namespaces;
    
    // Variants (parameterized overrides)
    std::vector<ScenarioOverride> variants;
    
    // Seed plan for ensemble runs
    SeedPlan seed_plan;
    
    // Stochastic overlays
    std::vector<StochasticOverlay> overlays;
    
    // Metrics to extract
    std::vector<ExperimentMetric> metrics;
    
    // Output settings
    std::string output_dir = ".";
    TraceLevel trace_level = TraceLevel::NONE;
    bool write_run_manifests = true;
    bool write_summary = true;
    bool fail_fast = false;
    
    // Runtime options passed through to simulation
    double max_time = -1.0;
    std::size_t max_events = 100000;
};

/**
 * @brief Specification for a single experiment run.
 */
struct ExperimentRunSpec {
    std::string run_id;
    std::size_t run_index = 0;
    uint64_t seed = 0;
    std::vector<ScenarioOverride> applied_overrides;
    std::map<std::string, schema::TypedScalarValue> overlay_samples;
    std::string resolved_scenario_hash;
};

/**
 * @brief Result from a single experiment run.
 */
struct ExperimentRunResult {
    ExperimentRunSpec spec;
    RunResult run_result;
    std::map<std::string, double> extracted_metrics;
    bool success = false;
    std::string error_message;
    double elapsed_seconds = 0.0;
};

/**
 * @brief Aggregated metric statistics across runs.
 */
struct AggregateMetric {
    std::string metric_id;
    std::size_t count = 0;
    double min_val = 0.0;
    double max_val = 0.0;
    double mean = 0.0;
    double stddev = 0.0;
    double sum = 0.0;
};

/**
 * @brief Manifest for a single run.
 */
struct RunManifest {
    std::string noisiax_version;
    std::string experiment_id;
    ExperimentRunSpec run_spec;
    std::string base_scenario_hash;
    std::string resolved_scenario_hash;
    RunOptions runtime_options;
    bool success = false;
    std::string final_fingerprint;
    std::map<std::string, double> metrics;
};

/**
 * @brief Report on scenario composition.
 */
struct CompositionReport {
    std::string scenario_id;
    std::vector<std::string> imported_fragments;
    std::map<std::string, std::string> namespace_mappings;
    std::vector<std::string> resolved_ids;
    std::vector<std::string> duplicate_errors;
    std::vector<std::string> unresolved_references;
    std::vector<std::string> circular_import_errors;
    std::string canonical_hash;
    bool success = false;
};

/**
 * @brief Complete experiment result.
 */
struct ExperimentResult {
    ExperimentDefinition definition;
    std::string experiment_id;
    std::string base_scenario_hash;
    
    // Run results
    std::vector<ExperimentRunResult> runs;
    
    // Aggregated metrics
    std::map<std::string, AggregateMetric> aggregated_metrics;
    
    // Summary statistics
    std::size_t total_runs = 0;
    std::size_t successful_runs = 0;
    std::size_t failed_runs = 0;
    double total_elapsed_seconds = 0.0;
    
    // Reports
    CompositionReport composition_report;
    std::vector<RunManifest> run_manifests;
    
    // Output paths
    std::string manifest_path;
    std::string summary_path;
    
    bool success = false;
    std::string error_message;
};

/**
 * @brief Resolve a scenario with imports and composition.
 * @param path Path to the scenario YAML file
 * @param options Resolution options
 * @return ResolvedScenario with canonical form and hashes
 */
ResolvedScenario resolve_scenario(const std::string& path, const ResolveOptions& options = {});

/**
 * @brief Run an experiment from a YAML definition file.
 * @param path Path to the experiment YAML file
 * @param options Optional runtime overrides
 * @return ExperimentResult with all run results and aggregates
 */
ExperimentResult run_experiment(const std::string& path, const RunOptions& options = {});

/**
 * @brief Run an experiment from an in-memory definition.
 * @param definition The experiment definition
 * @param options Optional runtime overrides
 * @return ExperimentResult with all run results and aggregates
 */
ExperimentResult run_experiment(const ExperimentDefinition& definition, const RunOptions& options = {});

} // namespace experiment

} // namespace noisiax
