#pragma once

#include <functional>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace noisiax {
struct RunResult;
}  // namespace noisiax

namespace noisiax::experiment {
struct JsonValue;
}  // namespace noisiax::experiment

namespace noisiax::extensions {

using MetricFn = std::function<std::string(const noisiax::RunResult& run,
                                           const noisiax::experiment::JsonValue& config)>;

class ExperimentMetricRegistry {
public:
    void register_metric(const std::string& symbol_id, MetricFn fn) {
        metrics_[symbol_id] = std::move(fn);
    }

    void register_alias(const std::string& alias, const std::string& canonical_symbol_id) {
        aliases_[alias] = canonical_symbol_id;
    }

    std::optional<std::string> resolve_id(std::string_view symbol_id) const {
        if (metrics_.find(std::string(symbol_id)) != metrics_.end()) {
            return std::string(symbol_id);
        }
        if (auto it = aliases_.find(std::string(symbol_id)); it != aliases_.end()) {
            return it->second;
        }
        return std::nullopt;
    }

    const MetricFn* find(std::string_view symbol_id) const {
        const auto resolved = resolve_id(symbol_id);
        if (!resolved.has_value()) return nullptr;
        if (auto it = metrics_.find(*resolved); it != metrics_.end()) {
            return &it->second;
        }
        return nullptr;
    }

private:
    std::map<std::string, MetricFn> metrics_;
    std::map<std::string, std::string> aliases_;
};

}  // namespace noisiax::extensions

