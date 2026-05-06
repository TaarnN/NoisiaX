#include "noisiax/extensions/acme_market_extension.hpp"

#include "noisiax/extensions/extension_registry.hpp"
#include "noisiax/experiment/experiment.hpp"
#include "noisiax/noisiax.hpp"

#include <cmath>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>
#include <variant>
#include <vector>

namespace noisiax::extensions {
namespace {

std::optional<double> json_number(const experiment::JsonValue& value) {
    if (const auto* v = std::get_if<double>(&value.data)) return *v;
    if (const auto* v_i = std::get_if<int64_t>(&value.data)) return static_cast<double>(*v_i);
    return std::nullopt;
}

std::optional<std::string> json_string(const experiment::JsonValue& value) {
    if (const auto* v = std::get_if<std::string>(&value.data)) return *v;
    return std::nullopt;
}

const experiment::JsonValue* json_object_get(const experiment::JsonValue& value, const std::string& key) {
    const auto* obj = std::get_if<experiment::JsonValue::Object>(&value.data);
    if (obj == nullptr) return nullptr;
    if (auto it = obj->find(key); it != obj->end()) {
        return &it->second;
    }
    return nullptr;
}

class AcmeMarketExtension final : public INoisiaXExtension {
public:
    ExtensionDescriptor descriptor() const override {
        ExtensionDescriptor d;
        d.id = "acme.market";
        d.version = SemVer{1, 2, 0};
        d.noisiax_compat = ">=5.0,<6.0";
        return d;
    }

    void register_symbols(ExtensionRegistry& registry) const override {
        registry.propagation_functions().register_function(
            "acme.market::bid_boost",
            [](double& target, const double& source, double weight) {
                target = (source + 0.5) * weight;
            });

        registry.expression_functions().register_function(
            "acme.market::bid_price_v2",
            [](const std::vector<ExprValue>& args) -> ExprValue {
                if (args.size() != 1) {
                    throw std::runtime_error("acme.market::bid_price_v2(x) expects 1 argument");
                }
                const auto* x = std::get_if<double>(&args[0]);
                if (x == nullptr) {
                    throw std::runtime_error("acme.market::bid_price_v2(x) expects numeric x");
                }
                return (*x) * 1.1 + 2.0;
            });

        registry.experiment_metrics().register_metric(
            "acme.market::scaled_typed_field_final",
            [](const noisiax::RunResult& run, const experiment::JsonValue& config) -> std::string {
                const auto* entity_id_v = json_object_get(config, "entity_id");
                const auto* component_v = json_object_get(config, "component_type_id");
                const auto* field_v = json_object_get(config, "field_name");
                const auto* mult_v = json_object_get(config, "multiplier");
                if (entity_id_v == nullptr || component_v == nullptr || field_v == nullptr || mult_v == nullptr) {
                    throw std::runtime_error("acme.market::scaled_typed_field_final requires config keys: entity_id, component_type_id, field_name, multiplier");
                }

                const auto entity_id = json_string(*entity_id_v);
                const auto component_type_id = json_string(*component_v);
                const auto field_name = json_string(*field_v);
                const auto mult = json_number(*mult_v);
                if (!entity_id.has_value() || !component_type_id.has_value() || !field_name.has_value() || !mult.has_value()) {
                    throw std::runtime_error("acme.market::scaled_typed_field_final config has invalid types");
                }

                if (!run.typed_final_state.has_value()) {
                    throw std::runtime_error("acme.market::scaled_typed_field_final requires typed_final_state");
                }

                const auto& snapshot = *run.typed_final_state;
                for (const auto& ent : snapshot.entities) {
                    if (ent.entity_id != *entity_id) continue;
                    for (const auto& comp : ent.components) {
                        if (comp.component_type_id != *component_type_id) continue;
                        if (auto it = comp.fields.find(*field_name); it != comp.fields.end()) {
                            if (const auto* num = std::get_if<double>(&it->second)) {
                                const double out = (*num) * (*mult);
                                std::ostringstream oss;
                                oss << out;
                                return oss.str();
                            }
                            if (const auto* i = std::get_if<int64_t>(&it->second)) {
                                const double out = static_cast<double>(*i) * (*mult);
                                std::ostringstream oss;
                                oss << out;
                                return oss.str();
                            }
                            throw std::runtime_error("acme.market::scaled_typed_field_final field is not numeric");
                        }
                    }
                }

                throw std::runtime_error("acme.market::scaled_typed_field_final could not find configured typed field");
            });
    }

    void validate_authoring_block(const YAML::Node& block,
                                  const YAML::Node&,
                                  DiagnosticSink& sink) const override {
        if (!block) {
            return;
        }
        if (!block.IsMap()) {
            sink.error("acme.market block must be a mapping");
            return;
        }
        if (block["demo"] && !block["demo"].IsMap()) {
            sink.error("acme.market.demo must be a mapping");
            return;
        }
        if (block["demo"]) {
            const auto demo = block["demo"];
            if (demo["base"] && !demo["base"].IsScalar()) {
                sink.error("acme.market.demo.base must be a scalar");
            }
            if (demo["multiplier"] && !demo["multiplier"].IsScalar()) {
                sink.error("acme.market.demo.multiplier must be a scalar");
            }
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

        double base = 10.0;
        double multiplier = 2.0;
        if (block && block.IsMap() && block["demo"] && block["demo"].IsMap()) {
            const auto demo = block["demo"];
            if (demo["base"]) {
                base = demo["base"].as<double>();
            }
            if (demo["multiplier"]) {
                multiplier = demo["multiplier"].as<double>();
            }
        }

        if (out && out.IsMap()) {
            out.remove(ext_id);
        }

        const std::string base_var = "var_acme_base";
        const std::string price_var = "var_acme_price";

        // Ensure all required top-level sequences exist.
        auto ensure_seq = [&](const char* key) {
            if (!out[key]) {
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

        if (out["assumptions"].IsSequence() && out["assumptions"].size() == 0) {
            YAML::Node a(YAML::NodeType::Map);
            a["assumption_id"] = "assume_acme_market_generated";
            a["category"] = "extension";
            a["description"] = "Scenario content generated by the acme.market v5 extension.";
            a["rationale"] = "Used to validate extension transforms and symbol registries.";
            a["confidence_level"] = "REJECT";
            out["assumptions"].push_back(a);
        }

        if (out["entities"].IsSequence() && out["entities"].size() == 0) {
            YAML::Node e(YAML::NodeType::Map);
            e["entity_id"] = "acme_world";
            e["entity_type"] = "world";
            e["description"] = "Generated world entity";
            e["attributes"] = YAML::Node(YAML::NodeType::Map);
            out["entities"].push_back(e);
        }

        // Variables.
        {
            YAML::Node v_base(YAML::NodeType::Map);
            v_base["variable_id"] = base_var;
            v_base["type"] = "FLOAT";
            v_base["default_value"] = base;
            v_base["description"] = "Generated base input (acme.market)";
            out["variables"].push_back(v_base);

            YAML::Node v_price(YAML::NodeType::Map);
            v_price["variable_id"] = price_var;
            v_price["type"] = "FLOAT";
            v_price["default_value"] = 0.0;
            v_price["description"] = "Generated price output (acme.market)";
            out["variables"].push_back(v_price);
        }

        // Dependency edge.
        {
            YAML::Node edge(YAML::NodeType::Map);
            edge["edge_id"] = "edge_acme_base_to_price";
            edge["source_variable"] = base_var;
            edge["target_variable"] = price_var;
            edge["propagation_function_id"] = "acme.market::bid_boost";
            edge["weight"] = multiplier;
            edge["metadata"] = YAML::Node(YAML::NodeType::Map);
            out["dependency_edges"].push_back(edge);
        }

        // Constraint.
        {
            YAML::Node c(YAML::NodeType::Map);
            c["constraint_id"] = "constraint_acme_price_nonneg";
            YAML::Node vars(YAML::NodeType::Sequence);
            vars.push_back(price_var);
            c["affected_variables"] = vars;
            c["constraint_expression"] = price_var + " >= 0";
            c["enforcement_level"] = "REJECT";
            c["error_message"] = "acme.market generated price must be >= 0";
            out["constraints"].push_back(c);
        }

        // Evaluation criterion.
        {
            YAML::Node crit(YAML::NodeType::Map);
            crit["criterion_id"] = "criterion_acme_price_final";
            crit["metric_name"] = "final";
            crit["aggregation_method"] = "FINAL";
            YAML::Node inputs(YAML::NodeType::Sequence);
            inputs.push_back(price_var);
            crit["input_variables"] = inputs;
            crit["description"] = "acme.market generated final price check";
            out["evaluation_criteria"].push_back(crit);
        }

        // Event to trigger dependency recompute.
        {
            YAML::Node ev(YAML::NodeType::Map);
            ev["event_id"] = "ev_acme_set_base";
            ev["event_type"] = "SCHEDULED";
            ev["timestamp"] = 0.0;
            ev["priority"] = 0;
            YAML::Node payload(YAML::NodeType::Map);
            payload["variable_id"] = base_var;
            {
                std::ostringstream oss;
                oss << base;
                payload["value"] = oss.str();
            }
            ev["event_payload"] = payload;
            ev["description"] = "acme.market init event";
            out["events"].push_back(ev);
        }

        return TransformResult{out};
    }
};

}  // namespace

std::unique_ptr<INoisiaXExtension> make_acme_market_extension() {
    return std::make_unique<AcmeMarketExtension>();
}

}  // namespace noisiax::extensions
