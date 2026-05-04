#include "noisiax/noisiax.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <functional>
#include <iomanip>
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

}  // namespace noisiax
