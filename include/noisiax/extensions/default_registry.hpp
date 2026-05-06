#pragma once

#include "noisiax/extensions/extension_registry.hpp"

namespace noisiax::extensions {

// Builds the default registry with core/std built-ins plus any statically linked extensions.
ExtensionRegistry make_default_registry();

}  // namespace noisiax::extensions

