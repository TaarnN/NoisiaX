#pragma once

#include <string>
#include <vector>
#include <map>
#include <variant>
#include <optional>
#include <cstdint>

namespace noisiax::schema {

/**
 * @brief Validation level for scenario gates
 */
enum class ValidationLevel {
    REJECT,      // Hard failure - scenario must not proceed
    WARN,        // Warning logged but scenario proceeds
    AUTO_CORRECT // Safe auto-correction with full logging
};

/**
 * @brief Variable type descriptor for strong typing
 */
enum class VariableType {
    INTEGER,
    FLOAT,
    STRING,
    BOOLEAN,
    ENUM,
    LIST
};

/**
 * @brief Scalar field types for the optional v3 typed simulation layer.
 */
enum class TypedFieldType {
    INTEGER,
    FLOAT,
    BOOLEAN,
    STRING
};

using TypedScalarValue = std::variant<int64_t, double, std::string, bool>;

/**
 * @brief Unit specification for variables
 */
struct UnitSpec {
    std::string name;
    std::string symbol;
    std::map<std::string, double> conversion_factors;

    bool operator==(const UnitSpec& other) const = default;
};

/**
 * @brief Entity descriptor - represents a domain entity in the scenario
 */
struct EntityDescriptor {
    std::string entity_id;
    std::string entity_type;
    std::string description;
    std::map<std::string, std::string> attributes;
    
    bool operator==(const EntityDescriptor& other) const = default;
};

/**
 * @brief Variable descriptor - typed parameter with optional units
 */
struct VariableDescriptor {
    std::string variable_id;
    std::string entity_ref;  // Optional: which entity this variable belongs to
    VariableType type;
    std::optional<UnitSpec> unit;
    std::variant<int64_t, double, std::string, bool, std::vector<std::string>> default_value;
    std::optional<std::variant<int64_t, double, std::string, bool>> min_value;
    std::optional<std::variant<int64_t, double, std::string, bool>> max_value;
    std::vector<std::string> enum_values;  // For ENUM type
    std::string description;
    
    bool operator==(const VariableDescriptor& other) const = default;
};

/**
 * @brief Dependency edge - directed relationship between variables
 */
struct DependencyEdge {
    std::string edge_id;
    std::string source_variable;  // The variable that drives change
    std::string target_variable;  // The variable that depends on source
    std::string propagation_function_id;  // ID of pre-registered function
    double weight;  // Optional weighting factor
    std::map<std::string, std::string> metadata;
    
    bool operator==(const DependencyEdge& other) const = default;
};

/**
 * @brief Constraint rule - hard or soft constraints on variables
 */
struct ConstraintRule {
    std::string constraint_id;
    std::vector<std::string> affected_variables;
    std::string constraint_expression;  // Expression language TBD
    ValidationLevel enforcement_level;
    std::string error_message;
    std::optional<std::string> correction_hint;
    
    bool operator==(const ConstraintRule& other) const = default;
};

/**
 * @brief Event descriptor - scheduled or triggered events
 */
struct EventDescriptor {
    std::string event_id;
    std::string event_type;  // SCHEDULED, TRIGGERED, CONDITIONAL
    double timestamp;  // For scheduled events
    int priority;  // For tie-breaking
    std::string trigger_condition;  // For triggered/conditional events
    std::map<std::string, std::string> event_payload;
    std::string description;
    
    bool operator==(const EventDescriptor& other) const = default;
};

/**
 * @brief Evaluation criterion - metrics for scenario assessment
 */
struct EvaluationCriterion {
    std::string criterion_id;
    std::string metric_name;
    std::string aggregation_method;  // SUM, AVG, MIN, MAX, FINAL
    std::vector<std::string> input_variables;
    std::optional<double> target_value;
    std::optional<double> threshold_min;
    std::optional<double> threshold_max;
    std::string description;
    
    bool operator==(const EvaluationCriterion& other) const = default;
};

/**
 * @brief Assumption - documented assumptions for the scenario
 */
struct Assumption {
    std::string assumption_id;
    std::string category;
    std::string description;
    std::string rationale;
    std::optional<std::string> source;
    ValidationLevel confidence_level;
    
    bool operator==(const Assumption& other) const = default;
};

/**
 * @brief Metadata - optional scenario metadata
 */
struct ScenarioMetadata {
    std::string author;
    std::string created_date;
    std::string modified_date;
    std::string version;
    std::map<std::string, std::string> custom_fields;
    
    bool operator==(const ScenarioMetadata& other) const = default;
};

/**
 * @brief World-level settings for the optional v2 agent simulation layer.
 */
struct AgentWorldDescriptor {
    double duration = 0.0;
    std::string time_unit = "minutes";
    double map_width = 0.0;
    double map_height = 0.0;
    double default_walking_speed = 1.0;
    std::size_t max_event_count = 100000;

    bool operator==(const AgentWorldDescriptor& other) const = default;
};

/**
 * @brief A named location with deterministic coordinates.
 */
struct LocationDescriptor {
    std::string location_id;
    std::string location_type;
    double x = 0.0;
    double y = 0.0;

    bool operator==(const LocationDescriptor& other) const = default;
};

/**
 * @brief Purchasable item definition for the market layer.
 */
struct ItemDescriptor {
    std::string item_id;
    std::string category;
    std::vector<std::string> tags;
    double base_appeal = 1.0;

    bool operator==(const ItemDescriptor& other) const = default;
};

/**
 * @brief Per-shop inventory row.
 */
struct ShopInventoryEntry {
    std::string item_id;
    double price = 0.0;
    int64_t stock = 0;

    bool operator==(const ShopInventoryEntry& other) const = default;
};

/**
 * @brief Shop definition for v2 agent runtime.
 */
struct ShopDescriptor {
    std::string shop_id;
    std::string location_ref;
    std::vector<ShopInventoryEntry> inventory;
    double service_time = 1.0;
    int64_t queue_capacity = 1;

    bool operator==(const ShopDescriptor& other) const = default;
};

/**
 * @brief Agent definition for v2 market runtime.
 */
struct AgentDescriptor {
    std::string agent_id;
    std::string start_location_ref;
    double budget = 0.0;
    double movement_speed = 0.0;
    double hunger = 0.0;
    double social_susceptibility = 0.0;
    std::map<std::string, double> traits;
    std::map<std::string, double> preferences;
    std::size_t memory_slots = 8;
    std::string policy_ref;

    bool operator==(const AgentDescriptor& other) const = default;
};

/**
 * @brief Named behavior policy selector for agents.
 */
struct PolicyDescriptor {
    std::string policy_id;
    std::string movement_policy = "default";
    std::string observation_policy = "default";
    std::string conversation_policy = "default";
    std::string purchase_policy = "default";

    bool operator==(const PolicyDescriptor& other) const = default;
};

/**
 * @brief Optional v2 market simulation layer.
 */
struct AgentLayerDefinition {
    AgentWorldDescriptor world;
    std::vector<LocationDescriptor> locations;
    std::vector<ItemDescriptor> items;
    std::vector<ShopDescriptor> shops;
    std::vector<AgentDescriptor> agents;
    std::vector<PolicyDescriptor> policies;

    bool operator==(const AgentLayerDefinition& other) const = default;
};

/**
 * @brief World-level settings for the optional v3 typed simulation layer.
 */
struct TypedWorldDescriptor {
    double duration = 0.0;
    std::string time_unit = "ticks";
    std::size_t max_event_count = 100000;
    std::optional<double> tick_interval;

    bool operator==(const TypedWorldDescriptor& other) const = default;
};

/**
 * @brief A named field inside a component type.
 */
struct ComponentFieldSchema {
    std::string field_name;
    TypedFieldType type = TypedFieldType::FLOAT;

    bool operator==(const ComponentFieldSchema& other) const = default;
};

/**
 * @brief Component type definition - reusable typed data shapes.
 */
struct ComponentTypeDefinition {
    std::string component_type_id;
    std::vector<ComponentFieldSchema> fields;

    bool operator==(const ComponentTypeDefinition& other) const = default;
};

/**
 * @brief Entity type definition - user-defined types composed from components.
 */
struct TypedEntityTypeDefinition {
    std::string entity_type_id;
    std::vector<std::string> components;

    bool operator==(const TypedEntityTypeDefinition& other) const = default;
};

/**
 * @brief Entity instance definition for typed layer.
 *
 * components: component_type_id -> field_name -> scalar value.
 */
struct TypedEntityInstanceDefinition {
    std::string entity_id;
    std::string entity_type_ref;
    std::map<std::string, std::map<std::string, TypedScalarValue>> components;

    bool operator==(const TypedEntityInstanceDefinition& other) const = default;
};

/**
 * @brief Relation type schema for typed layer.
 */
struct RelationTypeDefinition {
    std::string relation_type_id;
    bool directed = false;
    std::optional<std::size_t> max_per_entity;
    std::optional<std::size_t> max_total;
    std::vector<ComponentFieldSchema> payload_fields;

    bool operator==(const RelationTypeDefinition& other) const = default;
};

/**
 * @brief Relation instance definition for typed layer.
 */
struct RelationInstanceDefinition {
    std::string relation_type_ref;
    std::string source_entity_ref;
    std::string target_entity_ref;
    std::optional<double> expires_at;
    std::map<std::string, TypedScalarValue> payload;

    bool operator==(const RelationInstanceDefinition& other) const = default;
};

/**
 * @brief Event type schema for typed layer.
 */
struct TypedEventTypeDefinition {
    std::string event_type_id;
    std::vector<ComponentFieldSchema> payload_fields;

    bool operator==(const TypedEventTypeDefinition& other) const = default;
};

/**
 * @brief Initial event instance for typed layer.
 */
struct TypedInitialEvent {
    std::string event_type_ref;
    double timestamp = 0.0;
    int priority = 0;
    std::string event_handle;
    std::map<std::string, TypedScalarValue> payload;

    bool operator==(const TypedInitialEvent& other) const = default;
};

/**
 * @brief System write action for typed layer.
 */
struct TypedSystemWrite {
    std::string target;
    std::string expr;
    std::optional<std::string> when;

    bool operator==(const TypedSystemWrite& other) const = default;
};

/**
 * @brief System relation creation action for typed layer.
 */
struct TypedSystemCreateRelation {
    std::string relation_type_ref;
    std::string source;
    std::string target;
    std::optional<std::string> expires_after;
    std::map<std::string, std::string> payload_exprs;
    std::optional<std::string> when;

    bool operator==(const TypedSystemCreateRelation& other) const = default;
};

/**
 * @brief System event emission action for typed layer.
 */
struct TypedSystemEmitEvent {
    std::string event_type_ref;
    std::optional<std::string> timestamp;
    int priority = 0;
    std::map<std::string, std::string> payload_exprs;
    std::optional<std::string> when;

    bool operator==(const TypedSystemEmitEvent& other) const = default;
};

/**
 * @brief System definition for typed layer.
 */
struct TypedSystemDefinition {
    std::string system_id;
    std::vector<std::string> triggered_by;
    std::string kind = "per_entity";  // per_entity | pair | per_relation
    std::optional<std::string> entity_type_ref;
    std::optional<std::string> relation_type_ref;
    std::optional<std::string> where;
    std::vector<TypedSystemWrite> writes;
    std::vector<TypedSystemCreateRelation> create_relations;
    std::vector<TypedSystemEmitEvent> emit_events;

    bool operator==(const TypedSystemDefinition& other) const = default;
};

/**
 * @brief Optional v3 typed simulation layer.
 */
struct TypedLayerDefinition {
    TypedWorldDescriptor world;
    std::vector<ComponentTypeDefinition> component_types;
    std::vector<TypedEntityTypeDefinition> entity_types;
    std::vector<TypedEntityInstanceDefinition> entities;
    std::vector<RelationTypeDefinition> relation_types;
    std::vector<RelationInstanceDefinition> relations;
    std::vector<TypedEventTypeDefinition> event_types;
    std::vector<TypedInitialEvent> initial_events;
    std::vector<TypedSystemDefinition> systems;

    bool operator==(const TypedLayerDefinition& other) const = default;
};

/**
 * @brief Complete scenario definition - the root schema object
 */
struct ScenarioDefinition {
    // Required fields
    std::string scenario_id;
    std::string schema_version;
    uint64_t master_seed;
    
    // Core pipeline stages (required order: Goal → Assumptions → Entities → Variables → Dependencies → Constraints → Events → Evaluation)
    std::string goal_statement;
    std::vector<Assumption> assumptions;
    std::vector<EntityDescriptor> entities;
    std::vector<VariableDescriptor> variables;
    std::vector<DependencyEdge> dependency_edges;
    std::vector<ConstraintRule> constraints;
    std::vector<EvaluationCriterion> evaluation_criteria;
    
    // Optional fields for v1
    std::vector<EventDescriptor> events;
    std::optional<AgentLayerDefinition> agent_layer;
    std::optional<TypedLayerDefinition> typed_layer;
    std::optional<ScenarioMetadata> metadata;
    
    // Explicitly excluded in v1 (documented for clarity):
    // - stochastic_overlays
    // - runtime dynamic edge creation
    // - AI connector APIs
    
    bool operator==(const ScenarioDefinition& other) const = default;
};

/**
 * @brief Scenario report - output from validation/compilation/runtime
 */
struct ScenarioReport {
    std::string scenario_id;
    std::string report_type;  // VALIDATION, COMPILATION, RUNTIME
    bool success;
    std::vector<std::string> errors;
    std::vector<std::string> warnings;
    std::vector<std::string> info_messages;
    std::map<std::string, std::string> statistics;
    std::optional<std::string> checkpoint_path;
    
    bool operator==(const ScenarioReport& other) const = default;
};

} // namespace noisiax::schema
