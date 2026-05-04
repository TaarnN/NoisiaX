#pragma once

#include "noisiax/schema/scenario_schema.hpp"
#include "noisiax/compiler/scenario_compiler.hpp"
#include <string>
#include <optional>

namespace noisiax::serialization {

/**
 * @brief YAML serialization/deserialization for scenario definitions
 */
class YamlSerializer {
public:
    YamlSerializer() = default;
    ~YamlSerializer() = default;
    
    /**
     * @brief Serialize a scenario definition to YAML string
     * @param scenario The scenario to serialize
     * @return YAML string representation
     */
    std::string serialize(const schema::ScenarioDefinition& scenario) const;
    
    /**
     * @brief Deserialize a YAML string to scenario definition
     * @param yaml_content The YAML content to parse
     * @return Parsed scenario definition
     * @throws std::runtime_error if parsing fails
     */
    schema::ScenarioDefinition deserialize(const std::string& yaml_content) const;
    
    /**
     * @brief Load scenario from YAML file
     * @param filepath Path to the YAML file
     * @return Loaded scenario definition
     * @throws std::runtime_error if file cannot be read or parsed
     */
    schema::ScenarioDefinition load_file(const std::string& filepath) const;
    
    /**
     * @brief Save scenario to YAML file
     * @param scenario The scenario to save
     * @param filepath Path to the output file
     * @throws std::runtime_error if file cannot be written
     */
    void save_file(const schema::ScenarioDefinition& scenario, const std::string& filepath) const;
    
private:
    // Internal helpers for YAML manipulation (implementation depends on yaml-cpp or similar)
    std::string format_value(const std::variant<int64_t, double, std::string, bool, std::vector<std::string>>& value) const;
    std::variant<int64_t, double, std::string, bool> parse_scalar_value(const std::string& str, schema::VariableType type) const;
};

/**
 * @brief Checkpoint serialization for simulation state
 */
class CheckpointSerializer {
public:
    CheckpointSerializer() = default;
    ~CheckpointSerializer() = default;
    
    /**
     * @brief Serialize simulation checkpoint to binary/string format
     * @param scenario_id The scenario identifier
     * @param timestamp Simulation time at checkpoint
     * @param state_data Serialized state data
     * @return Checkpoint blob
     */
    std::string serialize_checkpoint(const std::string& scenario_id, 
                                     double timestamp,
                                     const std::string& state_data) const;
    
    /**
     * @brief Deserialize simulation checkpoint
     * @param checkpoint_blob The checkpoint data
     * @return Tuple of (scenario_id, timestamp, state_data)
     * @throws std::runtime_error if deserialization fails
     */
    std::tuple<std::string, double, std::string> deserialize_checkpoint(const std::string& checkpoint_blob) const;
    
    /**
     * @brief Save checkpoint to file
     * @param checkpoint_blob The checkpoint data
     * @param filepath Output file path
     * @throws std::runtime_error if write fails
     */
    void save_checkpoint(const std::string& checkpoint_blob, const std::string& filepath) const;
    
    /**
     * @brief Load checkpoint from file
     * @param filepath Input file path
     * @return Checkpoint blob
     * @throws std::runtime_error if read fails
     */
    std::string load_checkpoint(const std::string& filepath) const;
};

/**
 * @brief Report serializer for validation/compilation/runtime reports
 */
class ReportSerializer {
public:
    ReportSerializer() = default;
    ~ReportSerializer() = default;
    
    /**
     * @brief Serialize validation report to JSON/YAML
     * @param report The validation report
     * @param format Output format ("json" or "yaml")
     * @return Serialized report
     */
    std::string serialize_report(const schema::ScenarioReport& report, 
                                 const std::string& format = "json") const;
    
    /**
     * @brief Generate human-readable summary from report
     * @param report The report to summarize
     * @return Human-readable text summary
     */
    std::string generate_summary(const schema::ScenarioReport& report) const;
    
    /**
     * @brief Save report to file (machine-readable + human-readable)
     * @param report The report to save
     * @param base_filepath Base path (will add extensions as needed)
     * @throws std::runtime_error if write fails
     */
    void save_report(const schema::ScenarioReport& report, const std::string& base_filepath) const;
};

} // namespace noisiax::serialization
