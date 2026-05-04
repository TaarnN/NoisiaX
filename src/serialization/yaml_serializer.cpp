#include "noisiax/serialization/yaml_serializer.hpp"
#include <yaml-cpp/yaml.h>
#include <sstream>
#include <stdexcept>
#include <fstream>

namespace noisiax::serialization {

// Helper to convert VariableType enum to string
static std::string variable_type_to_string(schema::VariableType type) {
    switch (type) {
        case schema::VariableType::INTEGER: return "INTEGER";
        case schema::VariableType::FLOAT: return "FLOAT";
        case schema::VariableType::STRING: return "STRING";
        case schema::VariableType::BOOLEAN: return "BOOLEAN";
        case schema::VariableType::ENUM: return "ENUM";
        case schema::VariableType::LIST: return "LIST";
        default: return "UNKNOWN";
    }
}

// Helper to convert string to VariableType enum
static schema::VariableType string_to_variable_type(const std::string& str) {
    if (str == "INTEGER") return schema::VariableType::INTEGER;
    if (str == "FLOAT") return schema::VariableType::FLOAT;
    if (str == "STRING") return schema::VariableType::STRING;
    if (str == "BOOLEAN") return schema::VariableType::BOOLEAN;
    if (str == "ENUM") return schema::VariableType::ENUM;
    if (str == "LIST") return schema::VariableType::LIST;
    throw std::runtime_error("Unknown variable type: " + str);
}

// Helper to convert ValidationLevel enum to string
static std::string validation_level_to_string(schema::ValidationLevel level) {
    switch (level) {
        case schema::ValidationLevel::REJECT: return "REJECT";
        case schema::ValidationLevel::WARN: return "WARN";
        case schema::ValidationLevel::AUTO_CORRECT: return "AUTO_CORRECT";
        default: return "UNKNOWN";
    }
}

// Helper to convert string to ValidationLevel enum
static schema::ValidationLevel string_to_validation_level(const std::string& str) {
    if (str == "REJECT") return schema::ValidationLevel::REJECT;
    if (str == "WARN") return schema::ValidationLevel::WARN;
    if (str == "AUTO_CORRECT") return schema::ValidationLevel::AUTO_CORRECT;
    throw std::runtime_error("Unknown validation level: " + str);
}

std::string YamlSerializer::format_value(
    const std::variant<int64_t, double, std::string, bool, std::vector<std::string>>& value) const {
    return std::visit([](auto&& arg) -> std::string {
        using T = std::decay_t<decltype(arg)>;
        if constexpr (std::is_same_v<T, int64_t>) {
            return std::to_string(arg);
        } else if constexpr (std::is_same_v<T, double>) {
            return std::to_string(arg);
        } else if constexpr (std::is_same_v<T, std::string>) {
            return arg;
        } else if constexpr (std::is_same_v<T, bool>) {
            return arg ? "true" : "false";
        } else if constexpr (std::is_same_v<T, std::vector<std::string>>) {
            std::ostringstream oss;
            oss << "[";
            for (size_t i = 0; i < arg.size(); ++i) {
                if (i > 0) oss << ", ";
                oss << "\"" << arg[i] << "\"";
            }
            oss << "]";
            return oss.str();
        }
        return "";
    }, value);
}

std::variant<int64_t, double, std::string, bool, std::vector<std::string>> YamlSerializer::parse_scalar_value(
    const std::string& str, schema::VariableType type) const {
    switch (type) {
        case schema::VariableType::INTEGER: {
            return static_cast<int64_t>(std::stoll(str));
        }
        case schema::VariableType::FLOAT: {
            return std::stod(str);
        }
        case schema::VariableType::STRING: {
            return str;
        }
        case schema::VariableType::BOOLEAN: {
            return (str == "true" || str == "1" || str == "yes");
        }
        default:
            return str;
    }
}

std::string YamlSerializer::serialize(const schema::ScenarioDefinition& scenario) const {
    YAML::Emitter out;
    out.SetIndent(2);

    out << YAML::BeginMap;
    
    // Required fields
    out << YAML::Key << "scenario_id" << YAML::Value << scenario.scenario_id;
    out << YAML::Key << "schema_version" << YAML::Value << scenario.schema_version;
    out << YAML::Key << "master_seed" << YAML::Value << static_cast<int64_t>(scenario.master_seed);
    
    // Goal statement
    out << YAML::Key << "goal_statement" << YAML::Value << YAML::Literal << scenario.goal_statement;
    
    // Assumptions
    out << YAML::Key << "assumptions" << YAML::Value << YAML::BeginSeq;
    for (const auto& assumption : scenario.assumptions) {
        out << YAML::BeginMap;
        out << YAML::Key << "assumption_id" << YAML::Value << assumption.assumption_id;
        out << YAML::Key << "category" << YAML::Value << assumption.category;
        out << YAML::Key << "description" << YAML::Value << assumption.description;
        out << YAML::Key << "rationale" << YAML::Value << assumption.rationale;
        if (assumption.source) {
            out << YAML::Key << "source" << YAML::Value << *assumption.source;
        }
        out << YAML::Key << "confidence_level" << YAML::Value << validation_level_to_string(assumption.confidence_level);
        out << YAML::EndMap;
    }
    out << YAML::EndSeq;
    
    // Entities
    out << YAML::Key << "entities" << YAML::Value << YAML::BeginSeq;
    for (const auto& entity : scenario.entities) {
        out << YAML::BeginMap;
        out << YAML::Key << "entity_id" << YAML::Value << entity.entity_id;
        out << YAML::Key << "entity_type" << YAML::Value << entity.entity_type;
        out << YAML::Key << "description" << YAML::Value << entity.description;
        out << YAML::Key << "attributes" << YAML::Value << YAML::BeginMap;
        for (const auto& [key, value] : entity.attributes) {
            out << YAML::Key << key << YAML::Value << value;
        }
        out << YAML::EndMap;
        out << YAML::EndMap;
    }
    out << YAML::EndSeq;
    
    // Variables
    out << YAML::Key << "variables" << YAML::Value << YAML::BeginSeq;
    for (const auto& var : scenario.variables) {
        out << YAML::BeginMap;
        out << YAML::Key << "variable_id" << YAML::Value << var.variable_id;
        if (!var.entity_ref.empty()) {
            out << YAML::Key << "entity_ref" << YAML::Value << var.entity_ref;
        }
        out << YAML::Key << "type" << YAML::Value << variable_type_to_string(var.type);
        
        // Default value
        out << YAML::Key << "default_value" << YAML::Value;
        std::visit([&out](auto&& arg) {
            using T = std::decay_t<decltype(arg)>;
            if constexpr (std::is_same_v<T, int64_t>) {
                out << arg;
            } else if constexpr (std::is_same_v<T, double>) {
                out << arg;
            } else if constexpr (std::is_same_v<T, std::string>) {
                out << arg;
            } else if constexpr (std::is_same_v<T, bool>) {
                out << arg;
            } else if constexpr (std::is_same_v<T, std::vector<std::string>>) {
                out << YAML::BeginSeq;
                for (const auto& item : arg) {
                    out << item;
                }
                out << YAML::EndSeq;
            }
        }, var.default_value);
        
        // Min/max values
        if (var.min_value.has_value()) {
            out << YAML::Key << "min_value" << YAML::Value;
            std::visit([&out](auto&& arg) {
                using T = std::decay_t<decltype(arg)>;
                if constexpr (std::is_same_v<T, int64_t>) {
                    out << arg;
                } else if constexpr (std::is_same_v<T, double>) {
                    out << arg;
                } else if constexpr (std::is_same_v<T, std::string>) {
                    out << arg;
                } else if constexpr (std::is_same_v<T, bool>) {
                    out << arg;
                }
            }, *var.min_value);
        }
        
        if (var.max_value.has_value()) {
            out << YAML::Key << "max_value" << YAML::Value;
            std::visit([&out](auto&& arg) {
                using T = std::decay_t<decltype(arg)>;
                if constexpr (std::is_same_v<T, int64_t>) {
                    out << arg;
                } else if constexpr (std::is_same_v<T, double>) {
                    out << arg;
                } else if constexpr (std::is_same_v<T, std::string>) {
                    out << arg;
                } else if constexpr (std::is_same_v<T, bool>) {
                    out << arg;
                }
            }, *var.max_value);
        }
        
        // Enum values
        if (!var.enum_values.empty()) {
            out << YAML::Key << "enum_values" << YAML::Value << YAML::BeginSeq;
            for (const auto& val : var.enum_values) {
                out << val;
            }
            out << YAML::EndSeq;
        }
        
        out << YAML::Key << "description" << YAML::Value << var.description;
        out << YAML::EndMap;
    }
    out << YAML::EndSeq;
    
    // Dependency edges
    out << YAML::Key << "dependency_edges" << YAML::Value << YAML::BeginSeq;
    for (const auto& edge : scenario.dependency_edges) {
        out << YAML::BeginMap;
        out << YAML::Key << "edge_id" << YAML::Value << edge.edge_id;
        out << YAML::Key << "source_variable" << YAML::Value << edge.source_variable;
        out << YAML::Key << "target_variable" << YAML::Value << edge.target_variable;
        out << YAML::Key << "propagation_function_id" << YAML::Value << edge.propagation_function_id;
        out << YAML::Key << "weight" << YAML::Value << edge.weight;
        
        if (!edge.metadata.empty()) {
            out << YAML::Key << "metadata" << YAML::Value << YAML::BeginMap;
            for (const auto& [key, value] : edge.metadata) {
                out << YAML::Key << key << YAML::Value << value;
            }
            out << YAML::EndMap;
        }
        
        out << YAML::EndMap;
    }
    out << YAML::EndSeq;
    
    // Constraints
    out << YAML::Key << "constraints" << YAML::Value << YAML::BeginSeq;
    for (const auto& constraint : scenario.constraints) {
        out << YAML::BeginMap;
        out << YAML::Key << "constraint_id" << YAML::Value << constraint.constraint_id;
        out << YAML::Key << "affected_variables" << YAML::Value << YAML::BeginSeq;
        for (const auto& var : constraint.affected_variables) {
            out << var;
        }
        out << YAML::EndSeq;
        out << YAML::Key << "constraint_expression" << YAML::Value << constraint.constraint_expression;
        out << YAML::Key << "enforcement_level" << YAML::Value << validation_level_to_string(constraint.enforcement_level);
        out << YAML::Key << "error_message" << YAML::Value << constraint.error_message;
        if (constraint.correction_hint) {
            out << YAML::Key << "correction_hint" << YAML::Value << *constraint.correction_hint;
        }
        out << YAML::EndMap;
    }
    out << YAML::EndSeq;
    
    // Events
    out << YAML::Key << "events" << YAML::Value << YAML::BeginSeq;
    for (const auto& event : scenario.events) {
        out << YAML::BeginMap;
        out << YAML::Key << "event_id" << YAML::Value << event.event_id;
        out << YAML::Key << "event_type" << YAML::Value << event.event_type;
        out << YAML::Key << "timestamp" << YAML::Value << event.timestamp;
        out << YAML::Key << "priority" << YAML::Value << event.priority;
        out << YAML::Key << "trigger_condition" << YAML::Value << event.trigger_condition;
        
        if (!event.event_payload.empty()) {
            out << YAML::Key << "event_payload" << YAML::Value << YAML::BeginMap;
            for (const auto& [key, value] : event.event_payload) {
                out << YAML::Key << key << YAML::Value << value;
            }
            out << YAML::EndMap;
        }
        
        out << YAML::Key << "description" << YAML::Value << event.description;
        out << YAML::EndMap;
    }
    out << YAML::EndSeq;
    
    // Evaluation criteria
    out << YAML::Key << "evaluation_criteria" << YAML::Value << YAML::BeginSeq;
    for (const auto& criterion : scenario.evaluation_criteria) {
        out << YAML::BeginMap;
        out << YAML::Key << "criterion_id" << YAML::Value << criterion.criterion_id;
        out << YAML::Key << "metric_name" << YAML::Value << criterion.metric_name;
        out << YAML::Key << "aggregation_method" << YAML::Value << criterion.aggregation_method;
        out << YAML::Key << "input_variables" << YAML::Value << YAML::BeginSeq;
        for (const auto& var : criterion.input_variables) {
            out << var;
        }
        out << YAML::EndSeq;
        
        if (criterion.target_value.has_value()) {
            out << YAML::Key << "target_value" << YAML::Value << *criterion.target_value;
        }
        if (criterion.threshold_min.has_value()) {
            out << YAML::Key << "threshold_min" << YAML::Value << *criterion.threshold_min;
        }
        if (criterion.threshold_max.has_value()) {
            out << YAML::Key << "threshold_max" << YAML::Value << *criterion.threshold_max;
        }
        
        out << YAML::Key << "description" << YAML::Value << criterion.description;
        out << YAML::EndMap;
    }
    out << YAML::EndSeq;
    
    // Metadata (optional)
    if (scenario.metadata.has_value()) {
        out << YAML::Key << "metadata" << YAML::Value << YAML::BeginMap;
        out << YAML::Key << "author" << YAML::Value << scenario.metadata->author;
        out << YAML::Key << "created_date" << YAML::Value << scenario.metadata->created_date;
        out << YAML::Key << "modified_date" << YAML::Value << scenario.metadata->modified_date;
        out << YAML::Key << "version" << YAML::Value << scenario.metadata->version;
        
        if (!scenario.metadata->custom_fields.empty()) {
            out << YAML::Key << "custom_fields" << YAML::Value << YAML::BeginMap;
            for (const auto& [key, value] : scenario.metadata->custom_fields) {
                out << YAML::Key << key << YAML::Value << value;
            }
            out << YAML::EndMap;
        }
        
        out << YAML::EndMap;
    }
    
    out << YAML::EndMap;
    
    return out.c_str();
}

schema::ScenarioDefinition YamlSerializer::deserialize(const std::string& yaml_content) const {
    YAML::Node root = YAML::Load(yaml_content);
    
    schema::ScenarioDefinition scenario;
    
    // Required fields
    if (!root["scenario_id"]) {
        throw std::runtime_error("Missing required field: scenario_id");
    }
    scenario.scenario_id = root["scenario_id"].as<std::string>();
    
    if (!root["schema_version"]) {
        throw std::runtime_error("Missing required field: schema_version");
    }
    scenario.schema_version = root["schema_version"].as<std::string>();
    
    if (!root["master_seed"]) {
        throw std::runtime_error("Missing required field: master_seed");
    }
    scenario.master_seed = root["master_seed"].as<uint64_t>();
    
    // Goal statement
    if (!root["goal_statement"]) {
        throw std::runtime_error("Missing required field: goal_statement");
    }
    scenario.goal_statement = root["goal_statement"].as<std::string>();
    
    // Assumptions
    if (root["assumptions"]) {
        for (const auto& node : root["assumptions"]) {
            schema::Assumption assumption;
            assumption.assumption_id = node["assumption_id"].as<std::string>();
            assumption.category = node["category"].as<std::string>();
            assumption.description = node["description"].as<std::string>();
            assumption.rationale = node["rationale"].as<std::string>();
            
            if (node["source"]) {
                assumption.source = node["source"].as<std::string>();
            }
            
            assumption.confidence_level = string_to_validation_level(
                node["confidence_level"].as<std::string>());
            
            scenario.assumptions.push_back(assumption);
        }
    }
    
    // Entities
    if (root["entities"]) {
        for (const auto& node : root["entities"]) {
            schema::EntityDescriptor entity;
            entity.entity_id = node["entity_id"].as<std::string>();
            entity.entity_type = node["entity_type"].as<std::string>();
            entity.description = node["description"].as<std::string>();
            
            if (node["attributes"]) {
                for (const auto& attr : node["attributes"]) {
                    entity.attributes[attr.first.as<std::string>()] = attr.second.as<std::string>();
                }
            }
            
            scenario.entities.push_back(entity);
        }
    }
    
    // Variables
    if (root["variables"]) {
        for (const auto& node : root["variables"]) {
            schema::VariableDescriptor var;
            var.variable_id = node["variable_id"].as<std::string>();
            
            if (node["entity_ref"]) {
                var.entity_ref = node["entity_ref"].as<std::string>();
            }
            
            var.type = string_to_variable_type(node["type"].as<std::string>());
            
            // Parse default_value
            const auto& default_node = node["default_value"];
            if (default_node.IsScalar()) {
                std::string scalar_val = default_node.as<std::string>();
                auto parsed = parse_scalar_value(scalar_val, var.type);
                std::visit([var.default_value = parse_scalar_value(scalar_val, var.type);var](auto&& val) { var.default_value = std::forward<decltype(val)>(val); }, parsed);
            } else if (default_node.IsSequence()) {
                std::vector<std::string> values;
                for (const auto& item : default_node) {
                    values.push_back(item.as<std::string>());
                }
                var.default_value = values;
            }
            
            // Min/max values
            if (node["min_value"]) {
                var.min_value = parse_scalar_value(node["min_value"].as<std::string>(), var.type);
            }
            if (node["max_value"]) {
                var.max_value = parse_scalar_value(node["max_value"].as<std::string>(), var.type);
            }
            
            // Enum values
            if (node["enum_values"]) {
                for (const auto& item : node["enum_values"]) {
                    var.enum_values.push_back(item.as<std::string>());
                }
            }
            
            var.description = node["description"].as<std::string>();
            
            scenario.variables.push_back(var);
        }
    }
    
    // Dependency edges
    if (root["dependency_edges"]) {
        for (const auto& node : root["dependency_edges"]) {
            schema::DependencyEdge edge;
            edge.edge_id = node["edge_id"].as<std::string>();
            edge.source_variable = node["source_variable"].as<std::string>();
            edge.target_variable = node["target_variable"].as<std::string>();
            edge.propagation_function_id = node["propagation_function_id"].as<std::string>();
            edge.weight = node["weight"].as<double>();
            
            if (node["metadata"]) {
                for (const auto& meta : node["metadata"]) {
                    edge.metadata[meta.first.as<std::string>()] = meta.second.as<std::string>();
                }
            }
            
            scenario.dependency_edges.push_back(edge);
        }
    }
    
    // Constraints
    if (root["constraints"]) {
        for (const auto& node : root["constraints"]) {
            schema::ConstraintRule constraint;
            constraint.constraint_id = node["constraint_id"].as<std::string>();
            
            if (node["affected_variables"]) {
                for (const auto& item : node["affected_variables"]) {
                    constraint.affected_variables.push_back(item.as<std::string>());
                }
            }
            
            constraint.constraint_expression = node["constraint_expression"].as<std::string>();
            constraint.enforcement_level = string_to_validation_level(
                node["enforcement_level"].as<std::string>());
            constraint.error_message = node["error_message"].as<std::string>();
            
            if (node["correction_hint"]) {
                constraint.correction_hint = node["correction_hint"].as<std::string>();
            }
            
            scenario.constraints.push_back(constraint);
        }
    }
    
    // Events
    if (root["events"]) {
        for (const auto& node : root["events"]) {
            schema::EventDescriptor event;
            event.event_id = node["event_id"].as<std::string>();
            event.event_type = node["event_type"].as<std::string>();
            event.timestamp = node["timestamp"].as<double>();
            event.priority = node["priority"].as<int>();
            event.trigger_condition = node["trigger_condition"].as<std::string>();
            
            if (node["event_payload"]) {
                for (const auto& payload : node["event_payload"]) {
                    event.event_payload[payload.first.as<std::string>()] = payload.second.as<std::string>();
                }
            }
            
            event.description = node["description"].as<std::string>();
            
            scenario.events.push_back(event);
        }
    }
    
    // Evaluation criteria
    if (root["evaluation_criteria"]) {
        for (const auto& node : root["evaluation_criteria"]) {
            schema::EvaluationCriterion criterion;
            criterion.criterion_id = node["criterion_id"].as<std::string>();
            criterion.metric_name = node["metric_name"].as<std::string>();
            criterion.aggregation_method = node["aggregation_method"].as<std::string>();
            
            if (node["input_variables"]) {
                for (const auto& item : node["input_variables"]) {
                    criterion.input_variables.push_back(item.as<std::string>());
                }
            }
            
            if (node["target_value"]) {
                criterion.target_value = node["target_value"].as<double>();
            }
            if (node["threshold_min"]) {
                criterion.threshold_min = node["threshold_min"].as<double>();
            }
            if (node["threshold_max"]) {
                criterion.threshold_max = node["threshold_max"].as<double>();
            }
            
            criterion.description = node["description"].as<std::string>();
            
            scenario.evaluation_criteria.push_back(criterion);
        }
    }
    
    // Metadata (optional)
    if (root["metadata"]) {
        schema::ScenarioMetadata metadata;
        metadata.author = root["metadata"]["author"].as<std::string>();
        metadata.created_date = root["metadata"]["created_date"].as<std::string>();
        metadata.modified_date = root["metadata"]["modified_date"].as<std::string>();
        metadata.version = root["metadata"]["version"].as<std::string>();
        
        if (root["metadata"]["custom_fields"]) {
            for (const auto& field : root["metadata"]["custom_fields"]) {
                metadata.custom_fields[field.first.as<std::string>()] = field.second.as<std::string>();
            }
        }
        
        scenario.metadata = metadata;
    }
    
    return scenario;
}

schema::ScenarioDefinition YamlSerializer::load_file(const std::string& filepath) const {
    std::ifstream file(filepath);
    if (!file.is_open()) {
        throw std::runtime_error("Cannot open YAML file: " + filepath);
    }
    
    std::stringstream buffer;
    buffer << file.rdbuf();
    std::string content = buffer.str();
    
    return deserialize(content);
}

void YamlSerializer::save_file(const schema::ScenarioDefinition& scenario, const std::string& filepath) const {
    std::ofstream file(filepath);
    if (!file.is_open()) {
        throw std::runtime_error("Cannot write to YAML file: " + filepath);
    }
    
    file << serialize(scenario);
    file.close();
}

// CheckpointSerializer implementation

std::string CheckpointSerializer::serialize_checkpoint(
    const std::string& scenario_id,
    double timestamp,
    const std::string& state_data) const {
    
    YAML::Emitter out;
    out << YAML::BeginMap;
    out << YAML::Key << "scenario_id" << YAML::Value << scenario_id;
    out << YAML::Key << "timestamp" << YAML::Value << timestamp;
    out << YAML::Key << "state_data" << YAML::Value << state_data;
    out << YAML::EndMap;
    
    return out.c_str();
}

std::tuple<std::string, double, std::string> CheckpointSerializer::deserialize_checkpoint(
    const std::string& checkpoint_blob) const {
    
    YAML::Node root = YAML::Load(checkpoint_blob);
    
    std::string scenario_id = root["scenario_id"].as<std::string>();
    double timestamp = root["timestamp"].as<double>();
    std::string state_data = root["state_data"].as<std::string>();
    
    return std::make_tuple(scenario_id, timestamp, state_data);
}

void CheckpointSerializer::save_checkpoint(
    const std::string& checkpoint_blob,
    const std::string& filepath) const {
    
    std::ofstream file(filepath, std::ios::binary);
    if (!file.is_open()) {
        throw std::runtime_error("Cannot write checkpoint file: " + filepath);
    }
    
    file << checkpoint_blob;
    file.close();
}

std::string CheckpointSerializer::load_checkpoint(const std::string& filepath) const {
    std::ifstream file(filepath, std::ios::binary);
    if (!file.is_open()) {
        throw std::runtime_error("Cannot read checkpoint file: " + filepath);
    }
    
    std::stringstream buffer;
    buffer << file.rdbuf();
    return buffer.str();
}

// ReportSerializer implementation

std::string ReportSerializer::serialize_report(
    const schema::ScenarioReport& report,
    const std::string& format) const {
    
    if (format == "json") {
        YAML::Emitter out;
        out << YAML::BeginMap;
        out << YAML::Key << "scenario_id" << YAML::Value << report.scenario_id;
        out << YAML::Key << "report_type" << YAML::Value << report.report_type;
        out << YAML::Key << "success" << YAML::Value << report.success;
        
        out << YAML::Key << "errors" << YAML::Value << YAML::BeginSeq;
        for (const auto& error : report.errors) {
            out << error;
        }
        out << YAML::EndSeq;
        
        out << YAML::Key << "warnings" << YAML::Value << YAML::BeginSeq;
        for (const auto& warning : report.warnings) {
            out << warning;
        }
        out << YAML::EndSeq;
        
        out << YAML::Key << "info_messages" << YAML::Value << YAML::BeginSeq;
        for (const auto& info : report.info_messages) {
            out << info;
        }
        out << YAML::EndSeq;
        
        out << YAML::Key << "statistics" << YAML::Value << YAML::BeginMap;
        for (const auto& [key, value] : report.statistics) {
            out << YAML::Key << key << YAML::Value << value;
        }
        out << YAML::EndMap;
        
        if (report.checkpoint_path) {
            out << YAML::Key << "checkpoint_path" << YAML::Value << *report.checkpoint_path;
        }
        
        out << YAML::EndMap;
        return out.c_str();
    } else {
        // Default to YAML format
        YAML::Emitter out;
        out << YAML::BeginMap;
        out << YAML::Key << "scenario_id" << YAML::Value << report.scenario_id;
        out << YAML::Key << "report_type" << YAML::Value << report.report_type;
        out << YAML::Key << "success" << YAML::Value << report.success;
        out << YAML::Key << "errors" << YAML::Value << report.errors;
        out << YAML::Key << "warnings" << YAML::Value << report.warnings;
        out << YAML::Key << "info_messages" << YAML::Value << report.info_messages;
        out << YAML::Key << "statistics" << YAML::Value << report.statistics;
        if (report.checkpoint_path) {
            out << YAML::Key << "checkpoint_path" << YAML::Value << *report.checkpoint_path;
        }
        out << YAML::EndMap;
        return out.c_str();
    }
}

std::string ReportSerializer::generate_summary(const schema::ScenarioReport& report) const {
    std::ostringstream oss;
    
    oss << "=== NoisiaX Scenario Report ===\n\n";
    oss << "Scenario ID: " << report.scenario_id << "\n";
    oss << "Report Type: " << report.report_type << "\n";
    oss << "Status: " << (report.success ? "SUCCESS" : "FAILED") << "\n\n";
    
    if (!report.errors.empty()) {
        oss << "ERRORS (" << report.errors.size() << "):\n";
        for (const auto& error : report.errors) {
            oss << "  - " << error << "\n";
        }
        oss << "\n";
    }
    
    if (!report.warnings.empty()) {
        oss << "WARNINGS (" << report.warnings.size() << "):\n";
        for (const auto& warning : report.warnings) {
            oss << "  - " << warning << "\n";
        }
        oss << "\n";
    }
    
    if (!report.info_messages.empty()) {
        oss << "INFO (" << report.info_messages.size() << "):\n";
        for (const auto& info : report.info_messages) {
            oss << "  - " << info << "\n";
        }
        oss << "\n";
    }
    
    if (!report.statistics.empty()) {
        oss << "STATISTICS:\n";
        for (const auto& [key, value] : report.statistics) {
            oss << "  " << key << ": " << value << "\n";
        }
        oss << "\n";
    }
    
    if (report.checkpoint_path) {
        oss << "Checkpoint: " << *report.checkpoint_path << "\n";
    }
    
    return oss.str();
}

void ReportSerializer::save_report(
    const schema::ScenarioReport& report,
    const std::string& base_filepath) const {
    
    // Save machine-readable JSON
    std::string json_path = base_filepath + ".json";
    std::ofstream json_file(json_path);
    if (json_file.is_open()) {
        json_file << serialize_report(report, "json");
        json_file.close();
    }
    
    // Save human-readable summary
    std::string txt_path = base_filepath + ".txt";
    std::ofstream txt_file(txt_path);
    if (txt_file.is_open()) {
        txt_file << generate_summary(report);
        txt_file.close();
    }
}

} // namespace noisiax::serialization
