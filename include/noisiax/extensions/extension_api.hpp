#pragma once

#include "noisiax/extensions/diagnostics.hpp"
#include "noisiax/extensions/semver.hpp"

#include <optional>
#include <string>

#include <yaml-cpp/yaml.h>

namespace noisiax::extensions {

struct ExtensionDescriptor {
    std::string id;               // e.g. "acme.market"
    SemVer version;               // extension version
    std::string noisiax_compat;    // semver range, e.g. ">=5.0,<6.0"

    bool operator==(const ExtensionDescriptor& other) const = default;
};

struct TransformContext {
    // Reserved for future: deterministic RNG streams, symbol table, trace sinks, etc.
};

struct TransformResult {
    YAML::Node lowered_root;  // Authoring YAML with extension blocks removed/lowered.
};

class INoisiaXExtension {
public:
    virtual ~INoisiaXExtension() = default;

    virtual ExtensionDescriptor descriptor() const = 0;

    // Register propagation functions, expression functions, metrics, etc.
    virtual void register_symbols(class ExtensionRegistry&) const = 0;

    // Validate the extension-owned authoring block (the top-level key equal to descriptor().id).
    virtual void validate_authoring_block(const YAML::Node& block,
                                          const YAML::Node& config,
                                          DiagnosticSink& sink) const = 0;

    // Deterministically lower authoring YAML into a new authoring YAML tree with the extension block removed.
    virtual TransformResult transform(const TransformContext& ctx,
                                      const YAML::Node& root,
                                      const YAML::Node& config) const = 0;
};

}  // namespace noisiax::extensions
