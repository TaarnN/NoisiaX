#include "noisiax/validation/scenario_validator.hpp"
#include <algorithm>
#include <cctype>
#include <cmath>
#include <functional>
#include <queue>
#include <set>
#include <string_view>
#include <stdexcept>

namespace {

using noisiax::schema::VariableType;
using NumericResolver = std::function<std::optional<double>(std::string_view)>;

class NumericConstraintParser {
public:
    NumericConstraintParser(std::string_view expression, NumericResolver resolver)
        : expression_(expression), resolver_(std::move(resolver)) {}

    bool evaluate() {
        const bool result = parse_logical_or();
        skip_spaces();
        if (position_ != expression_.size()) {
            throw std::runtime_error("Unexpected trailing token in constraint expression");
        }
        return result;
    }

private:
    std::string_view expression_;
    std::size_t position_ = 0;
    NumericResolver resolver_;

    void skip_spaces() {
        while (position_ < expression_.size() &&
               std::isspace(static_cast<unsigned char>(expression_[position_])) != 0) {
            ++position_;
        }
    }

    bool consume(std::string_view token) {
        skip_spaces();
        if (expression_.substr(position_).starts_with(token)) {
            position_ += token.size();
            return true;
        }
        return false;
    }

    bool parse_logical_or() {
        bool value = parse_logical_and();
        while (consume("||")) {
            value = value || parse_logical_and();
        }
        return value;
    }

    bool parse_logical_and() {
        bool value = parse_comparison();
        while (consume("&&")) {
            value = value && parse_comparison();
        }
        return value;
    }

    bool parse_comparison() {
        const double lhs = parse_additive();

        if (consume(">=")) return lhs >= parse_additive();
        if (consume("<=")) return lhs <= parse_additive();
        if (consume("==")) return lhs == parse_additive();
        if (consume("!=")) return lhs != parse_additive();
        if (consume(">")) return lhs > parse_additive();
        if (consume("<")) return lhs < parse_additive();
        return lhs != 0.0;
    }

    double parse_additive() {
        double value = parse_multiplicative();
        while (true) {
            if (consume("+")) {
                value += parse_multiplicative();
                continue;
            }
            if (consume("-")) {
                value -= parse_multiplicative();
                continue;
            }
            break;
        }
        return value;
    }

    double parse_multiplicative() {
        double value = parse_unary();
        while (true) {
            if (consume("*")) {
                value *= parse_unary();
                continue;
            }
            if (consume("/")) {
                const double divisor = parse_unary();
                if (divisor == 0.0) {
                    throw std::runtime_error("Division by zero in constraint expression");
                }
                value /= divisor;
                continue;
            }
            break;
        }
        return value;
    }

    double parse_unary() {
        if (consume("+")) return parse_unary();
        if (consume("-")) return -parse_unary();
        return parse_primary();
    }

    double parse_primary() {
        skip_spaces();
        if (position_ >= expression_.size()) {
            throw std::runtime_error("Unexpected end of constraint expression");
        }

        if (consume("(")) {
            const double nested = parse_additive();
            if (!consume(")")) {
                throw std::runtime_error("Missing closing ')' in constraint expression");
            }
            return nested;
        }

        const char current = expression_[position_];
        if (std::isdigit(static_cast<unsigned char>(current)) != 0 || current == '.') {
            return parse_number();
        }
        if (std::isalpha(static_cast<unsigned char>(current)) != 0 || current == '_') {
            return parse_identifier();
        }

        throw std::runtime_error("Invalid token in constraint expression");
    }

    double parse_number() {
        skip_spaces();
        const std::size_t start = position_;
        bool saw_digit = false;

        while (position_ < expression_.size() &&
               std::isdigit(static_cast<unsigned char>(expression_[position_])) != 0) {
            saw_digit = true;
            ++position_;
        }

        if (position_ < expression_.size() && expression_[position_] == '.') {
            ++position_;
            while (position_ < expression_.size() &&
                   std::isdigit(static_cast<unsigned char>(expression_[position_])) != 0) {
                saw_digit = true;
                ++position_;
            }
        }

        if (position_ < expression_.size() &&
            (expression_[position_] == 'e' || expression_[position_] == 'E')) {
            const std::size_t exponent_start = position_;
            ++position_;
            if (position_ < expression_.size() &&
                (expression_[position_] == '+' || expression_[position_] == '-')) {
                ++position_;
            }

            bool exponent_digit = false;
            while (position_ < expression_.size() &&
                   std::isdigit(static_cast<unsigned char>(expression_[position_])) != 0) {
                exponent_digit = true;
                ++position_;
            }

            if (!exponent_digit) {
                position_ = exponent_start;
            }
        }

        if (!saw_digit) {
            throw std::runtime_error("Invalid numeric literal in constraint expression");
        }

        const std::string token(expression_.substr(start, position_ - start));
        try {
            return std::stod(token);
        } catch (...) {
            throw std::runtime_error("Invalid numeric literal in constraint expression");
        }
    }

    double parse_identifier() {
        skip_spaces();
        const std::size_t start = position_;
        ++position_;
        while (position_ < expression_.size()) {
            const char c = expression_[position_];
            if (std::isalnum(static_cast<unsigned char>(c)) != 0 || c == '_') {
                ++position_;
                continue;
            }
            break;
        }

        const std::string_view variable = expression_.substr(start, position_ - start);
        auto value = resolver_(variable);
        if (!value.has_value()) {
            throw std::runtime_error("Unknown/non-numeric variable in constraint expression: " +
                                     std::string(variable));
        }
        return *value;
    }
};

std::optional<double> scalar_default_to_numeric(const std::variant<int64_t, double, std::string, bool, std::vector<std::string>>& value,
                                                VariableType type) {
    switch (type) {
        case VariableType::INTEGER:
            if (const auto* v = std::get_if<int64_t>(&value)) {
                return static_cast<double>(*v);
            }
            break;
        case VariableType::FLOAT:
            if (const auto* v = std::get_if<double>(&value)) {
                return *v;
            }
            break;
        case VariableType::BOOLEAN:
            if (const auto* v = std::get_if<bool>(&value)) {
                return *v ? 1.0 : 0.0;
            }
            break;
        default:
            break;
    }
    return std::nullopt;
}

bool looks_like_semver(const std::string& version) {
    int dot_count = 0;
    int digits_in_segment = 0;
    for (const char c : version) {
        if (c == '.') {
            if (digits_in_segment == 0) {
                return false;
            }
            ++dot_count;
            digits_in_segment = 0;
            continue;
        }
        if (std::isdigit(static_cast<unsigned char>(c)) == 0) {
            return false;
        }
        ++digits_in_segment;
    }
    return dot_count == 2 && digits_in_segment > 0;
}

}  // namespace

namespace noisiax::validation {

void ValidationReport::add_result(const ValidationResult& result) {
    results.push_back(result);
    
    if (!result.passed) {
        if (result.level == schema::ValidationLevel::REJECT) {
            errors.push_back(result.message);
            overall_passed = false;
        } else if (result.level == schema::ValidationLevel::WARN) {
            warnings.push_back(result.message);
        }
    }
}

ValidationReport ScenarioValidator::validate(const schema::ScenarioDefinition& scenario) {
    ValidationReport report;
    report.scenario_id = scenario.scenario_id;
    report.overall_passed = true;
    
    // Gate 1: Validate goal statement
    auto goal_result = validate_goal(scenario);
    report.add_result(goal_result);
    
    // Gate 2: Validate assumptions
    auto assumptions_result = validate_assumptions(scenario);
    report.add_result(assumptions_result);

    if (scenario.typed_layer.has_value()) {
        auto typed_layer_result = validate_typed_layer(scenario);
        report.add_result(typed_layer_result);

        auto required_fields_result = check_required_fields(scenario);
        report.add_result(required_fields_result);

        auto unknown_fields_result = check_unknown_fields(scenario);
        report.add_result(unknown_fields_result);

        auto anti_patterns_result = check_anti_patterns(scenario);
        report.add_result(anti_patterns_result);

        return report;
    }
    
    // Gate 3: Validate entities
    auto entities_result = validate_entities(scenario);
    report.add_result(entities_result);
    
    // Gate 4: Validate variables
    auto variables_result = validate_variables(scenario);
    report.add_result(variables_result);
    
    // Gate 5: Validate dependencies
    auto dependencies_result = validate_dependencies(scenario);
    report.add_result(dependencies_result);
    
    // Gate 6: Validate constraints
    auto constraints_result = validate_constraints(scenario);
    report.add_result(constraints_result);
    
    // Gate 7: Validate events
    auto events_result = validate_events(scenario);
    report.add_result(events_result);

    // Gate 7.5: Validate optional v2 agent layer
    auto agent_layer_result = validate_agent_layer(scenario);
    report.add_result(agent_layer_result);
    
    // Gate 8: Validate evaluation criteria
    auto evaluation_result = validate_evaluation_criteria(scenario);
    report.add_result(evaluation_result);
    
    // Cross-cutting checks
    auto required_fields_result = check_required_fields(scenario);
    report.add_result(required_fields_result);
    
    auto unknown_fields_result = check_unknown_fields(scenario);
    report.add_result(unknown_fields_result);
    
    auto anti_patterns_result = check_anti_patterns(scenario);
    report.add_result(anti_patterns_result);
    
    auto cycles_result = check_static_cycles(scenario);
    report.add_result(cycles_result);
    
    auto type_compat_result = check_type_compatibility(scenario);
    report.add_result(type_compat_result);
    
    auto initial_constraints_result = check_initial_constraints(scenario);
    report.add_result(initial_constraints_result);
    
    auto orphan_result = check_orphan_variables(scenario);
    report.add_result(orphan_result);
    
    auto fan_out_result = check_fan_out_limits(scenario);
    report.add_result(fan_out_result);
    
    auto depth_result = check_depth_limits(scenario);
    report.add_result(depth_result);
    
    return report;
}

void ScenarioValidator::set_validation_level(const std::string& rule_category, schema::ValidationLevel level) {
    validation_policies_[rule_category] = level;
}

ValidationResult ScenarioValidator::validate_goal(const schema::ScenarioDefinition& scenario) {
    ValidationResult result;
    result.rule_name = "goal_statement";
    
    if (scenario.goal_statement.empty()) {
        result.passed = false;
        result.level = schema::ValidationLevel::REJECT;
        result.message = "Goal statement cannot be empty";
    } else if (scenario.goal_statement.length() < 10) {
        result.passed = false;
        result.level = schema::ValidationLevel::WARN;
        result.message = "Goal statement is very short, may lack detail";
    } else {
        result.passed = true;
        result.level = schema::ValidationLevel::REJECT;
        result.message = "Goal statement validated";
    }
    
    return result;
}

ValidationResult ScenarioValidator::validate_assumptions(const schema::ScenarioDefinition& scenario) {
    ValidationResult result;
    result.rule_name = "assumptions";
    result.passed = true;
    result.level = schema::ValidationLevel::REJECT;
    
    if (scenario.assumptions.empty()) {
        result.passed = false;
        result.message = "No assumptions defined - assumptions are required in v1";
        return result;
    }
    
    // Check for duplicate assumption IDs
    std::set<std::string> seen_ids;
    for (const auto& assumption : scenario.assumptions) {
        if (seen_ids.count(assumption.assumption_id)) {
            result.passed = false;
            result.message = "Duplicate assumption ID: " + assumption.assumption_id;
            return result;
        }
        seen_ids.insert(assumption.assumption_id);
        
        if (assumption.description.empty()) {
            result.passed = false;
            result.message = "Assumption " + assumption.assumption_id + " has empty description";
            return result;
        }
    }
    
    result.message = "All assumptions validated";
    return result;
}

ValidationResult ScenarioValidator::validate_entities(const schema::ScenarioDefinition& scenario) {
    ValidationResult result;
    result.rule_name = "entities";
    result.passed = true;
    result.level = schema::ValidationLevel::REJECT;
    
    if (scenario.entities.empty()) {
        result.passed = false;
        result.message = "No entities defined - entities are required in v1";
        return result;
    }
    
    // Check for duplicate entity IDs
    std::set<std::string> seen_ids;
    for (const auto& entity : scenario.entities) {
        if (seen_ids.count(entity.entity_id)) {
            result.passed = false;
            result.message = "Duplicate entity ID: " + entity.entity_id;
            return result;
        }
        seen_ids.insert(entity.entity_id);
        
        if (entity.entity_type.empty()) {
            result.passed = false;
            result.message = "Entity " + entity.entity_id + " has empty type";
            return result;
        }
    }
    
    result.message = "All entities validated";
    return result;
}

ValidationResult ScenarioValidator::validate_variables(const schema::ScenarioDefinition& scenario) {
    ValidationResult result;
    result.rule_name = "variables";
    result.passed = true;
    result.level = schema::ValidationLevel::REJECT;
    
    if (scenario.variables.empty()) {
        result.passed = false;
        result.message = "No variables defined - at least one variable is required";
        return result;
    }
    
    // Check for duplicate variable IDs
    std::set<std::string> seen_ids;
    for (const auto& var : scenario.variables) {
        if (seen_ids.count(var.variable_id)) {
            result.passed = false;
            result.message = "Duplicate variable ID: " + var.variable_id;
            return result;
        }
        seen_ids.insert(var.variable_id);
        
        // Check entity reference exists
        if (!var.entity_ref.empty()) {
            bool found = false;
            for (const auto& entity : scenario.entities) {
                if (entity.entity_id == var.entity_ref) {
                    found = true;
                    break;
                }
            }
            if (!found) {
                result.passed = false;
                result.message = "Variable " + var.variable_id + " references non-existent entity: " + var.entity_ref;
                return result;
            }
        }
        
        // Check enum values for ENUM type
        if (var.type == schema::VariableType::ENUM && var.enum_values.empty()) {
            result.passed = false;
            result.message = "ENUM variable " + var.variable_id + " must have enum_values defined";
            return result;
        }
    }
    
    result.message = "All variables validated";
    return result;
}

ValidationResult ScenarioValidator::validate_dependencies(const schema::ScenarioDefinition& scenario) {
    ValidationResult result;
    result.rule_name = "dependencies";
    result.passed = true;
    result.level = schema::ValidationLevel::REJECT;
    
    if (scenario.dependency_edges.empty()) {
        result.passed = false;
        result.message = "No dependency edges defined - dependencies are required in v1";
        return result;
    }
    
    auto all_vars = get_all_variable_ids(scenario);
    
    // Check for duplicate edge IDs and valid variable references
    std::set<std::string> seen_edge_ids;
    for (const auto& edge : scenario.dependency_edges) {
        if (seen_edge_ids.count(edge.edge_id)) {
            result.passed = false;
            result.message = "Duplicate edge ID: " + edge.edge_id;
            return result;
        }
        seen_edge_ids.insert(edge.edge_id);
        
        if (all_vars.find(edge.source_variable) == all_vars.end()) {
            result.passed = false;
            result.message = "Edge " + edge.edge_id + " references non-existent source variable: " + edge.source_variable;
            return result;
        }
        
        if (all_vars.find(edge.target_variable) == all_vars.end()) {
            result.passed = false;
            result.message = "Edge " + edge.edge_id + " references non-existent target variable: " + edge.target_variable;
            return result;
        }
        
        // Self-loop check
        if (edge.source_variable == edge.target_variable) {
            result.passed = false;
            result.message = "Edge " + edge.edge_id + " has self-loop (source equals target)";
            return result;
        }
    }
    
    result.message = "All dependencies validated";
    return result;
}

ValidationResult ScenarioValidator::validate_constraints(const schema::ScenarioDefinition& scenario) {
    ValidationResult result;
    result.rule_name = "constraints";
    result.passed = true;
    result.level = schema::ValidationLevel::REJECT;
    
    if (scenario.constraints.empty()) {
        result.passed = false;
        result.message = "No constraints defined - constraints are required in v1";
        return result;
    }
    
    auto all_vars = get_all_variable_ids(scenario);
    
    // Check for duplicate constraint IDs and valid variable references
    std::set<std::string> seen_ids;
    for (const auto& constraint : scenario.constraints) {
        if (seen_ids.count(constraint.constraint_id)) {
            result.passed = false;
            result.message = "Duplicate constraint ID: " + constraint.constraint_id;
            return result;
        }
        seen_ids.insert(constraint.constraint_id);
        
        if (constraint.affected_variables.empty()) {
            result.passed = false;
            result.message = "Constraint " + constraint.constraint_id + " has no affected variables";
            return result;
        }
        
        for (const auto& var : constraint.affected_variables) {
            if (all_vars.find(var) == all_vars.end()) {
                result.passed = false;
                result.message = "Constraint " + constraint.constraint_id + " references non-existent variable: " + var;
                return result;
            }
        }
        
        if (constraint.constraint_expression.empty()) {
            result.passed = false;
            result.message = "Constraint " + constraint.constraint_id + " has empty expression";
            return result;
        }
    }
    
    result.message = "All constraints validated";
    return result;
}

ValidationResult ScenarioValidator::validate_events(const schema::ScenarioDefinition& scenario) {
    ValidationResult result;
    result.rule_name = "events";
    result.passed = true;
    result.level = schema::ValidationLevel::REJECT;
    
    if (scenario.events.empty()) {
        result.level = schema::ValidationLevel::WARN;
        result.message = "No events defined";
        return result;
    }
    
    // Check for duplicate event IDs
    std::set<std::string> seen_ids;
    for (const auto& event : scenario.events) {
        if (seen_ids.count(event.event_id)) {
            result.passed = false;
            result.message = "Duplicate event ID: " + event.event_id;
            return result;
        }
        seen_ids.insert(event.event_id);
        
        // Validate event type
        if (event.event_type != "SCHEDULED" && 
            event.event_type != "TRIGGERED" && 
            event.event_type != "CONDITIONAL") {
            result.passed = false;
            result.message = "Event " + event.event_id + " has invalid type: " + event.event_type;
            return result;
        }
        
        // Scheduled events must have timestamp >= 0
        if (event.event_type == "SCHEDULED" && event.timestamp < 0) {
            result.passed = false;
            result.message = "Scheduled event " + event.event_id + " has negative timestamp";
            return result;
        }

        if ((event.event_type == "TRIGGERED" || event.event_type == "CONDITIONAL") &&
            event.trigger_condition.empty()) {
            result.passed = false;
            result.message = "Event " + event.event_id +
                             " must define trigger_condition for type " + event.event_type;
            return result;
        }
    }
    
    result.message = "All events validated";
    return result;
}

ValidationResult ScenarioValidator::validate_agent_layer(const schema::ScenarioDefinition& scenario) {
    ValidationResult result;
    result.rule_name = "agent_layer";
    result.passed = true;
    result.level = schema::ValidationLevel::REJECT;

    if (!scenario.agent_layer.has_value()) {
        result.message = "No agent_layer defined (v1 execution path)";
        return result;
    }

    const auto& layer = *scenario.agent_layer;
    if (!std::isfinite(layer.world.duration) || layer.world.duration <= 0.0) {
        result.passed = false;
        result.message = "agent_layer.world.duration must be finite and > 0";
        return result;
    }
    if (layer.world.max_event_count == 0) {
        result.passed = false;
        result.message = "agent_layer.world.max_event_count must be > 0";
        return result;
    }
    if (!std::isfinite(layer.world.default_walking_speed) || layer.world.default_walking_speed < 0.0) {
        result.passed = false;
        result.message = "agent_layer.world.default_walking_speed must be finite and >= 0";
        return result;
    }

    std::set<std::string> location_ids;
    for (const auto& location : layer.locations) {
        if (location.location_id.empty()) {
            result.passed = false;
            result.message = "agent_layer location_id cannot be empty";
            return result;
        }
        if (!location_ids.insert(location.location_id).second) {
            result.passed = false;
            result.message = "Duplicate agent_layer location_id: " + location.location_id;
            return result;
        }
        if (!std::isfinite(location.x) || !std::isfinite(location.y)) {
            result.passed = false;
            result.message = "agent_layer location coordinates must be finite";
            return result;
        }
    }
    if (location_ids.empty()) {
        result.passed = false;
        result.message = "agent_layer.locations must not be empty";
        return result;
    }

    std::set<std::string> item_ids;
    for (const auto& item : layer.items) {
        if (item.item_id.empty()) {
            result.passed = false;
            result.message = "agent_layer item_id cannot be empty";
            return result;
        }
        if (!item_ids.insert(item.item_id).second) {
            result.passed = false;
            result.message = "Duplicate agent_layer item_id: " + item.item_id;
            return result;
        }
        if (!std::isfinite(item.base_appeal) || item.base_appeal < 0.0) {
            result.passed = false;
            result.message = "agent_layer item base_appeal must be finite and >= 0 for item: " + item.item_id;
            return result;
        }
    }
    if (item_ids.empty()) {
        result.passed = false;
        result.message = "agent_layer.items must not be empty";
        return result;
    }

    std::set<std::string> policy_ids;
    for (const auto& policy : layer.policies) {
        if (policy.policy_id.empty()) {
            result.passed = false;
            result.message = "agent_layer policy_id cannot be empty";
            return result;
        }
        if (!policy_ids.insert(policy.policy_id).second) {
            result.passed = false;
            result.message = "Duplicate agent_layer policy_id: " + policy.policy_id;
            return result;
        }
    }

    std::set<std::string> shop_ids;
    for (const auto& shop : layer.shops) {
        if (shop.shop_id.empty()) {
            result.passed = false;
            result.message = "agent_layer shop_id cannot be empty";
            return result;
        }
        if (!shop_ids.insert(shop.shop_id).second) {
            result.passed = false;
            result.message = "Duplicate agent_layer shop_id: " + shop.shop_id;
            return result;
        }
        if (location_ids.find(shop.location_ref) == location_ids.end()) {
            result.passed = false;
            result.message = "Shop " + shop.shop_id + " references unknown location: " + shop.location_ref;
            return result;
        }
        if (!std::isfinite(shop.service_time) || shop.service_time < 0.0) {
            result.passed = false;
            result.message = "Shop " + shop.shop_id + " has invalid non-negative service_time";
            return result;
        }
        if (shop.queue_capacity < 0) {
            result.passed = false;
            result.message = "Shop " + shop.shop_id + " has negative queue_capacity";
            return result;
        }
        for (const auto& inventory : shop.inventory) {
            if (item_ids.find(inventory.item_id) == item_ids.end()) {
                result.passed = false;
                result.message = "Shop " + shop.shop_id + " inventory references unknown item: " + inventory.item_id;
                return result;
            }
            if (!std::isfinite(inventory.price) || inventory.price < 0.0) {
                result.passed = false;
                result.message = "Shop " + shop.shop_id + " has invalid non-negative price";
                return result;
            }
            if (inventory.stock < 0) {
                result.passed = false;
                result.message = "Shop " + shop.shop_id + " has negative stock";
                return result;
            }
        }
    }
    if (shop_ids.empty()) {
        result.passed = false;
        result.message = "agent_layer.shops must not be empty";
        return result;
    }

    std::set<std::string> agent_ids;
    for (const auto& agent : layer.agents) {
        if (agent.agent_id.empty()) {
            result.passed = false;
            result.message = "agent_layer agent_id cannot be empty";
            return result;
        }
        if (!agent_ids.insert(agent.agent_id).second) {
            result.passed = false;
            result.message = "Duplicate agent_layer agent_id: " + agent.agent_id;
            return result;
        }
        if (location_ids.find(agent.start_location_ref) == location_ids.end()) {
            result.passed = false;
            result.message = "Agent " + agent.agent_id + " references unknown start_location_ref: " + agent.start_location_ref;
            return result;
        }
        if (!agent.policy_ref.empty() && policy_ids.find(agent.policy_ref) == policy_ids.end()) {
            result.passed = false;
            result.message = "Agent " + agent.agent_id + " references unknown policy_ref: " + agent.policy_ref;
            return result;
        }
        if (!std::isfinite(agent.budget) || agent.budget < 0.0) {
            result.passed = false;
            result.message = "Agent " + agent.agent_id + " has invalid non-negative budget";
            return result;
        }
        if (!std::isfinite(agent.movement_speed) || agent.movement_speed < 0.0) {
            result.passed = false;
            result.message = "Agent " + agent.agent_id + " has invalid non-negative movement_speed";
            return result;
        }
        if (!std::isfinite(agent.hunger) || agent.hunger < 0.0) {
            result.passed = false;
            result.message = "Agent " + agent.agent_id + " has invalid non-negative hunger";
            return result;
        }
        if (!std::isfinite(agent.social_susceptibility) || agent.social_susceptibility < 0.0) {
            result.passed = false;
            result.message = "Agent " + agent.agent_id + " has invalid non-negative social_susceptibility";
            return result;
        }
    }
    if (agent_ids.empty()) {
        result.passed = false;
        result.message = "agent_layer.agents must not be empty";
        return result;
    }

    result.message = "agent_layer validated";
    return result;
}

namespace {

bool typed_scalar_matches(const noisiax::schema::TypedScalarValue& value,
                          noisiax::schema::TypedFieldType type) {
    switch (type) {
        case noisiax::schema::TypedFieldType::INTEGER:
            return std::holds_alternative<int64_t>(value);
        case noisiax::schema::TypedFieldType::FLOAT:
            return std::holds_alternative<double>(value);
        case noisiax::schema::TypedFieldType::BOOLEAN:
            return std::holds_alternative<bool>(value);
        case noisiax::schema::TypedFieldType::STRING:
            return std::holds_alternative<std::string>(value);
    }
    return false;
}

std::vector<std::string> split_dotted_target(const std::string& value) {
    std::vector<std::string> parts;
    std::string current;
    for (const char c : value) {
        if (c == '.') {
            parts.push_back(current);
            current.clear();
            continue;
        }
        current.push_back(c);
    }
    parts.push_back(current);
    return parts;
}

}  // namespace

ValidationResult ScenarioValidator::validate_typed_layer(const schema::ScenarioDefinition& scenario) {
    ValidationResult result;
    result.rule_name = "typed_layer";
    result.passed = true;
    result.level = schema::ValidationLevel::REJECT;

    if (!scenario.typed_layer.has_value()) {
        result.message = "No typed_layer defined (v1/v2 execution path)";
        return result;
    }

    if (scenario.agent_layer.has_value()) {
        result.passed = false;
        result.message = "Scenario cannot contain both agent_layer and typed_layer";
        return result;
    }

    const auto& layer = *scenario.typed_layer;
    if (!std::isfinite(layer.world.duration) || layer.world.duration <= 0.0) {
        result.passed = false;
        result.message = "typed_layer.world.duration must be finite and > 0";
        return result;
    }
    if (layer.world.max_event_count == 0) {
        result.passed = false;
        result.message = "typed_layer.world.max_event_count must be > 0";
        return result;
    }
    if (layer.world.tick_interval.has_value() &&
        (!std::isfinite(*layer.world.tick_interval) || *layer.world.tick_interval <= 0.0)) {
        result.passed = false;
        result.message = "typed_layer.world.tick_interval must be finite and > 0 when provided";
        return result;
    }

    std::set<std::string> component_type_ids;
    std::map<std::string, std::map<std::string, schema::TypedFieldType>> component_fields;
    for (const auto& component : layer.component_types) {
        if (component.component_type_id.empty()) {
            result.passed = false;
            result.message = "typed_layer component_type_id cannot be empty";
            return result;
        }
        if (!component_type_ids.insert(component.component_type_id).second) {
            result.passed = false;
            result.message = "Duplicate typed_layer component_type_id: " + component.component_type_id;
            return result;
        }
        if (component.fields.empty()) {
            result.passed = false;
            result.message = "typed_layer component_type has no fields: " + component.component_type_id;
            return result;
        }

        std::set<std::string> field_names;
        for (const auto& field : component.fields) {
            if (field.field_name.empty()) {
                result.passed = false;
                result.message = "typed_layer component field_name cannot be empty in " + component.component_type_id;
                return result;
            }
            if (!field_names.insert(field.field_name).second) {
                result.passed = false;
                result.message = "Duplicate typed_layer component field_name '" + field.field_name +
                                 "' in " + component.component_type_id;
                return result;
            }
            component_fields[component.component_type_id][field.field_name] = field.type;
        }
    }
    if (component_type_ids.empty()) {
        result.passed = false;
        result.message = "typed_layer.component_types must not be empty";
        return result;
    }

    std::set<std::string> entity_type_ids;
    std::map<std::string, std::set<std::string>> entity_type_components;
    for (const auto& entity_type : layer.entity_types) {
        if (entity_type.entity_type_id.empty()) {
            result.passed = false;
            result.message = "typed_layer entity_type_id cannot be empty";
            return result;
        }
        if (!entity_type_ids.insert(entity_type.entity_type_id).second) {
            result.passed = false;
            result.message = "Duplicate typed_layer entity_type_id: " + entity_type.entity_type_id;
            return result;
        }
        if (entity_type.components.empty()) {
            result.passed = false;
            result.message = "typed_layer entity_type.components must not be empty for " + entity_type.entity_type_id;
            return result;
        }

        for (const auto& component_ref : entity_type.components) {
            if (component_type_ids.find(component_ref) == component_type_ids.end()) {
                result.passed = false;
                result.message = "typed_layer entity_type " + entity_type.entity_type_id +
                                 " references unknown component_type: " + component_ref;
                return result;
            }
            entity_type_components[entity_type.entity_type_id].insert(component_ref);
        }
    }
    if (entity_type_ids.empty()) {
        result.passed = false;
        result.message = "typed_layer.entity_types must not be empty";
        return result;
    }

    std::set<std::string> entity_ids;
    for (const auto& entity : layer.entities) {
        if (entity.entity_id.empty()) {
            result.passed = false;
            result.message = "typed_layer entity_id cannot be empty";
            return result;
        }
        if (!entity_ids.insert(entity.entity_id).second) {
            result.passed = false;
            result.message = "Duplicate typed_layer entity_id: " + entity.entity_id;
            return result;
        }
        if (entity_type_ids.find(entity.entity_type_ref) == entity_type_ids.end()) {
            result.passed = false;
            result.message = "typed_layer entity " + entity.entity_id +
                             " references unknown entity_type: " + entity.entity_type_ref;
            return result;
        }

        const auto& allowed_components = entity_type_components[entity.entity_type_ref];
        for (const auto& [component_id, fields] : entity.components) {
            if (component_type_ids.find(component_id) == component_type_ids.end()) {
                result.passed = false;
                result.message = "typed_layer entity " + entity.entity_id +
                                 " references unknown component_type: " + component_id;
                return result;
            }
            if (allowed_components.find(component_id) == allowed_components.end()) {
                result.passed = false;
                result.message = "typed_layer entity " + entity.entity_id +
                                 " assigns component '" + component_id +
                                 "' not declared on entity_type " + entity.entity_type_ref;
                return result;
            }

            const auto& schema_fields = component_fields[component_id];
            for (const auto& [field_name, value] : fields) {
                const auto field_it = schema_fields.find(field_name);
                if (field_it == schema_fields.end()) {
                    result.passed = false;
                    result.message = "typed_layer entity " + entity.entity_id +
                                     " writes undeclared field '" + field_name +
                                     "' on component " + component_id;
                    return result;
                }
                if (!typed_scalar_matches(value, field_it->second)) {
                    result.passed = false;
                    result.message = "typed_layer entity " + entity.entity_id +
                                     " field type mismatch for " + component_id + "." + field_name;
                    return result;
                }
            }
        }
    }
    if (entity_ids.empty()) {
        result.passed = false;
        result.message = "typed_layer.entities must not be empty";
        return result;
    }

    std::set<std::string> relation_type_ids;
    std::map<std::string, std::map<std::string, schema::TypedFieldType>> relation_payload_fields;
    for (const auto& relation_type : layer.relation_types) {
        if (relation_type.relation_type_id.empty()) {
            result.passed = false;
            result.message = "typed_layer relation_type_id cannot be empty";
            return result;
        }
        if (!relation_type_ids.insert(relation_type.relation_type_id).second) {
            result.passed = false;
            result.message = "Duplicate typed_layer relation_type_id: " + relation_type.relation_type_id;
            return result;
        }
        if (relation_type.max_per_entity.has_value() && *relation_type.max_per_entity == 0) {
            result.passed = false;
            result.message = "typed_layer relation_type.max_per_entity must be > 0 when provided";
            return result;
        }
        if (relation_type.max_total.has_value() && *relation_type.max_total == 0) {
            result.passed = false;
            result.message = "typed_layer relation_type.max_total must be > 0 when provided";
            return result;
        }

        std::set<std::string> payload_field_names;
        for (const auto& field : relation_type.payload_fields) {
            if (field.field_name.empty()) {
                result.passed = false;
                result.message = "typed_layer relation_type payload field_name cannot be empty in " +
                                 relation_type.relation_type_id;
                return result;
            }
            if (!payload_field_names.insert(field.field_name).second) {
                result.passed = false;
                result.message = "Duplicate typed_layer relation_type payload field '" + field.field_name +
                                 "' in " + relation_type.relation_type_id;
                return result;
            }
            relation_payload_fields[relation_type.relation_type_id][field.field_name] = field.type;
        }
    }

    for (const auto& relation : layer.relations) {
        if (relation_type_ids.find(relation.relation_type_ref) == relation_type_ids.end()) {
            result.passed = false;
            result.message = "typed_layer relation references unknown relation_type: " + relation.relation_type_ref;
            return result;
        }
        if (entity_ids.find(relation.source_entity_ref) == entity_ids.end()) {
            result.passed = false;
            result.message = "typed_layer relation references unknown source entity: " + relation.source_entity_ref;
            return result;
        }
        if (entity_ids.find(relation.target_entity_ref) == entity_ids.end()) {
            result.passed = false;
            result.message = "typed_layer relation references unknown target entity: " + relation.target_entity_ref;
            return result;
        }
        if (relation.expires_at.has_value() && !std::isfinite(*relation.expires_at)) {
            result.passed = false;
            result.message = "typed_layer relation expires_at must be finite when provided";
            return result;
        }

        const auto& schema_fields = relation_payload_fields[relation.relation_type_ref];
        for (const auto& [field_name, value] : relation.payload) {
            const auto field_it = schema_fields.find(field_name);
            if (field_it == schema_fields.end()) {
                result.passed = false;
                result.message = "typed_layer relation writes undeclared payload field '" + field_name +
                                 "' on relation_type " + relation.relation_type_ref;
                return result;
            }
            if (!typed_scalar_matches(value, field_it->second)) {
                result.passed = false;
                result.message = "typed_layer relation payload type mismatch for " + relation.relation_type_ref +
                                 "." + field_name;
                return result;
            }
        }
    }

    std::set<std::string> event_type_ids;
    std::map<std::string, std::map<std::string, schema::TypedFieldType>> event_payload_fields;
    for (const auto& event_type : layer.event_types) {
        if (event_type.event_type_id.empty()) {
            result.passed = false;
            result.message = "typed_layer event_type_id cannot be empty";
            return result;
        }
        if (!event_type_ids.insert(event_type.event_type_id).second) {
            result.passed = false;
            result.message = "Duplicate typed_layer event_type_id: " + event_type.event_type_id;
            return result;
        }

        std::set<std::string> payload_field_names;
        for (const auto& field : event_type.payload_fields) {
            if (field.field_name.empty()) {
                result.passed = false;
                result.message = "typed_layer event_type payload field_name cannot be empty in " +
                                 event_type.event_type_id;
                return result;
            }
            if (!payload_field_names.insert(field.field_name).second) {
                result.passed = false;
                result.message = "Duplicate typed_layer event_type payload field '" + field.field_name +
                                 "' in " + event_type.event_type_id;
                return result;
            }
            event_payload_fields[event_type.event_type_id][field.field_name] = field.type;
        }
    }

    for (const auto& event : layer.initial_events) {
        if (event_type_ids.find(event.event_type_ref) == event_type_ids.end()) {
            result.passed = false;
            result.message = "typed_layer initial_event references unknown event_type: " + event.event_type_ref;
            return result;
        }
        if (!std::isfinite(event.timestamp) || event.timestamp < 0.0) {
            result.passed = false;
            result.message = "typed_layer initial_event timestamp must be finite and >= 0";
            return result;
        }
        if (event.timestamp > layer.world.duration) {
            result.passed = false;
            result.message = "typed_layer initial_event timestamp exceeds world.duration";
            return result;
        }

        const auto& schema_fields = event_payload_fields[event.event_type_ref];
        for (const auto& [field_name, value] : event.payload) {
            const auto field_it = schema_fields.find(field_name);
            if (field_it == schema_fields.end()) {
                result.passed = false;
                result.message = "typed_layer initial_event writes undeclared payload field '" + field_name +
                                 "' on event_type " + event.event_type_ref;
                return result;
            }
            if (!typed_scalar_matches(value, field_it->second)) {
                result.passed = false;
                result.message = "typed_layer initial_event payload type mismatch for " + event.event_type_ref +
                                 "." + field_name;
                return result;
            }
        }
    }

    std::set<std::string> system_ids;
    for (const auto& system : layer.systems) {
        if (system.system_id.empty()) {
            result.passed = false;
            result.message = "typed_layer system_id cannot be empty";
            return result;
        }
        if (!system_ids.insert(system.system_id).second) {
            result.passed = false;
            result.message = "Duplicate typed_layer system_id: " + system.system_id;
            return result;
        }
        if (system.triggered_by.empty()) {
            result.passed = false;
            result.message = "typed_layer system triggered_by must not be empty for " + system.system_id;
            return result;
        }
        for (const auto& trigger : system.triggered_by) {
            if (event_type_ids.find(trigger) == event_type_ids.end()) {
                result.passed = false;
                result.message = "typed_layer system " + system.system_id +
                                 " references unknown event_type trigger: " + trigger;
                return result;
            }
        }

        const bool kind_per_entity = (system.kind == "per_entity");
        const bool kind_pair = (system.kind == "pair");
        const bool kind_per_relation = (system.kind == "per_relation");
        if (!kind_per_entity && !kind_pair && !kind_per_relation) {
            result.passed = false;
            result.message = "typed_layer system " + system.system_id + " has unknown kind: " + system.kind;
            return result;
        }

        if ((kind_per_entity || kind_pair) && !system.entity_type_ref.has_value()) {
            result.passed = false;
            result.message = "typed_layer system " + system.system_id + " requires entity_type for kind " + system.kind;
            return result;
        }
        if (kind_per_relation && !system.relation_type_ref.has_value()) {
            result.passed = false;
            result.message = "typed_layer system " + system.system_id + " requires relation_type for kind per_relation";
            return result;
        }

        if (system.entity_type_ref.has_value() &&
            entity_type_ids.find(*system.entity_type_ref) == entity_type_ids.end()) {
            result.passed = false;
            result.message = "typed_layer system " + system.system_id +
                             " references unknown entity_type: " + *system.entity_type_ref;
            return result;
        }
        if (system.relation_type_ref.has_value() &&
            relation_type_ids.find(*system.relation_type_ref) == relation_type_ids.end()) {
            result.passed = false;
            result.message = "typed_layer system " + system.system_id +
                             " references unknown relation_type: " + *system.relation_type_ref;
            return result;
        }

        for (const auto& write : system.writes) {
            const auto parts = split_dotted_target(write.target);
            if (parts.size() != 2 && parts.size() != 3) {
                result.passed = false;
                result.message = "typed_layer system " + system.system_id +
                                 " write target must be component.field or self.component.field";
                return result;
            }
            std::string role = "self";
            std::string component_id;
            std::string field_name;
            if (parts.size() == 2) {
                component_id = parts[0];
                field_name = parts[1];
            } else {
                role = parts[0];
                component_id = parts[1];
                field_name = parts[2];
            }

            if (kind_per_entity && role != "self") {
                result.passed = false;
                result.message = "typed_layer system " + system.system_id + " cannot write to role '" + role + "'";
                return result;
            }

            auto component_it = component_fields.find(component_id);
            if (component_it == component_fields.end()) {
                result.passed = false;
                result.message = "typed_layer system " + system.system_id +
                                 " writes unknown component_type: " + component_id;
                return result;
            }
            if (component_it->second.find(field_name) == component_it->second.end()) {
                result.passed = false;
                result.message = "typed_layer system " + system.system_id +
                                 " writes undeclared field: " + component_id + "." + field_name;
                return result;
            }
        }

        for (const auto& create_relation : system.create_relations) {
            if (relation_type_ids.find(create_relation.relation_type_ref) == relation_type_ids.end()) {
                result.passed = false;
                result.message = "typed_layer system " + system.system_id +
                                 " create_relation references unknown relation_type: " + create_relation.relation_type_ref;
                return result;
            }
            if (create_relation.source != "self" && create_relation.source != "other") {
                result.passed = false;
                result.message = "typed_layer system " + system.system_id +
                                 " create_relation source must be 'self' or 'other'";
                return result;
            }
            if (create_relation.target != "self" && create_relation.target != "other") {
                result.passed = false;
                result.message = "typed_layer system " + system.system_id +
                                 " create_relation target must be 'self' or 'other'";
                return result;
            }

            const auto& schema_fields = relation_payload_fields[create_relation.relation_type_ref];
            for (const auto& [field_name, _expr] : create_relation.payload_exprs) {
                if (schema_fields.find(field_name) == schema_fields.end()) {
                    result.passed = false;
                    result.message = "typed_layer system " + system.system_id +
                                     " create_relation writes undeclared payload field '" + field_name + "'";
                    return result;
                }
            }
        }

        for (const auto& emit_event : system.emit_events) {
            if (event_type_ids.find(emit_event.event_type_ref) == event_type_ids.end()) {
                result.passed = false;
                result.message = "typed_layer system " + system.system_id +
                                 " emit_event references unknown event_type: " + emit_event.event_type_ref;
                return result;
            }

            const auto& schema_fields = event_payload_fields[emit_event.event_type_ref];
            for (const auto& [field_name, _expr] : emit_event.payload_exprs) {
                if (schema_fields.find(field_name) == schema_fields.end()) {
                    result.passed = false;
                    result.message = "typed_layer system " + system.system_id +
                                     " emit_event writes undeclared payload field '" + field_name + "'";
                    return result;
                }
            }
        }
    }

    result.message = "typed_layer validated";
    return result;
}

ValidationResult ScenarioValidator::validate_evaluation_criteria(const schema::ScenarioDefinition& scenario) {
    ValidationResult result;
    result.rule_name = "evaluation_criteria";
    result.passed = true;
    result.level = schema::ValidationLevel::REJECT;
    
    if (scenario.evaluation_criteria.empty()) {
        result.passed = false;
        result.message = "No evaluation criteria defined - evaluation criteria are required in v1";
        return result;
    }
    
    auto all_vars = get_all_variable_ids(scenario);
    
    // Check for duplicate criterion IDs
    std::set<std::string> seen_ids;
    for (const auto& criterion : scenario.evaluation_criteria) {
        if (seen_ids.count(criterion.criterion_id)) {
            result.passed = false;
            result.message = "Duplicate criterion ID: " + criterion.criterion_id;
            return result;
        }
        seen_ids.insert(criterion.criterion_id);
        
        if (criterion.input_variables.empty()) {
            result.passed = false;
            result.message = "Criterion " + criterion.criterion_id + " has no input variables";
            return result;
        }
        
        for (const auto& var : criterion.input_variables) {
            if (all_vars.find(var) == all_vars.end()) {
                result.passed = false;
                result.message = "Criterion " + criterion.criterion_id + " references non-existent variable: " + var;
                return result;
            }
        }
    }
    
    result.message = "All evaluation criteria validated";
    return result;
}

ValidationResult ScenarioValidator::check_required_fields(const schema::ScenarioDefinition& scenario) {
    ValidationResult result;
    result.rule_name = "required_fields";
    result.passed = true;
    result.level = schema::ValidationLevel::REJECT;
    
    if (scenario.scenario_id.empty()) {
        result.passed = false;
        result.message = "Missing required field: scenario_id";
        return result;
    }
    
    if (scenario.schema_version.empty()) {
        result.passed = false;
        result.message = "Missing required field: schema_version";
        return result;
    }
    
    if (!looks_like_semver(scenario.schema_version)) {
        result.passed = false;
        result.message = "schema_version must follow semver format (e.g., 1.0.0)";
        return result;
    }

    if (scenario.assumptions.empty()) {
        result.passed = false;
        result.message = "Missing required stage data: assumptions";
        return result;
    }

    if (scenario.typed_layer.has_value()) {
        const auto& typed = *scenario.typed_layer;
        if (!std::isfinite(typed.world.duration) || typed.world.duration <= 0.0) {
            result.passed = false;
            result.message = "typed_layer.world.duration must be finite and > 0";
            return result;
        }
        if (typed.world.max_event_count == 0) {
            result.passed = false;
            result.message = "typed_layer.world.max_event_count must be > 0";
            return result;
        }
        if (typed.component_types.empty()) {
            result.passed = false;
            result.message = "typed_layer.component_types must not be empty";
            return result;
        }
        if (typed.entity_types.empty()) {
            result.passed = false;
            result.message = "typed_layer.entity_types must not be empty";
            return result;
        }
        if (typed.entities.empty()) {
            result.passed = false;
            result.message = "typed_layer.entities must not be empty";
            return result;
        }
        result.message = "All required fields present";
        return result;
    }

    if (scenario.entities.empty()) {
        result.passed = false;
        result.message = "Missing required stage data: entities";
        return result;
    }
    if (scenario.variables.empty()) {
        result.passed = false;
        result.message = "Missing required stage data: variables";
        return result;
    }
    if (scenario.dependency_edges.empty()) {
        result.passed = false;
        result.message = "Missing required stage data: dependency_edges";
        return result;
    }
    if (scenario.constraints.empty()) {
        result.passed = false;
        result.message = "Missing required stage data: constraints";
        return result;
    }
    if (scenario.evaluation_criteria.empty()) {
        result.passed = false;
        result.message = "Missing required stage data: evaluation_criteria";
        return result;
    }
    
    result.message = "All required fields present";
    return result;
}

ValidationResult ScenarioValidator::check_unknown_fields(const schema::ScenarioDefinition& scenario) {
    ValidationResult result;
    result.rule_name = "unknown_fields";
    result.passed = true;
    result.level = schema::ValidationLevel::WARN;
    
    // In C++, unknown fields are not stored in the struct, so this check passes by default
    // This would be more relevant during YAML parsing
    result.message = "No unknown fields detected (checked at parse time)";
    return result;
}

ValidationResult ScenarioValidator::check_anti_patterns(const schema::ScenarioDefinition& scenario) {
    ValidationResult result;
    result.rule_name = "anti_patterns";
    result.passed = true;
    result.level = schema::ValidationLevel::WARN;
    
    // Check for v1 excluded features
    // Since these aren't in the schema, we just note their absence
    
    // Check for excessive number of variables (heuristic)
    if (scenario.variables.size() > 1000) {
        result.message = "Large number of variables (" + std::to_string(scenario.variables.size()) + ") - consider splitting scenario";
        return result;
    }
    
    // Check for excessive number of dependencies
    if (scenario.dependency_edges.size() > 5000) {
        result.message = "Large number of dependencies (" + std::to_string(scenario.dependency_edges.size()) + ") - may impact performance";
        return result;
    }
    
    result.message = "No anti-patterns detected";
    return result;
}

ValidationResult ScenarioValidator::check_static_cycles(const schema::ScenarioDefinition& scenario) {
    ValidationResult result;
    result.rule_name = "cycle_detection";
    result.passed = !detect_cycle(scenario);
    result.level = schema::ValidationLevel::REJECT;
    
    if (result.passed) {
        result.message = "No cyclic dependencies detected";
    } else {
        result.message = "Cyclic dependency detected in dependency graph";
    }
    
    return result;
}

ValidationResult ScenarioValidator::check_type_compatibility(const schema::ScenarioDefinition& scenario) {
    ValidationResult result;
    result.rule_name = "type_compatibility";
    result.passed = true;
    result.level = schema::ValidationLevel::REJECT;
    
    // Build variable type map
    std::map<std::string, schema::VariableType> var_types;
    for (const auto& var : scenario.variables) {
        var_types[var.variable_id] = var.type;
    }
    
    // Check edge type compatibility (simplified - full check would require function signatures)
    for (const auto& edge : scenario.dependency_edges) {
        auto src_it = var_types.find(edge.source_variable);
        auto tgt_it = var_types.find(edge.target_variable);
        
        if (src_it != var_types.end() && tgt_it != var_types.end()) {
            // Runtime propagation currently supports numeric/boolean variables only.
            if (src_it->second == schema::VariableType::STRING ||
                src_it->second == schema::VariableType::ENUM ||
                src_it->second == schema::VariableType::LIST ||
                tgt_it->second == schema::VariableType::STRING ||
                tgt_it->second == schema::VariableType::ENUM ||
                tgt_it->second == schema::VariableType::LIST) {
                result.passed = false;
                result.message = "Type incompatibility: edge " + edge.edge_id +
                                " uses unsupported non-numeric variable type in v1 propagation";
                return result;
            }
        }
    }
    
    result.message = "Type compatibility verified";
    return result;
}

ValidationResult ScenarioValidator::check_initial_constraints(const schema::ScenarioDefinition& scenario) {
    ValidationResult result;
    result.rule_name = "initial_constraints";
    result.passed = true;
    result.level = schema::ValidationLevel::REJECT;
    
    std::map<std::string, double> initial_numeric_values;
    for (const auto& variable : scenario.variables) {
        auto numeric = scalar_default_to_numeric(variable.default_value, variable.type);
        if (numeric.has_value()) {
            initial_numeric_values[variable.variable_id] = *numeric;
        }
    }

    for (const auto& constraint : scenario.constraints) {
        try {
            NumericConstraintParser parser(
                constraint.constraint_expression,
                [&](std::string_view variable) -> std::optional<double> {
                    auto it = initial_numeric_values.find(std::string(variable));
                    if (it == initial_numeric_values.end()) {
                        return std::nullopt;
                    }
                    return it->second;
                });

            const bool passed = parser.evaluate();
            if (!passed) {
                result.passed = false;
                result.message = "Initial-state constraint unsatisfied: " + constraint.constraint_id;
                return result;
            }
        } catch (const std::exception& ex) {
            result.passed = false;
            result.message = "Failed to evaluate initial constraint " + constraint.constraint_id +
                            ": " + ex.what();
            return result;
        }
    }

    result.message = "Initial constraints are satisfiable";
    return result;
}

ValidationResult ScenarioValidator::check_orphan_variables(const schema::ScenarioDefinition& scenario) {
    ValidationResult result;
    result.rule_name = "orphan_variables";
    result.passed = true;
    result.level = schema::ValidationLevel::WARN;
    
    if (scenario.variables.empty()) {
        result.message = "No variables to check";
        return result;
    }
    
    // Find variables that are neither sources nor targets of any edge
    std::set<std::string> connected_vars;
    for (const auto& edge : scenario.dependency_edges) {
        connected_vars.insert(edge.source_variable);
        connected_vars.insert(edge.target_variable);
    }
    
    std::vector<std::string> orphans;
    for (const auto& var : scenario.variables) {
        if (connected_vars.find(var.variable_id) == connected_vars.end()) {
            orphans.push_back(var.variable_id);
        }
    }
    
    if (!orphans.empty()) {
        result.message = "Found " + std::to_string(orphans.size()) + " orphan variables (not connected to any edge): ";
        for (size_t i = 0; i < std::min(orphans.size(), size_t(5)); ++i) {
            result.message += orphans[i];
            if (i < std::min(orphans.size(), size_t(5)) - 1) result.message += ", ";
        }
        if (orphans.size() > 5) result.message += "...";
    } else {
        result.message = "No orphan variables detected";
    }
    
    return result;
}

ValidationResult ScenarioValidator::check_fan_out_limits(const schema::ScenarioDefinition& scenario) {
    ValidationResult result;
    result.rule_name = "fan_out_limits";
    result.passed = true;
    result.level = schema::ValidationLevel::WARN;
    
    // Count outgoing edges per variable
    std::map<std::string, int> fan_out;
    for (const auto& edge : scenario.dependency_edges) {
        fan_out[edge.source_variable]++;
    }
    
    // Check for excessive fan-out
    int max_fan_out = 0;
    std::string max_var;
    for (const auto& [var, count] : fan_out) {
        if (count > max_fan_out) {
            max_fan_out = count;
            max_var = var;
        }
    }
    
    if (max_fan_out > 50) {
        result.message = "Variable " + max_var + " has high fan-out (" + std::to_string(max_fan_out) + ")";
    } else {
        result.message = "Fan-out within acceptable limits";
    }
    
    return result;
}

ValidationResult ScenarioValidator::check_depth_limits(const schema::ScenarioDefinition& scenario) {
    ValidationResult result;
    result.rule_name = "depth_limits";
    result.passed = true;
    result.level = schema::ValidationLevel::WARN;
    
    // Compute maximum depth using BFS from root nodes
    std::map<std::string, std::vector<std::string>> adjacency;
    std::set<std::string> all_vars;
    std::set<std::string> targets;
    
    for (const auto& edge : scenario.dependency_edges) {
        adjacency[edge.source_variable].push_back(edge.target_variable);
        all_vars.insert(edge.source_variable);
        all_vars.insert(edge.target_variable);
        targets.insert(edge.target_variable);
    }
    
    // Find root nodes (sources that are not targets)
    std::vector<std::string> roots;
    for (const auto& var : all_vars) {
        if (targets.find(var) == targets.end()) {
            roots.push_back(var);
        }
    }
    
    // BFS to find max depth
    int max_depth = 0;
    for (const auto& root : roots) {
        std::queue<std::pair<std::string, int>> q;
        q.push({root, 0});
        std::set<std::string> visited;
        
        while (!q.empty()) {
            auto [current, depth] = q.front();
            q.pop();
            
            if (visited.count(current)) continue;
            visited.insert(current);
            
            max_depth = std::max(max_depth, depth);
            
            for (const auto& next : adjacency[current]) {
                if (!visited.count(next)) {
                    q.push({next, depth + 1});
                }
            }
        }
    }
    
    if (max_depth > 20) {
        result.message = "Dependency graph has large depth (" + std::to_string(max_depth) + ")";
    } else {
        result.message = "Graph depth within acceptable limits (max: " + std::to_string(max_depth) + ")";
    }
    
    return result;
}

bool ScenarioValidator::detect_cycle(const schema::ScenarioDefinition& scenario) {
    // Build adjacency list
    std::map<std::string, std::vector<std::string>> adjacency;
    std::set<std::string> all_vars;
    
    for (const auto& edge : scenario.dependency_edges) {
        adjacency[edge.source_variable].push_back(edge.target_variable);
        all_vars.insert(edge.source_variable);
        all_vars.insert(edge.target_variable);
    }
    
    // DFS-based cycle detection
    std::set<std::string> white;  // Not visited
    std::set<std::string> gray;   // Currently visiting (in stack)
    std::set<std::string> black;  // Completely visited
    
    for (const auto& var : all_vars) {
        white.insert(var);
    }
    
    while (!white.empty()) {
        std::string start = *white.begin();
        if (dfs_visit(start, adjacency, white, gray, black)) {
            return true;
        }
    }
    
    return false;
}

bool ScenarioValidator::dfs_visit(const std::string& node,
                                   const std::map<std::string, std::vector<std::string>>& adjacency,
                                   std::set<std::string>& white,
                                   std::set<std::string>& gray,
                                   std::set<std::string>& black) {
    white.erase(node);
    gray.insert(node);
    
    auto it = adjacency.find(node);
    if (it != adjacency.end()) {
        for (const auto& neighbor : it->second) {
            if (gray.count(neighbor)) {
                return true;  // Back edge found - cycle!
            }
            if (white.count(neighbor)) {
                if (dfs_visit(neighbor, adjacency, white, gray, black)) {
                    return true;
                }
            }
        }
    }
    
    gray.erase(node);
    black.insert(node);
    return false;
}

std::set<std::string> ScenarioValidator::get_all_variable_ids(const schema::ScenarioDefinition& scenario) {
    std::set<std::string> ids;
    for (const auto& var : scenario.variables) {
        ids.insert(var.variable_id);
    }
    return ids;
}

std::set<std::string> ScenarioValidator::get_all_entity_ids(const schema::ScenarioDefinition& scenario) {
    std::set<std::string> ids;
    for (const auto& entity : scenario.entities) {
        ids.insert(entity.entity_id);
    }
    return ids;
}

} // namespace noisiax::validation
