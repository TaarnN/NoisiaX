#include "noisiax/noisiax.hpp"
#include "noisiax/engine/agent_runtime.hpp"
#include "noisiax/engine/typed_runtime.hpp"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <functional>
#include <iomanip>
#include <numeric>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <variant>

namespace noisiax {
namespace {

using ScalarValue = std::variant<int64_t, double, std::string, bool>;

std::string to_lower_copy(std::string_view text) {
    std::string lowered(text);
    std::transform(lowered.begin(), lowered.end(), lowered.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return lowered;
}

bool parse_double_strict(const std::string& text, double& out) {
    try {
        std::size_t processed = 0;
        out = std::stod(text, &processed);
        return processed == text.size();
    } catch (...) {
        return false;
    }
}

bool parse_bool_strict(const std::string& text, bool& out) {
    std::string lowered = to_lower_copy(text);
    if (lowered == "true" || lowered == "1" || lowered == "yes") {
        out = true;
        return true;
    }
    if (lowered == "false" || lowered == "0" || lowered == "no") {
        out = false;
        return true;
    }
    return false;
}

schema::ScenarioReport make_validation_report(const validation::ValidationReport& validation_report) {
    schema::ScenarioReport report;
    report.scenario_id = validation_report.scenario_id;
    report.report_type = "VALIDATION";
    report.success = validation_report.overall_passed;
    report.errors = validation_report.errors;
    report.warnings = validation_report.warnings;

    for (const auto& result : validation_report.results) {
        if (result.passed) {
            report.info_messages.push_back(result.rule_name + ": " + result.message);
        }
    }

    report.statistics["checks_total"] = std::to_string(validation_report.results.size());
    report.statistics["errors"] = std::to_string(validation_report.errors.size());
    report.statistics["warnings"] = std::to_string(validation_report.warnings.size());
    return report;
}

std::optional<double> get_numeric_value(
    const compiler::CompiledScenario& compiled,
    const engine::SimulationState& state,
    const std::string& variable_id) {

    auto handle_it = compiled.parameter_handles.find(variable_id);
    if (handle_it == compiled.parameter_handles.end()) {
        return std::nullopt;
    }

    const auto& handle = handle_it->second;
    switch (handle.type) {
        case schema::VariableType::INTEGER:
            if (handle.buffer_offset < state.int_buffer().size()) {
                return static_cast<double>(state.int_buffer()[handle.buffer_offset]);
            }
            break;
        case schema::VariableType::FLOAT:
            if (handle.buffer_offset < state.float_buffer().size()) {
                return state.float_buffer()[handle.buffer_offset];
            }
            break;
        case schema::VariableType::BOOLEAN:
            if (handle.buffer_offset < state.bool_buffer().size()) {
                return state.bool_buffer()[handle.buffer_offset] ? 1.0 : 0.0;
            }
            break;
        default:
            break;
    }

    return std::nullopt;
}

bool set_scalar_value(
    const compiler::CompiledScenario& compiled,
    engine::SimulationState& state,
    const std::string& variable_id,
    const ScalarValue& value) {

    auto handle_it = compiled.parameter_handles.find(variable_id);
    if (handle_it == compiled.parameter_handles.end()) {
        return false;
    }

    const auto& handle = handle_it->second;
    const std::size_t offset = handle.buffer_offset;

    switch (handle.type) {
        case schema::VariableType::INTEGER: {
            if (offset >= state.int_buffer().size()) {
                return false;
            }

            if (const auto* int_value = std::get_if<int64_t>(&value)) {
                state.int_buffer()[offset] = *int_value;
                return true;
            }
            if (const auto* float_value = std::get_if<double>(&value)) {
                state.int_buffer()[offset] = static_cast<int64_t>(std::llround(*float_value));
                return true;
            }
            if (const auto* bool_value = std::get_if<bool>(&value)) {
                state.int_buffer()[offset] = *bool_value ? 1 : 0;
                return true;
            }
            if (const auto* string_value = std::get_if<std::string>(&value)) {
                double parsed = 0.0;
                if (!parse_double_strict(*string_value, parsed)) {
                    return false;
                }
                state.int_buffer()[offset] = static_cast<int64_t>(std::llround(parsed));
                return true;
            }
            return false;
        }
        case schema::VariableType::FLOAT: {
            if (offset >= state.float_buffer().size()) {
                return false;
            }

            if (const auto* float_value = std::get_if<double>(&value)) {
                state.float_buffer()[offset] = *float_value;
                return true;
            }
            if (const auto* int_value = std::get_if<int64_t>(&value)) {
                state.float_buffer()[offset] = static_cast<double>(*int_value);
                return true;
            }
            if (const auto* bool_value = std::get_if<bool>(&value)) {
                state.float_buffer()[offset] = *bool_value ? 1.0 : 0.0;
                return true;
            }
            if (const auto* string_value = std::get_if<std::string>(&value)) {
                double parsed = 0.0;
                if (!parse_double_strict(*string_value, parsed)) {
                    return false;
                }
                state.float_buffer()[offset] = parsed;
                return true;
            }
            return false;
        }
        case schema::VariableType::BOOLEAN: {
            if (offset >= state.bool_buffer().size()) {
                return false;
            }

            if (const auto* bool_value = std::get_if<bool>(&value)) {
                state.bool_buffer()[offset] = *bool_value;
                return true;
            }
            if (const auto* int_value = std::get_if<int64_t>(&value)) {
                state.bool_buffer()[offset] = (*int_value != 0);
                return true;
            }
            if (const auto* float_value = std::get_if<double>(&value)) {
                state.bool_buffer()[offset] = (*float_value != 0.0);
                return true;
            }
            if (const auto* string_value = std::get_if<std::string>(&value)) {
                bool parsed = false;
                if (!parse_bool_strict(*string_value, parsed)) {
                    return false;
                }
                state.bool_buffer()[offset] = parsed;
                return true;
            }
            return false;
        }
        case schema::VariableType::STRING: {
            if (offset >= state.string_buffer().size()) {
                return false;
            }

            if (const auto* string_value = std::get_if<std::string>(&value)) {
                state.string_buffer()[offset] = *string_value;
                return true;
            }
            if (const auto* int_value = std::get_if<int64_t>(&value)) {
                state.string_buffer()[offset] = std::to_string(*int_value);
                return true;
            }
            if (const auto* float_value = std::get_if<double>(&value)) {
                std::ostringstream oss;
                oss << *float_value;
                state.string_buffer()[offset] = oss.str();
                return true;
            }
            if (const auto* bool_value = std::get_if<bool>(&value)) {
                state.string_buffer()[offset] = *bool_value ? "true" : "false";
                return true;
            }
            return false;
        }
        default:
            return false;
    }
}

void apply_initial_values(const compiler::CompiledScenario& compiled, engine::SimulationState& state) {
    for (const auto& [variable_id, value] : compiled.initial_values) {
        set_scalar_value(compiled, state, variable_id, value);
    }
}

void apply_event_payload(
    const schema::EventDescriptor& event,
    const compiler::CompiledScenario& compiled,
    engine::SimulationState& state,
    engine::DependencyGraph& graph) {

    auto set_variable_from_payload = [&](const std::string& variable_id,
                                         const std::string& payload_value,
                                         bool additive) {
        if (additive) {
            double delta = 0.0;
            if (!parse_double_strict(payload_value, delta)) {
                return;
            }

            auto current = get_numeric_value(compiled, state, variable_id);
            if (!current.has_value()) {
                return;
            }

            if (!set_scalar_value(compiled, state, variable_id, *current + delta)) {
                return;
            }
            graph.push_invalidation(variable_id, state);
            return;
        }

        if (set_scalar_value(compiled, state, variable_id, payload_value)) {
            graph.push_invalidation(variable_id, state);
        }
    };

    const auto& payload = event.event_payload;

    auto variable_id_it = payload.find("variable_id");
    if (variable_id_it == payload.end()) {
        variable_id_it = payload.find("target_variable");
    }

    if (variable_id_it != payload.end()) {
        auto value_it = payload.find("value");
        if (value_it == payload.end()) {
            value_it = payload.find("new_value");
        }
        if (value_it != payload.end()) {
            set_variable_from_payload(variable_id_it->second, value_it->second, false);
        }

        auto delta_it = payload.find("delta");
        if (delta_it != payload.end()) {
            set_variable_from_payload(variable_id_it->second, delta_it->second, true);
        }
    }

    auto action_it = payload.find("action");
    if (action_it != payload.end()) {
        if (action_it->second == "update_demand") {
            auto new_value_it = payload.find("new_value");
            if (new_value_it != payload.end()) {
                set_variable_from_payload("var_demand", new_value_it->second, false);
            }
        } else if (action_it->second == "apply_promotion") {
            auto discount_it = payload.find("discount_increase");
            if (discount_it != payload.end()) {
                set_variable_from_payload("var_discount_rate", discount_it->second, true);
            }
        }
    }
}

class ConstraintExpressionParser {
public:
    using Resolver = std::function<std::optional<double>(std::string_view)>;

    ConstraintExpressionParser(std::string_view expression, Resolver resolver)
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
    Resolver resolver_;

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
            const bool rhs = parse_logical_and();
            value = value || rhs;
        }
        return value;
    }

    bool parse_logical_and() {
        bool value = parse_comparison();
        while (consume("&&")) {
            const bool rhs = parse_comparison();
            value = value && rhs;
        }
        return value;
    }

    bool parse_comparison() {
        const double lhs = parse_additive();

        if (consume(">=")) {
            return lhs >= parse_additive();
        }
        if (consume("<=")) {
            return lhs <= parse_additive();
        }
        if (consume("==")) {
            return lhs == parse_additive();
        }
        if (consume("!=")) {
            return lhs != parse_additive();
        }
        if (consume(">")) {
            return lhs > parse_additive();
        }
        if (consume("<")) {
            return lhs < parse_additive();
        }

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
        if (consume("+")) {
            return parse_unary();
        }
        if (consume("-")) {
            return -parse_unary();
        }
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

        if (!saw_digit) {
            throw std::runtime_error("Invalid numeric literal in constraint expression");
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

        double number = 0.0;
        const std::string token(expression_.substr(start, position_ - start));
        if (!parse_double_strict(token, number)) {
            throw std::runtime_error("Invalid numeric literal in constraint expression");
        }

        return number;
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
        auto resolved = resolver_(variable);
        if (!resolved.has_value()) {
            throw std::runtime_error("Unknown or non-numeric variable in constraint expression: " +
                                     std::string(variable));
        }
        return *resolved;
    }
};

bool enforce_constraints(
    const compiler::CompiledScenario& compiled,
    const engine::SimulationState& state,
    schema::ScenarioReport& report) {

    for (const auto& program : compiled.constraint_programs) {
        ConstraintExpressionParser parser(
            program.compiled_expression,
            [&](std::string_view variable) -> std::optional<double> {
                return get_numeric_value(compiled, state, std::string(variable));
            });

        bool passed = false;
        try {
            passed = parser.evaluate();
        } catch (const std::exception& ex) {
            report.errors.push_back("Constraint parse/eval failure for " + program.constraint_id +
                                    ": " + ex.what());
            report.success = false;
            return false;
        }

        if (passed) {
            continue;
        }

        const std::string failure_message =
            program.error_message.empty()
                ? ("Constraint violated: " + program.constraint_id)
                : (program.constraint_id + ": " + program.error_message);

        if (program.enforcement_level == schema::ValidationLevel::REJECT) {
            report.errors.push_back(failure_message);
            report.success = false;
            return false;
        }

        if (program.enforcement_level == schema::ValidationLevel::WARN) {
            report.warnings.push_back(failure_message);
        } else {
            report.info_messages.push_back("AUTO_CORRECT not implemented for " + program.constraint_id +
                                           "; logged without mutation");
        }
    }

    return true;
}

std::string fingerprint_state(const engine::SimulationState& state) {
    constexpr uint64_t kFnvOffsetBasis = 14695981039346656037ULL;
    constexpr uint64_t kFnvPrime = 1099511628211ULL;

    auto mix_bytes = [](uint64_t hash, const unsigned char* bytes, std::size_t size) {
        uint64_t current = hash;
        for (std::size_t i = 0; i < size; ++i) {
            current ^= static_cast<uint64_t>(bytes[i]);
            current *= kFnvPrime;
        }
        return current;
    };

    uint64_t hash = kFnvOffsetBasis;

    for (const auto value : state.int_buffer()) {
        hash = mix_bytes(hash,
                         reinterpret_cast<const unsigned char*>(&value),
                         sizeof(value));
    }

    for (const auto value : state.float_buffer()) {
        hash = mix_bytes(hash,
                         reinterpret_cast<const unsigned char*>(&value),
                         sizeof(value));
    }

    for (const auto& value : state.string_buffer()) {
        const auto size = static_cast<uint64_t>(value.size());
        hash = mix_bytes(hash,
                         reinterpret_cast<const unsigned char*>(&size),
                         sizeof(size));
        hash = mix_bytes(hash,
                         reinterpret_cast<const unsigned char*>(value.data()),
                         value.size());
    }

    for (const bool value : state.bool_buffer()) {
        const unsigned char byte = value ? 1 : 0;
        hash = mix_bytes(hash, &byte, 1);
    }

    for (const auto& [variable_id, stale] : state.stale_flags()) {
        hash = mix_bytes(hash,
                         reinterpret_cast<const unsigned char*>(variable_id.data()),
                         variable_id.size());
        const unsigned char byte = stale ? 1 : 0;
        hash = mix_bytes(hash, &byte, 1);
    }

    const double current_time = state.current_time();
    hash = mix_bytes(hash,
                     reinterpret_cast<const unsigned char*>(&current_time),
                     sizeof(current_time));

    std::ostringstream oss;
    oss << std::hex << std::setw(16) << std::setfill('0') << hash;
    return oss.str();
}

validation::ValidationReport validate_definition(const schema::ScenarioDefinition& scenario) {
    validation::ScenarioValidator validator;
    return validator.validate(scenario);
}

schema::ScenarioDefinition load_scenario_file(const std::string& filepath) {
    serialization::YamlSerializer serializer;
    return serializer.load_file(filepath);
}

schema::ScenarioDefinition load_scenario_string(const std::string& yaml_content) {
    serialization::YamlSerializer serializer;
    return serializer.deserialize(yaml_content);
}

void throw_if_validation_failed(const validation::ValidationReport& report) {
    if (report.overall_passed) {
        return;
    }

    std::ostringstream oss;
    oss << "Scenario validation failed";
    for (const auto& error : report.errors) {
        oss << "\n- " << error;
    }
    throw std::runtime_error(oss.str());
}

}  // namespace

std::string_view name() noexcept {
    return "NoisiaX";
}

schema::ScenarioReport validate_scenario(const std::string& filepath) {
    try {
        const auto scenario = load_scenario_file(filepath);
        return make_validation_report(validate_definition(scenario));
    } catch (const std::exception& ex) {
        schema::ScenarioReport report;
        report.scenario_id = "unknown";
        report.report_type = "VALIDATION";
        report.success = false;
        report.errors.push_back(std::string("Validation failed: ") + ex.what());
        return report;
    }
}

schema::ScenarioReport validate_scenario_from_string(const std::string& yaml_content) {
    try {
        const auto scenario = load_scenario_string(yaml_content);
        return make_validation_report(validate_definition(scenario));
    } catch (const std::exception& ex) {
        schema::ScenarioReport report;
        report.scenario_id = "unknown";
        report.report_type = "VALIDATION";
        report.success = false;
        report.errors.push_back(std::string("Validation failed: ") + ex.what());
        return report;
    }
}

compiler::CompiledScenario compile_scenario(const std::string& filepath) {
    const auto scenario = load_scenario_file(filepath);
    const auto validation_report = validate_definition(scenario);
    throw_if_validation_failed(validation_report);

    return compile_scenario(scenario);
}

compiler::CompiledScenario compile_scenario(const schema::ScenarioDefinition& scenario) {
    const auto validation_report = validate_definition(scenario);
    throw_if_validation_failed(validation_report);

    compiler::ScenarioCompiler compiler;
    return compiler.compile(scenario);
}

schema::ScenarioReport run_scenario(const compiler::CompiledScenario& compiled) {
    if (compiled.typed_layer.has_value()) {
        RunOptions options;
        options.trace_level = TraceLevel::NONE;
        options.include_final_state = false;
        options.include_causal_graph = false;
        return engine::run_typed_layer_scenario(compiled, options).report;
    }

    if (compiled.agent_layer.has_value()) {
        RunOptions options;
        options.trace_level = TraceLevel::NONE;
        options.include_final_state = false;
        options.include_causal_graph = false;
        return engine::run_agent_layer_scenario(compiled, options).report;
    }

    schema::ScenarioReport report;
    report.scenario_id = compiled.scenario_id;
    report.report_type = "RUNTIME";
    report.success = true;

    try {
        engine::SimulationState state;
        state.initialize(compiled, compiled.master_seed);
        apply_initial_values(compiled, state);

        engine::DependencyGraph graph;
        graph.build_from_compiled(compiled);

        scheduler::EventScheduler event_scheduler;
        event_scheduler.initialize(compiled);

        auto callback = [&](const schema::EventDescriptor& event, engine::SimulationState& state_ref) {
            apply_event_payload(event, compiled, state_ref, graph);
            state_ref.set_current_time(event.timestamp);
        };

        event_scheduler.register_callback("SCHEDULED", callback);
        event_scheduler.register_callback("TRIGGERED", callback);
        event_scheduler.register_callback("CONDITIONAL", callback);

        if (!enforce_constraints(compiled, state, report)) {
            report.statistics["state_fingerprint"] = fingerprint_state(state);
            return report;
        }

        while (event_scheduler.has_events()) {
            if (!event_scheduler.process_next_event(state)) {
                break;
            }

            auto stale_variables = graph.compute_stale_closure(state);
            for (const auto& variable_id : stale_variables) {
                graph.pull_recompute(variable_id, state);
            }

            if (!enforce_constraints(compiled, state, report)) {
                report.statistics["state_fingerprint"] = fingerprint_state(state);
                return report;
            }
        }

        report.info_messages.push_back("Scenario executed successfully");
        report.statistics["total_variables"] = std::to_string(compiled.total_variables);
        report.statistics["total_dependencies"] = std::to_string(compiled.total_dependencies);
        report.statistics["total_constraints"] = std::to_string(compiled.total_constraints);
        report.statistics["total_events"] = std::to_string(compiled.total_events);
        report.statistics["processed_events"] = std::to_string(event_scheduler.event_history().size());
        report.statistics["final_time"] = std::to_string(state.current_time());
        report.statistics["state_fingerprint"] = fingerprint_state(state);
    } catch (const std::exception& ex) {
        report.success = false;
        report.errors.push_back(std::string("Runtime failure: ") + ex.what());
    }

    return report;
}

schema::ScenarioReport run_scenario(const std::string& filepath) {
    const auto compiled = compile_scenario(filepath);
    return run_scenario(compiled);
}

RunResult run_scenario_detailed(const compiler::CompiledScenario& compiled, const RunOptions& options) {
    if (compiled.typed_layer.has_value()) {
        return engine::run_typed_layer_scenario(compiled, options);
    }

    if (compiled.agent_layer.has_value()) {
        return engine::run_agent_layer_scenario(compiled, options);
    }

    RunResult detailed;
    detailed.report = run_scenario(compiled);
    detailed.report.report_type = "RUNTIME";
    detailed.report.statistics["trace_level"] = [&]() {
        switch (options.trace_level) {
            case TraceLevel::NONE: return std::string("none");
            case TraceLevel::EVENTS: return std::string("events");
            case TraceLevel::DECISIONS: return std::string("decisions");
            case TraceLevel::FULL: return std::string("full");
        }
        return std::string("none");
    }();
    return detailed;
}

RunResult run_scenario_detailed(const std::string& filepath, const RunOptions& options) {
    const auto compiled = compile_scenario(filepath);
    return run_scenario_detailed(compiled, options);
}

bool save_checkpoint(const engine::SimulationState& state,
                     const std::string& scenario_id,
                     const std::string& filepath) {
    try {
        serialization::CheckpointSerializer serializer;
        const std::string state_blob = state.create_checkpoint();
        const std::string checkpoint_blob =
            serializer.serialize_checkpoint(scenario_id, state.current_time(), state_blob);
        serializer.save_checkpoint(checkpoint_blob, filepath);
        return true;
    } catch (...) {
        return false;
    }
}

std::string load_checkpoint(const std::string& filepath, engine::SimulationState& state) {
    serialization::CheckpointSerializer serializer;
    const std::string checkpoint_blob = serializer.load_checkpoint(filepath);
    auto [scenario_id, _timestamp, state_blob] = serializer.deserialize_checkpoint(checkpoint_blob);
    state.restore_checkpoint(state_blob);
    return scenario_id;
}

// ============================================================================
// V4 Experiment Implementation
// ============================================================================

namespace experiment {

namespace {

// Simple hash function for strings (FNV-1a variant)
uint64_t compute_hash_u64(const std::string& data) {
    uint64_t hash = 14695981039346656037ULL;
    for (char c : data) {
        hash ^= static_cast<uint64_t>(static_cast<unsigned char>(c));
        hash *= 1099511628211ULL;
    }
    return hash;
}

std::string compute_hash(const std::string& data) {
    uint64_t hash = compute_hash_u64(data);
    std::ostringstream oss;
    oss << std::hex << std::setfill('0') << std::setw(16) << hash;
    return oss.str();
}

// Resolve JSON Pointer path to get/set values in scenario
std::optional<schema::TypedScalarValue> get_by_json_pointer(
    const schema::ScenarioDefinition& scenario,
    const std::string& json_pointer) {
    
    if (json_pointer.empty() || json_pointer[0] != '/') {
        return std::nullopt;
    }
    
    // Simple implementation for common paths
    // Format: /section/index/field or /section/field
    std::vector<std::string> parts;
    std::stringstream ss(json_pointer.substr(1));
    std::string part;
    while (std::getline(ss, part, '/')) {
        parts.push_back(part);
    }
    
    if (parts.empty()) {
        return std::nullopt;
    }
    
    // Handle /variables/N/default_value pattern
    if (parts.size() >= 3 && parts[0] == "variables") {
        try {
            size_t idx = std::stoul(parts[1]);
            if (idx >= scenario.variables.size()) {
                return std::nullopt;
            }
            const auto& var = scenario.variables[idx];
            if (parts[2] == "default_value") {
                return var.default_value;
            }
        } catch (...) {
            return std::nullopt;
        }
    }
    
    return std::nullopt;
}

bool apply_override_to_scenario(
    schema::ScenarioDefinition& scenario,
    const ScenarioOverride& override) {
    
    if (!override.json_pointer_path.empty()) {
        // Apply via JSON pointer
        if (override.json_pointer_path.find("/variables/") == 0) {
            std::stringstream ss(override.json_pointer_path.substr(1));
            std::string section, index_str, field;
            std::getline(ss, section, '/');
            std::getline(ss, index_str, '/');
            std::getline(ss, field, '/');
            
            if (section == "variables") {
                try {
                    size_t idx = std::stoul(index_str);
                    if (idx >= scenario.variables.size()) {
                        return false;
                    }
                    
                    if (field == "default_value") {
                        if (override.mode == OverrideMode::REPLACE) {
                            // Use std::visit to assign the value correctly
                            std::visit([&](const auto& val) {
                                scenario.variables[idx].default_value = val;
                            }, override.value);
                            return true;
                        }
                        // APPEND and MERGE not fully implemented for scalars
                        return false;
                    }
                } catch (...) {
                    return false;
                }
            }
        }
        return false;
    }
    
    // Apply via typed-layer component field
    if (!override.entity_id.empty() && !override.component_type_id.empty() && 
        !override.field_name.empty() && scenario.typed_layer.has_value()) {
        
        auto& typed_layer = *scenario.typed_layer;
        for (auto& entity : typed_layer.entities) {
            if (entity.entity_id == override.entity_id) {
                auto it = entity.components.find(override.component_type_id);
                if (it != entity.components.end()) {
                    auto field_it = it->second.find(override.field_name);
                    if (field_it != it->second.end()) {
                        field_it->second = override.value;
                        return true;
                    }
                }
            }
        }
    }
    
    return false;
}

// Generate seeds based on seed plan
std::vector<uint64_t> generate_seeds(const SeedPlan& plan) {
    std::vector<uint64_t> seeds;
    
    switch (plan.plan_type) {
        case SeedPlan::PlanType::EXPLICIT_LIST:
            seeds = plan.explicit_seeds;
            break;
            
        case SeedPlan::PlanType::RANGE: {
            for (std::size_t i = 0; i < plan.num_runs; ++i) {
                seeds.push_back(plan.base_seed + i * plan.seed_stride);
            }
            break;
        }
        
        case SeedPlan::PlanType::HASH_DERIVED: {
            for (std::size_t i = 0; i < plan.num_runs; ++i) {
                std::string seed_input = std::to_string(plan.base_seed) + ":" + std::to_string(i);
                uint64_t derived = compute_hash_u64(seed_input);
                seeds.push_back(derived);
            }
            break;
        }
    }
    
    return seeds;
}

// Sample from a stochastic sampler using deterministic RNG
schema::TypedScalarValue sample_from_sampler(
    const StochasticSampler& sampler,
    uint64_t base_seed,
    const std::string& stream_key,
    std::size_t draw_index) {
    
    // Deterministic RNG based on stream key
    std::string rng_seed_str = stream_key + ":" + std::to_string(base_seed) + ":" + std::to_string(draw_index);
    uint64_t rng_state = compute_hash_u64(rng_seed_str);
    
    auto next_random = [&rng_state]() -> double {
        rng_state = rng_state * 6364136223846793005ULL + 1442695040888963407ULL;
        return static_cast<double>(rng_state >> 11) / static_cast<double>(1ULL << 53);
    };
    
    switch (sampler.type) {
        case SamplerType::UNIFORM_INT: {
            double u = next_random();
            int64_t value = sampler.int_min + static_cast<int64_t>(u * (sampler.int_max - sampler.int_min + 1));
            value = std::clamp(value, sampler.int_min, sampler.int_max);
            return value;
        }
        
        case SamplerType::UNIFORM_FLOAT: {
            double u = next_random();
            double value = sampler.float_min + u * (sampler.float_max - sampler.float_min);
            return value;
        }
        
        case SamplerType::BERNOULLI: {
            double u = next_random();
            return u < sampler.probability;
        }
        
        case SamplerType::CHOICE: {
            if (sampler.choices.empty()) {
                return schema::TypedScalarValue{};
            }
            double u = next_random();
            size_t idx = static_cast<size_t>(u * sampler.choices.size());
            idx = std::min(idx, sampler.choices.size() - 1);
            return sampler.choices[idx];
        }
        
        case SamplerType::WEIGHTED_CHOICE: {
            if (sampler.choices.empty() || sampler.weights.empty()) {
                return schema::TypedScalarValue{};
            }
            
            double total_weight = 0.0;
            for (double w : sampler.weights) {
                total_weight += w;
            }
            
            if (total_weight <= 0.0) {
                return sampler.choices[0];
            }
            
            double u = next_random() * total_weight;
            double cumulative = 0.0;
            for (size_t i = 0; i < sampler.weights.size() && i < sampler.choices.size(); ++i) {
                cumulative += sampler.weights[i];
                if (u <= cumulative) {
                    return sampler.choices[i];
                }
            }
            return sampler.choices.back();
        }
    }
    
    return schema::TypedScalarValue{};
}

// Apply stochastic overlays to scenario
void apply_overlays(
    schema::ScenarioDefinition& scenario,
    const std::vector<StochasticOverlay>& overlays,
    uint64_t base_seed,
    const std::string& experiment_id,
    const std::string& variant_id,
    std::map<std::string, schema::TypedScalarValue>& sampled_values) {
    
    for (const auto& overlay : overlays) {
        for (const auto& sampler : overlay.samplers) {
            std::string stream_key = "overlay:" + experiment_id + ":" + variant_id + ":" + overlay.overlay_id + ":" + sampler.sampler_id;
            schema::TypedScalarValue sample = sample_from_sampler(sampler, base_seed, stream_key, 0);
            
            sampled_values[sampler.sampler_id] = sample;
            
            // Apply sampled value to scenario
            ScenarioOverride override;
            override.override_id = sampler.sampler_id + "_sample";
            override.json_pointer_path = sampler.json_pointer_path;
            override.entity_id = sampler.entity_id;
            override.component_type_id = sampler.component_type_id;
            override.field_name = sampler.field_name;
            override.mode = OverrideMode::REPLACE;
            override.value = sample;
            
            apply_override_to_scenario(scenario, override);
        }
    }
}

// Extract metric value from run result
std::optional<double> extract_metric_value(
    const RunResult& run_result,
    const ExperimentMetric& metric) {
    
    // Simple implementation for common cases
    if (metric.source == "final_state" || metric.source == "summary") {
        if (run_result.final_state.summary.count(metric.field_path)) {
            try {
                return std::stod(run_result.final_state.summary.at(metric.field_path));
            } catch (...) {
                return std::nullopt;
            }
        }
    }
    
    if (metric.source == "typed_final_state" && run_result.typed_final_state.has_value()) {
        const auto& snapshot = *run_result.typed_final_state;
        if (snapshot.summary.count(metric.field_path)) {
            try {
                return std::stod(snapshot.summary.at(metric.field_path));
            } catch (...) {
                return std::nullopt;
            }
        }
    }
    
    return std::nullopt;
}

// Compute aggregate statistics
AggregateMetric compute_aggregate(
    const std::string& metric_id,
    const std::vector<double>& values) {
    
    AggregateMetric agg;
    agg.metric_id = metric_id;
    agg.count = values.size();
    
    if (values.empty()) {
        return agg;
    }
    
    agg.min_val = *std::min_element(values.begin(), values.end());
    agg.max_val = *std::max_element(values.begin(), values.end());
    agg.sum = std::accumulate(values.begin(), values.end(), 0.0);
    agg.mean = agg.sum / static_cast<double>(agg.count);
    
    if (agg.count > 1) {
        double sq_sum = 0.0;
        for (double v : values) {
            double diff = v - agg.mean;
            sq_sum += diff * diff;
        }
        agg.stddev = std::sqrt(sq_sum / static_cast<double>(agg.count - 1));
    }
    
    return agg;
}

// Serialize scenario to YAML string (simplified)
std::string scenario_to_yaml(const schema::ScenarioDefinition& scenario) {
    std::ostringstream oss;
    oss << "scenario_id: \"" << scenario.scenario_id << "\"\n";
    oss << "schema_version: \"" << scenario.schema_version << "\"\n";
    oss << "master_seed: " << scenario.master_seed << "\n";
    // Simplified - full implementation would serialize all fields
    return oss.str();
}

// Write JSON manifest to file
void write_manifest_json(const std::string& filepath, const RunManifest& manifest) {
    std::ofstream ofs(filepath);
    if (!ofs.is_open()) {
        throw std::runtime_error("Failed to open manifest file: " + filepath);
    }
    
    ofs << "{\n";
    ofs << "  \"noisiax_version\": \"" << manifest.noisiax_version << "\",\n";
    ofs << "  \"experiment_id\": \"" << manifest.experiment_id << "\",\n";
    ofs << "  \"run_id\": \"" << manifest.run_spec.run_id << "\",\n";
    ofs << "  \"run_index\": " << manifest.run_spec.run_index << ",\n";
    ofs << "  \"seed\": " << manifest.run_spec.seed << ",\n";
    ofs << "  \"base_scenario_hash\": \"" << manifest.base_scenario_hash << "\",\n";
    ofs << "  \"resolved_scenario_hash\": \"" << manifest.resolved_scenario_hash << "\",\n";
    ofs << "  \"success\": " << (manifest.success ? "true" : "false") << ",\n";
    ofs << "  \"final_fingerprint\": \"" << manifest.final_fingerprint << "\",\n";
    ofs << "  \"metrics\": {";
    bool first = true;
    for (const auto& [key, value] : manifest.metrics) {
        if (!first) ofs << ",";
        first = false;
        ofs << "\"" << key << "\": " << value;
    }
    ofs << "}\n";
    ofs << "}\n";
}

// Write summary JSON
void write_summary_json(
    const std::string& filepath,
    const ExperimentResult& result) {
    
    std::ofstream ofs(filepath);
    if (!ofs.is_open()) {
        throw std::runtime_error("Failed to open summary file: " + filepath);
    }
    
    ofs << "{\n";
    ofs << "  \"experiment_id\": \"" << result.experiment_id << "\",\n";
    ofs << "  \"total_runs\": " << result.total_runs << ",\n";
    ofs << "  \"successful_runs\": " << result.successful_runs << ",\n";
    ofs << "  \"failed_runs\": " << result.failed_runs << ",\n";
    ofs << "  \"total_elapsed_seconds\": " << result.total_elapsed_seconds << ",\n";
    ofs << "  \"aggregated_metrics\": {\n";
    
    bool first_metric = true;
    for (const auto& [metric_id, agg] : result.aggregated_metrics) {
        if (!first_metric) ofs << ",\n";
        first_metric = false;
        ofs << "    \"" << metric_id << "\": {\n";
        ofs << "      \"count\": " << agg.count << ",\n";
        ofs << "      \"min\": " << agg.min_val << ",\n";
        ofs << "      \"max\": " << agg.max_val << ",\n";
        ofs << "      \"mean\": " << agg.mean << ",\n";
        ofs << "      \"stddev\": " << agg.stddev << ",\n";
        ofs << "      \"sum\": " << agg.sum << "\n";
        ofs << "    }";
    }
    ofs << "\n  }\n";
    ofs << "}\n";
}

} // anonymous namespace

ResolvedScenario resolve_scenario(const std::string& path, const ResolveOptions& options) {
    ResolvedScenario result;
    
    try {
        // Load and parse the scenario YAML
        auto scenario = load_scenario_file(path);
        
        result.imported_files.push_back(path);
        
        // Handle imports if present (simplified - full impl would process imports list)
        // For now, just validate and produce canonical form
        
        // Validate after resolve if requested
        if (options.validate_after_resolve) {
            validation::ValidationReport validation_report = 
                validate_definition(scenario);
            
            if (!validation_report.overall_passed) {
                result.errors.insert(result.errors.end(), 
                                    validation_report.errors.begin(), 
                                    validation_report.errors.end());
                result.success = false;
                return result;
            }
        }
        
        result.definition = scenario;
        result.canonical_yaml = scenario_to_yaml(scenario);
        result.source_hash = compute_hash(result.canonical_yaml);
        result.resolved_hash = result.source_hash;  // Same for non-composed scenarios
        result.success = true;
        
    } catch (const std::exception& e) {
        result.errors.push_back(e.what());
        result.success = false;
    }
    
    return result;
}

ExperimentResult run_experiment(const std::string& path, const RunOptions& /*options*/) {
    // Load experiment definition from YAML - simplified inline parser
    ExperimentDefinition def;
    
    // For now, throw not implemented - full implementation would use YAML parser
    throw std::runtime_error("run_experiment(path) requires YAML experiment parser - use run_experiment(ExperimentDefinition) instead");
    
    return run_experiment(def, {});
}

ExperimentResult run_experiment(const ExperimentDefinition& definition, const RunOptions& options) {
    ExperimentResult result;
    result.definition = definition;
    result.experiment_id = definition.experiment_id;
    
    auto start_time = std::chrono::steady_clock::now();
    
    try {
        // Step 1: Resolve base scenario with composition
        ResolveOptions resolve_opts;
        resolve_opts.import_paths.push_back(".");
        resolve_opts.validate_after_resolve = true;
        
        ResolvedScenario resolved = resolve_scenario(definition.base_scenario_path, resolve_opts);
        
        if (!resolved.success) {
            result.error_message = "Failed to resolve base scenario: ";
            for (const auto& err : resolved.errors) {
                result.error_message += err + "; ";
            }
            result.success = false;
            return result;
        }
        
        result.composition_report.scenario_id = resolved.definition.scenario_id;
        result.composition_report.imported_fragments = resolved.imported_files;
        result.composition_report.canonical_hash = resolved.resolved_hash;
        result.composition_report.success = true;
        
        result.base_scenario_hash = resolved.source_hash;
        
        // Step 2: Generate seeds for ensemble
        std::vector<uint64_t> seeds = generate_seeds(definition.seed_plan);
        
        // Step 3: Run each experiment iteration
        for (std::size_t run_idx = 0; run_idx < seeds.size(); ++run_idx) {
            uint64_t seed = seeds[run_idx];
            std::string run_id = definition.experiment_id + "_run_" + std::to_string(run_idx);
            
            ExperimentRunSpec run_spec;
            run_spec.run_id = run_id;
            run_spec.run_index = run_idx;
            run_spec.seed = seed;
            run_spec.resolved_scenario_hash = resolved.resolved_hash;
            
            // Create working copy of scenario
            schema::ScenarioDefinition scenario_copy = resolved.definition;
            
            // Apply variants (overrides)
            for (const auto& variant : definition.variants) {
                ScenarioOverride applied_variant = variant;
                if (apply_override_to_scenario(scenario_copy, applied_variant)) {
                    run_spec.applied_overrides.push_back(applied_variant);
                }
            }
            
            // Apply stochastic overlays
            apply_overlays(
                scenario_copy,
                definition.overlays,
                seed,
                definition.experiment_id,
                run_id,
                run_spec.overlay_samples);
            
            // Compile the scenario
            compiler::CompiledScenario compiled;
            try {
                compiled = compile_scenario(scenario_copy);
            } catch (const std::exception& e) {
                ExperimentRunResult run_result;
                run_result.spec = run_spec;
                run_result.success = false;
                run_result.error_message = std::string("Compilation failed: ") + e.what();
                result.runs.push_back(run_result);
                result.failed_runs++;
                
                if (definition.fail_fast) {
                    break;
                }
                continue;
            }
            
            // Run the scenario
            RunOptions run_opts = options;
            if (definition.max_time > 0) {
                run_opts.max_time = definition.max_time;
            }
            if (definition.max_events > 0) {
                run_opts.max_events = definition.max_events;
            }
            run_opts.seed_override = seed;
            run_opts.trace_level = definition.trace_level;
            
            auto run_start = std::chrono::steady_clock::now();
            RunResult run_output = run_scenario_detailed(compiled, run_opts);
            auto run_end = std::chrono::steady_clock::now();
            
            double elapsed = std::chrono::duration<double>(run_end - run_start).count();
            
            ExperimentRunResult exp_run_result;
            exp_run_result.spec = run_spec;
            exp_run_result.run_result = run_output;
            exp_run_result.elapsed_seconds = elapsed;
            exp_run_result.success = run_output.report.success;
            
            // Extract metrics
            for (const auto& metric_def : definition.metrics) {
                auto metric_value = extract_metric_value(run_output, metric_def);
                if (metric_value.has_value()) {
                    exp_run_result.extracted_metrics[metric_def.metric_id] = *metric_value;
                }
            }
            
            result.runs.push_back(exp_run_result);
            
            if (exp_run_result.success) {
                result.successful_runs++;
            } else {
                result.failed_runs++;
            }
            
            // Create run manifest
            if (definition.write_run_manifests) {
                RunManifest manifest;
                manifest.noisiax_version = std::string(version());
                manifest.experiment_id = definition.experiment_id;
                manifest.run_spec = run_spec;
                manifest.base_scenario_hash = result.base_scenario_hash;
                manifest.resolved_scenario_hash = run_spec.resolved_scenario_hash;
                manifest.runtime_options = run_opts;
                manifest.success = exp_run_result.success;
                manifest.final_fingerprint = run_output.typed_final_state.has_value() 
                    ? run_output.typed_final_state->state_fingerprint 
                    : "";
                manifest.metrics = exp_run_result.extracted_metrics;
                
                result.run_manifests.push_back(manifest);
                
                // Write individual manifest file
                std::string manifest_path = definition.output_dir + "/" + run_id + "_manifest.json";
                try {
                    write_manifest_json(manifest_path, manifest);
                } catch (...) {
                    // Non-fatal, continue
                }
            }
            
            if (definition.fail_fast && !exp_run_result.success) {
                break;
            }
        }
        
        // Step 4: Compute aggregate metrics
        std::map<std::string, std::vector<double>> metric_values;
        for (const auto& run_result : result.runs) {
            for (const auto& [metric_id, value] : run_result.extracted_metrics) {
                metric_values[metric_id].push_back(value);
            }
        }
        
        for (const auto& [metric_id, values] : metric_values) {
            result.aggregated_metrics[metric_id] = compute_aggregate(metric_id, values);
        }
        
        // Step 5: Write summary
        result.total_runs = result.runs.size();
        result.total_elapsed_seconds = std::chrono::duration<double>(
            std::chrono::steady_clock::now() - start_time).count();
        
        if (definition.write_summary) {
            result.summary_path = definition.output_dir + "/" + definition.experiment_id + "_summary.json";
            try {
                write_summary_json(result.summary_path, result);
            } catch (...) {
                // Non-fatal
            }
        }
        
        result.manifest_path = definition.output_dir + "/" + definition.experiment_id + "_manifest.json";
        result.success = (result.failed_runs == 0) || !definition.fail_fast;
        
    } catch (const std::exception& e) {
        result.error_message = e.what();
        result.success = false;
    }
    
    return result;
}

} // namespace experiment

}  // namespace noisiax
