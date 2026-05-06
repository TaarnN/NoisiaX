#pragma once

#include "noisiax/schema/scenario_schema.hpp"
#include "noisiax/validation/scenario_validator.hpp"
#include "noisiax/extensions/expression_registry.hpp"
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
 * @brief Compiled typed-layer field storage for a single component field.
 */
struct CompiledTypedComponentFieldStorage {
    std::string field_name;
    schema::TypedFieldType type = schema::TypedFieldType::FLOAT;
    std::variant<std::vector<int64_t>, std::vector<double>, std::vector<bool>, std::vector<std::string>> initial_buffer;
};

/**
 * @brief Compiled component type tables for v3 typed layer.
 */
struct CompiledTypedComponentType {
    std::string component_type_id;
    std::vector<CompiledTypedComponentFieldStorage> fields;
    std::map<std::string, std::size_t> field_index;
    std::vector<int32_t> entity_to_instance;
    std::vector<int32_t> instance_to_entity;
};

/**
 * @brief Compiled entity type tables for v3 typed layer.
 */
struct CompiledTypedEntityType {
    std::string entity_type_id;
    std::vector<std::size_t> component_type_indices;
};

/**
 * @brief Compiled relation type tables for v3 typed layer.
 */
struct CompiledTypedRelationType {
    std::string relation_type_id;
    bool directed = false;
    std::optional<std::size_t> max_per_entity;
    std::optional<std::size_t> max_total;
    std::vector<schema::ComponentFieldSchema> payload_fields;
    std::map<std::string, std::size_t> payload_field_index;
};

/**
 * @brief Compiled relation instance record.
 */
struct CompiledTypedRelationInstance {
    std::size_t relation_type_index = 0;
    std::size_t source_entity_index = 0;
    std::size_t target_entity_index = 0;
    std::optional<double> expires_at;
    std::map<std::string, schema::TypedScalarValue> payload;
};

/**
 * @brief Compiled event type tables for v3 typed layer.
 */
struct CompiledTypedEventType {
    std::string event_type_id;
    std::vector<schema::ComponentFieldSchema> payload_fields;
    std::map<std::string, std::size_t> payload_field_index;
};

/**
 * @brief Compiled initial event instance.
 */
struct CompiledTypedInitialEvent {
    std::size_t event_type_index = 0;
    double timestamp = 0.0;
    int priority = 0;
    std::string event_handle;
    std::map<std::string, schema::TypedScalarValue> payload;
};

enum class CompiledTypedSystemKind {
    PER_ENTITY,
    PAIR,
    PER_RELATION
};

enum class CompiledTypedEntityRole {
    SELF,
    OTHER
};

struct CompiledTypedSystemWrite {
    CompiledTypedEntityRole role = CompiledTypedEntityRole::SELF;
    std::size_t component_type_index = 0;
    std::size_t field_index = 0;
    schema::TypedFieldType field_type = schema::TypedFieldType::FLOAT;
    std::string expr;
    std::optional<std::string> when;
};

struct CompiledTypedSystemCreateRelation {
    std::size_t relation_type_index = 0;
    CompiledTypedEntityRole source_role = CompiledTypedEntityRole::SELF;
    CompiledTypedEntityRole target_role = CompiledTypedEntityRole::OTHER;
    std::optional<std::string> expires_after;
    std::map<std::string, std::string> payload_exprs;
    std::optional<std::string> when;
};

struct CompiledTypedSystemEmitEvent {
    std::size_t event_type_index = 0;
    std::optional<std::string> timestamp;
    int priority = 0;
    std::map<std::string, std::string> payload_exprs;
    std::optional<std::string> when;
};

/**
 * @brief Compiled typed system definition.
 */
struct CompiledTypedSystem {
    std::string system_id;
    CompiledTypedSystemKind kind = CompiledTypedSystemKind::PER_ENTITY;
    std::vector<std::size_t> trigger_event_type_indices;
    std::optional<std::size_t> entity_type_index;
    std::optional<std::size_t> relation_type_index;
    std::optional<std::string> where;
    std::vector<CompiledTypedSystemWrite> writes;
    std::vector<CompiledTypedSystemCreateRelation> create_relations;
    std::vector<CompiledTypedSystemEmitEvent> emit_events;
};

/**
 * @brief Compiled typed-layer artifact for v3 runtime.
 */
struct CompiledTypedLayer {
    schema::TypedWorldDescriptor world;
    std::vector<CompiledTypedComponentType> component_types;
    std::vector<CompiledTypedEntityType> entity_types;
    std::vector<std::string> entity_ids;
    std::vector<std::size_t> entity_type_index_by_entity;
    std::map<std::string, std::size_t> component_type_index;
    std::map<std::string, std::size_t> entity_type_index;
    std::map<std::string, std::size_t> entity_index;
    std::vector<CompiledTypedRelationType> relation_types;
    std::map<std::string, std::size_t> relation_type_index;
    std::vector<CompiledTypedRelationInstance> relations;
    std::vector<CompiledTypedEventType> event_types;
    std::map<std::string, std::size_t> event_type_index;
    std::vector<CompiledTypedInitialEvent> initial_events;
    std::vector<CompiledTypedSystem> systems;
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
    std::optional<CompiledTypedLayer> typed_layer;
    
    // Pre-registered propagation functions mapping
    std::map<std::string, std::function<void(double&, const double&, double)>> propagation_functions;

    // Expression functions for v3 typed runtime (built-ins + extensions).
    extensions::ExpressionFunctionRegistry expression_functions;
    
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
