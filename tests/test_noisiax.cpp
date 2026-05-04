#include "noisiax/noisiax.hpp"

#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>

namespace fs = std::filesystem;

namespace {

int failures = 0;

void expect_true(bool condition, const std::string& message) {
    if (!condition) {
        std::cerr << "[FAIL] " << message << "\n";
        ++failures;
    }
}

void expect_eq(const std::string& actual, const std::string& expected, const std::string& message) {
    if (actual != expected) {
        std::cerr << "[FAIL] " << message << " (actual='" << actual
                  << "', expected='" << expected << "')\n";
        ++failures;
    }
}

fs::path scenario_path(const std::string& filename) {
    const fs::path local = fs::path("scenarios") / filename;
    if (fs::exists(local)) {
        return local;
    }

    const fs::path parent = fs::path("..") / "scenarios" / filename;
    if (fs::exists(parent)) {
        return parent;
    }

    throw std::runtime_error("Scenario fixture not found: " + filename);
}

std::string stat_or_empty(const noisiax::schema::ScenarioReport& report, const std::string& key) {
    const auto it = report.statistics.find(key);
    if (it == report.statistics.end()) {
        return "";
    }
    return it->second;
}

void set_numeric_var(noisiax::engine::SimulationState& state,
                     const noisiax::compiler::CompiledScenario& compiled,
                     const std::string& variable_id,
                     double value) {
    const auto handle_it = compiled.parameter_handles.find(variable_id);
    if (handle_it == compiled.parameter_handles.end()) {
        throw std::runtime_error("Variable not found in compiled scenario: " + variable_id);
    }

    const auto& handle = handle_it->second;
    switch (handle.type) {
        case noisiax::schema::VariableType::INTEGER:
            state.int_buffer().at(handle.buffer_offset) = static_cast<int64_t>(value);
            break;
        case noisiax::schema::VariableType::FLOAT:
            state.float_buffer().at(handle.buffer_offset) = value;
            break;
        case noisiax::schema::VariableType::BOOLEAN:
            state.bool_buffer().at(handle.buffer_offset) = (value != 0.0);
            break;
        default:
            throw std::runtime_error("Unsupported variable type in numeric setter");
    }
}

void test_validation() {
    const auto happy_report = noisiax::validate_scenario(scenario_path("happy_path.yaml").string());
    expect_true(happy_report.success, "Happy-path scenario must validate successfully");
    expect_eq(happy_report.scenario_id, "happy_path_demo_001", "Validation should preserve scenario_id");

    const auto cycle_report = noisiax::validate_scenario(scenario_path("failure_cycle.yaml").string());
    expect_true(!cycle_report.success, "Cycle fixture must fail validation");

    bool saw_cycle_message = false;
    for (const auto& error : cycle_report.errors) {
        if (error.find("Cyclic dependency detected") != std::string::npos) {
            saw_cycle_message = true;
            break;
        }
    }
    expect_true(saw_cycle_message, "Cycle validation failure should mention cyclic dependency");
}

void test_unknown_field_rejection() {
    const std::string yaml_with_unknown = R"(
scenario_id: "unknown_field_001"
schema_version: "1.0.0"
master_seed: 7
goal_statement: "Validate unknown field rejection behavior."
assumptions:
  - assumption_id: "assume_001"
    category: "test"
    description: "Unknown-field test assumption"
    rationale: "Exercise strict parser behavior"
    confidence_level: REJECT
entities:
  - entity_id: "entity_001"
    entity_type: "test_entity"
    description: "Entity for unknown-field test"
    attributes: {}
variables:
  - variable_id: "var_a"
    entity_ref: "entity_001"
    type: FLOAT
    default_value: 1.0
    description: "Variable A"
  - variable_id: "var_b"
    entity_ref: "entity_001"
    type: FLOAT
    default_value: 2.0
    description: "Variable B"
dependency_edges:
  - edge_id: "edge_001"
    source_variable: "var_a"
    target_variable: "var_b"
    propagation_function_id: "linear_scale"
    weight: 1.0
constraints:
  - constraint_id: "constraint_001"
    affected_variables: ["var_a"]
    constraint_expression: "var_a >= 0"
    enforcement_level: REJECT
    error_message: "var_a must be non-negative"
events: []
evaluation_criteria:
  - criterion_id: "eval_001"
    metric_name: "metric"
    aggregation_method: FINAL
    input_variables: ["var_b"]
    description: "Simple criterion"
hidden_variables:
  secret: 123
)";

    const auto report = noisiax::validate_scenario_from_string(yaml_with_unknown);
    expect_true(!report.success, "Scenario with unknown top-level field must fail validation");

    bool saw_unknown_message = false;
    for (const auto& error : report.errors) {
        if (error.find("Unknown field") != std::string::npos) {
            saw_unknown_message = true;
            break;
        }
    }
    expect_true(saw_unknown_message, "Unknown-field failure should mention unknown field");
}

void test_unsatisfied_initial_constraint_rejection() {
    const std::string unsat_constraint_yaml = R"(
scenario_id: "unsat_constraint_001"
schema_version: "1.0.0"
master_seed: 11
goal_statement: "Validate pre-run rejection for unsatisfied initial constraints."
assumptions:
  - assumption_id: "assume_001"
    category: "test"
    description: "Constraint pre-check assumption"
    rationale: "Exercise initial satisfiability gate"
    confidence_level: REJECT
entities:
  - entity_id: "entity_001"
    entity_type: "test_entity"
    description: "Entity for unsatisfied-constraint test"
    attributes: {}
variables:
  - variable_id: "var_x"
    entity_ref: "entity_001"
    type: FLOAT
    default_value: 1.0
    description: "Variable X"
  - variable_id: "var_y"
    entity_ref: "entity_001"
    type: FLOAT
    default_value: 0.0
    description: "Variable Y"
dependency_edges:
  - edge_id: "edge_001"
    source_variable: "var_x"
    target_variable: "var_y"
    propagation_function_id: "linear_scale"
    weight: 1.0
constraints:
  - constraint_id: "constraint_001"
    affected_variables: ["var_x"]
    constraint_expression: "var_x < 0"
    enforcement_level: REJECT
    error_message: "var_x must be negative at initialization"
events: []
evaluation_criteria:
  - criterion_id: "eval_001"
    metric_name: "metric"
    aggregation_method: FINAL
    input_variables: ["var_y"]
    description: "Simple criterion"
)";

    const auto validation_report = noisiax::validate_scenario_from_string(unsat_constraint_yaml);
    expect_true(!validation_report.success, "Unsatisfied initial constraints must fail validation");

    bool compile_threw = false;
    try {
        noisiax::serialization::YamlSerializer serializer;
        const auto scenario = serializer.deserialize(unsat_constraint_yaml);
        (void)noisiax::compile_scenario(scenario);
    } catch (...) {
        compile_threw = true;
    }
    expect_true(compile_threw, "Compilation should reject unsatisfied initial constraints");
}

void test_compile_and_run() {
    const auto compiled = noisiax::compile_scenario(scenario_path("happy_path.yaml").string());
    expect_eq(compiled.scenario_id, "happy_path_demo_001", "Compilation should preserve scenario_id");
    expect_true(compiled.total_variables == 4, "Happy path should compile 4 variables");
    expect_true(compiled.total_dependencies == 3, "Happy path should compile 3 dependency edges");
    expect_true(compiled.total_constraints == 2, "Happy path should compile 2 constraints");
    expect_true(compiled.total_events == 2, "Happy path should compile 2 events");

    const auto runtime_report = noisiax::run_scenario(compiled);
    expect_true(runtime_report.success, "Runtime on happy path must succeed");
    expect_eq(stat_or_empty(runtime_report, "processed_events"), "2", "Runtime should process both scheduled events");
    expect_true(!stat_or_empty(runtime_report, "state_fingerprint").empty(),
                "Runtime report should include a state fingerprint");
}

void test_determinism() {
    const auto first = noisiax::run_scenario(scenario_path("happy_path.yaml").string());
    const auto second = noisiax::run_scenario(scenario_path("happy_path.yaml").string());

    expect_true(first.success && second.success, "Determinism test requires successful runs");
    expect_eq(stat_or_empty(first, "state_fingerprint"),
              stat_or_empty(second, "state_fingerprint"),
              "Repeated runs should produce identical state fingerprints");
    expect_eq(stat_or_empty(first, "final_time"),
              stat_or_empty(second, "final_time"),
              "Repeated runs should produce identical final logical time");
}

void test_checkpoint_metadata() {
    const auto compiled = noisiax::compile_scenario(scenario_path("happy_path.yaml").string());

    noisiax::engine::SimulationState state;
    state.initialize(compiled, compiled.master_seed);
    state.set_current_time(4.5);
    if (!state.float_buffer().empty()) {
        state.float_buffer()[0] = 123.0;
    }

    const fs::path checkpoint = fs::temp_directory_path() / "noisiax_checkpoint_test.chk";
    const bool saved = noisiax::save_checkpoint(state, compiled.scenario_id, checkpoint.string());
    expect_true(saved, "Checkpoint save must succeed");

    noisiax::engine::SimulationState restored;
    const std::string scenario_id = noisiax::load_checkpoint(checkpoint.string(), restored);
    expect_eq(scenario_id, compiled.scenario_id, "Loaded checkpoint must restore original scenario_id metadata");

    expect_true(restored.current_time() == 4.5, "Checkpoint restore must preserve simulation time");
    if (!state.float_buffer().empty()) {
        expect_true(!restored.float_buffer().empty() && restored.float_buffer()[0] == 123.0,
                    "Checkpoint restore must preserve float buffer values");
    }

    std::error_code cleanup_error;
    fs::remove(checkpoint, cleanup_error);
}

void test_pull_recompute_semantics() {
    const auto compiled = noisiax::compile_scenario(scenario_path("happy_path.yaml").string());

    noisiax::engine::SimulationState state;
    state.initialize(compiled, compiled.master_seed);

    set_numeric_var(state, compiled, "var_base_price", 100.0);
    set_numeric_var(state, compiled, "var_demand", 250.0);
    set_numeric_var(state, compiled, "var_revenue", 0.0);

    noisiax::engine::DependencyGraph graph;
    graph.build_from_compiled(compiled);

    graph.push_invalidation("var_base_price", state);
    expect_true(state.is_stale("var_base_price"), "Source should be marked stale after push invalidation");
    expect_true(state.is_stale("var_revenue"), "Downstream should be marked stale after push invalidation");

    const bool recomputed = graph.pull_recompute("var_revenue", state);
    expect_true(recomputed, "Pull recompute should execute for stale target");
    expect_true(!state.is_stale("var_revenue"), "Pull recompute should clear stale flag on recomputed target");

    const bool recomputed_again = graph.pull_recompute("var_revenue", state);
    expect_true(!recomputed_again, "Pull recompute should no-op on clean target");
}

void test_scheduler_no_double_pop() {
    noisiax::compiler::CompiledScenario compiled;

    noisiax::schema::EventDescriptor e1;
    e1.event_id = "event_1";
    e1.event_type = "UNHANDLED";
    e1.timestamp = 1.0;

    noisiax::schema::EventDescriptor e2 = e1;
    e2.event_id = "event_2";
    e2.timestamp = 2.0;

    noisiax::schema::EventDescriptor e3 = e1;
    e3.event_id = "event_3";
    e3.timestamp = 3.0;

    compiled.event_queue = {
        {1.0, 0, "event_1", e1, false},
        {2.0, 0, "event_2", e2, false},
        {3.0, 0, "event_3", e3, false},
    };

    noisiax::scheduler::EventScheduler scheduler;
    scheduler.initialize(compiled);

    noisiax::engine::SimulationState dummy_state;
    const std::size_t consumed = scheduler.process_events_until(10.0, dummy_state);

    expect_true(consumed == 3, "Scheduler should consume all events even when callback is missing");
    expect_true(scheduler.event_history().size() == 3,
                "Scheduler history should include every consumed event (no double-pop skipping)");
    expect_true(scheduler.pending_count() == 0, "Scheduler queue should be empty after consuming all events");
}

}  // namespace

int main() {
    std::cout << "Running NoisiaX integration tests...\n";

    test_validation();
    test_unknown_field_rejection();
    test_unsatisfied_initial_constraint_rejection();
    test_compile_and_run();
    test_determinism();
    test_checkpoint_metadata();
    test_pull_recompute_semantics();
    test_scheduler_no_double_pop();

    if (failures == 0) {
        std::cout << "All tests passed\n";
        return 0;
    }

    std::cerr << failures << " test(s) failed\n";
    return 1;
}
