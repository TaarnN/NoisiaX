#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <tuple>

namespace noisiax::extensions {

struct SemVer {
    uint32_t major = 0;
    uint32_t minor = 0;
    uint32_t patch = 0;

    constexpr auto as_tuple() const { return std::tuple{major, minor, patch}; }

    constexpr bool operator==(const SemVer& other) const = default;
    constexpr bool operator<(const SemVer& other) const { return as_tuple() < other.as_tuple(); }
    constexpr bool operator>(const SemVer& other) const { return other < *this; }
    constexpr bool operator<=(const SemVer& other) const { return !(other < *this); }
    constexpr bool operator>=(const SemVer& other) const { return !(*this < other); }
};

inline std::string to_string(const SemVer& ver) {
    return std::to_string(ver.major) + "." + std::to_string(ver.minor) + "." + std::to_string(ver.patch);
}

inline std::optional<SemVer> parse_semver(std::string_view text) {
    auto parse_uint = [](std::string_view slice, uint32_t& out) -> bool {
        if (slice.empty()) return false;
        uint64_t value = 0;
        for (const char c : slice) {
            if (c < '0' || c > '9') return false;
            value = (value * 10ULL) + static_cast<uint64_t>(c - '0');
            if (value > 0xFFFFFFFFULL) return false;
        }
        out = static_cast<uint32_t>(value);
        return true;
    };

    // Allow "MAJOR", "MAJOR.MINOR", or "MAJOR.MINOR.PATCH" and coerce missing parts to 0.
    // We intentionally do not accept prerelease/build metadata yet; v5 compatibility ranges
    // are expected to be numeric.
    SemVer out;
    std::size_t first_dot = text.find('.');
    if (first_dot == std::string_view::npos) {
        if (!parse_uint(text, out.major)) return std::nullopt;
        return out;
    }

    std::size_t second_dot = text.find('.', first_dot + 1);
    if (second_dot == std::string_view::npos) {
        if (!parse_uint(text.substr(0, first_dot), out.major)) return std::nullopt;
        if (!parse_uint(text.substr(first_dot + 1), out.minor)) return std::nullopt;
        return out;
    }

    if (!parse_uint(text.substr(0, first_dot), out.major)) return std::nullopt;
    if (!parse_uint(text.substr(first_dot + 1, second_dot - first_dot - 1), out.minor)) return std::nullopt;
    if (!parse_uint(text.substr(second_dot + 1), out.patch)) return std::nullopt;
    return out;
}

enum class VersionCmpOp {
    LT,
    LTE,
    EQ,
    GTE,
    GT
};

struct VersionConstraint {
    VersionCmpOp op = VersionCmpOp::EQ;
    SemVer version;
};

inline std::optional<VersionConstraint> parse_constraint(std::string_view text) {
    auto trim = [](std::string_view v) {
        while (!v.empty() && (v.front() == ' ' || v.front() == '\t' || v.front() == '\n' || v.front() == '\r')) {
            v.remove_prefix(1);
        }
        while (!v.empty() && (v.back() == ' ' || v.back() == '\t' || v.back() == '\n' || v.back() == '\r')) {
            v.remove_suffix(1);
        }
        return v;
    };

    text = trim(text);
    if (text.empty()) return std::nullopt;

    VersionCmpOp op = VersionCmpOp::EQ;
    std::string_view rest = text;
    if (rest.starts_with(">=")) {
        op = VersionCmpOp::GTE;
        rest.remove_prefix(2);
    } else if (rest.starts_with("<=")) {
        op = VersionCmpOp::LTE;
        rest.remove_prefix(2);
    } else if (rest.starts_with("==")) {
        op = VersionCmpOp::EQ;
        rest.remove_prefix(2);
    } else if (rest.starts_with(">")) {
        op = VersionCmpOp::GT;
        rest.remove_prefix(1);
    } else if (rest.starts_with("<")) {
        op = VersionCmpOp::LT;
        rest.remove_prefix(1);
    } else if (rest.starts_with("=")) {
        op = VersionCmpOp::EQ;
        rest.remove_prefix(1);
    }

    rest = trim(rest);
    const auto ver = parse_semver(rest);
    if (!ver.has_value()) return std::nullopt;
    return VersionConstraint{op, *ver};
}

inline bool satisfies(const SemVer& version, const VersionConstraint& constraint) {
    switch (constraint.op) {
        case VersionCmpOp::LT:
            return version < constraint.version;
        case VersionCmpOp::LTE:
            return version <= constraint.version;
        case VersionCmpOp::EQ:
            return version == constraint.version;
        case VersionCmpOp::GTE:
            return version >= constraint.version;
        case VersionCmpOp::GT:
            return version > constraint.version;
    }
    return false;
}

inline bool satisfies_range(const SemVer& version, std::string_view range) {
    // Supported syntax: comma-separated constraints, e.g. ">=1.2,<2.0".
    // Empty range means "any".
    auto trim = [](std::string_view v) {
        while (!v.empty() && (v.front() == ' ' || v.front() == '\t' || v.front() == '\n' || v.front() == '\r')) {
            v.remove_prefix(1);
        }
        while (!v.empty() && (v.back() == ' ' || v.back() == '\t' || v.back() == '\n' || v.back() == '\r')) {
            v.remove_suffix(1);
        }
        return v;
    };

    range = trim(range);
    if (range.empty()) return true;

    std::size_t start = 0;
    while (start < range.size()) {
        std::size_t comma = range.find(',', start);
        if (comma == std::string_view::npos) comma = range.size();
        const std::string_view part = trim(range.substr(start, comma - start));
        if (!part.empty()) {
            const auto constraint = parse_constraint(part);
            if (!constraint.has_value()) return false;
            if (!satisfies(version, *constraint)) return false;
        }
        start = comma + 1;
    }

    return true;
}

}  // namespace noisiax::extensions

