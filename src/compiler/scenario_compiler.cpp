#include "noisiax/compiler/scenario_compiler.hpp"
#include <algorithm>
#include <stdexcept>
#include <type_traits>

namespace noisiax::compiler {
namespace {

std::optional<CompiledAgentLayer> compile_agent_layer(const schema::ScenarioDefinition& scenario) {
    if (!scenario.agent_layer.has_value()) {
        return std::nullopt;
    }

    const auto& layer = *scenario.agent_layer;
    CompiledAgentLayer compiled;
    compiled.world = layer.world;
    compiled.locations = layer.locations;
    compiled.items = layer.items;
    compiled.shops = layer.shops;
    compiled.agents = layer.agents;
    compiled.policies = layer.policies;

    for (std::size_t i = 0; i < compiled.locations.size(); ++i) {
        compiled.location_index.emplace(compiled.locations[i].location_id, i);
    }
    for (std::size_t i = 0; i < compiled.items.size(); ++i) {
        compiled.item_index.emplace(compiled.items[i].item_id, i);
    }
    for (std::size_t i = 0; i < compiled.shops.size(); ++i) {
        compiled.shop_index.emplace(compiled.shops[i].shop_id, i);
    }
    for (std::size_t i = 0; i < compiled.agents.size(); ++i) {
        compiled.agent_index.emplace(compiled.agents[i].agent_id, i);
    }
    for (std::size_t i = 0; i < compiled.policies.size(); ++i) {
        compiled.policy_index.emplace(compiled.policies[i].policy_id, i);
    }

    compiled.shops_by_item_index.assign(compiled.items.size(), {});
    for (std::size_t shop_index = 0; shop_index < compiled.shops.size(); ++shop_index) {
        const auto& shop = compiled.shops[shop_index];
        for (const auto& inventory : shop.inventory) {
            const auto item_it = compiled.item_index.find(inventory.item_id);
            if (item_it == compiled.item_index.end()) {
                throw std::runtime_error(
                    "Invalid agent_layer inventory item reference: " + inventory.item_id);
            }
            auto& slots = compiled.shops_by_item_index[item_it->second];
            if (std::find(slots.begin(), slots.end(), shop_index) == slots.end()) {
                slots.push_back(shop_index);
            }
        }
    }

    return compiled;
}

}  // namespace

CompiledScenario ScenarioCompiler::compile(const schema::ScenarioDefinition& scenario) {
    CompiledScenario compiled;
    compiled.scenario_id = scenario.scenario_id;
    compiled.master_seed = scenario.master_seed;
    compiled.dependency_edges = scenario.dependency_edges;
    
    // Register default propagation functions
    register_default_functions();
    
    // Build parameter handles
    compiled.parameter_handles = build_parameter_handles(scenario);
    for (const auto& variable : scenario.variables) {
        if (variable.type == schema::VariableType::LIST) {
            continue;
        }

        std::visit([&](const auto& value) {
            using ValueType = std::decay_t<decltype(value)>;
            if constexpr (std::is_same_v<ValueType, int64_t> ||
                          std::is_same_v<ValueType, double> ||
                          std::is_same_v<ValueType, std::string> ||
                          std::is_same_v<ValueType, bool>) {
                compiled.initial_values[variable.variable_id] = value;
            }
        }, variable.default_value);
    }
    
    // Build adjacency lists
    compiled.adjacency_lists = build_adjacency_lists(scenario, compiled.parameter_handles);
    
    // Build event queue
    compiled.event_queue = build_event_queue(scenario);
    
    // Build constraint programs
    compiled.constraint_programs = build_constraint_programs(scenario, compiled.parameter_handles);
    compiled.agent_layer = compile_agent_layer(scenario);
    
    // Copy registered functions
    compiled.propagation_functions = registered_functions_;
    
    // Statistics
    compiled.total_variables = scenario.variables.size();
    compiled.total_dependencies = scenario.dependency_edges.size();
    compiled.total_constraints = scenario.constraints.size();
    compiled.total_events = scenario.events.size();
    
    return compiled;
}

void ScenarioCompiler::register_propagation_function(
    const std::string& function_id,
    std::function<void(double&, const double&, double)> func) {
    registered_functions_[function_id] = std::move(func);
}

std::vector<std::string> ScenarioCompiler::get_registered_functions() const {
    std::vector<std::string> funcs;
    for (const auto& [id, _] : registered_functions_) {
        funcs.push_back(id);
    }
    return funcs;
}

std::map<std::string, ParameterHandle> ScenarioCompiler::build_parameter_handles(
    const schema::ScenarioDefinition& scenario) {
    
    std::map<std::string, ParameterHandle> handles;
    std::size_t int_offset = 0;
    std::size_t float_offset = 0;
    std::size_t string_offset = 0;
    std::size_t bool_offset = 0;
    
    for (const auto& var : scenario.variables) {
        ParameterHandle handle;
        handle.variable_id = var.variable_id;
        handle.type = var.type;
        handle.is_stale = false;
        
        switch (var.type) {
            case schema::VariableType::INTEGER:
                handle.buffer_offset = int_offset++;
                break;
            case schema::VariableType::FLOAT:
                handle.buffer_offset = float_offset++;
                break;
            case schema::VariableType::STRING:
                handle.buffer_offset = string_offset++;
                break;
            case schema::VariableType::BOOLEAN:
                handle.buffer_offset = bool_offset++;
                break;
            default:
                throw std::runtime_error("Unknown variable type for: " + var.variable_id);
        }
        
        handles[var.variable_id] = handle;
    }
    
    return handles;
}

std::map<std::string, std::vector<AdjacencyEntry>> ScenarioCompiler::build_adjacency_lists(
    const schema::ScenarioDefinition& scenario,
    const std::map<std::string, ParameterHandle>& handles) {
    
    std::map<std::string, std::vector<AdjacencyEntry>> adjacency;
    
    for (const auto& edge : scenario.dependency_edges) {
        if (!registered_functions_.contains(edge.propagation_function_id)) {
            throw std::runtime_error("Unknown propagation function: " + edge.propagation_function_id);
        }

        AdjacencyEntry entry;
        entry.target_variable = edge.target_variable;
        entry.propagation_function_id = edge.propagation_function_id;
        entry.weight = edge.weight;
        
        auto it = handles.find(edge.target_variable);
        if (it != handles.end()) {
            entry.target_buffer_offset = it->second.buffer_offset;
        } else {
            entry.target_buffer_offset = 0;
        }
        
        adjacency[edge.source_variable].push_back(entry);
    }
    
    return adjacency;
}

std::vector<ScheduledEvent> ScenarioCompiler::build_event_queue(
    const schema::ScenarioDefinition& scenario) {
    
    std::vector<ScheduledEvent> queue;
    
    for (const auto& event : scenario.events) {
        ScheduledEvent scheduled;
        scheduled.timestamp = event.timestamp;
        scheduled.priority = event.priority;
        scheduled.event_handle = event.event_id;
        scheduled.descriptor = event;
        scheduled.triggered = false;
        
        queue.push_back(scheduled);
    }
    
    std::sort(queue.begin(), queue.end());
    
    return queue;
}

std::vector<ConstraintProgram> ScenarioCompiler::build_constraint_programs(
    const schema::ScenarioDefinition& scenario,
    const std::map<std::string, ParameterHandle>& handles) {
    
    std::vector<ConstraintProgram> programs;
    
    for (const auto& constraint : scenario.constraints) {
        ConstraintProgram program;
        program.constraint_id = constraint.constraint_id;
        program.enforcement_level = constraint.enforcement_level;
        program.compiled_expression = constraint.constraint_expression;
        program.error_message = constraint.error_message;
        program.variable_ids = constraint.affected_variables;
        
        for (const auto& var_id : constraint.affected_variables) {
            auto it = handles.find(var_id);
            if (it != handles.end()) {
                program.variable_offsets.push_back(it->second.buffer_offset);
            }
        }
        
        programs.push_back(program);
    }
    
    return programs;
}

void ScenarioCompiler::register_default_functions() {
    register_propagation_function("linear_scale", [](double& target, const double& source, double weight) {
        target = source * weight;
    });
    
    register_propagation_function("apply_discount", [](double& target, const double& source, double weight) {
        target = target * (1.0 + source * weight);
    });
    
    register_propagation_function("additive", [](double& target, const double& source, double weight) {
        target += source * weight;
    });
    
    register_propagation_function("max_propagate", [](double& target, const double& source, double weight) {
        target = std::max(target, source * weight);
    });
    
    register_propagation_function("min_propagate", [](double& target, const double& source, double weight) {
        target = std::min(target, source * weight);
    });
}

} // namespace noisiax::compiler
