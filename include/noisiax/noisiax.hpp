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

struct RunResult {
    schema::ScenarioReport report;
    FinalStateSnapshot final_state;
    std::vector<EventTrace> events;
    std::vector<DecisionTrace> decisions;
    std::vector<StateChangeTrace> state_changes;

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

} // namespace noisiax
