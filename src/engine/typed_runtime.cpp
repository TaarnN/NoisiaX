#include "noisiax/engine/typed_runtime.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <functional>
#include <iomanip>
#include <limits>
#include <map>
#include <optional>
#include <queue>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

namespace noisiax::engine {
namespace {

bool trace_level_at_least(TraceLevel current, TraceLevel expected) {
    const auto as_int = [](TraceLevel level) -> int {
        switch (level) {
            case TraceLevel::NONE: return 0;
            case TraceLevel::EVENTS: return 1;
            case TraceLevel::DECISIONS: return 2;
            case TraceLevel::FULL: return 3;
        }
        return 0;
    };
    return as_int(current) >= as_int(expected);
}

uint64_t fnv1a_64(std::string_view text) {
    constexpr uint64_t kOffsetBasis = 14695981039346656037ULL;
    constexpr uint64_t kPrime = 1099511628211ULL;
    uint64_t hash = kOffsetBasis;
    for (const unsigned char c : text) {
        hash ^= static_cast<uint64_t>(c);
        hash *= kPrime;
    }
    return hash;
}

uint64_t splitmix64(uint64_t value) {
    uint64_t z = value + 0x9E3779B97F4A7C15ULL;
    z = (z ^ (z >> 30U)) * 0xBF58476D1CE4E5B9ULL;
    z = (z ^ (z >> 27U)) * 0x94D049BB133111EBULL;
    return z ^ (z >> 31U);
}

struct RawRandomDraw {
    uint64_t raw_u64 = 0;
    uint64_t draw_index = 0;
    double normalized = 0.0;
};

class DeterministicRng {
public:
    DeterministicRng(uint64_t master_seed, const std::string& scenario_id)
        : master_seed_(master_seed), scenario_hash_(fnv1a_64(scenario_id)) {}

    RawRandomDraw draw(std::string_view stream_key) {
        const std::string key(stream_key);
        uint64_t draw_index = stream_counters_[key]++;
        const uint64_t stream_hash = fnv1a_64(key);

        uint64_t state = splitmix64(master_seed_);
        state = splitmix64(state ^ scenario_hash_);
        state = splitmix64(state ^ stream_hash);
        state = splitmix64(state ^ draw_index);

        RawRandomDraw draw;
        draw.raw_u64 = state;
        draw.draw_index = draw_index;
        draw.normalized = static_cast<double>((state >> 11U) & ((1ULL << 53U) - 1ULL)) *
                          (1.0 / static_cast<double>(1ULL << 53U));
        return draw;
    }

private:
    uint64_t master_seed_ = 0;
    uint64_t scenario_hash_ = 0;
    std::map<std::string, uint64_t> stream_counters_;
};

std::string to_string_precise(double value) {
    std::ostringstream oss;
    oss << std::fixed << std::setprecision(6) << value;
    return oss.str();
}

std::string typed_scalar_to_string(const schema::TypedScalarValue& value) {
    return std::visit([](auto&& arg) -> std::string {
        using T = std::decay_t<decltype(arg)>;
        if constexpr (std::is_same_v<T, int64_t>) {
            return std::to_string(arg);
        } else if constexpr (std::is_same_v<T, double>) {
            return to_string_precise(arg);
        } else if constexpr (std::is_same_v<T, bool>) {
            return arg ? "true" : "false";
        } else if constexpr (std::is_same_v<T, std::string>) {
            return arg;
        }
        return "";
    }, value);
}

RandomDrawTrace make_random_trace(const std::string& stream_key,
                                 const RawRandomDraw& draw,
                                 const std::string& interpreted_result) {
    RandomDrawTrace trace;
    trace.stream_key = stream_key;
    trace.draw_index = draw.draw_index;
    trace.raw_u64 = draw.raw_u64;
    trace.normalized = draw.normalized;
    trace.interpreted_result = interpreted_result;
    return trace;
}

using ExprValue = std::variant<double, bool, std::string>;

bool expr_is_number(const ExprValue& value) {
    return std::holds_alternative<double>(value);
}

bool expr_is_bool(const ExprValue& value) {
    return std::holds_alternative<bool>(value);
}

bool expr_is_string(const ExprValue& value) {
    return std::holds_alternative<std::string>(value);
}

double expr_as_number(const ExprValue& value) {
    if (const auto* v = std::get_if<double>(&value)) {
        return *v;
    }
    if (const auto* v_bool = std::get_if<bool>(&value)) {
        return *v_bool ? 1.0 : 0.0;
    }
    throw std::runtime_error("Expected numeric expression value");
}

bool expr_as_bool(const ExprValue& value) {
    if (const auto* v = std::get_if<bool>(&value)) {
        return *v;
    }
    if (const auto* v_num = std::get_if<double>(&value)) {
        return *v_num != 0.0;
    }
    throw std::runtime_error("Expected boolean expression value");
}

const std::string& expr_as_string(const ExprValue& value) {
    if (const auto* v = std::get_if<std::string>(&value)) {
        return *v;
    }
    throw std::runtime_error("Expected string expression value");
}

struct TypedRuntimeEvent {
    double timestamp = 0.0;
    int priority = 0;
    std::string handle;
    uint64_t ordinal = 0;
    std::size_t event_type_index = 0;
    uint64_t parent_event_id = 0;
    std::map<std::string, schema::TypedScalarValue> payload;
};

struct TypedRuntimeEventComparator {
    bool operator()(const TypedRuntimeEvent& a, const TypedRuntimeEvent& b) const {
        if (a.timestamp != b.timestamp) return a.timestamp > b.timestamp;
        if (a.priority != b.priority) return a.priority < b.priority;
        if (a.handle != b.handle) return a.handle > b.handle;
        return a.ordinal > b.ordinal;
    }
};

std::vector<std::string_view> split_path(std::string_view value) {
    std::vector<std::string_view> parts;
    std::size_t start = 0;
    for (std::size_t i = 0; i < value.size(); ++i) {
        if (value[i] != '.') {
            continue;
        }
        parts.push_back(value.substr(start, i - start));
        start = i + 1;
    }
    parts.push_back(value.substr(start));
    return parts;
}

schema::TypedScalarValue read_component_field(
    const std::vector<compiler::CompiledTypedComponentType>& component_types,
    std::size_t component_type_index,
    std::size_t field_index,
    std::size_t entity_index) {

    const auto& component = component_types.at(component_type_index);
    const int32_t instance_index = component.entity_to_instance.at(entity_index);
    if (instance_index < 0) {
        throw std::runtime_error("Entity lacks component '" + component.component_type_id + "'");
    }

    const auto& storage = component.fields.at(field_index);
    const std::size_t idx = static_cast<std::size_t>(instance_index);
    return std::visit([&](auto&& buffer) -> schema::TypedScalarValue {
        using BufferT = std::decay_t<decltype(buffer)>;
        if constexpr (std::is_same_v<BufferT, std::vector<int64_t>>) {
            return buffer.at(idx);
        } else if constexpr (std::is_same_v<BufferT, std::vector<double>>) {
            return buffer.at(idx);
        } else if constexpr (std::is_same_v<BufferT, std::vector<bool>>) {
            return static_cast<bool>(buffer.at(idx));
        } else if constexpr (std::is_same_v<BufferT, std::vector<std::string>>) {
            return buffer.at(idx);
        }
        throw std::runtime_error("Unsupported component field buffer type");
    }, storage.initial_buffer);
}

void write_component_field(
    std::vector<compiler::CompiledTypedComponentType>& component_types,
    std::size_t component_type_index,
    std::size_t field_index,
    std::size_t entity_index,
    const schema::TypedScalarValue& value) {

    auto& component = component_types.at(component_type_index);
    const int32_t instance_index = component.entity_to_instance.at(entity_index);
    if (instance_index < 0) {
        throw std::runtime_error("Entity lacks component '" + component.component_type_id + "'");
    }
    const std::size_t idx = static_cast<std::size_t>(instance_index);

    auto& storage = component.fields.at(field_index);
    std::visit([&](auto&& buffer) {
        using BufferT = std::decay_t<decltype(buffer)>;
        if constexpr (std::is_same_v<BufferT, std::vector<int64_t>>) {
            const auto* v = std::get_if<int64_t>(&value);
            if (!v) {
                throw std::runtime_error("Type mismatch writing integer component field");
            }
            buffer.at(idx) = *v;
        } else if constexpr (std::is_same_v<BufferT, std::vector<double>>) {
            if (const auto* v_float = std::get_if<double>(&value)) {
                buffer.at(idx) = *v_float;
                return;
            }
            if (const auto* v_int = std::get_if<int64_t>(&value)) {
                buffer.at(idx) = static_cast<double>(*v_int);
                return;
            }
            throw std::runtime_error("Type mismatch writing float component field");
        } else if constexpr (std::is_same_v<BufferT, std::vector<bool>>) {
            const auto* v = std::get_if<bool>(&value);
            if (!v) {
                throw std::runtime_error("Type mismatch writing boolean component field");
            }
            buffer.at(idx) = *v;
        } else if constexpr (std::is_same_v<BufferT, std::vector<std::string>>) {
            const auto* v = std::get_if<std::string>(&value);
            if (!v) {
                throw std::runtime_error("Type mismatch writing string component field");
            }
            buffer.at(idx) = *v;
        } else {
            throw std::runtime_error("Unsupported component field buffer type");
        }
    }, storage.initial_buffer);
}

schema::TypedScalarValue convert_expr_to_scalar(const ExprValue& value, schema::TypedFieldType type) {
    switch (type) {
        case schema::TypedFieldType::INTEGER: {
            const double num = expr_as_number(value);
            if (!std::isfinite(num)) {
                throw std::runtime_error("Non-finite numeric value for INTEGER field");
            }
            return static_cast<int64_t>(num);
        }
        case schema::TypedFieldType::FLOAT: {
            const double num = expr_as_number(value);
            if (!std::isfinite(num)) {
                throw std::runtime_error("Non-finite numeric value for FLOAT field");
            }
            return num;
        }
        case schema::TypedFieldType::BOOLEAN:
            return expr_as_bool(value);
        case schema::TypedFieldType::STRING:
            return expr_as_string(value);
    }
    throw std::runtime_error("Unknown TypedFieldType conversion");
}

class TypedExpressionParser {
public:
    using Resolver = std::function<std::optional<schema::TypedScalarValue>(std::string_view)>;

    TypedExpressionParser(std::string_view expression,
                          Resolver resolver,
                          const noisiax::extensions::ExpressionFunctionRegistry* function_registry,
                          DeterministicRng* rng,
                          std::vector<RandomDrawTrace>* event_draws,
                          std::vector<RandomDrawTrace>* system_draws)
        : expression_(expression),
          resolver_(std::move(resolver)),
          function_registry_(function_registry),
          rng_(rng),
          event_draws_(event_draws),
          system_draws_(system_draws) {}

    ExprValue evaluate_value() {
        ExprValue result = parse_logical_or();
        skip_spaces();
        if (position_ != expression_.size()) {
            throw std::runtime_error("Unexpected trailing token in expression");
        }
        return result;
    }

private:
    std::string_view expression_;
    std::size_t position_ = 0;
    Resolver resolver_;
    const noisiax::extensions::ExpressionFunctionRegistry* function_registry_ = nullptr;
    DeterministicRng* rng_ = nullptr;
    std::vector<RandomDrawTrace>* event_draws_ = nullptr;
    std::vector<RandomDrawTrace>* system_draws_ = nullptr;

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

    char peek() {
        skip_spaces();
        if (position_ >= expression_.size()) {
            return '\0';
        }
        return expression_[position_];
    }

    ExprValue parse_logical_or() {
        ExprValue value = parse_logical_and();
        while (consume("||")) {
            const bool rhs = expr_as_bool(parse_logical_and());
            const bool lhs = expr_as_bool(value);
            value = lhs || rhs;
        }
        return value;
    }

    ExprValue parse_logical_and() {
        ExprValue value = parse_equality();
        while (consume("&&")) {
            const bool rhs = expr_as_bool(parse_equality());
            const bool lhs = expr_as_bool(value);
            value = lhs && rhs;
        }
        return value;
    }

    ExprValue parse_equality() {
        ExprValue lhs = parse_comparison();

        while (true) {
            if (consume("==")) {
                ExprValue rhs = parse_comparison();
                lhs = compare_equal(lhs, rhs, true);
                continue;
            }
            if (consume("!=")) {
                ExprValue rhs = parse_comparison();
                lhs = compare_equal(lhs, rhs, false);
                continue;
            }
            break;
        }
        return lhs;
    }

    ExprValue compare_equal(const ExprValue& lhs, const ExprValue& rhs, bool equal) {
        if (expr_is_number(lhs) && expr_is_number(rhs)) {
            return equal ? (std::get<double>(lhs) == std::get<double>(rhs))
                         : (std::get<double>(lhs) != std::get<double>(rhs));
        }
        if (expr_is_bool(lhs) && expr_is_bool(rhs)) {
            return equal ? (std::get<bool>(lhs) == std::get<bool>(rhs))
                         : (std::get<bool>(lhs) != std::get<bool>(rhs));
        }
        if (expr_is_string(lhs) && expr_is_string(rhs)) {
            return equal ? (std::get<std::string>(lhs) == std::get<std::string>(rhs))
                         : (std::get<std::string>(lhs) != std::get<std::string>(rhs));
        }
        throw std::runtime_error("Type mismatch in equality comparison");
    }

    ExprValue parse_comparison() {
        ExprValue lhs = parse_additive();

        if (consume(">=")) return expr_as_number(lhs) >= expr_as_number(parse_additive());
        if (consume("<=")) return expr_as_number(lhs) <= expr_as_number(parse_additive());
        if (consume(">")) return expr_as_number(lhs) > expr_as_number(parse_additive());
        if (consume("<")) return expr_as_number(lhs) < expr_as_number(parse_additive());

        return lhs;
    }

    ExprValue parse_additive() {
        ExprValue value = parse_multiplicative();
        while (true) {
            if (consume("+")) {
                value = expr_as_number(value) + expr_as_number(parse_multiplicative());
                continue;
            }
            if (consume("-")) {
                value = expr_as_number(value) - expr_as_number(parse_multiplicative());
                continue;
            }
            break;
        }
        return value;
    }

    ExprValue parse_multiplicative() {
        ExprValue value = parse_unary();
        while (true) {
            if (consume("*")) {
                value = expr_as_number(value) * expr_as_number(parse_unary());
                continue;
            }
            if (consume("/")) {
                const double rhs = expr_as_number(parse_unary());
                if (rhs == 0.0) {
                    throw std::runtime_error("Division by zero in expression");
                }
                value = expr_as_number(value) / rhs;
                continue;
            }
            break;
        }
        return value;
    }

    ExprValue parse_unary() {
        if (consume("!")) {
            return !expr_as_bool(parse_unary());
        }
        if (consume("-")) {
            return -expr_as_number(parse_unary());
        }
        return parse_primary();
    }

    ExprValue parse_primary() {
        if (consume("(")) {
            ExprValue value = parse_logical_or();
            if (!consume(")")) {
                throw std::runtime_error("Missing closing ')' in expression");
            }
            return value;
        }

        const char c = peek();
        if (c == '"' || c == '\'') {
            return parse_string_literal();
        }
        if (std::isdigit(static_cast<unsigned char>(c)) != 0 || c == '.') {
            return parse_number_literal();
        }
        if (std::isalpha(static_cast<unsigned char>(c)) != 0 || c == '_') {
            return parse_identifier_or_call();
        }

        throw std::runtime_error("Unexpected token in expression");
    }

    ExprValue parse_string_literal() {
        skip_spaces();
        const char quote = expression_[position_++];
        std::string out;
        while (position_ < expression_.size()) {
            const char c = expression_[position_++];
            if (c == quote) {
                return out;
            }
            if (c == '\\' && position_ < expression_.size()) {
                const char next = expression_[position_++];
                switch (next) {
                    case 'n': out.push_back('\n'); break;
                    case 't': out.push_back('\t'); break;
                    case '\\': out.push_back('\\'); break;
                    case '\'': out.push_back('\''); break;
                    case '"': out.push_back('"'); break;
                    default: out.push_back(next); break;
                }
                continue;
            }
            out.push_back(c);
        }
        throw std::runtime_error("Unterminated string literal");
    }

    ExprValue parse_number_literal() {
        skip_spaces();
        const std::size_t start = position_;
        bool saw_digit = false;

        auto consume_digits = [&]() {
            while (position_ < expression_.size() &&
                   std::isdigit(static_cast<unsigned char>(expression_[position_])) != 0) {
                ++position_;
                saw_digit = true;
            }
        };

        consume_digits();
        if (position_ < expression_.size() && expression_[position_] == '.') {
            ++position_;
            consume_digits();
        }

        if (position_ < expression_.size() &&
            (expression_[position_] == 'e' || expression_[position_] == 'E')) {
            const std::size_t exp_start = position_;
            ++position_;
            if (position_ < expression_.size() && (expression_[position_] == '+' || expression_[position_] == '-')) {
                ++position_;
            }
            const std::size_t digits_start = position_;
            consume_digits();
            if (digits_start == position_) {
                position_ = exp_start;
            }
        }

        if (!saw_digit) {
            throw std::runtime_error("Invalid numeric literal in expression");
        }

        const std::string token(expression_.substr(start, position_ - start));
        double value = 0.0;
        try {
            std::size_t processed = 0;
            value = std::stod(token, &processed);
            if (processed != token.size()) {
                throw std::runtime_error("Invalid numeric literal in expression");
            }
        } catch (...) {
            throw std::runtime_error("Invalid numeric literal in expression");
        }
        return value;
    }

    ExprValue parse_identifier_or_call() {
        skip_spaces();
        const std::size_t start = position_;
        ++position_;
        while (position_ < expression_.size()) {
            const char c = expression_[position_];
            if (std::isalnum(static_cast<unsigned char>(c)) != 0 || c == '_' || c == ':' || c == '.') {
                ++position_;
                continue;
            }
            break;
        }

        std::string name(expression_.substr(start, position_ - start));
        if (name == "true") return true;
        if (name == "false") return false;

        if (consume("(")) {
            std::vector<ExprValue> args;
            if (!consume(")")) {
                while (true) {
                    args.push_back(parse_logical_or());
                    if (consume(")")) {
                        break;
                    }
                    if (!consume(",")) {
                        throw std::runtime_error("Expected ',' in function call");
                    }
                }
            }
            return eval_function(name, args);
        }

        auto resolved = resolver_(name);
        if (!resolved.has_value()) {
            throw std::runtime_error("Unknown identifier in expression: " + name);
        }

        return std::visit([](auto&& arg) -> ExprValue {
            using T = std::decay_t<decltype(arg)>;
            if constexpr (std::is_same_v<T, int64_t>) {
                return static_cast<double>(arg);
            } else if constexpr (std::is_same_v<T, double>) {
                return arg;
            } else if constexpr (std::is_same_v<T, bool>) {
                return arg;
            } else if constexpr (std::is_same_v<T, std::string>) {
                return arg;
            }
            return 0.0;
        }, *resolved);
    }

    ExprValue eval_function(const std::string& name, const std::vector<ExprValue>& args) {
        if (name == "rng" || name == "std::rng_uniform") {
            if (args.size() != 3) throw std::runtime_error("rng(key,min,max) expects 3 arguments");
            if (rng_ == nullptr) throw std::runtime_error("rng() used without RNG context");
            const std::string key = expr_as_string(args[0]);
            const double lo = expr_as_number(args[1]);
            const double hi = expr_as_number(args[2]);
            if (!std::isfinite(lo) || !std::isfinite(hi)) {
                throw std::runtime_error("rng(key,min,max) expects finite bounds");
            }
            if (hi < lo) {
                throw std::runtime_error("rng(key,min,max) expects max >= min");
            }
            const auto draw = rng_->draw(key);
            const double value = lo + draw.normalized * (hi - lo);
            const auto trace = make_random_trace(
                key,
                draw,
                "uniform(" + to_string_precise(lo) + "," + to_string_precise(hi) + ")=" + to_string_precise(value));
            if (event_draws_ != nullptr) {
                event_draws_->push_back(trace);
            }
            if (system_draws_ != nullptr) {
                system_draws_->push_back(trace);
            }
            return value;
        }

        if (function_registry_ != nullptr) {
            if (const auto* fn = function_registry_->find(name); fn != nullptr) {
                return (*fn)(args);
            }
        }

        throw std::runtime_error("Unknown function in expression: " + name);
    }
};

std::optional<std::size_t> find_tick_event_type(const compiler::CompiledTypedLayer& layer) {
    auto it = layer.event_type_index.find("tick");
    if (it != layer.event_type_index.end()) return it->second;
    it = layer.event_type_index.find("TICK");
    if (it != layer.event_type_index.end()) return it->second;
    return std::nullopt;
}

std::string fingerprint_typed_state(const std::vector<compiler::CompiledTypedComponentType>& components,
                                    const std::vector<compiler::CompiledTypedRelationInstance>& relations,
                                    double final_time) {
    constexpr uint64_t kOffsetBasis = 14695981039346656037ULL;
    constexpr uint64_t kPrime = 1099511628211ULL;

    auto mix_bytes = [](uint64_t hash, const unsigned char* bytes, std::size_t size) {
        uint64_t current = hash;
        for (std::size_t i = 0; i < size; ++i) {
            current ^= static_cast<uint64_t>(bytes[i]);
            current *= kPrime;
        }
        return current;
    };

    uint64_t hash = kOffsetBasis;
    hash = mix_bytes(hash, reinterpret_cast<const unsigned char*>(&final_time), sizeof(final_time));

    for (const auto& component : components) {
        for (const auto& field : component.fields) {
            std::visit([&](auto&& buffer) {
                using BufferT = std::decay_t<decltype(buffer)>;
                if constexpr (std::is_same_v<BufferT, std::vector<int64_t>>) {
                    for (const auto value : buffer) {
                        hash = mix_bytes(hash,
                                         reinterpret_cast<const unsigned char*>(&value),
                                         sizeof(value));
                    }
                } else if constexpr (std::is_same_v<BufferT, std::vector<double>>) {
                    for (const auto value : buffer) {
                        hash = mix_bytes(hash,
                                         reinterpret_cast<const unsigned char*>(&value),
                                         sizeof(value));
                    }
                } else if constexpr (std::is_same_v<BufferT, std::vector<bool>>) {
                    for (const bool value : buffer) {
                        const unsigned char byte = value ? 1 : 0;
                        hash = mix_bytes(hash, &byte, sizeof(byte));
                    }
                } else if constexpr (std::is_same_v<BufferT, std::vector<std::string>>) {
                    for (const auto& value : buffer) {
                        const uint64_t size = static_cast<uint64_t>(value.size());
                        hash = mix_bytes(hash,
                                         reinterpret_cast<const unsigned char*>(&size),
                                         sizeof(size));
                        hash = mix_bytes(hash,
                                         reinterpret_cast<const unsigned char*>(value.data()),
                                         value.size());
                    }
                }
            }, field.initial_buffer);
        }
    }

    for (const auto& relation : relations) {
        hash = mix_bytes(hash,
                         reinterpret_cast<const unsigned char*>(&relation.relation_type_index),
                         sizeof(relation.relation_type_index));
        hash = mix_bytes(hash,
                         reinterpret_cast<const unsigned char*>(&relation.source_entity_index),
                         sizeof(relation.source_entity_index));
        hash = mix_bytes(hash,
                         reinterpret_cast<const unsigned char*>(&relation.target_entity_index),
                         sizeof(relation.target_entity_index));
        const bool has_expiry = relation.expires_at.has_value();
        hash = mix_bytes(hash,
                         reinterpret_cast<const unsigned char*>(&has_expiry),
                         sizeof(has_expiry));
        if (relation.expires_at.has_value()) {
            const double expiry = *relation.expires_at;
            hash = mix_bytes(hash,
                             reinterpret_cast<const unsigned char*>(&expiry),
                             sizeof(expiry));
        }
        for (const auto& [key, value] : relation.payload) {
            hash = mix_bytes(hash,
                             reinterpret_cast<const unsigned char*>(key.data()),
                             key.size());
            const std::string rendered = typed_scalar_to_string(value);
            hash = mix_bytes(hash,
                             reinterpret_cast<const unsigned char*>(rendered.data()),
                             rendered.size());
        }
    }

    std::ostringstream oss;
    oss << std::hex << hash;
    return oss.str();
}

void expire_relations(std::vector<compiler::CompiledTypedRelationInstance>& relations, double current_time) {
    relations.erase(
        std::remove_if(relations.begin(), relations.end(),
                       [&](const compiler::CompiledTypedRelationInstance& relation) {
                           return relation.expires_at.has_value() && *relation.expires_at <= current_time;
                       }),
        relations.end());
}

}  // namespace

RunResult run_typed_layer_scenario(const compiler::CompiledScenario& compiled, const RunOptions& options) {
    RunResult result;
    result.report.scenario_id = compiled.scenario_id;
    result.report.report_type = "RUNTIME";
    result.report.success = true;

    if (!compiled.typed_layer.has_value()) {
        result.report.success = false;
        result.report.errors.push_back("No typed_layer compiled for v3 runtime");
        return result;
    }

    const auto& layer = *compiled.typed_layer;

    const uint64_t seed = options.seed_override.has_value() ? *options.seed_override : compiled.master_seed;
    DeterministicRng rng(seed, compiled.scenario_id);

    const TraceLevel trace_level = options.trace_level;
    const bool record_events = trace_level_at_least(trace_level, TraceLevel::EVENTS);
    const bool record_state_changes = trace_level_at_least(trace_level, TraceLevel::FULL);
    const bool record_system_traces = trace_level_at_least(trace_level, TraceLevel::FULL);
    const bool include_causal = options.include_causal_graph;

    auto max_events = options.max_events;
    if (max_events == 0) {
        max_events = layer.world.max_event_count;
    }
    max_events = std::min(max_events, layer.world.max_event_count);
    if (max_events == 0) {
        max_events = 1;
    }

    const double max_time = (options.max_time > 0.0) ? std::min(options.max_time, layer.world.duration)
                                                      : layer.world.duration;

    std::vector<compiler::CompiledTypedComponentType> components = layer.component_types;
    std::vector<compiler::CompiledTypedRelationInstance> relations = layer.relations;

    uint64_t next_ordinal = 1;
    auto next_handle = [&](std::size_t event_type_index, uint64_t ordinal) {
        std::ostringstream oss;
        oss << layer.event_types.at(event_type_index).event_type_id
            << "_" << std::setw(8) << std::setfill('0') << ordinal;
        return oss.str();
    };

    std::priority_queue<TypedRuntimeEvent, std::vector<TypedRuntimeEvent>, TypedRuntimeEventComparator> queue;

    auto schedule_event = [&](double timestamp,
                              int priority,
                              std::size_t event_type_index,
                              std::map<std::string, schema::TypedScalarValue> payload,
                              uint64_t parent_event_id,
                              std::optional<std::string> handle_override = std::nullopt) -> uint64_t {
        TypedRuntimeEvent event;
        event.timestamp = timestamp;
        event.priority = priority;
        event.ordinal = next_ordinal++;
        event.handle = handle_override.has_value() && !handle_override->empty()
                           ? *handle_override
                           : next_handle(event_type_index, event.ordinal);
        event.event_type_index = event_type_index;
        event.parent_event_id = parent_event_id;
        event.payload = std::move(payload);
        queue.push(std::move(event));
        return next_ordinal - 1;
    };

    for (const auto& event : layer.initial_events) {
        schedule_event(event.timestamp,
                       event.priority,
                       event.event_type_index,
                       event.payload,
                       0,
                       event.event_handle);
    }

    if (layer.world.tick_interval.has_value()) {
        try {
            const auto tick_type_opt = find_tick_event_type(layer);
            if (!tick_type_opt.has_value()) {
                throw std::runtime_error(
                    "typed_layer.world.tick_interval requires an event_type_id of 'tick' or 'TICK'");
            }
            const double dt = *layer.world.tick_interval;
            for (double t = 0.0; t <= layer.world.duration + 1e-12; t += dt) {
                std::map<std::string, schema::TypedScalarValue> payload;
                const auto& schema_fields = layer.event_types.at(*tick_type_opt).payload_field_index;
                if (schema_fields.contains("dt")) {
                    payload["dt"] = dt;
                }
                schedule_event(t, 0, *tick_type_opt, std::move(payload), 0, std::nullopt);
            }
        } catch (const std::exception& ex) {
            result.report.success = false;
            result.report.errors.push_back(std::string("Runtime failure: ") + ex.what());
            queue = decltype(queue){};
        }
    }

    std::map<std::size_t, std::vector<std::size_t>> systems_by_event_type;
    for (std::size_t sys_index = 0; sys_index < layer.systems.size(); ++sys_index) {
        const auto& system = layer.systems[sys_index];
        if (system.trigger_event_type_indices.empty()) {
            for (std::size_t event_type_index = 0; event_type_index < layer.event_types.size(); ++event_type_index) {
                systems_by_event_type[event_type_index].push_back(sys_index);
            }
            continue;
        }
        for (const auto trigger : system.trigger_event_type_indices) {
            systems_by_event_type[trigger].push_back(sys_index);
        }
    }

    auto resolve_identifier = [&](std::string_view name,
                                 std::optional<std::size_t> self_entity,
                                 std::optional<std::size_t> other_entity,
                                 const TypedRuntimeEvent& event,
                                 const compiler::CompiledTypedRelationInstance* relation) -> std::optional<schema::TypedScalarValue> {
        const auto parts = split_path(name);
        if (parts.empty()) {
            return std::nullopt;
        }

        std::optional<std::size_t> entity = self_entity;
        std::size_t index = 0;
        if (parts[0] == "self") {
            entity = self_entity;
            index = 1;
        } else if (parts[0] == "other") {
            entity = other_entity;
            index = 1;
        }

        if (index >= parts.size()) {
            return std::nullopt;
        }

        if (parts[index] == "event") {
            if (index + 1 >= parts.size()) {
                return std::nullopt;
            }
            const auto field = parts[index + 1];
            if (field == "timestamp") {
                return event.timestamp;
            }
            if (field == "priority") {
                return static_cast<int64_t>(event.priority);
            }
            auto it = event.payload.find(std::string(field));
            if (it == event.payload.end()) {
                return std::nullopt;
            }
            return it->second;
        }

        if (parts[index] == "relation") {
            if (relation == nullptr) {
                return std::nullopt;
            }
            if (index + 1 >= parts.size()) {
                return std::nullopt;
            }
            const auto field = parts[index + 1];
            if (field == "expires_at") {
                if (!relation->expires_at.has_value()) {
                    return std::nullopt;
                }
                return *relation->expires_at;
            }
            auto it = relation->payload.find(std::string(field));
            if (it == relation->payload.end()) {
                return std::nullopt;
            }
            return it->second;
        }

        if (!entity.has_value()) {
            return std::nullopt;
        }
        if (index + 1 >= parts.size()) {
            return std::nullopt;
        }
        const std::string component_id(parts[index]);
        const std::string field_name(parts[index + 1]);

        const auto component_it = layer.component_type_index.find(component_id);
        if (component_it == layer.component_type_index.end()) {
            return std::nullopt;
        }
        const auto& component = components.at(component_it->second);
        const auto field_it = component.field_index.find(field_name);
        if (field_it == component.field_index.end()) {
            return std::nullopt;
        }
        return read_component_field(components, component_it->second, field_it->second, *entity);
    };

    auto add_state_change = [&](uint64_t event_id,
                                const std::string& entity_type,
                                const std::string& entity_id,
                                const std::string& field_name,
                                const std::string& old_value,
                                const std::string& new_value,
                                const std::vector<uint64_t>& parent_ids) {
        if (!record_state_changes) {
            return;
        }
        StateChangeTrace trace;
        trace.event_id = event_id;
        trace.entity_type = entity_type;
        trace.entity_id = entity_id;
        trace.field_name = field_name;
        trace.old_value = old_value;
        trace.new_value = new_value;
        if (include_causal) {
            trace.causal_parent_event_ids = parent_ids;
        }
        result.state_changes.push_back(std::move(trace));
    };

    auto enforce_relation_bounds = [&](std::size_t relation_type_index,
                                      std::size_t source_entity_index,
                                      std::size_t target_entity_index) {
        const auto& relation_type = layer.relation_types.at(relation_type_index);
        if (relation_type.max_total.has_value()) {
            std::size_t count = 0;
            for (const auto& rel : relations) {
                if (rel.relation_type_index == relation_type_index) {
                    ++count;
                }
            }
            if (count >= *relation_type.max_total) {
                throw std::runtime_error("Relation bound exceeded: relation_type '" +
                                         relation_type.relation_type_id + "' max_total reached");
            }
        }
        if (relation_type.max_per_entity.has_value()) {
            std::size_t count_source = 0;
            std::size_t count_target = 0;
            for (const auto& rel : relations) {
                if (rel.relation_type_index != relation_type_index) {
                    continue;
                }
                if (rel.source_entity_index == source_entity_index || rel.target_entity_index == source_entity_index) {
                    ++count_source;
                }
                if (rel.source_entity_index == target_entity_index || rel.target_entity_index == target_entity_index) {
                    ++count_target;
                }
            }
            if (count_source >= *relation_type.max_per_entity || count_target >= *relation_type.max_per_entity) {
                throw std::runtime_error("Relation bound exceeded: relation_type '" +
                                         relation_type.relation_type_id + "' max_per_entity reached");
            }
        }
    };

    std::size_t processed_events = 0;
    double current_time = 0.0;

    try {
        while (!queue.empty()) {
            if (processed_events >= max_events) {
                result.report.success = false;
                result.report.errors.push_back("Runtime halted: max_events limit reached");
                break;
            }

            TypedRuntimeEvent event = queue.top();
            queue.pop();

            if (event.timestamp > max_time) {
                break;
            }

            current_time = event.timestamp;
            ++processed_events;
            expire_relations(relations, current_time);

            std::vector<uint64_t> parent_ids;
            if (include_causal && event.parent_event_id != 0) {
                parent_ids.push_back(event.parent_event_id);
            }

            std::vector<RandomDrawTrace> event_draws;

            const auto systems_it = systems_by_event_type.find(event.event_type_index);
            if (systems_it != systems_by_event_type.end()) {
                for (const auto sys_index : systems_it->second) {
                    const auto& system = layer.systems.at(sys_index);
                    TypedSystemTrace system_trace;
                    system_trace.event_id = event.ordinal;
                    system_trace.system_id = system.system_id;
                    system_trace.timestamp = current_time;

                    std::vector<RandomDrawTrace> system_draws;

                    auto eval_bool = [&](std::string_view expr,
                                         std::optional<std::size_t> self_entity,
                                         std::optional<std::size_t> other_entity,
                                         const compiler::CompiledTypedRelationInstance* relation) -> bool {
                        TypedExpressionParser parser(
                            expr,
                            [&](std::string_view ident) {
                                return resolve_identifier(ident, self_entity, other_entity, event, relation);
                            },
                            &compiled.expression_functions,
                            &rng,
                            &event_draws,
                            &system_draws);
                        return expr_as_bool(parser.evaluate_value());
                    };

                    auto eval_value = [&](std::string_view expr,
                                          std::optional<std::size_t> self_entity,
                                          std::optional<std::size_t> other_entity,
                                          const compiler::CompiledTypedRelationInstance* relation) -> ExprValue {
                        TypedExpressionParser parser(
                            expr,
                            [&](std::string_view ident) {
                                return resolve_identifier(ident, self_entity, other_entity, event, relation);
                            },
                            &compiled.expression_functions,
                            &rng,
                            &event_draws,
                            &system_draws);
                        return parser.evaluate_value();
                    };

                auto execute_match = [&](std::optional<std::size_t> self_entity,
                                         std::optional<std::size_t> other_entity,
                                         const compiler::CompiledTypedRelationInstance* relation) {
                    if (system.where.has_value() && !eval_bool(*system.where, self_entity, other_entity, relation)) {
                        return;
                    }

                    if (self_entity.has_value()) {
                        system_trace.matched_entity_ids.push_back(layer.entity_ids.at(*self_entity));
                    }

                    for (const auto& write : system.writes) {
                        if (write.when.has_value() && !eval_bool(*write.when, self_entity, other_entity, relation)) {
                            continue;
                        }

                        const auto target_entity =
                            (write.role == compiler::CompiledTypedEntityRole::SELF) ? self_entity : other_entity;
                        if (!target_entity.has_value()) {
                            throw std::runtime_error("typed_layer system write requires entity role not present");
                        }

                        const schema::TypedScalarValue old_value =
                            read_component_field(components,
                                                 write.component_type_index,
                                                 write.field_index,
                                                 *target_entity);

                        const ExprValue expr_value = eval_value(write.expr, self_entity, other_entity, relation);
                        const schema::TypedScalarValue new_value = convert_expr_to_scalar(expr_value, write.field_type);

                        write_component_field(components,
                                              write.component_type_index,
                                              write.field_index,
                                              *target_entity,
                                              new_value);

                        TypedSystemWriteTrace write_trace;
                        write_trace.entity_id = layer.entity_ids.at(*target_entity);
                        write_trace.component_type_id =
                            layer.component_types.at(write.component_type_index).component_type_id;
                        write_trace.field_name =
                            layer.component_types.at(write.component_type_index).fields.at(write.field_index).field_name;
                        write_trace.old_value = typed_scalar_to_string(old_value);
                        write_trace.new_value = typed_scalar_to_string(new_value);
                        system_trace.writes.push_back(std::move(write_trace));

                        add_state_change(
                            event.ordinal,
                            layer.entity_types.at(layer.entity_type_index_by_entity.at(*target_entity)).entity_type_id,
                            layer.entity_ids.at(*target_entity),
                            write_trace.component_type_id + "." + write_trace.field_name,
                            typed_scalar_to_string(old_value),
                            typed_scalar_to_string(new_value),
                            parent_ids);
                    }

                    for (const auto& create_relation : system.create_relations) {
                        if (create_relation.when.has_value() &&
                            !eval_bool(*create_relation.when, self_entity, other_entity, relation)) {
                            continue;
                        }

                        const auto source_entity =
                            (create_relation.source_role == compiler::CompiledTypedEntityRole::SELF) ? self_entity : other_entity;
                        const auto target_entity =
                            (create_relation.target_role == compiler::CompiledTypedEntityRole::SELF) ? self_entity : other_entity;
                        if (!source_entity.has_value() || !target_entity.has_value()) {
                            throw std::runtime_error("typed_layer create_relation requires entity role not present");
                        }

                        enforce_relation_bounds(create_relation.relation_type_index, *source_entity, *target_entity);

                        compiler::CompiledTypedRelationInstance new_relation;
                        new_relation.relation_type_index = create_relation.relation_type_index;
                        new_relation.source_entity_index = *source_entity;
                        new_relation.target_entity_index = *target_entity;

                        if (create_relation.expires_after.has_value()) {
                            const ExprValue expires_value =
                                eval_value(*create_relation.expires_after, self_entity, other_entity, relation);
                            const double dt = expr_as_number(expires_value);
                            if (!std::isfinite(dt) || dt <= 0.0) {
                                throw std::runtime_error("expires_after must evaluate to finite positive number");
                            }
                            new_relation.expires_at = current_time + dt;
                        }

                        const auto& schema_fields =
                            layer.relation_types.at(create_relation.relation_type_index).payload_field_index;
                        for (const auto& [field_name, expr] : create_relation.payload_exprs) {
                            const auto schema_it = schema_fields.find(field_name);
                            if (schema_it == schema_fields.end()) {
                                continue;
                            }
                            const auto& field_schema =
                                layer.relation_types.at(create_relation.relation_type_index).payload_fields.at(schema_it->second);
                            const ExprValue payload_value = eval_value(expr, self_entity, other_entity, relation);
                            new_relation.payload[field_name] = convert_expr_to_scalar(payload_value, field_schema.type);
                        }

                        relations.push_back(std::move(new_relation));

                        TypedSystemCreateRelationTrace relation_trace;
                        relation_trace.relation_type_id =
                            layer.relation_types.at(create_relation.relation_type_index).relation_type_id;
                        relation_trace.source_entity_id = layer.entity_ids.at(*source_entity);
                        relation_trace.target_entity_id = layer.entity_ids.at(*target_entity);
                        system_trace.created_relations.push_back(std::move(relation_trace));
                    }

                    for (const auto& emit_event : system.emit_events) {
                        if (emit_event.when.has_value() &&
                            !eval_bool(*emit_event.when, self_entity, other_entity, relation)) {
                            continue;
                        }

                        double timestamp = current_time;
                        if (emit_event.timestamp.has_value()) {
                            const ExprValue ts_value =
                                eval_value(*emit_event.timestamp, self_entity, other_entity, relation);
                            timestamp = expr_as_number(ts_value);
                            if (!std::isfinite(timestamp) || timestamp < 0.0) {
                                throw std::runtime_error("emit_event.timestamp must be finite and >= 0");
                            }
                        }

                        std::map<std::string, schema::TypedScalarValue> payload;
                        const auto& schema_fields =
                            layer.event_types.at(emit_event.event_type_index).payload_field_index;
                        for (const auto& [field_name, expr] : emit_event.payload_exprs) {
                            const auto schema_it = schema_fields.find(field_name);
                            if (schema_it == schema_fields.end()) {
                                continue;
                            }
                            const auto& field_schema =
                                layer.event_types.at(emit_event.event_type_index).payload_fields.at(schema_it->second);
                            const ExprValue payload_value = eval_value(expr, self_entity, other_entity, relation);
                            payload[field_name] = convert_expr_to_scalar(payload_value, field_schema.type);
                        }

                        const uint64_t child_event_id =
                            schedule_event(timestamp,
                                           emit_event.priority,
                                           emit_event.event_type_index,
                                           std::move(payload),
                                           event.ordinal,
                                           std::nullopt);

                        TypedSystemEmitEventTrace emit_trace;
                        emit_trace.event_type_id = layer.event_types.at(emit_event.event_type_index).event_type_id;
                        emit_trace.event_id = child_event_id;
                        system_trace.emitted_events.push_back(std::move(emit_trace));
                    }
                };

                switch (system.kind) {
                    case compiler::CompiledTypedSystemKind::PER_ENTITY: {
                        for (std::size_t entity_index = 0; entity_index < layer.entity_ids.size(); ++entity_index) {
                            if (system.entity_type_index.has_value() &&
                                layer.entity_type_index_by_entity.at(entity_index) != *system.entity_type_index) {
                                continue;
                            }
                            execute_match(entity_index, std::nullopt, nullptr);
                        }
                        break;
                    }
                    case compiler::CompiledTypedSystemKind::PAIR: {
                        for (std::size_t a = 0; a < layer.entity_ids.size(); ++a) {
                            if (system.entity_type_index.has_value() &&
                                layer.entity_type_index_by_entity.at(a) != *system.entity_type_index) {
                                continue;
                            }
                            for (std::size_t b = a + 1; b < layer.entity_ids.size(); ++b) {
                                if (system.entity_type_index.has_value() &&
                                    layer.entity_type_index_by_entity.at(b) != *system.entity_type_index) {
                                    continue;
                                }
                                execute_match(a, b, nullptr);
                            }
                        }
                        break;
                    }
                    case compiler::CompiledTypedSystemKind::PER_RELATION: {
                        for (const auto& rel : relations) {
                            if (system.relation_type_index.has_value() &&
                                rel.relation_type_index != *system.relation_type_index) {
                                continue;
                            }
                            execute_match(rel.source_entity_index, rel.target_entity_index, &rel);
                        }
                        break;
                    }
                }

                    if (record_system_traces) {
                        system_trace.random_draws = std::move(system_draws);
                        result.typed_system_traces.push_back(std::move(system_trace));
                    }
                }
            }

            if (record_events) {
                EventTrace trace;
                trace.event_id = event.ordinal;
                trace.event_type = layer.event_types.at(event.event_type_index).event_type_id;
                trace.event_handle = event.handle;
                trace.timestamp = event.timestamp;
                trace.priority = event.priority;
                if (include_causal) {
                    trace.causal_parent_event_ids = parent_ids;
                }
                for (const auto& [key, value] : event.payload) {
                    trace.payload[key] = typed_scalar_to_string(value);
                }
                trace.random_draws = std::move(event_draws);
                result.events.push_back(std::move(trace));
            }
        }
    } catch (const std::exception& ex) {
        result.report.success = false;
        result.report.errors.push_back(std::string("Runtime failure: ") + ex.what());
    }

    result.report.statistics["processed_events"] = std::to_string(processed_events);
    result.report.statistics["final_time"] = to_string_precise(current_time);
    result.report.statistics["state_fingerprint"] = fingerprint_typed_state(components, relations, current_time);

    if (options.include_final_state) {
        TypedFinalStateSnapshot snapshot;
        snapshot.state_fingerprint = result.report.statistics["state_fingerprint"];
        snapshot.summary["processed_events"] = result.report.statistics["processed_events"];
        snapshot.summary["final_time"] = result.report.statistics["final_time"];

        snapshot.entities.reserve(layer.entity_ids.size());
        for (std::size_t entity_index = 0; entity_index < layer.entity_ids.size(); ++entity_index) {
            TypedEntityFinalState entity_state;
            entity_state.entity_id = layer.entity_ids.at(entity_index);
            const std::size_t entity_type_index = layer.entity_type_index_by_entity.at(entity_index);
            entity_state.entity_type_id = layer.entity_types.at(entity_type_index).entity_type_id;

            const auto& entity_type = layer.entity_types.at(entity_type_index);
            for (const auto component_index : entity_type.component_type_indices) {
                const auto& component = components.at(component_index);
                const int32_t instance_index = component.entity_to_instance.at(entity_index);
                if (instance_index < 0) {
                    continue;
                }

                TypedEntityComponentFinalState component_state;
                component_state.component_type_id = component.component_type_id;
                for (const auto& field : component.fields) {
                    const auto field_it = component.field_index.find(field.field_name);
                    if (field_it == component.field_index.end()) {
                        continue;
                    }
                    component_state.fields[field.field_name] =
                        read_component_field(components, component_index, field_it->second, entity_index);
                }
                entity_state.components.push_back(std::move(component_state));
            }
            snapshot.entities.push_back(std::move(entity_state));
        }

        snapshot.relations.reserve(relations.size());
        for (const auto& rel : relations) {
            TypedRelationFinalState rel_state;
            rel_state.relation_type_id = layer.relation_types.at(rel.relation_type_index).relation_type_id;
            rel_state.source_entity_id = layer.entity_ids.at(rel.source_entity_index);
            rel_state.target_entity_id = layer.entity_ids.at(rel.target_entity_index);
            rel_state.expires_at = rel.expires_at;
            rel_state.payload = rel.payload;
            snapshot.relations.push_back(std::move(rel_state));
        }

        result.typed_final_state = std::move(snapshot);
    }

    return result;
}

}  // namespace noisiax::engine
