#pragma once

#include "noisiax/schema/scenario_schema.hpp"
#include "noisiax/validation/scenario_validator.hpp"
#include <string>
#include <map>
#include <memory>
#include <functional>
#include <variant>
#include <optional>

namespace noisiax::compiler {

/**
 * @brief Parameter handle - runtime reference to a variable
 */
struct ParameterHandle {
    std::string variable_id;
    std::size_t buffer_offset;
    schema::VariableType type;
    bool is_stale;
    
    bool operator==(const ParameterHandle& other) const = default;
};

/**
 * @brief Static adjacency list entry for dependency graph
 */
struct AdjacencyEntry {
    std::string target_variable;
    std::string propagation_function_id;
    double weight;
    std::size_t target_buffer_offset;
};

/**
 * @brief Event queue entry for deterministic scheduling
 */
struct ScheduledEvent {
    double timestamp;
    int priority;
    std::string event_handle;  // Lexicographic key for tie-breaking
    schema::EventDescriptor descriptor;
    bool triggered;
    
    bool operator<(const ScheduledEvent& other) const {
        if (timestamp != other.timestamp) return timestamp < other.timestamp;
        if (priority != other.priority) return priority > other.priority;  // Higher priority first
        return event_handle < other.event_handle;  // Lexicographic ordering
    }
};

/**
 * @brief Constraint program - compiled constraint representation
 */
struct ConstraintProgram {
    std::string constraint_id;
    std::vector<std::string> variable_ids;
    std::vector<std::size_t> variable_offsets;
    std::string compiled_expression;
    schema::ValidationLevel enforcement_level;
    std::string error_message;
};

/**
 * @brief Compiled index tables for optional v2 agent layer.
 */
struct CompiledAgentLayer {
    schema::AgentWorldDescriptor world;
    std::vector<schema::LocationDescriptor> locations;
    std::vector<schema::ItemDescriptor> items;
    std::vector<schema::ShopDescriptor> shops;
    std::vector<schema::AgentDescriptor> agents;
    std::vector<schema::PolicyDescriptor> policies;
    std::map<std::string, std::size_t> location_index;
    std::map<std::string, std::size_t> item_index;
    std::map<std::string, std::size_t> shop_index;
    std::map<std::string, std::size_t> agent_index;
    std::map<std::string, std::size_t> policy_index;
    std::vector<std::vector<std::size_t>> shops_by_item_index;
};

/**
 * @brief Compiled scenario artifact - output from ScenarioCompiler
 */
struct CompiledScenario {
    std::string scenario_id;
    uint64_t master_seed;
    
    // Runtime artifacts
    std::map<std::string, std::variant<int64_t, double, std::string, bool>> initial_values;
    std::map<std::string, ParameterHandle> parameter_handles;
    std::map<std::string, std::vector<AdjacencyEntry>> adjacency_lists;  // source -> [targets]
    std::vector<schema::DependencyEdge> dependency_edges;
    std::vector<ScheduledEvent> event_queue;
    std::vector<ConstraintProgram> constraint_programs;
    std::optional<CompiledAgentLayer> agent_layer;
    
    // Pre-registered propagation functions mapping
    std::map<std::string, std::function<void(double&, const double&, double)>> propagation_functions;
    
    // Statistics
    std::size_t total_variables;
    std::size_t total_dependencies;
    std::size_t total_constraints;
    std::size_t total_events;
};

/**
 * @brief ScenarioCompiler - converts validated definitions into runtime artifacts
 * 
 * Produces:
 * - ParameterHandle map for typed SoA buffers
 * - Static adjacency lists for dependency graph
 * - Event queue with stable ordering
 * - Constraint program for runtime evaluation
 */
class ScenarioCompiler {
public:
    ScenarioCompiler() = default;
    ~ScenarioCompiler() = default;
    
    /**
     * @brief Compile a validated scenario definition
     * @param scenario The validated scenario
     * @return CompiledScenario with runtime artifacts
     * @throws std::runtime_error if compilation fails
     */
    CompiledScenario compile(const schema::ScenarioDefinition& scenario);
    
    /**
     * @brief Register a propagation function for use in scenarios
     * @param function_id Unique identifier for the function
     * @param func The propagation function: (target, source, weight) -> new_target
     */
    void register_propagation_function(const std::string& function_id, 
                                       std::function<void(double&, const double&, double)> func);
    
    /**
     * @brief Get list of registered propagation functions
     */
    std::vector<std::string> get_registered_functions() const;
    
private:
    std::map<std::string, std::function<void(double&, const double&, double)>> registered_functions_;
    
    // Compilation helpers
    std::map<std::string, ParameterHandle> build_parameter_handles(const schema::ScenarioDefinition& scenario);
    std::map<std::string, std::vector<AdjacencyEntry>> build_adjacency_lists(
        const schema::ScenarioDefinition& scenario,
        const std::map<std::string, ParameterHandle>& handles);
    std::vector<ScheduledEvent> build_event_queue(const schema::ScenarioDefinition& scenario);
    std::vector<ConstraintProgram> build_constraint_programs(
        const schema::ScenarioDefinition& scenario,
        const std::map<std::string, ParameterHandle>& handles);
    
    // Default propagation functions
    void register_default_functions();
};

} // namespace noisiax::compiler
