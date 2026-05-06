#pragma once

#include "noisiax/extensions/symbol_id.hpp"

#include <functional>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace noisiax::extensions {

using ExprValue = std::variant<double, bool, std::string>;
using ExpressionFn = std::function<ExprValue(const std::vector<ExprValue>& args)>;

class ExpressionFunctionRegistry {
public:
    void register_function(const std::string& symbol_id, ExpressionFn fn) {
        functions_[symbol_id] = std::move(fn);
    }

    void register_alias(const std::string& alias, const std::string& canonical_symbol_id) {
        aliases_[alias] = canonical_symbol_id;
    }

    std::optional<std::string> resolve_id(std::string_view symbol_id) const {
        if (functions_.find(std::string(symbol_id)) != functions_.end()) {
            return std::string(symbol_id);
        }
        if (auto it = aliases_.find(std::string(symbol_id)); it != aliases_.end()) {
            return it->second;
        }
        return std::nullopt;
    }

    const ExpressionFn* find(std::string_view symbol_id) const {
        const auto resolved = resolve_id(symbol_id);
        if (!resolved.has_value()) return nullptr;
        if (auto it = functions_.find(*resolved); it != functions_.end()) {
            return &it->second;
        }
        return nullptr;
    }

    std::vector<std::string> registered_ids() const {
        std::vector<std::string> ids;
        ids.reserve(functions_.size());
        for (const auto& [id, _] : functions_) {
            ids.push_back(id);
        }
        return ids;
    }

    const std::map<std::string, ExpressionFn>& functions() const { return functions_; }
    const std::map<std::string, std::string>& aliases() const { return aliases_; }

private:
    std::map<std::string, ExpressionFn> functions_;
    std::map<std::string, std::string> aliases_;
};

}  // namespace noisiax::extensions
