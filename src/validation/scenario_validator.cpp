#include "noisiax/validation/scenario_validator.hpp"
#include <algorithm>
#include <queue>
#include <set>

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
        result.level = schema::ValidationLevel::WARN;
        result.message = "No assumptions defined - consider documenting key assumptions";
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
        result.level = schema::ValidationLevel::WARN;
        result.message = "No entities defined";
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
        result.level = schema::ValidationLevel::WARN;
        result.message = "No dependency edges defined";
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
        result.level = schema::ValidationLevel::WARN;
        result.message = "No constraints defined";
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
    }
    
    result.message = "All events validated";
    return result;
}

ValidationResult ScenarioValidator::validate_evaluation_criteria(const schema::ScenarioDefinition& scenario) {
    ValidationResult result;
    result.rule_name = "evaluation_criteria";
    result.passed = true;
    result.level = schema::ValidationLevel::REJECT;
    
    if (scenario.evaluation_criteria.empty()) {
        result.level = schema::ValidationLevel::WARN;
        result.message = "No evaluation criteria defined";
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
    
    // Check schema version format (simple check)
    if (scenario.schema_version.find('.') == std::string::npos) {
        result.level = schema::ValidationLevel::WARN;
        result.passed = true;
        result.message = "Schema version should follow semver format (e.g., 1.0.0)";
        result.suggestion = "Consider using format like '1.0.0'";
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
            // Basic type compatibility check
            // For v1, we allow most combinations but warn about obvious mismatches
            if (src_it->second == schema::VariableType::STRING && 
                tgt_it->second == schema::VariableType::INTEGER) {
                result.passed = false;
                result.message = "Type incompatibility: edge " + edge.edge_id + 
                                " connects STRING source to INTEGER target";
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
    
    // For v1, we do a basic sanity check on constraint expressions
    // Full satisfiability checking is deferred
    for (const auto& constraint : scenario.constraints) {
        // Check that expression contains referenced variables
        for (const auto& var : constraint.affected_variables) {
            if (constraint.constraint_expression.find(var) == std::string::npos) {
                result.passed = false;
                result.message = "Constraint " + constraint.constraint_id + 
                                " expression does not reference variable: " + var;
                return result;
            }
        }
    }
    
    result.message = "Initial constraints appear satisfiable";
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
