#include "noisiax/noisiax.hpp"
#include "noisiax/experiment/experiment.hpp"

#include <algorithm>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

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

bool has_error_containing(const noisiax::schema::ScenarioReport& report, const std::string& needle) {
    return std::any_of(report.errors.begin(), report.errors.end(), [&](const std::string& error) {
        return error.find(needle) != std::string::npos;
    });
}

bool has_event_type(const noisiax::RunResult& result, const std::string& event_type) {
    return std::any_of(result.events.begin(), result.events.end(), [&](const noisiax::EventTrace& event) {
        return event.event_type == event_type;
    });
}

std::size_t total_purchase_count(const noisiax::RunResult& result) {
    std::size_t count = 0;
    for (const auto& agent : result.final_state.agents) {
        count += agent.purchases.size();
    }
    return count;
}

bool any_agent_observed_purchase(const noisiax::RunResult& result) {
    return std::any_of(result.final_state.agents.begin(), result.final_state.agents.end(),
                       [](const noisiax::AgentFinalState& agent) {
                           return agent.observed_purchases > 0 && agent.influence_received > 0.0;
                       });
}

bool has_candidate_matching(const noisiax::RunResult& result,
                            const std::string& item_id,
                            bool affordable,
                            bool in_stock) {
    for (const auto& decision : result.decisions) {
        for (const auto& candidate : decision.candidates) {
            if (candidate.item_id == item_id &&
                candidate.affordable == affordable &&
                candidate.in_stock == in_stock) {
                return true;
            }
        }
    }
    return false;
}

bool has_positive_queue_penalty(const noisiax::RunResult& result) {
    for (const auto& decision : result.decisions) {
        for (const auto& candidate : decision.candidates) {
            if (candidate.queue_penalty > 0.0) {
                return true;
            }
        }
    }
    return false;
}

bool has_typed_component_type_id(const noisiax::schema::TypedLayerDefinition& typed_layer,
                                 const std::string& component_type_id) {
    return std::any_of(typed_layer.component_types.begin(), typed_layer.component_types.end(),
                       [&](const noisiax::schema::ComponentTypeDefinition& comp) {
                           return comp.component_type_id == component_type_id;
                       });
}

std::optional<double> parse_double(const std::string& text) {
    try {
        std::size_t processed = 0;
        const double value = std::stod(text, &processed);
        if (processed != text.size()) return std::nullopt;
        return value;
    } catch (...) {
        return std::nullopt;
    }
}

const noisiax::TypedEntityFinalState* find_typed_entity(const noisiax::TypedFinalStateSnapshot& snapshot,
                                                        const std::string& entity_id) {
    for (const auto& entity : snapshot.entities) {
        if (entity.entity_id == entity_id) {
            return &entity;
        }
    }
    return nullptr;
}

std::optional<noisiax::schema::TypedScalarValue> find_typed_field(const noisiax::TypedFinalStateSnapshot& snapshot,
                                                                  const std::string& entity_id,
                                                                  const std::string& component_type_id,
                                                                  const std::string& field_name) {
    const auto* entity = find_typed_entity(snapshot, entity_id);
    if (entity == nullptr) {
        return std::nullopt;
    }
    for (const auto& component : entity->components) {
        if (component.component_type_id != component_type_id) {
            continue;
        }
        const auto it = component.fields.find(field_name);
        if (it == component.fields.end()) {
            return std::nullopt;
        }
        return it->second;
    }
    return std::nullopt;
}

double typed_as_double(const noisiax::schema::TypedScalarValue& value) {
    if (const auto* v = std::get_if<double>(&value)) {
        return *v;
    }
    if (const auto* v_int = std::get_if<int64_t>(&value)) {
        return static_cast<double>(*v_int);
    }
    throw std::runtime_error("Expected numeric TypedScalarValue");
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

void test_detailed_v1_path() {
    noisiax::RunOptions options;
    options.trace_level = noisiax::TraceLevel::FULL;
    const auto result = noisiax::run_scenario_detailed(scenario_path("happy_path.yaml").string(), options);
    expect_true(result.report.success, "Detailed run on v1 scenario should succeed");
    expect_true(result.events.empty(), "v1 detailed fallback currently leaves event trace empty");
}

void test_market_butterfly_v2_determinism() {
    const auto compiled = noisiax::compile_scenario(scenario_path("market_butterfly_v2.yaml").string());
    expect_true(compiled.agent_layer.has_value(), "market_butterfly_v2 should compile with agent_layer");

    noisiax::RunOptions options;
    options.trace_level = noisiax::TraceLevel::FULL;
    options.include_final_state = true;
    options.include_causal_graph = true;

    const auto first = noisiax::run_scenario_detailed(compiled, options);
    const auto second = noisiax::run_scenario_detailed(compiled, options);

    expect_true(first.report.success && second.report.success,
                "market_butterfly_v2 detailed runs should succeed");
    expect_true(!first.events.empty(), "market_butterfly_v2 should produce event traces");
    expect_true(!first.decisions.empty(), "market_butterfly_v2 should produce decision traces");
    expect_true(!first.state_changes.empty(), "market_butterfly_v2 should produce state-change traces");
    expect_true(first.final_state.agents.size() == 10,
                "market_butterfly_v2 final state should include 10 agents");
    expect_true(first.final_state.shops.size() == 20,
                "market_butterfly_v2 final state should include 20 shops");
    expect_eq(stat_or_empty(first.report, "state_fingerprint"),
              stat_or_empty(second.report, "state_fingerprint"),
              "market_butterfly_v2 should be deterministic for same seed");
}

void test_market_butterfly_v2_seed_divergence() {
    const auto compiled = noisiax::compile_scenario(scenario_path("market_butterfly_v2.yaml").string());

    noisiax::RunOptions options_a;
    options_a.trace_level = noisiax::TraceLevel::DECISIONS;
    options_a.seed_override = 7;
    const auto run_a = noisiax::run_scenario_detailed(compiled, options_a);

    noisiax::RunOptions options_b;
    options_b.trace_level = noisiax::TraceLevel::DECISIONS;
    options_b.seed_override = 71;
    const auto run_b = noisiax::run_scenario_detailed(compiled, options_b);

    expect_true(run_a.report.success && run_b.report.success,
                "Seed divergence test requires successful runs");
    expect_true(stat_or_empty(run_a.report, "state_fingerprint") !=
                    stat_or_empty(run_b.report, "state_fingerprint"),
                "Different seeds should change final state fingerprint in market_butterfly_v2");
}

void test_v2_fixture_validation_and_compilation() {
    const std::vector<std::string> valid_fixtures = {
        "v2_minimal_purchase.yaml",
        "v2_social_influence.yaml",
        "v2_budget_stock_pressure.yaml",
        "v2_queue_capacity.yaml",
        "v2_max_events_limit.yaml",
    };

    for (const auto& fixture : valid_fixtures) {
        const auto report = noisiax::validate_scenario(scenario_path(fixture).string());
        expect_true(report.success, fixture + " should validate successfully");

        const auto compiled = noisiax::compile_scenario(scenario_path(fixture).string());
        expect_true(compiled.agent_layer.has_value(), fixture + " should compile with agent_layer");
    }

    const auto invalid_report =
        noisiax::validate_scenario(scenario_path("v2_invalid_agent_layer_reference.yaml").string());
    expect_true(!invalid_report.success, "v2 invalid reference fixture should fail validation");
    expect_true(has_error_containing(invalid_report, "references unknown location"),
                "v2 invalid reference fixture should identify the missing location reference");
}

void test_v2_minimal_purchase_fixture() {
    noisiax::RunOptions options;
    options.trace_level = noisiax::TraceLevel::FULL;

    const auto result = noisiax::run_scenario_detailed(
        scenario_path("v2_minimal_purchase.yaml").string(),
        options);

    expect_true(result.report.success, "v2 minimal purchase should run successfully");
    expect_true(has_event_type(result, "PURCHASE"), "v2 minimal purchase should emit a purchase event");
    expect_true(total_purchase_count(result) >= 1, "v2 minimal purchase should record at least one purchase");
    expect_true(!result.state_changes.empty(), "v2 minimal purchase should trace state changes");
}

void test_v2_social_influence_fixture() {
    noisiax::RunOptions options;
    options.trace_level = noisiax::TraceLevel::FULL;

    const auto result = noisiax::run_scenario_detailed(
        scenario_path("v2_social_influence.yaml").string(),
        options);

    expect_true(result.report.success, "v2 social influence should run successfully");
    expect_true(has_event_type(result, "OBSERVE_PURCHASE"),
                "v2 social influence should emit observation events");
    expect_true(any_agent_observed_purchase(result),
                "v2 social influence should update observed purchase and influence counters");
}

void test_v2_budget_stock_pressure_fixture() {
    noisiax::RunOptions options;
    options.trace_level = noisiax::TraceLevel::DECISIONS;

    const auto result = noisiax::run_scenario_detailed(
        scenario_path("v2_budget_stock_pressure.yaml").string(),
        options);

    expect_true(result.report.success, "v2 budget and stock pressure should run successfully");
    expect_true(total_purchase_count(result) == 1,
                "v2 budget and stock pressure should buy the single cheap item once");
    expect_true(has_candidate_matching(result, "premium_plate", false, true),
                "v2 budget and stock pressure should trace an unaffordable in-stock candidate");
    expect_true(has_candidate_matching(result, "single_cheap_bite", true, false),
                "v2 budget and stock pressure should trace the affordable but depleted cheap candidate");
}

void test_v2_queue_capacity_fixture() {
    noisiax::RunOptions options;
    options.trace_level = noisiax::TraceLevel::DECISIONS;

    const auto result = noisiax::run_scenario_detailed(
        scenario_path("v2_queue_capacity.yaml").string(),
        options);

    expect_true(result.report.success, "v2 queue capacity should run successfully");
    expect_true(total_purchase_count(result) >= 1, "v2 queue capacity should complete at least one purchase");
    expect_true(has_positive_queue_penalty(result),
                "v2 queue capacity should expose positive queue penalties in decision traces");
}

void test_v2_max_events_limit_fixture() {
    noisiax::RunOptions options;
    options.trace_level = noisiax::TraceLevel::EVENTS;

    const auto result = noisiax::run_scenario_detailed(
        scenario_path("v2_max_events_limit.yaml").string(),
        options);

    expect_true(!result.report.success, "v2 max-events fixture should halt with a runtime failure");
    expect_true(has_error_containing(result.report, "max_events limit reached"),
                "v2 max-events fixture should report the max_events limit clearly");
}

void test_v3_fixture_validation_and_compilation() {
    const std::vector<std::string> valid_fixtures = {
        "v3_particle_motion.yaml",
        "v3_seeded_branching.yaml",
        "v3_event_emission.yaml",
        "v3_relation_decay.yaml",
        "v3_atom_bonding.yaml",
        "v3_max_events_limit.yaml",
        "v3_relation_bound_violation.yaml"
    };

    for (const auto& fixture : valid_fixtures) {
        const auto report = noisiax::validate_scenario(scenario_path(fixture).string());
        expect_true(report.success, fixture + " should validate successfully");

        const auto compiled = noisiax::compile_scenario(scenario_path(fixture).string());
        expect_true(compiled.typed_layer.has_value(), fixture + " should compile with typed_layer");
    }

    const auto unknown_component_report =
        noisiax::validate_scenario(scenario_path("v3_invalid_unknown_component.yaml").string());
    expect_true(!unknown_component_report.success, "v3 unknown component fixture should fail validation");
    expect_true(has_error_containing(unknown_component_report, "references unknown component_type"),
                "v3 unknown component fixture should identify missing component reference");

    const auto type_mismatch_report =
        noisiax::validate_scenario(scenario_path("v3_invalid_field_type_mismatch.yaml").string());
    expect_true(!type_mismatch_report.success, "v3 field type mismatch fixture should fail validation");
    expect_true(has_error_containing(type_mismatch_report, "field type mismatch"),
                "v3 field type mismatch fixture should identify the mismatch");

    const auto bad_write_report =
        noisiax::validate_scenario(scenario_path("v3_invalid_write_undeclared_field.yaml").string());
    expect_true(!bad_write_report.success, "v3 undeclared field write fixture should fail validation");
    expect_true(has_error_containing(bad_write_report, "writes undeclared field"),
                "v3 undeclared field write fixture should identify the missing field");

    const auto both_layers_report =
        noisiax::validate_scenario(scenario_path("v3_invalid_both_layers.yaml").string());
    expect_true(!both_layers_report.success, "v3 both-layers fixture should fail validation");
    expect_true(has_error_containing(both_layers_report, "both agent_layer and typed_layer"),
                "v3 both-layers fixture should reject mixed layer scenarios");
}

void test_v3_particle_motion_runtime() {
    noisiax::RunOptions options;
    options.trace_level = noisiax::TraceLevel::FULL;
    // Limit the run so the fixture asserts the state after three ticks at t=0,1,2.
    options.max_time = 2.0;

    const auto result = noisiax::run_scenario_detailed(
        scenario_path("v3_particle_motion.yaml").string(),
        options);

    expect_true(result.report.success, "v3 particle motion should run successfully");
    expect_true(result.typed_final_state.has_value(), "v3 particle motion should emit typed_final_state");
    expect_true(!result.state_changes.empty(), "v3 particle motion should trace state changes at FULL trace");

    const auto& snapshot = *result.typed_final_state;
    const auto p1x = find_typed_field(snapshot, "p1", "Position", "x");
    const auto p1y = find_typed_field(snapshot, "p1", "Position", "y");
    const auto p2x = find_typed_field(snapshot, "p2", "Position", "x");
    const auto p2y = find_typed_field(snapshot, "p2", "Position", "y");

    expect_true(p1x.has_value() && p1y.has_value() && p2x.has_value() && p2y.has_value(),
                "v3 particle motion should expose Position component fields");
    if (p1x.has_value() && p1y.has_value()) {
        expect_true(typed_as_double(*p1x) == 3.0, "p1.Position.x should be 3.0 after 3 ticks");
        expect_true(typed_as_double(*p1y) == 1.5, "p1.Position.y should be 1.5 after 3 ticks");
    }
    if (p2x.has_value() && p2y.has_value()) {
        expect_true(typed_as_double(*p2x) == 4.0, "p2.Position.x should be 4.0 after 3 ticks");
        expect_true(typed_as_double(*p2y) == -1.0, "p2.Position.y should remain -1.0");
    }
}

void test_v3_seeded_branching_determinism() {
    noisiax::RunOptions options;
    options.trace_level = noisiax::TraceLevel::EVENTS;

    const auto first = noisiax::run_scenario_detailed(
        scenario_path("v3_seeded_branching.yaml").string(),
        options);
    const auto second = noisiax::run_scenario_detailed(
        scenario_path("v3_seeded_branching.yaml").string(),
        options);

    expect_true(first.report.success && second.report.success, "v3 seeded branching determinism requires successful runs");
    expect_eq(stat_or_empty(first.report, "state_fingerprint"),
              stat_or_empty(second.report, "state_fingerprint"),
              "v3 seeded branching should produce identical state fingerprints for the same seed");

    expect_true(!first.events.empty() && !second.events.empty(), "v3 seeded branching should emit at least one event trace");
    if (!first.events.empty() && !second.events.empty()) {
        expect_true(!first.events[0].random_draws.empty() && !second.events[0].random_draws.empty(),
                    "v3 seeded branching should trace random draws on events");
        if (!first.events[0].random_draws.empty() && !second.events[0].random_draws.empty()) {
            expect_true(first.events[0].random_draws[0].raw_u64 == second.events[0].random_draws[0].raw_u64,
                        "v3 seeded branching should reproduce identical RNG raw_u64 for the same seed");
        }
    }

    options.seed_override = 2;
    const auto different_seed = noisiax::run_scenario_detailed(
        scenario_path("v3_seeded_branching.yaml").string(),
        options);
    expect_true(different_seed.report.success, "v3 seeded branching should run successfully with a seed override");
    expect_true(!different_seed.events.empty(), "v3 seeded branching should emit events with seed override");
    if (!first.events.empty() && !first.events[0].random_draws.empty() &&
        !different_seed.events.empty() && !different_seed.events[0].random_draws.empty()) {
        expect_true(first.events[0].random_draws[0].raw_u64 != different_seed.events[0].random_draws[0].raw_u64,
                    "v3 seeded branching RNG raw_u64 should differ across seeds");
    }
}

void test_v3_event_emission_fixture() {
    noisiax::RunOptions options;
    options.trace_level = noisiax::TraceLevel::EVENTS;

    const auto result = noisiax::run_scenario_detailed(
        scenario_path("v3_event_emission.yaml").string(),
        options);

    expect_true(result.report.success, "v3 event emission should run successfully");
    expect_true(has_event_type(result, "start"), "v3 event emission should record the start event");
    expect_true(has_event_type(result, "child"), "v3 event emission should record the emitted child event");

    uint64_t start_id = 0;
    uint64_t child_id = 0;
    std::vector<uint64_t> child_parents;
    for (const auto& event : result.events) {
        if (event.event_type == "start") {
            start_id = event.event_id;
        }
        if (event.event_type == "child") {
            child_id = event.event_id;
            child_parents = event.causal_parent_event_ids;
        }
    }

    expect_true(start_id != 0 && child_id != 0, "v3 event emission should assign event ids");
    if (start_id != 0 && !child_parents.empty()) {
        expect_true(child_parents[0] == start_id, "v3 emitted child event should include the start event as causal parent");
    }
}

void test_v3_relation_decay_fixture() {
    const auto result = noisiax::run_scenario_detailed(
        scenario_path("v3_relation_decay.yaml").string(),
        {});

    expect_true(result.report.success, "v3 relation decay should run successfully");
    expect_true(result.typed_final_state.has_value(), "v3 relation decay should emit typed_final_state");
    if (result.typed_final_state.has_value()) {
        expect_true(result.typed_final_state->relations.empty(), "v3 relation decay should expire the initial relation");
    }
}

void test_v3_atom_bonding_fixture() {
    const auto result = noisiax::run_scenario_detailed(
        scenario_path("v3_atom_bonding.yaml").string(),
        {});

    expect_true(result.report.success, "v3 atom bonding should run successfully");
    expect_true(result.typed_final_state.has_value(), "v3 atom bonding should emit typed_final_state");
    if (result.typed_final_state.has_value()) {
        expect_true(result.typed_final_state->relations.size() == 1,
                    "v3 atom bonding should create exactly one bond relation");
        if (result.typed_final_state->relations.size() == 1) {
            const auto& bond = result.typed_final_state->relations[0];
            expect_true(bond.relation_type_id == "bond", "v3 atom bonding should create a bond relation_type_id");
            expect_true(bond.source_entity_id == "a1" && bond.target_entity_id == "a2",
                        "v3 atom bonding should bond a1 -> a2 deterministically");
        }
    }
}

void test_v3_max_events_limit_fixture() {
    noisiax::RunOptions options;
    options.trace_level = noisiax::TraceLevel::EVENTS;

    const auto result = noisiax::run_scenario_detailed(
        scenario_path("v3_max_events_limit.yaml").string(),
        options);

    expect_true(!result.report.success, "v3 max-events fixture should halt with a runtime failure");
    expect_true(has_error_containing(result.report, "max_events limit reached"),
                "v3 max-events fixture should report the max_events limit clearly");
}

void test_v3_relation_bound_violation_fixture() {
    const auto result = noisiax::run_scenario_detailed(
        scenario_path("v3_relation_bound_violation.yaml").string(),
        {});

    expect_true(!result.report.success, "v3 relation bound violation should halt with a runtime failure");
    expect_true(has_error_containing(result.report, "Relation bound exceeded"),
                "v3 relation bound violation should report the relation bound clearly");
}

void test_v4_composition_resolve_namespaces() {
    const auto path = scenario_path("v4_composition_base.yaml").string();

    const auto resolved = noisiax::experiment::resolve_scenario(path);
    expect_true(resolved.scenario.typed_layer.has_value(), "v4 resolve should preserve typed_layer");
    expect_true(resolved.canonical_yaml.find("imports:") == std::string::npos,
                "v4 resolve output should not contain imports stanza");

    const auto& typed = *resolved.scenario.typed_layer;
    expect_true(has_typed_component_type_id(typed, "frag_a__Branch"),
                "v4 resolve should namespace component ids (frag_a__Branch)");
    expect_true(has_typed_component_type_id(typed, "frag_b__Counter"),
                "v4 resolve should namespace component ids (frag_b__Counter)");

    const auto again = noisiax::experiment::resolve_scenario(path);
    expect_eq(resolved.resolved_hash, again.resolved_hash, "v4 resolve resolved_hash should be stable");
}

void test_v4_composition_rejects_circular_imports() {
    bool threw = false;
    try {
        (void)noisiax::experiment::resolve_scenario(scenario_path("v4_circular_a.yaml").string());
    } catch (const std::exception& ex) {
        threw = true;
        expect_true(std::string(ex.what()).find("Circular import detected") != std::string::npos,
                    "v4 resolve should report circular imports");
    }
    expect_true(threw, "v4 resolve should throw on circular imports");
}

void test_v4_composition_rejects_duplicate_ids() {
    bool threw = false;
    try {
        (void)noisiax::experiment::resolve_scenario(scenario_path("v4_duplicate_ids_base.yaml").string());
    } catch (const std::exception& ex) {
        threw = true;
        const std::string msg = ex.what();
        expect_true(msg.find("Duplicate resolved") != std::string::npos,
                    "v4 resolve should reject duplicate ids");
    }
    expect_true(threw, "v4 resolve should throw on duplicate ids");
}

void test_v4_experiment_determinism_and_aggregation() {
    noisiax::experiment::ExperimentDefinition def;
    def.experiment_id = "test_v4_det";
    def.base_scenario = scenario_path("v4_rng_value.yaml").string();
    def.seed_plan.seeds = {1, 2, 3};

    noisiax::experiment::ExperimentMetric metric;
    metric.metric_id = "sample_value";
    metric.kind = "typed_field_final";
    noisiax::experiment::TypedFieldTarget tf;
    tf.entity_id = "n1";
    tf.component_type_id = "Sample";
    tf.field_name = "value";
    metric.typed_field = tf;
    def.metrics.push_back(metric);

    noisiax::experiment::ExperimentOptions options_a;
    options_a.output_dir = "test_output_v4/exp_det_a";

    noisiax::experiment::ExperimentOptions options_b;
    options_b.output_dir = "test_output_v4/exp_det_b";

    const auto a = noisiax::experiment::run_experiment(def, options_a);
    const auto b = noisiax::experiment::run_experiment(def, options_b);

    expect_true(a.runs.size() == b.runs.size(), "v4 experiment should produce same number of runs");
    expect_true(a.aggregates == b.aggregates, "v4 experiment aggregates should be deterministic");

    for (std::size_t i = 0; i < a.runs.size() && i < b.runs.size(); ++i) {
        expect_eq(a.runs[i].final_fingerprint, b.runs[i].final_fingerprint, "v4 experiment run fingerprint should match");
        expect_true(a.runs[i].metrics == b.runs[i].metrics, "v4 experiment run metrics should match");
    }
}

void test_v4_experiment_seed_change_changes_output() {
    auto make_def = [&](uint64_t seed) {
        noisiax::experiment::ExperimentDefinition def;
        def.experiment_id = "test_v4_seed_change_" + std::to_string(seed);
        def.base_scenario = scenario_path("v4_rng_value.yaml").string();
        def.seed_plan.seeds = {seed};

        noisiax::experiment::ExperimentMetric metric;
        metric.metric_id = "sample_value";
        metric.kind = "typed_field_final";
        noisiax::experiment::TypedFieldTarget tf;
        tf.entity_id = "n1";
        tf.component_type_id = "Sample";
        tf.field_name = "value";
        metric.typed_field = tf;
        def.metrics.push_back(metric);
        return def;
    };

    noisiax::experiment::ExperimentOptions options_a;
    options_a.output_dir = "test_output_v4/exp_seed_1";
    const auto a = noisiax::experiment::run_experiment(make_def(1), options_a);

    noisiax::experiment::ExperimentOptions options_b;
    options_b.output_dir = "test_output_v4/exp_seed_2";
    const auto b = noisiax::experiment::run_experiment(make_def(2), options_b);

    expect_true(!a.runs.empty() && !b.runs.empty(), "v4 experiment should produce at least one run");

    const auto it_a = a.runs.front().metrics.find("sample_value");
    const auto it_b = b.runs.front().metrics.find("sample_value");
    expect_true(it_a != a.runs.front().metrics.end(), "v4 experiment should record sample_value metric");
    expect_true(it_b != b.runs.front().metrics.end(), "v4 experiment should record sample_value metric");
    if (it_a == a.runs.front().metrics.end() || it_b == b.runs.front().metrics.end()) {
        return;
    }

    const auto v_a = parse_double(it_a->second);
    const auto v_b = parse_double(it_b->second);
    expect_true(v_a.has_value() && v_b.has_value(), "v4 experiment metric should be parseable as a double");
    if (!v_a.has_value() || !v_b.has_value()) {
        return;
    }
    expect_true(v_a.value() != v_b.value(), "v4 experiment metric should change when seed changes");
}

void test_v4_experiment_failed_run_reporting_and_fail_fast() {
    noisiax::experiment::ExperimentDefinition def;
    def.experiment_id = "test_v4_invalid_overlay";
    def.base_scenario = scenario_path("v4_rng_value.yaml").string();
    def.seed_plan.seeds = {1, 2};
    def.fail_fast = false;

    noisiax::experiment::StochasticOverlay overlay;
    overlay.overlay_id = "bad_value_type";
    overlay.sampler = noisiax::experiment::SamplerType::CHOICE;
    noisiax::experiment::TypedFieldTarget tf;
    tf.entity_id = "n1";
    tf.component_type_id = "Sample";
    tf.field_name = "value";
    overlay.target.typed_field = tf;

    noisiax::experiment::JsonValue::Object params;
    params.emplace("values", noisiax::experiment::JsonValue::Array{noisiax::experiment::JsonValue("bad")});
    overlay.params = noisiax::experiment::JsonValue(std::move(params));
    def.overlays.push_back(std::move(overlay));

    noisiax::experiment::ExperimentOptions options;
    options.output_dir = "test_output_v4/exp_invalid_overlay_nonfailfast";
    const auto result = noisiax::experiment::run_experiment(def, options);

    expect_true(result.runs.size() == 2, "v4 experiment should continue after failed runs when fail_fast=false");
    if (result.runs.size() >= 2) {
        expect_true(!result.runs[0].success && !result.runs[1].success,
                    "v4 experiment invalid overlay should cause each run to fail");
    }

    noisiax::experiment::ExperimentDefinition ff = def;
    ff.experiment_id = "test_v4_invalid_overlay_failfast";
    ff.fail_fast = true;
    noisiax::experiment::ExperimentOptions options_ff;
    options_ff.output_dir = "test_output_v4/exp_invalid_overlay_failfast";

    bool threw = false;
    try {
        (void)noisiax::experiment::run_experiment(ff, options_ff);
    } catch (...) {
        threw = true;
    }
    expect_true(threw, "v4 experiment should throw when fail_fast=true and a run throws");
}

void test_v4_experiment_rejects_invalid_override_targets() {
    {
        noisiax::experiment::ExperimentDefinition def;
        def.experiment_id = "test_v4_invalid_override_ptr";
        def.base_scenario = scenario_path("v4_rng_value.yaml").string();
        def.seed_plan.seeds = {1};

        noisiax::experiment::ScenarioOverride bad_ptr;
        bad_ptr.override_id = "bad_ptr";
        bad_ptr.op = noisiax::experiment::OverrideOp::REPLACE;
        bad_ptr.target.json_pointer = "/typed_layer/entities/999/components/Sample/value";
        bad_ptr.value = noisiax::experiment::JsonValue(1.0);
        def.global_overrides.push_back(bad_ptr);

        noisiax::experiment::ExperimentOptions options;
        options.output_dir = "test_output_v4/exp_invalid_override_ptr";

        const auto result = noisiax::experiment::run_experiment(def, options);
        expect_true(result.runs.size() == 1, "v4 experiment should produce one run for invalid override test");
        if (!result.runs.empty()) {
            expect_true(!result.runs[0].success, "v4 experiment should fail when JSON pointer override is invalid");
            expect_true(!result.runs[0].errors.empty(), "v4 experiment should report an error for invalid JSON pointer override");
            if (!result.runs[0].errors.empty()) {
                expect_true(result.runs[0].errors[0].find("JSON pointer") != std::string::npos,
                            "v4 experiment invalid JSON pointer error should mention JSON pointer");
            }
        }
    }

    {
        noisiax::experiment::ExperimentDefinition def;
        def.experiment_id = "test_v4_invalid_override_typed_field";
        def.base_scenario = scenario_path("v4_rng_value.yaml").string();
        def.seed_plan.seeds = {1};

        noisiax::experiment::ScenarioOverride bad_tf;
        bad_tf.override_id = "bad_typed_field";
        bad_tf.op = noisiax::experiment::OverrideOp::REPLACE;
        noisiax::experiment::TypedFieldTarget tf;
        tf.entity_id = "missing_entity";
        tf.component_type_id = "Sample";
        tf.field_name = "value";
        bad_tf.target.typed_field = tf;
        bad_tf.value = noisiax::experiment::JsonValue(1.0);
        def.global_overrides.push_back(bad_tf);

        noisiax::experiment::ExperimentOptions options;
        options.output_dir = "test_output_v4/exp_invalid_override_typed_field";

        const auto result = noisiax::experiment::run_experiment(def, options);
        expect_true(result.runs.size() == 1, "v4 experiment should produce one run for invalid typed_field override test");
        if (!result.runs.empty()) {
            expect_true(!result.runs[0].success, "v4 experiment should fail when typed_field override target is invalid");
            expect_true(!result.runs[0].errors.empty(), "v4 experiment should report an error for invalid typed_field override");
            if (!result.runs[0].errors.empty()) {
                expect_true(result.runs[0].errors[0].find("typed_field") != std::string::npos,
                            "v4 experiment invalid typed_field override error should mention typed_field");
            }
        }
    }
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
    test_detailed_v1_path();
    test_market_butterfly_v2_determinism();
    test_market_butterfly_v2_seed_divergence();
    test_v2_fixture_validation_and_compilation();
    test_v2_minimal_purchase_fixture();
    test_v2_social_influence_fixture();
    test_v2_budget_stock_pressure_fixture();
    test_v2_queue_capacity_fixture();
    test_v2_max_events_limit_fixture();
    test_v3_fixture_validation_and_compilation();
    test_v3_particle_motion_runtime();
    test_v3_seeded_branching_determinism();
    test_v3_event_emission_fixture();
    test_v3_relation_decay_fixture();
    test_v3_atom_bonding_fixture();
    test_v3_max_events_limit_fixture();
    test_v3_relation_bound_violation_fixture();
    test_v4_composition_resolve_namespaces();
    test_v4_composition_rejects_circular_imports();
    test_v4_composition_rejects_duplicate_ids();
    test_v4_experiment_determinism_and_aggregation();
    test_v4_experiment_seed_change_changes_output();
    test_v4_experiment_failed_run_reporting_and_fail_fast();
    test_v4_experiment_rejects_invalid_override_targets();

    if (failures == 0) {
        std::cout << "All tests passed\n";
        return 0;
    }

    std::cerr << failures << " test(s) failed\n";
    return 1;
}
