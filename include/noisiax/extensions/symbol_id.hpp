#pragma once

#include <optional>
#include <string>
#include <string_view>

namespace noisiax::extensions {

struct SymbolId {
    std::string value;

    bool operator==(const SymbolId& other) const = default;
};

inline bool is_valid_symbol_id(std::string_view text) {
    const std::size_t pos = text.find("::");
    if (pos == std::string_view::npos) return false;
    if (text.find("::", pos + 2) != std::string_view::npos) return false;

    const std::string_view ns = text.substr(0, pos);
    const std::string_view name = text.substr(pos + 2);
    if (ns.empty() || name.empty()) return false;

    auto valid_part = [](std::string_view part) {
        for (const char c : part) {
            const bool ok =
                (c >= 'a' && c <= 'z') ||
                (c >= 'A' && c <= 'Z') ||
                (c >= '0' && c <= '9') ||
                c == '_' || c == '.' || c == '-';
            if (!ok) return false;
        }
        return true;
    };

    return valid_part(ns) && valid_part(name);
}

inline std::optional<SymbolId> parse_symbol_id(std::string_view text) {
    if (!is_valid_symbol_id(text)) return std::nullopt;
    return SymbolId{std::string(text)};
}

inline std::string_view symbol_namespace(std::string_view symbol_id) {
    const std::size_t pos = symbol_id.find("::");
    if (pos == std::string_view::npos) return {};
    return symbol_id.substr(0, pos);
}

inline std::string_view symbol_name(std::string_view symbol_id) {
    const std::size_t pos = symbol_id.find("::");
    if (pos == std::string_view::npos) return {};
    return symbol_id.substr(pos + 2);
}

}  // namespace noisiax::extensions

