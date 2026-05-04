#include "noisiax/noisiax.hpp"

#include <cassert>
#include <iostream>
#include <string>

int main() {
  // Basic library info tests
  assert(noisiax::name() == "NoisiaX");
  assert(noisiax::version() == "1.0.0");
  
  std::cout << "NoisiaX V1 Core - Basic Tests Passed" << std::endl;
  std::cout << "Library: " << noisiax::name() << std::endl;
  std::cout << "Version: " << noisiax::version() << std::endl;
  
  // Test schema types are accessible
  noisiax::schema::ScenarioDefinition scenario;
  scenario.scenario_id = "test_scenario";
  scenario.schema_version = "1.0.0";
  scenario.master_seed = 12345;
  scenario.goal_statement = "Test goal";
  
  assert(scenario.scenario_id == "test_scenario");
  assert(scenario.master_seed == 12345);
  
  // Test validation level enum
  noisiax::schema::ValidationLevel reject_level = noisiax::schema::ValidationLevel::REJECT;
  noisiax::schema::ValidationLevel warn_level = noisiax::schema::ValidationLevel::WARN;
  noisiax::schema::ValidationLevel autocorrect_level = noisiax::schema::ValidationLevel::AUTO_CORRECT;
  
  assert(static_cast<int>(reject_level) == 0);
  assert(static_cast<int>(warn_level) == 1);
  assert(static_cast<int>(autocorrect_level) == 2);
  
  // Test variable type enum
  noisiax::schema::VariableType int_type = noisiax::schema::VariableType::INTEGER;
  noisiax::schema::VariableType float_type = noisiax::schema::VariableType::FLOAT;
  noisiax::schema::VariableType string_type = noisiax::schema::VariableType::STRING;
  
  // Test entity descriptor
  noisiax::schema::EntityDescriptor entity;
  entity.entity_id = "test_entity";
  entity.entity_type = "test";
  entity.description = "A test entity";
  entity.attributes["key"] = "value";
  
  assert(entity.entity_id == "test_entity");
  assert(entity.attributes.at("key") == "value");
  
  // Test variable descriptor with variant default value
  noisiax::schema::VariableDescriptor var;
  var.variable_id = "test_var";
  var.type = noisiax::schema::VariableType::FLOAT;
  var.default_value = 42.0;
  var.description = "A test variable";
  
  assert(var.variable_id == "test_var");
  assert(std::get<double>(var.default_value) == 42.0);
  
  // Test dependency edge
  noisiax::schema::DependencyEdge edge;
  edge.edge_id = "test_edge";
  edge.source_variable = "var_a";
  edge.target_variable = "var_b";
  edge.propagation_function_id = "linear_scale";
  edge.weight = 1.5;
  
  assert(edge.edge_id == "test_edge");
  assert(edge.source_variable == "var_a");
  assert(edge.weight == 1.5);
  
  // Test constraint rule
  noisiax::schema::ConstraintRule constraint;
  constraint.constraint_id = "test_constraint";
  constraint.affected_variables.push_back("var_a");
  constraint.affected_variables.push_back("var_b");
  constraint.constraint_expression = "var_a + var_b > 0";
  constraint.enforcement_level = noisiax::schema::ValidationLevel::REJECT;
  constraint.error_message = "Sum must be positive";
  
  assert(constraint.constraint_id == "test_constraint");
  assert(constraint.affected_variables.size() == 2);
  assert(constraint.enforcement_level == noisiax::schema::ValidationLevel::REJECT);
  
  // Test event descriptor
  noisiax::schema::EventDescriptor event;
  event.event_id = "test_event";
  event.event_type = "SCHEDULED";
  event.timestamp = 10.0;
  event.priority = 5;
  event.description = "A test event";
  
  assert(event.event_id == "test_event");
  assert(event.timestamp == 10.0);
  assert(event.priority == 5);
  
  // Test evaluation criterion
  noisiax::schema::EvaluationCriterion criterion;
  criterion.criterion_id = "test_criterion";
  criterion.metric_name = "total_output";
  criterion.aggregation_method = "SUM";
  criterion.input_variables.push_back("var_output");
  criterion.target_value = 100.0;
  
  assert(criterion.criterion_id == "test_criterion");
  assert(criterion.target_value == 100.0);
  
  // Test assumption
  noisiax::schema::Assumption assumption;
  assumption.assumption_id = "test_assumption";
  assumption.category = "test_category";
  assumption.description = "A test assumption";
  assumption.rationale = "For testing purposes";
  assumption.confidence_level = noisiax::schema::ValidationLevel::WARN;
  
  assert(assumption.assumption_id == "test_assumption");
  assert(assumption.confidence_level == noisiax::schema::ValidationLevel::WARN);
  
  // Test scenario report
  noisiax::schema::ScenarioReport report;
  report.scenario_id = "test_scenario";
  report.report_type = "VALIDATION";
  report.success = true;
  report.errors.push_back("No errors");
  report.warnings.push_back("Sample warning");
  report.info_messages.push_back("Test completed");
  report.statistics["test_stat"] = "42";
  
  assert(report.scenario_id == "test_scenario");
  assert(report.success == true);
  assert(report.warnings.size() == 1);
  
  std::cout << "All schema structure tests passed!" << std::endl;
  
  return 0;
}
