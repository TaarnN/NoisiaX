#pragma once

#include "noisiax/extensions/diagnostics.hpp"
#include "noisiax/extensions/extension_api.hpp"
#include "noisiax/extensions/expression_registry.hpp"
#include "noisiax/extensions/metric_registry.hpp"
#include "noisiax/extensions/propagation_registry.hpp"
#include "noisiax/extensions/semver.hpp"

#include <map>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace noisiax::extensions {

struct DeclaredExtension {
    std::string id;
    std::optional<SemVer> version;
    std::string compatibility;
    YAML::Node config;
};

struct ResolvedExtension {
    ExtensionDescriptor descriptor;
    YAML::Node config;
};

class ExtensionRegistry {
public:
    void register_extension(std::unique_ptr<INoisiaXExtension> ext) {
        const auto desc = ext->descriptor();
        extensions_[desc.id] = std::move(ext);
    }

    const INoisiaXExtension* find_extension(std::string_view id) const {
        auto it = extensions_.find(std::string(id));
        if (it == extensions_.end()) return nullptr;
        return it->second.get();
    }

    std::vector<ResolvedExtension> resolve_declared(const std::vector<DeclaredExtension>& declared,
                                                    DiagnosticSink& sink) const {
        std::vector<ResolvedExtension> out;
        out.reserve(declared.size());

        for (const auto& req : declared) {
            const INoisiaXExtension* ext = find_extension(req.id);
            if (ext == nullptr) {
                sink.error("Unknown extension id: " + req.id);
                continue;
            }

            const auto desc = ext->descriptor();
            if (req.version.has_value() && (*req.version != desc.version)) {
                sink.error("Extension version mismatch for " + req.id +
                           " (requested " + to_string(*req.version) +
                           ", available " + to_string(desc.version) + ")");
                continue;
            }
            if (!req.compatibility.empty() && !satisfies_range(desc.version, req.compatibility)) {
                sink.error("Extension compatibility range not satisfied for " + req.id +
                           " (available " + to_string(desc.version) +
                           " not in '" + req.compatibility + "')");
                continue;
            }

            out.push_back(ResolvedExtension{desc, req.config});
        }
        return out;
    }

    // Symbol registries (populated by built-ins + resolved extensions).
    PropagationFunctionRegistry& propagation_functions() { return propagation_registry_; }
    const PropagationFunctionRegistry& propagation_functions() const { return propagation_registry_; }

    ExpressionFunctionRegistry& expression_functions() { return expression_registry_; }
    const ExpressionFunctionRegistry& expression_functions() const { return expression_registry_; }

    ExperimentMetricRegistry& experiment_metrics() { return metric_registry_; }
    const ExperimentMetricRegistry& experiment_metrics() const { return metric_registry_; }

private:
    std::map<std::string, std::unique_ptr<INoisiaXExtension>> extensions_;
    PropagationFunctionRegistry propagation_registry_;
    ExpressionFunctionRegistry expression_registry_;
    ExperimentMetricRegistry metric_registry_;
};

}  // namespace noisiax::extensions
