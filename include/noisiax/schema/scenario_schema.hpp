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
 * @brief Unit specification for variables
 */
struct UnitSpec {
    std::string name;
    std::string symbol;
    std::map<std::string, double> conversion_factors;
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
