#pragma once

#include "noisiax/noisiax.hpp"

namespace noisiax::engine {

RunResult run_typed_layer_scenario(const compiler::CompiledScenario& compiled,
                                  const RunOptions& options);

}  // namespace noisiax::engine

