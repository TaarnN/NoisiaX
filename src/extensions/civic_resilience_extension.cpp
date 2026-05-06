#include "noisiax/extensions/civic_resilience_extension.hpp"

#include "noisiax/extensions/extension_registry.hpp"
#include "noisiax/experiment/experiment.hpp"
#include "noisiax/noisiax.hpp"

#include <algorithm>
#include <cmath>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <variant>
#include <vector>

namespace noisiax::extensions {
namespace {

double clamp(double value, double lo, double hi) {
    return std::max(lo, std::min(value, hi));
}

double expr_number(const ExprValue& value, const std::string& name) {
    if (const auto* v = std::get_if<double>(&value)) return *v;
    if (const auto* v_bool = std::get_if<bool>(&value)) return *v_bool ? 1.0 : 0.0;
    throw std::runtime_error(name + " expects numeric arguments");
}

void expect_arity(const std::vector<ExprValue>& args,
                  std::size_t expected,
                  const std::string& fn_name) {
    if (args.size() != expected) {
        std::ostringstream oss;
        oss << fn_name << " expects " << expected << " arguments";
        throw std::runtime_error(oss.str());
    }
}

std::optional<double> json_number(const experiment::JsonValue& value) {
    if (const auto* v = std::get_if<double>(&value.data)) return *v;
    if (const auto* v_i = std::get_if<int64_t>(&value.data)) return static_cast<double>(*v_i);
    return std::nullopt;
}

std::optional<std::string> json_string(const experiment::JsonValue& value) {
    if (const auto* v = std::get_if<std::string>(&value.data)) return *v;
    return std::nullopt;
}

const experiment::JsonValue* json_object_get(const experiment::JsonValue& value,
                                             const std::string& key) {
    const auto* obj = std::get_if<experiment::JsonValue::Object>(&value.data);
    if (obj == nullptr) return nullptr;
    if (auto it = obj->find(key); it != obj->end()) {
        return &it->second;
    }
    return nullptr;
}

std::optional<double> typed_scalar_number(const schema::TypedScalarValue& value) {
    if (const auto* v = std::get_if<double>(&value)) return *v;
    if (const auto* v_i = std::get_if<int64_t>(&value)) return static_cast<double>(*v_i);
    if (const auto* v_bool = std::get_if<bool>(&value)) return *v_bool ? 1.0 : 0.0;
    return std::nullopt;
}

bool starts_with(const std::string& text, const std::string& prefix) {
    return prefix.empty() || text.rfind(prefix, 0) == 0;
}

void validate_optional_numeric_scalar(const YAML::Node& node,
                                      const std::string& path,
                                      DiagnosticSink& sink) {
    if (!node) {
        return;
    }
    if (!node.IsScalar()) {
        sink.error(path + " must be a scalar");
        return;
    }
    try {
        (void)node.as<double>();
    } catch (...) {
        sink.error(path + " must be numeric");
    }
}

YAML::Node make_scalar_payload_value(double value) {
    std::ostringstream oss;
    oss << value;
    YAML::Node out;
    out = oss.str();
    return out;
}

class CivicResilienceExtension final : public INoisiaXExtension {
public:
    ExtensionDescriptor descriptor() const override {
        ExtensionDescriptor d;
        d.id = "civic.resilience";
        d.version = SemVer{1, 0, 0};
        d.noisiax_compat = ">=5.0,<6.0";
        return d;
    }

    void register_symbols(ExtensionRegistry& registry) const override {
        registry.propagation_functions().register_function(
            "civic.resilience::risk_weighted_boost",
            [](double& target, const double& source, double weight) {
                const double gain = source * weight / (1.0 + std::abs(target));
                target = clamp(target + gain, 0.0, 100.0);
            });

        registry.expression_functions().register_function(
            "civic.resilience::resilience_index",
            [](const std::vector<ExprValue>& args) -> ExprValue {
                constexpr const char* kFn = "civic.resilience::resilience_index";
                expect_arity(args, 9, kFn);
                const double services =
                    (expr_number(args[0], kFn) +
                     expr_number(args[1], kFn) +
                     expr_number(args[2], kFn) +
                     expr_number(args[3], kFn) +
                     expr_number(args[4], kFn) +
                     expr_number(args[5], kFn) +
                     expr_number(args[6], kFn)) / 7.0;
                const double stress = expr_number(args[7], kFn);
                const double rumor = expr_number(args[8], kFn);
                return clamp(services - stress * 0.35 - rumor * 0.20, 0.0, 1.0);
            });

        registry.expression_functions().register_function(
            "civic.resilience::scarcity_pressure",
            [](const std::vector<ExprValue>& args) -> ExprValue {
                constexpr const char* kFn = "civic.resilience::scarcity_pressure";
                expect_arity(args, 5, kFn);
                const double stock = expr_number(args[0], kFn);
                const double load = expr_number(args[1], kFn);
                const double capacity = std::max(1.0, expr_number(args[2], kFn));
                const double damage = expr_number(args[3], kFn);
                const double reliability = expr_number(args[4], kFn);
                const double load_ratio = load / capacity;
                return clamp((1.0 - stock) * 0.80 +
                                 load_ratio * 0.18 +
                                 damage * 0.65 +
                                 (1.0 - reliability) * 0.42,
                             0.0,
                             25.0);
            });

        registry.expression_functions().register_function(
            "civic.resilience::triage_priority",
            [](const std::vector<ExprValue>& args) -> ExprValue {
                constexpr const char* kFn = "civic.resilience::triage_priority";
                expect_arity(args, 5, kFn);
                const double stress = expr_number(args[0], kFn);
                const double care_load = expr_number(args[1], kFn);
                const double mobility = expr_number(args[2], kFn);
                const double preparedness = expr_number(args[3], kFn);
                const double trust = expr_number(args[4], kFn);
                return clamp(stress * 0.42 +
                                 care_load * 0.22 +
                                 (1.0 - mobility) * 0.18 +
                                 (1.0 - preparedness) * 0.12 +
                                 (1.0 - trust) * 0.06,
                             0.0,
                             1.0);
            });

        registry.experiment_metrics().register_metric(
            "civic.resilience::population_average_field",
            [](const noisiax::RunResult& run, const experiment::JsonValue& config) -> std::string {
                const auto* component_v = json_object_get(config, "component_type_id");
                const auto* field_v = json_object_get(config, "field_name");
                if (component_v == nullptr || field_v == nullptr) {
                    throw std::runtime_error("civic.resilience::population_average_field requires component_type_id and field_name");
                }
                const auto component_id = json_string(*component_v);
                const auto field_name = json_string(*field_v);
                if (!component_id.has_value() || !field_name.has_value()) {
                    throw std::runtime_error("civic.resilience::population_average_field config has invalid types");
                }

                std::string entity_prefix;
                if (const auto* prefix_v = json_object_get(config, "entity_id_prefix"); prefix_v != nullptr) {
                    const auto prefix = json_string(*prefix_v);
                    if (!prefix.has_value()) {
                        throw std::runtime_error("civic.resilience::population_average_field entity_id_prefix must be a string");
                    }
                    entity_prefix = *prefix;
                }

                double multiplier = 1.0;
                if (const auto* mult_v = json_object_get(config, "multiplier"); mult_v != nullptr) {
                    const auto mult = json_number(*mult_v);
                    if (!mult.has_value()) {
                        throw std::runtime_error("civic.resilience::population_average_field multiplier must be numeric");
                    }
                    multiplier = *mult;
                }

                if (!run.typed_final_state.has_value()) {
                    throw std::runtime_error("civic.resilience::population_average_field requires typed_final_state");
                }

                double sum = 0.0;
                std::size_t count = 0;
                for (const auto& ent : run.typed_final_state->entities) {
                    if (!starts_with(ent.entity_id, entity_prefix)) continue;
                    for (const auto& comp : ent.components) {
                        if (comp.component_type_id != *component_id) continue;
                        const auto it = comp.fields.find(*field_name);
                        if (it == comp.fields.end()) continue;
                        const auto numeric = typed_scalar_number(it->second);
                        if (!numeric.has_value()) {
                            throw std::runtime_error("civic.resilience::population_average_field selected field is not numeric");
                        }
                        sum += *numeric;
                        ++count;
                    }
                }

                if (count == 0) {
                    throw std::runtime_error("civic.resilience::population_average_field matched no numeric fields");
                }

                std::ostringstream oss;
                oss << (sum / static_cast<double>(count)) * multiplier;
                return oss.str();
            });
    }

    void validate_authoring_block(const YAML::Node& block,
                                  const YAML::Node&,
                                  DiagnosticSink& sink) const override {
        if (!block) {
            return;
        }
        if (!block.IsMap()) {
            sink.error("civic.resilience block must be a mapping");
            return;
        }
        if (block["control_index"] && !block["control_index"].IsMap()) {
            sink.error("civic.resilience.control_index must be a mapping");
            return;
        }
        if (block["control_index"]) {
            const auto ci = block["control_index"];
            validate_optional_numeric_scalar(ci["hazard_baseline"],
                                             "civic.resilience.control_index.hazard_baseline",
                                             sink);
            validate_optional_numeric_scalar(ci["resource_readiness"],
                                             "civic.resilience.control_index.resource_readiness",
                                             sink);
            validate_optional_numeric_scalar(ci["governance_weight"],
                                             "civic.resilience.control_index.governance_weight",
                                             sink);
        }
    }

    TransformResult transform(const TransformContext&,
                              const YAML::Node& root,
                              const YAML::Node&) const override {
        YAML::Node out = YAML::Clone(root);
        const std::string ext_id = descriptor().id;
        const YAML::Node block = out[ext_id];
        if (!block) {
            return TransformResult{out};
        }

        double hazard_baseline = 0.40;
        double resource_readiness = 0.66;
        double governance_weight = 1.25;
        if (block.IsMap() && block["control_index"] && block["control_index"].IsMap()) {
            const auto ci = block["control_index"];
            if (ci["hazard_baseline"]) hazard_baseline = ci["hazard_baseline"].as<double>();
            if (ci["resource_readiness"]) resource_readiness = ci["resource_readiness"].as<double>();
            if (ci["governance_weight"]) governance_weight = ci["governance_weight"].as<double>();
        }

        if (out && out.IsMap()) {
            out.remove(ext_id);
        }

        auto ensure_seq = [&](const char* key) {
            if (!out[key] || !out[key].IsSequence()) {
                out[key] = YAML::Node(YAML::NodeType::Sequence);
            }
        };

        ensure_seq("assumptions");
        ensure_seq("entities");
        ensure_seq("variables");
        ensure_seq("dependency_edges");
        ensure_seq("constraints");
        ensure_seq("events");
        ensure_seq("evaluation_criteria");

        {
            YAML::Node a(YAML::NodeType::Map);
            a["assumption_id"] = "assume_civic_resilience_extension_control";
            a["category"] = "extension";
            a["description"] = "Scenario control variables generated by the civic.resilience v5 extension.";
            a["rationale"] = "Exercises deterministic authoring-block lowering, extension propagation symbols, and scenario provenance.";
            a["confidence_level"] = "REJECT";
            out["assumptions"].push_back(a);
        }

        {
            YAML::Node e(YAML::NodeType::Map);
            e["entity_id"] = "civic_resilience_extension_control";
            e["entity_type"] = "extension_control";
            e["description"] = "Control node generated by civic.resilience.";
            e["attributes"] = YAML::Node(YAML::NodeType::Map);
            e["attributes"]["extension"] = ext_id;
            out["entities"].push_back(e);
        }

        const std::string hazard_var = "var_civic_hazard_baseline";
        const std::string readiness_var = "var_civic_resource_readiness";
        const std::string index_var = "var_civic_composite_readiness";

        auto add_float_variable = [&](const std::string& id,
                                      double value,
                                      const std::string& description) {
            YAML::Node v(YAML::NodeType::Map);
            v["variable_id"] = id;
            v["entity_ref"] = "civic_resilience_extension_control";
            v["type"] = "FLOAT";
            v["default_value"] = value;
            v["description"] = description;
            out["variables"].push_back(v);
        };

        add_float_variable(hazard_var, hazard_baseline, "Generated hazard baseline.");
        add_float_variable(readiness_var, resource_readiness, "Generated resource readiness.");
        add_float_variable(index_var, hazard_baseline, "Generated composite readiness.");

        {
            YAML::Node edge(YAML::NodeType::Map);
            edge["edge_id"] = "edge_civic_readiness_to_composite";
            edge["source_variable"] = readiness_var;
            edge["target_variable"] = index_var;
            edge["propagation_function_id"] = "civic.resilience::risk_weighted_boost";
            edge["weight"] = governance_weight;
            edge["metadata"] = YAML::Node(YAML::NodeType::Map);
            edge["metadata"]["extension"] = ext_id;
            out["dependency_edges"].push_back(edge);
        }

        {
            YAML::Node c(YAML::NodeType::Map);
            c["constraint_id"] = "constraint_civic_composite_readiness_bounds";
            YAML::Node vars(YAML::NodeType::Sequence);
            vars.push_back(index_var);
            c["affected_variables"] = vars;
            c["constraint_expression"] = index_var + " >= 0";
            c["enforcement_level"] = "REJECT";
            c["error_message"] = "civic.resilience generated composite readiness must be non-negative";
            out["constraints"].push_back(c);
        }

        {
            YAML::Node ev(YAML::NodeType::Map);
            ev["event_id"] = "ev_civic_resource_readiness_seed";
            ev["event_type"] = "SCHEDULED";
            ev["timestamp"] = 0.0;
            ev["priority"] = 0;
            YAML::Node payload(YAML::NodeType::Map);
            payload["variable_id"] = readiness_var;
            payload["value"] = make_scalar_payload_value(resource_readiness);
            ev["event_payload"] = payload;
            ev["description"] = "civic.resilience generated readiness seed event";
            out["events"].push_back(ev);
        }

        {
            YAML::Node crit(YAML::NodeType::Map);
            crit["criterion_id"] = "criterion_civic_composite_readiness_final";
            crit["metric_name"] = "final";
            crit["aggregation_method"] = "FINAL";
            YAML::Node inputs(YAML::NodeType::Sequence);
            inputs.push_back(index_var);
            crit["input_variables"] = inputs;
            crit["description"] = "civic.resilience generated control index.";
            out["evaluation_criteria"].push_back(crit);
        }

        return TransformResult{out};
    }
};

}  // namespace

std::unique_ptr<INoisiaXExtension> make_civic_resilience_extension() {
    return std::make_unique<CivicResilienceExtension>();
}

}  // namespace noisiax::extensions
