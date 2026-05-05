#include "noisiax/experiment/experiment.hpp"

#include "noisiax/serialization/yaml_serializer.hpp"

#include <yaml-cpp/yaml.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <limits>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

namespace noisiax::experiment {
namespace {

namespace fs = std::filesystem;

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

std::string to_hex_u64(uint64_t value) {
    std::ostringstream oss;
    oss << std::hex;
    oss.width(16);
    oss.fill('0');
    oss << value;
    return oss.str();
}

std::string read_file_or_throw(const fs::path& path) {
    std::ifstream in(path);
    if (!in) {
        throw std::runtime_error("Failed to open file: " + path.string());
    }
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

void write_file_or_throw(const fs::path& path, const std::string& contents) {
    std::ofstream out(path);
    if (!out) {
        throw std::runtime_error("Failed to write file: " + path.string());
    }
    out << contents;
}

std::string escape_json(const std::string& input) {
    std::ostringstream oss;
    for (const char c : input) {
        switch (c) {
            case '"': oss << "\\\""; break;
            case '\\': oss << "\\\\"; break;
            case '\b': oss << "\\b"; break;
            case '\f': oss << "\\f"; break;
            case '\n': oss << "\\n"; break;
            case '\r': oss << "\\r"; break;
            case '\t': oss << "\\t"; break;
            default:
                if (static_cast<unsigned char>(c) < 0x20) {
                    oss << "\\u00";
                    oss << std::hex << static_cast<int>(static_cast<unsigned char>(c));
                } else {
                    oss << c;
                }
        }
    }
    return oss.str();
}

std::string json_string(const std::string& value) {
    return "\"" + escape_json(value) + "\"";
}

std::string trace_level_to_string(TraceLevel level) {
    switch (level) {
        case TraceLevel::NONE: return "none";
        case TraceLevel::EVENTS: return "events";
        case TraceLevel::DECISIONS: return "decisions";
        case TraceLevel::FULL: return "full";
    }
    return "none";
}

JsonValue yaml_to_json_value(const YAML::Node& node) {
    if (!node || node.IsNull()) {
        return JsonValue{};
    }
    if (node.IsSequence()) {
        JsonValue::Array arr;
        arr.reserve(node.size());
        for (const auto& item : node) {
            arr.push_back(yaml_to_json_value(item));
        }
        return JsonValue(std::move(arr));
    }
    if (node.IsMap()) {
        JsonValue::Object obj;
        for (const auto& entry : node) {
            obj.emplace(entry.first.as<std::string>(), yaml_to_json_value(entry.second));
        }
        return JsonValue(std::move(obj));
    }
    if (!node.IsScalar()) {
        return JsonValue(node.as<std::string>());
    }

    const std::string text = node.as<std::string>();
    if (text == "true") return JsonValue(true);
    if (text == "false") return JsonValue(false);

    auto parse_int = [&]() -> std::optional<int64_t> {
        try {
            std::size_t processed = 0;
            long long value = std::stoll(text, &processed, 10);
            if (processed != text.size()) return std::nullopt;
            return static_cast<int64_t>(value);
        } catch (...) {
            return std::nullopt;
        }
    };

    auto parse_double = [&]() -> std::optional<double> {
        try {
            std::size_t processed = 0;
            double value = std::stod(text, &processed);
            if (processed != text.size()) return std::nullopt;
            return value;
        } catch (...) {
            return std::nullopt;
        }
    };

    if (auto v = parse_int(); v.has_value()) {
        return JsonValue(*v);
    }
    if (auto v = parse_double(); v.has_value()) {
        return JsonValue(*v);
    }
    return JsonValue(text);
}

YAML::Node json_to_yaml_node(const JsonValue& value) {
    YAML::Node node;
    std::visit([&](auto&& arg) {
        using T = std::decay_t<decltype(arg)>;
        if constexpr (std::is_same_v<T, std::monostate>) {
            node = YAML::Node();
        } else if constexpr (std::is_same_v<T, bool>) {
            node = arg;
        } else if constexpr (std::is_same_v<T, int64_t>) {
            node = arg;
        } else if constexpr (std::is_same_v<T, double>) {
            node = arg;
        } else if constexpr (std::is_same_v<T, std::string>) {
            node = arg;
        } else if constexpr (std::is_same_v<T, JsonValue::Array>) {
            node = YAML::Node(YAML::NodeType::Sequence);
            for (const auto& item : arg) {
                node.push_back(json_to_yaml_node(item));
            }
        } else if constexpr (std::is_same_v<T, JsonValue::Object>) {
            node = YAML::Node(YAML::NodeType::Map);
            for (const auto& [k, v] : arg) {
                node[k] = json_to_yaml_node(v);
            }
        }
    }, value.data);
    return node;
}

std::vector<std::string> parse_json_pointer(const std::string& ptr) {
    if (ptr.empty()) {
        return {};
    }
    if (ptr.front() != '/') {
        throw std::runtime_error("JSON pointer must start with '/'");
    }
    std::vector<std::string> tokens;
    std::string token;
    for (std::size_t i = 1; i <= ptr.size(); ++i) {
        if (i == ptr.size() || ptr[i] == '/') {
            std::string decoded;
            decoded.reserve(token.size());
            for (std::size_t j = 0; j < token.size(); ++j) {
                const char c = token[j];
                if (c == '~' && j + 1 < token.size()) {
                    const char next = token[j + 1];
                    if (next == '0') {
                        decoded.push_back('~');
                        ++j;
                        continue;
                    }
                    if (next == '1') {
                        decoded.push_back('/');
                        ++j;
                        continue;
                    }
                }
                decoded.push_back(c);
            }
            tokens.push_back(std::move(decoded));
            token.clear();
            continue;
        }
        token.push_back(ptr[i]);
    }
    return tokens;
}

std::optional<std::size_t> parse_index_token(const std::string& token) {
    if (token.empty()) {
        return std::nullopt;
    }
    std::size_t value = 0;
    for (char c : token) {
        if (std::isdigit(static_cast<unsigned char>(c)) == 0) {
            return std::nullopt;
        }
        value = value * 10 + static_cast<std::size_t>(c - '0');
    }
    return value;
}

YAML::Node resolve_pointer_node(YAML::Node node, const std::vector<std::string>& tokens, std::size_t index) {
    if (index >= tokens.size()) {
        return node;
    }

    const std::string& token = tokens[index];
    if (node.IsMap()) {
        YAML::Node child = node[token];
        if (!child) {
            throw std::runtime_error("JSON pointer path not found: '" + token + "'");
        }
        return resolve_pointer_node(child, tokens, index + 1);
    }
    if (node.IsSequence()) {
        const auto idx = parse_index_token(token);
        if (!idx.has_value() || *idx >= node.size()) {
            throw std::runtime_error("JSON pointer array index out of bounds: '" + token + "'");
        }
        YAML::Node child = node[*idx];
        return resolve_pointer_node(child, tokens, index + 1);
    }
    throw std::runtime_error("JSON pointer traversed into non-container node at '" + token + "'");
}

void apply_json_pointer_override(YAML::Node& root,
                                const std::string& ptr,
                                OverrideOp op,
                                const JsonValue& value) {
    const auto tokens = parse_json_pointer(ptr);
    if (tokens.empty()) {
        throw std::runtime_error("Empty JSON pointer is not allowed");
    }
    YAML::Node target = resolve_pointer_node(root, tokens, 0);

    if (op == OverrideOp::REPLACE) {
        target = json_to_yaml_node(value);
        return;
    }

    if (op == OverrideOp::APPEND) {
        if (!target.IsSequence()) {
            throw std::runtime_error("append override requires target to be a sequence");
        }
        YAML::Node payload = json_to_yaml_node(value);
        if (payload && payload.IsSequence()) {
            for (const auto& item : payload) {
                target.push_back(item);
            }
        } else {
            target.push_back(payload);
        }
        return;
    }

    if (op == OverrideOp::MERGE) {
        if (!target.IsMap()) {
            throw std::runtime_error("merge override requires target to be a mapping");
        }
        YAML::Node payload = json_to_yaml_node(value);
        if (!payload || !payload.IsMap()) {
            throw std::runtime_error("merge override value must be a mapping");
        }
        for (const auto& entry : payload) {
            target[entry.first.as<std::string>()] = entry.second;
        }
        return;
    }

    throw std::runtime_error("Unknown override operation");
}

void apply_typed_field_override(YAML::Node& root,
                               const TypedFieldTarget& target,
                               OverrideOp op,
                               const JsonValue& value) {
    if (op != OverrideOp::REPLACE) {
        throw std::runtime_error("typed_field override currently supports only replace");
    }

    YAML::Node typed_layer = root["typed_layer"];
    if (!typed_layer || !typed_layer.IsMap()) {
        throw std::runtime_error("typed_field override requires typed_layer");
    }

    YAML::Node entities = typed_layer["entities"];
    if (!entities || !entities.IsSequence()) {
        throw std::runtime_error("typed_field override requires typed_layer.entities sequence");
    }

    for (auto entity : entities) {
        if (!entity || !entity.IsMap()) continue;
        const std::string entity_id = entity["entity_id"] ? entity["entity_id"].as<std::string>() : "";
        if (entity_id != target.entity_id) {
            continue;
        }
        YAML::Node components = entity["components"];
        if (!components || !components.IsMap()) {
            throw std::runtime_error("typed_layer entity missing components map");
        }
        YAML::Node component = components[target.component_type_id];
        if (!component || !component.IsMap()) {
            throw std::runtime_error("typed_layer entity missing component: " + target.component_type_id);
        }
        if (!component[target.field_name]) {
            throw std::runtime_error("typed_layer component missing field: " + target.field_name);
        }
        component[target.field_name] = json_to_yaml_node(value);
        return;
    }

    throw std::runtime_error("typed_layer entity not found for typed_field override: " + target.entity_id);
}

void apply_override(YAML::Node& root, const ScenarioOverride& ovr) {
    if (ovr.target.json_pointer.has_value()) {
        apply_json_pointer_override(root, *ovr.target.json_pointer, ovr.op, ovr.value);
        return;
    }
    if (ovr.target.typed_field.has_value()) {
        apply_typed_field_override(root, *ovr.target.typed_field, ovr.op, ovr.value);
        return;
    }
    throw std::runtime_error("Override missing target");
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
    DeterministicRng(uint64_t master_seed, std::string_view scenario_id)
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
    std::unordered_map<std::string, uint64_t> stream_counters_;
};

double json_as_number(const JsonValue& v) {
    if (const auto* d = std::get_if<double>(&v.data)) return *d;
    if (const auto* i = std::get_if<int64_t>(&v.data)) return static_cast<double>(*i);
    throw std::runtime_error("Expected numeric JsonValue");
}

int64_t json_as_int(const JsonValue& v) {
    if (const auto* i = std::get_if<int64_t>(&v.data)) return *i;
    if (const auto* d = std::get_if<double>(&v.data)) return static_cast<int64_t>(std::llround(*d));
    throw std::runtime_error("Expected integer JsonValue");
}

bool json_as_bool(const JsonValue& v) {
    if (const auto* b = std::get_if<bool>(&v.data)) return *b;
    throw std::runtime_error("Expected boolean JsonValue");
}

const JsonValue::Array& json_as_array(const JsonValue& v) {
    if (const auto* a = std::get_if<JsonValue::Array>(&v.data)) return *a;
    throw std::runtime_error("Expected array JsonValue");
}

const JsonValue::Object& json_as_object(const JsonValue& v) {
    if (const auto* o = std::get_if<JsonValue::Object>(&v.data)) return *o;
    throw std::runtime_error("Expected object JsonValue");
}

JsonValue sample_overlay(const StochasticOverlay& overlay,
                         DeterministicRng& rng,
                         const std::string& stream_key) {
    const RawRandomDraw draw = rng.draw(stream_key);

    const auto& params = overlay.params;
    switch (overlay.sampler) {
        case SamplerType::UNIFORM_INT: {
            const auto& obj = json_as_object(params);
            const int64_t min_v = json_as_int(obj.at("min"));
            const int64_t max_v = json_as_int(obj.at("max"));
            if (max_v < min_v) throw std::runtime_error("uniform_int expects max >= min");
            const double span = static_cast<double>(max_v - min_v + 1);
            const int64_t value = min_v + static_cast<int64_t>(std::floor(draw.normalized * span));
            return JsonValue(std::min(max_v, value));
        }
        case SamplerType::UNIFORM_FLOAT: {
            const auto& obj = json_as_object(params);
            const double min_v = json_as_number(obj.at("min"));
            const double max_v = json_as_number(obj.at("max"));
            if (!(std::isfinite(min_v) && std::isfinite(max_v))) {
                throw std::runtime_error("uniform_float expects finite bounds");
            }
            if (max_v < min_v) throw std::runtime_error("uniform_float expects max >= min");
            const double value = min_v + draw.normalized * (max_v - min_v);
            return JsonValue(value);
        }
        case SamplerType::BERNOULLI: {
            const auto& obj = json_as_object(params);
            const double p = json_as_number(obj.at("p"));
            if (!(p >= 0.0 && p <= 1.0)) throw std::runtime_error("bernoulli expects 0<=p<=1");
            return JsonValue(draw.normalized < p);
        }
        case SamplerType::CHOICE: {
            const auto& obj = json_as_object(params);
            const auto& values = json_as_array(obj.at("values"));
            if (values.empty()) throw std::runtime_error("choice expects non-empty values");
            const std::size_t idx =
                std::min(values.size() - 1, static_cast<std::size_t>(std::floor(draw.normalized * values.size())));
            return values[idx];
        }
        case SamplerType::WEIGHTED_CHOICE: {
            const auto& obj = json_as_object(params);
            const auto& values = json_as_array(obj.at("values"));
            const auto& weights = json_as_array(obj.at("weights"));
            if (values.empty()) throw std::runtime_error("weighted_choice expects non-empty values");
            if (weights.size() != values.size()) throw std::runtime_error("weighted_choice expects weights.size == values.size");
            double sum = 0.0;
            std::vector<double> cumulative;
            cumulative.reserve(weights.size());
            for (const auto& w : weights) {
                const double val = json_as_number(w);
                if (!(val >= 0.0) || !std::isfinite(val)) {
                    throw std::runtime_error("weighted_choice expects finite non-negative weights");
                }
                sum += val;
                cumulative.push_back(sum);
            }
            if (sum <= 0.0) throw std::runtime_error("weighted_choice expects sum(weights) > 0");
            const double x = draw.normalized * sum;
            const auto it = std::upper_bound(cumulative.begin(), cumulative.end(), x);
            const std::size_t idx = static_cast<std::size_t>(std::distance(cumulative.begin(), it));
            return values[std::min(idx, values.size() - 1)];
        }
    }
    throw std::runtime_error("Unknown sampler type");
}

std::string to_json(const JsonValue& value) {
    return std::visit([&](auto&& arg) -> std::string {
        using T = std::decay_t<decltype(arg)>;
        if constexpr (std::is_same_v<T, std::monostate>) {
            return "null";
        } else if constexpr (std::is_same_v<T, bool>) {
            return arg ? "true" : "false";
        } else if constexpr (std::is_same_v<T, int64_t>) {
            return std::to_string(arg);
        } else if constexpr (std::is_same_v<T, double>) {
            std::ostringstream oss;
            oss << std::fixed << std::setprecision(6) << arg;
            return oss.str();
        } else if constexpr (std::is_same_v<T, std::string>) {
            return json_string(arg);
        } else if constexpr (std::is_same_v<T, JsonValue::Array>) {
            std::ostringstream oss;
            oss << "[";
            for (std::size_t i = 0; i < arg.size(); ++i) {
                if (i) oss << ", ";
                oss << to_json(arg[i]);
            }
            oss << "]";
            return oss.str();
        } else if constexpr (std::is_same_v<T, JsonValue::Object>) {
            std::ostringstream oss;
            oss << "{";
            bool first = true;
            for (const auto& [k, v] : arg) {
                if (!first) oss << ", ";
                first = false;
                oss << json_string(k) << ": " << to_json(v);
            }
            oss << "}";
            return oss.str();
        }
        return "null";
    }, value.data);
}

std::string override_op_to_string(OverrideOp op) {
    switch (op) {
        case OverrideOp::REPLACE: return "replace";
        case OverrideOp::APPEND: return "append";
        case OverrideOp::MERGE: return "merge";
    }
    return "replace";
}

std::string override_target_to_json(const ScenarioOverrideTarget& target) {
    std::ostringstream oss;
    oss << "{";
    bool first = true;
    if (target.json_pointer.has_value()) {
        oss << "\"json_pointer\": " << json_string(*target.json_pointer);
        first = false;
    }
    if (target.typed_field.has_value()) {
        if (!first) oss << ", ";
        const auto& tf = *target.typed_field;
        oss << "\"typed_field\": {"
            << "\"entity_id\": " << json_string(tf.entity_id) << ", "
            << "\"component_type_id\": " << json_string(tf.component_type_id) << ", "
            << "\"field_name\": " << json_string(tf.field_name)
            << "}";
    }
    oss << "}";
    return oss.str();
}

std::string override_to_json(const ScenarioOverride& ovr) {
    std::ostringstream oss;
    oss << "{";
    oss << "\"override_id\": " << json_string(ovr.override_id) << ", ";
    oss << "\"op\": " << json_string(override_op_to_string(ovr.op)) << ", ";
    oss << "\"target\": " << override_target_to_json(ovr.target) << ", ";
    oss << "\"value\": " << to_json(ovr.value);
    oss << "}";
    return oss.str();
}

std::optional<double> parse_double_strict(const std::string& text) {
    try {
        std::size_t processed = 0;
        const double value = std::stod(text, &processed);
        if (processed != text.size()) return std::nullopt;
        return value;
    } catch (...) {
        return std::nullopt;
    }
}

AggregateMetric compute_aggregate(const std::string& metric_id, const std::vector<double>& values) {
    AggregateMetric agg;
    agg.metric_id = metric_id;
    if (values.empty()) {
        return agg;
    }

    double mean = 0.0;
    double m2 = 0.0;
    agg.count = 0;
    agg.min = values.front();
    agg.max = values.front();

    for (const double x : values) {
        agg.min = std::min(agg.min, x);
        agg.max = std::max(agg.max, x);
        ++agg.count;
        const double delta = x - mean;
        mean += delta / static_cast<double>(agg.count);
        const double delta2 = x - mean;
        m2 += delta * delta2;
    }

    agg.mean = mean;
    const double variance = (agg.count > 1) ? (m2 / static_cast<double>(agg.count)) : 0.0;
    agg.stddev = std::sqrt(std::max(0.0, variance));
    return agg;
}

SamplerType parse_sampler_type(const std::string& text) {
    if (text == "uniform_int") return SamplerType::UNIFORM_INT;
    if (text == "uniform_float") return SamplerType::UNIFORM_FLOAT;
    if (text == "bernoulli") return SamplerType::BERNOULLI;
    if (text == "choice") return SamplerType::CHOICE;
    if (text == "weighted_choice") return SamplerType::WEIGHTED_CHOICE;
    throw std::runtime_error("Unknown sampler: " + text);
}

OverrideOp parse_override_op(const std::string& text) {
    if (text == "replace") return OverrideOp::REPLACE;
    if (text == "append") return OverrideOp::APPEND;
    if (text == "merge") return OverrideOp::MERGE;
    throw std::runtime_error("Unknown override op: " + text);
}

ScenarioOverrideTarget parse_override_target(const YAML::Node& node) {
    if (!node || !node.IsMap()) {
        throw std::runtime_error("override target must be a map");
    }
    ScenarioOverrideTarget target;
    if (node["json_pointer"]) {
        target.json_pointer = node["json_pointer"].as<std::string>();
    }
    if (node["typed_field"]) {
        const auto tf = node["typed_field"];
        if (!tf.IsMap()) {
            throw std::runtime_error("typed_field target must be a map");
        }
        TypedFieldTarget typed;
        typed.entity_id = tf["entity_id"].as<std::string>();
        typed.component_type_id = tf["component_type_id"].as<std::string>();
        typed.field_name = tf["field_name"].as<std::string>();
        target.typed_field = typed;
    }
    if (!target.json_pointer.has_value() && !target.typed_field.has_value()) {
        throw std::runtime_error("override target must specify json_pointer or typed_field");
    }
    if (target.json_pointer.has_value() && target.typed_field.has_value()) {
        throw std::runtime_error("override target must not specify both json_pointer and typed_field");
    }
    return target;
}

ScenarioOverride parse_override(const YAML::Node& node) {
    if (!node || !node.IsMap()) {
        throw std::runtime_error("override entry must be a map");
    }
    ScenarioOverride ovr;
    if (node["override_id"]) {
        ovr.override_id = node["override_id"].as<std::string>();
    }
    if (node["op"]) {
        ovr.op = parse_override_op(node["op"].as<std::string>());
    }
    if (!node["target"]) {
        throw std::runtime_error("override missing target");
    }
    ovr.target = parse_override_target(node["target"]);
    if (!node["value"]) {
        throw std::runtime_error("override missing value");
    }
    ovr.value = yaml_to_json_value(node["value"]);
    return ovr;
}

StochasticOverlay parse_overlay(const YAML::Node& node) {
    if (!node || !node.IsMap()) {
        throw std::runtime_error("overlay entry must be a map");
    }
    StochasticOverlay overlay;
    overlay.overlay_id = node["overlay_id"].as<std::string>();
    overlay.sampler = parse_sampler_type(node["sampler"].as<std::string>());
    overlay.target = parse_override_target(node["target"]);
    if (!node["params"]) {
        throw std::runtime_error("overlay missing params");
    }
    overlay.params = yaml_to_json_value(node["params"]);
    return overlay;
}

ExperimentMetric parse_metric(const YAML::Node& node) {
    if (!node || !node.IsMap()) {
        throw std::runtime_error("metric entry must be a map");
    }
    ExperimentMetric metric;
    metric.metric_id = node["metric_id"].as<std::string>();
    metric.kind = node["kind"].as<std::string>();
    if (metric.kind == "runtime_stat") {
        metric.key = node["key"].as<std::string>();
        return metric;
    }
    if (metric.kind == "typed_field_final") {
        const auto tf = node["typed_field"];
        if (!tf) {
            throw std::runtime_error("typed_field_final metric missing typed_field");
        }
        TypedFieldTarget typed;
        typed.entity_id = tf["entity_id"].as<std::string>();
        typed.component_type_id = tf["component_type_id"].as<std::string>();
        typed.field_name = tf["field_name"].as<std::string>();
        metric.typed_field = typed;
        return metric;
    }
    throw std::runtime_error("Unknown metric kind: " + metric.kind);
}

ExperimentDefinition parse_experiment_definition(const fs::path& file_path) {
    const std::string raw = read_file_or_throw(file_path);
    YAML::Node root = YAML::Load(raw);
    if (!root || !root.IsMap()) {
        throw std::runtime_error("Experiment file must be a YAML mapping");
    }

    ExperimentDefinition def;
    def.experiment_id = root["experiment_id"].as<std::string>();
    def.base_scenario = root["base_scenario"].as<std::string>();
    if (root["fail_fast"]) {
        def.fail_fast = root["fail_fast"].as<bool>();
    }
    if (root["write_run_details"]) {
        def.write_run_details = root["write_run_details"].as<bool>();
    }

    if (root["global_overrides"]) {
        for (const auto& item : root["global_overrides"]) {
            def.global_overrides.push_back(parse_override(item));
        }
    }

    if (root["seed_plan"]) {
        const auto sp = root["seed_plan"];
        if (sp["seeds"]) {
            for (const auto& seed : sp["seeds"]) {
                def.seed_plan.seeds.push_back(seed.as<uint64_t>());
            }
        }
    } else if (root["seeds"]) {
        for (const auto& seed : root["seeds"]) {
            def.seed_plan.seeds.push_back(seed.as<uint64_t>());
        }
    }

    if (root["variants"]) {
        for (const auto& v : root["variants"]) {
            ExperimentDefinition::VariantDefinition variant;
            variant.variant_id = v["variant_id"].as<std::string>();
            if (v["overrides"]) {
                for (const auto& o : v["overrides"]) {
                    variant.overrides.push_back(parse_override(o));
                }
            }
            if (v["overlays"]) {
                for (const auto& o : v["overlays"]) {
                    variant.overlays.push_back(parse_overlay(o));
                }
            }
            def.variants.push_back(std::move(variant));
        }
    }

    if (root["overlays"]) {
        for (const auto& o : root["overlays"]) {
            def.overlays.push_back(parse_overlay(o));
        }
    }

    if (root["metrics"]) {
        for (const auto& m : root["metrics"]) {
            def.metrics.push_back(parse_metric(m));
        }
    }

    return def;
}

std::string manifest_to_json(const ExperimentResult& result) {
    std::ostringstream oss;
    oss << "{\n";
    oss << "  \"noisiax_version\": " << json_string(result.noisiax_version) << ",\n";
    oss << "  \"experiment_id\": " << json_string(result.experiment_id) << ",\n";
    oss << "  \"base_scenario_path\": " << json_string(result.base_scenario_path) << ",\n";
    oss << "  \"base_scenario_hash\": " << json_string(result.base_scenario_hash) << ",\n";
    oss << "  \"runs\": [";
    for (std::size_t i = 0; i < result.runs.size(); ++i) {
        const auto& run = result.runs[i];
        if (i) oss << ",";
        oss << "\n    {";
        oss << "\"run_id\": " << json_string(run.run_id) << ", ";
        oss << "\"variant_id\": " << json_string(run.variant_id) << ", ";
        oss << "\"seed\": " << run.seed << ", ";
        oss << "\"base_scenario_hash\": " << json_string(run.base_scenario_hash) << ", ";
        oss << "\"resolved_scenario_hash\": " << json_string(run.resolved_scenario_hash) << ", ";
        oss << "\"run_options\": {"
            << "\"trace_level\": " << json_string(trace_level_to_string(run.trace_level)) << ", "
            << "\"max_time\": " << run.max_time << ", "
            << "\"max_events\": " << run.max_events << ", "
            << "\"include_final_state\": " << (run.include_final_state ? "true" : "false") << ", "
            << "\"include_causal_graph\": " << (run.include_causal_graph ? "true" : "false")
            << "}, ";
        oss << "\"success\": " << (run.success ? "true" : "false") << ", ";
        oss << "\"final_fingerprint\": " << json_string(run.final_fingerprint);

        if (!run.errors.empty()) {
            oss << ", \"errors\": [";
            for (std::size_t e = 0; e < run.errors.size(); ++e) {
                if (e) oss << ", ";
                oss << json_string(run.errors[e]);
            }
            oss << "]";
        }

        if (!run.metrics.empty()) {
            oss << ", \"metrics\": {";
            bool first = true;
            for (const auto& [k, v] : run.metrics) {
                if (!first) oss << ", ";
                first = false;
                oss << json_string(k) << ": " << json_string(v);
            }
            oss << "}";
        }

        if (!run.overrides.empty()) {
            oss << ", \"overrides\": [";
            for (std::size_t o = 0; o < run.overrides.size(); ++o) {
                if (o) oss << ", ";
                oss << override_to_json(run.overrides[o]);
            }
            oss << "]";
        }

        if (!run.overlay_samples.empty()) {
            oss << ", \"overlay_samples\": {";
            bool first = true;
            for (const auto& [k, v] : run.overlay_samples) {
                if (!first) oss << ", ";
                first = false;
                oss << json_string(k) << ": " << to_json(v);
            }
            oss << "}";
        }

        oss << "}";
    }
    if (!result.runs.empty()) {
        oss << "\n  ";
    }
    oss << "],\n";

    oss << "  \"aggregates\": [";
    for (std::size_t i = 0; i < result.aggregates.size(); ++i) {
        if (i) oss << ",";
        const auto& agg = result.aggregates[i];
        oss << "\n    {";
        oss << "\"metric_id\": " << json_string(agg.metric_id) << ", ";
        oss << "\"count\": " << agg.count << ", ";
        oss << "\"min\": " << agg.min << ", ";
        oss << "\"max\": " << agg.max << ", ";
        oss << "\"mean\": " << agg.mean << ", ";
        oss << "\"stddev\": " << agg.stddev;
        oss << "}";
    }
    if (!result.aggregates.empty()) {
        oss << "\n  ";
    }
    oss << "]\n";
    oss << "}\n";
    return oss.str();
}

std::string summary_to_json(const ExperimentResult& result) {
    std::size_t ok = 0;
    for (const auto& run : result.runs) {
        if (run.success) ++ok;
    }
    const std::size_t failed = result.runs.size() - ok;

    std::ostringstream oss;
    oss << "{\n";
    oss << "  \"noisiax_version\": " << json_string(result.noisiax_version) << ",\n";
    oss << "  \"experiment_id\": " << json_string(result.experiment_id) << ",\n";
    oss << "  \"runs_total\": " << result.runs.size() << ",\n";
    oss << "  \"runs_succeeded\": " << ok << ",\n";
    oss << "  \"runs_failed\": " << failed << ",\n";
    oss << "  \"aggregates\": [";
    for (std::size_t i = 0; i < result.aggregates.size(); ++i) {
        if (i) oss << ",";
        const auto& agg = result.aggregates[i];
        oss << "\n    {";
        oss << "\"metric_id\": " << json_string(agg.metric_id) << ", ";
        oss << "\"count\": " << agg.count << ", ";
        oss << "\"min\": " << agg.min << ", ";
        oss << "\"max\": " << agg.max << ", ";
        oss << "\"mean\": " << agg.mean << ", ";
        oss << "\"stddev\": " << agg.stddev;
        oss << "}";
    }
    if (!result.aggregates.empty()) {
        oss << "\n  ";
    }
    oss << "]\n";
    oss << "}\n";
    return oss.str();
}

}  // namespace

ExperimentResult run_experiment(const std::string& path, const ExperimentOptions& options) {
    const fs::path exp_path(path);
    ExperimentDefinition def = parse_experiment_definition(exp_path);

    fs::path base = def.base_scenario;
    if (base.is_relative()) {
        base = exp_path.parent_path() / base;
    }
    def.base_scenario = base.lexically_normal().string();

    return run_experiment(def, options);
}

ExperimentResult run_experiment(const ExperimentDefinition& definition, const ExperimentOptions& options) {
    if (definition.experiment_id.empty()) {
        throw std::runtime_error("experiment_id is required");
    }
    if (definition.base_scenario.empty()) {
        throw std::runtime_error("base_scenario is required");
    }
    if (options.output_dir.empty()) {
        throw std::runtime_error("ExperimentOptions.output_dir is required");
    }

    std::error_code ec;
    fs::create_directories(options.output_dir, ec);
    if (ec) {
        throw std::runtime_error("Failed to create output_dir: " + options.output_dir);
    }

    ResolveOptions resolve_options;
    resolve_options.validate_resolved = true;
    const ResolvedScenario base = resolve_scenario(definition.base_scenario, resolve_options);

    serialization::YamlSerializer serializer;
    const std::string base_hash = base.resolved_hash;

    std::vector<ExperimentDefinition::VariantDefinition> variants = definition.variants;
    if (variants.empty()) {
        ExperimentDefinition::VariantDefinition v;
        v.variant_id = "default";
        variants.push_back(std::move(v));
    }

    std::vector<uint64_t> seeds = definition.seed_plan.seeds;
    if (seeds.empty()) {
        seeds.push_back(base.scenario.master_seed);
    }

    ExperimentResult result;
    result.experiment_id = definition.experiment_id;
    result.noisiax_version = std::string(noisiax::version());
    result.base_scenario_path = definition.base_scenario;
    result.base_scenario_hash = base_hash;
    result.output_dir = options.output_dir;

    std::unordered_map<std::string, std::vector<double>> metric_samples;

    std::size_t run_counter = 0;
    for (const auto& variant : variants) {
        for (const uint64_t seed : seeds) {
            RunManifest manifest;
            manifest.variant_id = variant.variant_id;
            manifest.seed = seed;
            manifest.base_scenario_hash = base_hash;
            manifest.run_id = variant.variant_id + "_run_" + std::to_string(run_counter++) + "_seed_" + std::to_string(seed);

            try {
                YAML::Node scenario_node = YAML::Load(base.canonical_yaml);

                for (const auto& o : definition.global_overrides) {
                    apply_override(scenario_node, o);
                    manifest.overrides.push_back(o);
                }
                for (const auto& o : variant.overrides) {
                    apply_override(scenario_node, o);
                    manifest.overrides.push_back(o);
                }

                DeterministicRng overlay_rng(seed, base.scenario.scenario_id);
                auto apply_overlay = [&](const StochasticOverlay& overlay) {
                    const std::string stream_key = "overlay:" + definition.experiment_id + ":" + variant.variant_id + ":" + overlay.overlay_id;
                    const JsonValue sampled = sample_overlay(overlay, overlay_rng, stream_key);
                    manifest.overlay_samples.emplace(overlay.overlay_id, sampled);

                    ScenarioOverride synthetic;
                    synthetic.override_id = overlay.overlay_id;
                    synthetic.op = OverrideOp::REPLACE;
                    synthetic.target = overlay.target;
                    synthetic.value = sampled;
                    apply_override(scenario_node, synthetic);
                    manifest.overrides.push_back(std::move(synthetic));
                };

                for (const auto& overlay : definition.overlays) {
                    apply_overlay(overlay);
                }
                for (const auto& overlay : variant.overlays) {
                    apply_overlay(overlay);
                }

                YAML::Emitter emitter;
                emitter.SetIndent(2);
                emitter << scenario_node;
                const std::string overridden_yaml = emitter.c_str();

                schema::ScenarioDefinition scenario_def = serializer.deserialize(overridden_yaml);
                const std::string canonical_yaml = serializer.serialize(scenario_def);
                manifest.resolved_scenario_hash = to_hex_u64(fnv1a_64(canonical_yaml));

                const auto compiled = noisiax::compile_scenario(scenario_def);

                RunOptions run_options = options.run_options;
                run_options.seed_override = seed;
                manifest.trace_level = run_options.trace_level;
                manifest.max_time = run_options.max_time;
                manifest.max_events = run_options.max_events;
                manifest.include_final_state = run_options.include_final_state;
                manifest.include_causal_graph = run_options.include_causal_graph;
                const auto run_result = noisiax::run_scenario_detailed(compiled, run_options);
                manifest.success = run_result.report.success;
                for (const auto& err : run_result.report.errors) {
                    manifest.errors.push_back(err);
                }
                manifest.final_fingerprint = [&]() -> std::string {
                    const auto it = run_result.report.statistics.find("state_fingerprint");
                    if (it != run_result.report.statistics.end()) {
                        return it->second;
                    }
                    return "";
                }();

                for (const auto& metric : definition.metrics) {
                    if (metric.kind == "runtime_stat") {
                        const auto it = run_result.report.statistics.find(metric.key);
                        if (it == run_result.report.statistics.end()) {
                            continue;
                        }
                        manifest.metrics[metric.metric_id] = it->second;
                        if (auto v = parse_double_strict(it->second); v.has_value()) {
                            metric_samples[metric.metric_id].push_back(*v);
                        }
                        continue;
                    }
                    if (metric.kind == "typed_field_final") {
                        if (!metric.typed_field.has_value()) continue;
                        if (!run_result.typed_final_state.has_value()) continue;
                        const auto& tf = *metric.typed_field;

                        std::optional<schema::TypedScalarValue> field_value;
                        for (const auto& ent : run_result.typed_final_state->entities) {
                            if (ent.entity_id != tf.entity_id) continue;
                            for (const auto& comp : ent.components) {
                                if (comp.component_type_id != tf.component_type_id) continue;
                                const auto it = comp.fields.find(tf.field_name);
                                if (it == comp.fields.end()) continue;
                                field_value = it->second;
                                break;
                            }
                        }
                        if (!field_value.has_value()) continue;

                        const double numeric = std::visit([](auto&& arg) -> double {
                            using T = std::decay_t<decltype(arg)>;
                            if constexpr (std::is_same_v<T, int64_t>) return static_cast<double>(arg);
                            if constexpr (std::is_same_v<T, double>) return arg;
                            if constexpr (std::is_same_v<T, bool>) return arg ? 1.0 : 0.0;
                            return std::numeric_limits<double>::quiet_NaN();
                        }, *field_value);

                        if (std::isfinite(numeric)) {
                            manifest.metrics[metric.metric_id] = std::to_string(numeric);
                            metric_samples[metric.metric_id].push_back(numeric);
                        }
                        continue;
                    }
                }

                if (definition.write_run_details) {
                    std::ostringstream detail;
                    detail << "{\n";
                    detail << "  \"scenario_id\": " << json_string(run_result.report.scenario_id) << ",\n";
                    detail << "  \"success\": " << (run_result.report.success ? "true" : "false") << ",\n";
                    detail << "  \"errors\": [";
                    for (std::size_t i = 0; i < run_result.report.errors.size(); ++i) {
                        if (i) detail << ", ";
                        detail << json_string(run_result.report.errors[i]);
                    }
                    detail << "],\n";
                    detail << "  \"warnings\": [";
                    for (std::size_t i = 0; i < run_result.report.warnings.size(); ++i) {
                        if (i) detail << ", ";
                        detail << json_string(run_result.report.warnings[i]);
                    }
                    detail << "],\n";
                    detail << "  \"statistics\": {";
                    bool first = true;
                    for (const auto& [k, v] : run_result.report.statistics) {
                        if (!first) detail << ", ";
                        first = false;
                        detail << json_string(k) << ": " << json_string(v);
                    }
                    detail << "}\n";
                    detail << "}\n";

                    const fs::path detail_path = fs::path(options.output_dir) / (manifest.run_id + ".json");
                    write_file_or_throw(detail_path, detail.str());
                }
            } catch (const std::exception& ex) {
                manifest.success = false;
                manifest.errors.push_back(ex.what());
                if (definition.fail_fast) {
                    result.runs.push_back(std::move(manifest));
                    throw;
                }
            }

            result.runs.push_back(std::move(manifest));
        }
    }

    for (const auto& [metric_id, samples] : metric_samples) {
        result.aggregates.push_back(compute_aggregate(metric_id, samples));
    }
    std::sort(result.aggregates.begin(), result.aggregates.end(), [](const AggregateMetric& a, const AggregateMetric& b) {
        return a.metric_id < b.metric_id;
    });

    write_file_or_throw(fs::path(options.output_dir) / "experiment_manifest.json", manifest_to_json(result));
    write_file_or_throw(fs::path(options.output_dir) / "summary.json", summary_to_json(result));

    return result;
}

}  // namespace noisiax::experiment
