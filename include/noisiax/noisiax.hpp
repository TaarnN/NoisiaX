#pragma once

#include "noisiax/schema/scenario_schema.hpp"
#include "noisiax/validation/scenario_validator.hpp"
#include "noisiax/compiler/scenario_compiler.hpp"
#include "noisiax/engine/simulation_engine.hpp"
#include "noisiax/scheduler/event_scheduler.hpp"
#include "noisiax/serialization/yaml_serializer.hpp"

#include <string>
#include <memory>
#include <optional>

namespace noisiax {

/**
 * @brief Validate a scenario definition from YAML file
 * @param filepath Path to the YAML scenario file
 * @return Validation report with errors, warnings, and info messages
 */
schema::ScenarioReport validate_scenario(const std::string& filepath);

/**
 * @brief Validate a scenario definition from string content
 * @param yaml_content The YAML content to validate
 * @return Validation report with errors, warnings, and info messages
 */
schema::ScenarioReport validate_scenario_from_string(const std::string& yaml_content);

/**
 * @brief Compile a validated scenario into runtime artifacts
 * @param filepath Path to the YAML scenario file (must be valid)
 * @return Compiled scenario artifact, or throws on validation failure
 */
compiler::CompiledScenario compile_scenario(const std::string& filepath);

/**
 * @brief Compile a scenario from already-parsed definition
 * @param scenario The validated scenario definition
 * @return Compiled scenario artifact
 */
compiler::CompiledScenario compile_scenario(const schema::ScenarioDefinition& scenario);

/**
 * @brief Run a compiled scenario to completion
 * @param compiled The compiled scenario
 * @return Runtime report with execution results
 */
schema::ScenarioReport run_scenario(const compiler::CompiledScenario& compiled);

/**
 * @brief Run a scenario from YAML file (validates, compiles, runs)
 * @param filepath Path to the YAML scenario file
 * @return Runtime report with execution results
 */
schema::ScenarioReport run_scenario(const std::string& filepath);

/**
 * @brief Save a checkpoint of simulation state
 * @param state The current simulation state
 * @param scenario_id The scenario identifier
 * @param filepath Output checkpoint file path
 * @return true if successful
 */
bool save_checkpoint(const engine::SimulationState& state, 
                     const std::string& scenario_id,
                     const std::string& filepath);

/**
 * @brief Load a checkpoint and restore simulation state
 * @param filepath Input checkpoint file path
 * @param state The simulation state to restore into
 * @return Scenario ID from checkpoint
 */
std::string load_checkpoint(const std::string& filepath, engine::SimulationState& state);

/**
 * @brief Library version information
 */
[[nodiscard]] constexpr std::string_view version() noexcept {
    return "1.0.0";
}

/**
 * @brief Library name
 */
[[nodiscard]] std::string_view name() noexcept;

} // namespace noisiax
