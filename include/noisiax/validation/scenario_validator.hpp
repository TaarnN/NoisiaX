#pragma once

#include "noisiax/schema/scenario_schema.hpp"
#include <string>
#include <vector>
#include <set>

namespace noisiax::validation {

/**
 * @brief Validation result for a single check
 */
struct ValidationResult {
    bool passed;
    schema::ValidationLevel level;
    std::string rule_name;
    std::string message;
    std::optional<std::string> suggestion;
};

/**
 * @brief Complete validation report
 */
struct ValidationReport {
    std::string scenario_id;
    bool overall_passed;
    std::vector<ValidationResult> results;
    std::vector<std::string> errors;
    std::vector<std::string> warnings;
    std::vector<std::string> info_messages;
    
    void add_result(const ValidationResult& result);
};

/**
 * @brief ScenarioValidator - enforces gate order and validation levels
 * 
 * Implements strict authoring pipeline validation:
 * Goal → Assumptions → Entities → Variables → Dependencies → Constraints → Events → Evaluation
 */
class ScenarioValidator {
public:
    ScenarioValidator() = default;
    ~ScenarioValidator() = default;
    
    /**
     * @brief Validate a complete scenario definition
     * @param scenario The scenario to validate
     * @return ValidationReport with all results
     */
    ValidationReport validate(const schema::ScenarioDefinition& scenario);
    
    /**
     * @brief Set validation policy for specific rule categories
     * @param rule_category Category of rules
     * @param level Validation level to apply
     */
    void set_validation_level(const std::string& rule_category, schema::ValidationLevel level);
    
private:
    std::map<std::string, schema::ValidationLevel> validation_policies_;
    
    // Gate validation methods (must be called in order)
    ValidationResult validate_goal(const schema::ScenarioDefinition& scenario);
    ValidationResult validate_assumptions(const schema::ScenarioDefinition& scenario);
    ValidationResult validate_entities(const schema::ScenarioDefinition& scenario);
    ValidationResult validate_variables(const schema::ScenarioDefinition& scenario);
    ValidationResult validate_dependencies(const schema::ScenarioDefinition& scenario);
    ValidationResult validate_constraints(const schema::ScenarioDefinition& scenario);
    ValidationResult validate_events(const schema::ScenarioDefinition& scenario);
    ValidationResult validate_evaluation_criteria(const schema::ScenarioDefinition& scenario);
    
    // Cross-cutting validation checks
    ValidationResult check_required_fields(const schema::ScenarioDefinition& scenario);
    ValidationResult check_unknown_fields(const schema::ScenarioDefinition& scenario);
    ValidationResult check_anti_patterns(const schema::ScenarioDefinition& scenario);
    ValidationResult check_static_cycles(const schema::ScenarioDefinition& scenario);
    ValidationResult check_type_compatibility(const schema::ScenarioDefinition& scenario);
    ValidationResult check_initial_constraints(const schema::ScenarioDefinition& scenario);
    ValidationResult check_orphan_variables(const schema::ScenarioDefinition& scenario);
    ValidationResult check_fan_out_limits(const schema::ScenarioDefinition& scenario);
    ValidationResult check_depth_limits(const schema::ScenarioDefinition& scenario);
    
    // Helper methods
    bool detect_cycle(const schema::ScenarioDefinition& scenario);
    bool dfs_visit(const std::string& node,
                   const std::map<std::string, std::vector<std::string>>& adjacency,
                   std::set<std::string>& white,
                   std::set<std::string>& gray,
                   std::set<std::string>& black);
    std::set<std::string> get_all_variable_ids(const schema::ScenarioDefinition& scenario);
    std::set<std::string> get_all_entity_ids(const schema::ScenarioDefinition& scenario);
};

} // namespace noisiax::validation
