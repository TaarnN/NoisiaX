#include "noisiax/compiler/scenario_compiler.hpp"
#include "noisiax/extensions/default_registry.hpp"
#include <algorithm>
#include <stdexcept>
#include <string_view>
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

CompiledTypedSystemKind parse_typed_system_kind(const std::string& kind) {
    if (kind == "per_entity") return CompiledTypedSystemKind::PER_ENTITY;
    if (kind == "pair") return CompiledTypedSystemKind::PAIR;
    if (kind == "per_relation") return CompiledTypedSystemKind::PER_RELATION;
    throw std::runtime_error("Unknown typed_layer system kind: " + kind);
}

CompiledTypedEntityRole parse_typed_entity_role(const std::string& role) {
    if (role == "self") return CompiledTypedEntityRole::SELF;
    if (role == "other") return CompiledTypedEntityRole::OTHER;
    throw std::runtime_error("Unknown typed_layer entity role: " + role);
}

std::optional<CompiledTypedEntityRole> parse_optional_prefixed_role(std::string_view prefix) {
    if (prefix == "self") return CompiledTypedEntityRole::SELF;
    if (prefix == "other") return CompiledTypedEntityRole::OTHER;
    return std::nullopt;
}

std::vector<std::string> split_dotted_path(const std::string& value) {
    std::vector<std::string> parts;
    std::string current;
    for (const char c : value) {
        if (c == '.') {
            parts.push_back(current);
            current.clear();
            continue;
        }
        current.push_back(c);
    }
    parts.push_back(current);
    return parts;
}

std::optional<CompiledTypedLayer> compile_typed_layer(const schema::ScenarioDefinition& scenario) {
    if (!scenario.typed_layer.has_value()) {
        return std::nullopt;
    }

    const auto& layer = *scenario.typed_layer;
    CompiledTypedLayer compiled;
    compiled.world = layer.world;

    compiled.component_types.reserve(layer.component_types.size());
    for (std::size_t i = 0; i < layer.component_types.size(); ++i) {
        const auto& component = layer.component_types[i];
        CompiledTypedComponentType compiled_component;
        compiled_component.component_type_id = component.component_type_id;
        compiled.component_type_index.emplace(compiled_component.component_type_id, i);

        compiled_component.fields.reserve(component.fields.size());
        for (std::size_t f = 0; f < component.fields.size(); ++f) {
            const auto& field = component.fields[f];
            CompiledTypedComponentFieldStorage storage;
            storage.field_name = field.field_name;
            storage.type = field.type;

            switch (field.type) {
                case schema::TypedFieldType::INTEGER:
                    storage.initial_buffer = std::vector<int64_t>{};
                    break;
                case schema::TypedFieldType::FLOAT:
                    storage.initial_buffer = std::vector<double>{};
                    break;
                case schema::TypedFieldType::BOOLEAN:
                    storage.initial_buffer = std::vector<bool>{};
                    break;
                case schema::TypedFieldType::STRING:
                    storage.initial_buffer = std::vector<std::string>{};
                    break;
            }

            compiled_component.field_index.emplace(storage.field_name, f);
            compiled_component.fields.push_back(std::move(storage));
        }

        compiled.component_types.push_back(std::move(compiled_component));
    }

    compiled.entity_types.reserve(layer.entity_types.size());
    for (std::size_t i = 0; i < layer.entity_types.size(); ++i) {
        const auto& entity_type = layer.entity_types[i];
        CompiledTypedEntityType compiled_entity_type;
        compiled_entity_type.entity_type_id = entity_type.entity_type_id;
        compiled.entity_type_index.emplace(compiled_entity_type.entity_type_id, i);

        for (const auto& component_ref : entity_type.components) {
            const auto component_it = compiled.component_type_index.find(component_ref);
            if (component_it == compiled.component_type_index.end()) {
                throw std::runtime_error("typed_layer entity_type references unknown component_type: " + component_ref);
            }
            compiled_entity_type.component_type_indices.push_back(component_it->second);
        }

        compiled.entity_types.push_back(std::move(compiled_entity_type));
    }

    compiled.entity_ids.reserve(layer.entities.size());
    compiled.entity_type_index_by_entity.reserve(layer.entities.size());
    for (std::size_t entity_index = 0; entity_index < layer.entities.size(); ++entity_index) {
        const auto& entity = layer.entities[entity_index];
        compiled.entity_index.emplace(entity.entity_id, entity_index);
        compiled.entity_ids.push_back(entity.entity_id);

        const auto type_it = compiled.entity_type_index.find(entity.entity_type_ref);
        if (type_it == compiled.entity_type_index.end()) {
            throw std::runtime_error("typed_layer entity references unknown entity_type: " + entity.entity_type_ref);
        }
        compiled.entity_type_index_by_entity.push_back(type_it->second);
    }

    const std::size_t entity_count = compiled.entity_ids.size();
    for (auto& component_type : compiled.component_types) {
        component_type.entity_to_instance.assign(entity_count, -1);
        component_type.instance_to_entity.clear();
    }

    for (std::size_t entity_index = 0; entity_index < layer.entities.size(); ++entity_index) {
        const auto& entity = layer.entities[entity_index];
        const auto type_index = compiled.entity_type_index_by_entity[entity_index];
        const auto& entity_type = compiled.entity_types[type_index];

        for (const auto component_index : entity_type.component_type_indices) {
            auto& component_type = compiled.component_types[component_index];
            const int32_t instance_index =
                static_cast<int32_t>(component_type.instance_to_entity.size());
            component_type.entity_to_instance[entity_index] = instance_index;
            component_type.instance_to_entity.push_back(static_cast<int32_t>(entity_index));
        }
    }

    for (auto& component_type : compiled.component_types) {
        const std::size_t instance_count = component_type.instance_to_entity.size();
        for (auto& field : component_type.fields) {
            std::visit([&](auto& buffer) {
                using BufferT = std::decay_t<decltype(buffer)>;
                if constexpr (std::is_same_v<BufferT, std::vector<int64_t>>) {
                    buffer.assign(instance_count, 0);
                } else if constexpr (std::is_same_v<BufferT, std::vector<double>>) {
                    buffer.assign(instance_count, 0.0);
                } else if constexpr (std::is_same_v<BufferT, std::vector<bool>>) {
                    buffer.assign(instance_count, false);
                } else if constexpr (std::is_same_v<BufferT, std::vector<std::string>>) {
                    buffer.assign(instance_count, std::string{});
                }
            }, field.initial_buffer);
        }
    }

    for (std::size_t entity_index = 0; entity_index < layer.entities.size(); ++entity_index) {
        const auto& entity = layer.entities[entity_index];
        for (const auto& [component_id, fields] : entity.components) {
            const auto component_it = compiled.component_type_index.find(component_id);
            if (component_it == compiled.component_type_index.end()) {
                continue;
            }

            auto& component_type = compiled.component_types[component_it->second];
            const int32_t instance_index = component_type.entity_to_instance[entity_index];
            if (instance_index < 0) {
                continue;
            }

            for (const auto& [field_name, value] : fields) {
                const auto field_it = component_type.field_index.find(field_name);
                if (field_it == component_type.field_index.end()) {
                    continue;
                }

                auto& storage = component_type.fields[field_it->second];
                std::visit([&](auto& buffer) {
                    using BufferT = std::decay_t<decltype(buffer)>;
                    if constexpr (std::is_same_v<BufferT, std::vector<int64_t>>) {
                        if (const auto* v = std::get_if<int64_t>(&value)) {
                            buffer[static_cast<std::size_t>(instance_index)] = *v;
                        }
                    } else if constexpr (std::is_same_v<BufferT, std::vector<double>>) {
                        if (const auto* v = std::get_if<double>(&value)) {
                            buffer[static_cast<std::size_t>(instance_index)] = *v;
                        } else if (const auto* v_int = std::get_if<int64_t>(&value)) {
                            buffer[static_cast<std::size_t>(instance_index)] = static_cast<double>(*v_int);
                        }
                    } else if constexpr (std::is_same_v<BufferT, std::vector<bool>>) {
                        if (const auto* v = std::get_if<bool>(&value)) {
                            buffer[static_cast<std::size_t>(instance_index)] = *v;
                        }
                    } else if constexpr (std::is_same_v<BufferT, std::vector<std::string>>) {
                        if (const auto* v = std::get_if<std::string>(&value)) {
                            buffer[static_cast<std::size_t>(instance_index)] = *v;
                        }
                    }
                }, storage.initial_buffer);
            }
        }
    }

    compiled.relation_types.reserve(layer.relation_types.size());
    for (std::size_t i = 0; i < layer.relation_types.size(); ++i) {
        const auto& relation_type = layer.relation_types[i];
        CompiledTypedRelationType compiled_relation_type;
        compiled_relation_type.relation_type_id = relation_type.relation_type_id;
        compiled_relation_type.directed = relation_type.directed;
        compiled_relation_type.max_per_entity = relation_type.max_per_entity;
        compiled_relation_type.max_total = relation_type.max_total;
        compiled_relation_type.payload_fields = relation_type.payload_fields;

        for (std::size_t f = 0; f < compiled_relation_type.payload_fields.size(); ++f) {
            compiled_relation_type.payload_field_index.emplace(
                compiled_relation_type.payload_fields[f].field_name, f);
        }

        compiled.relation_type_index.emplace(compiled_relation_type.relation_type_id, i);
        compiled.relation_types.push_back(std::move(compiled_relation_type));
    }

    compiled.relations.reserve(layer.relations.size());
    for (const auto& relation : layer.relations) {
        CompiledTypedRelationInstance compiled_relation;
        const auto type_it = compiled.relation_type_index.find(relation.relation_type_ref);
        if (type_it == compiled.relation_type_index.end()) {
            throw std::runtime_error("typed_layer relation references unknown relation_type: " + relation.relation_type_ref);
        }
        compiled_relation.relation_type_index = type_it->second;

        const auto source_it = compiled.entity_index.find(relation.source_entity_ref);
        const auto target_it = compiled.entity_index.find(relation.target_entity_ref);
        if (source_it == compiled.entity_index.end() || target_it == compiled.entity_index.end()) {
            throw std::runtime_error("typed_layer relation references unknown entity");
        }
        compiled_relation.source_entity_index = source_it->second;
        compiled_relation.target_entity_index = target_it->second;
        compiled_relation.expires_at = relation.expires_at;
        compiled_relation.payload = relation.payload;
        compiled.relations.push_back(std::move(compiled_relation));
    }

    compiled.event_types.reserve(layer.event_types.size());
    for (std::size_t i = 0; i < layer.event_types.size(); ++i) {
        const auto& event_type = layer.event_types[i];
        CompiledTypedEventType compiled_event_type;
        compiled_event_type.event_type_id = event_type.event_type_id;
        compiled_event_type.payload_fields = event_type.payload_fields;

        for (std::size_t f = 0; f < compiled_event_type.payload_fields.size(); ++f) {
            compiled_event_type.payload_field_index.emplace(
                compiled_event_type.payload_fields[f].field_name, f);
        }

        compiled.event_type_index.emplace(compiled_event_type.event_type_id, i);
        compiled.event_types.push_back(std::move(compiled_event_type));
    }

    compiled.initial_events.reserve(layer.initial_events.size());
    for (const auto& event : layer.initial_events) {
        CompiledTypedInitialEvent compiled_event;
        const auto event_type_it = compiled.event_type_index.find(event.event_type_ref);
        if (event_type_it == compiled.event_type_index.end()) {
            throw std::runtime_error("typed_layer initial_event references unknown event_type: " + event.event_type_ref);
        }
        compiled_event.event_type_index = event_type_it->second;
        compiled_event.timestamp = event.timestamp;
        compiled_event.priority = event.priority;
        compiled_event.event_handle = event.event_handle;
        compiled_event.payload = event.payload;
        compiled.initial_events.push_back(std::move(compiled_event));
    }

    compiled.systems.reserve(layer.systems.size());
    for (const auto& system : layer.systems) {
        CompiledTypedSystem compiled_system;
        compiled_system.system_id = system.system_id;
        compiled_system.kind = parse_typed_system_kind(system.kind);

        for (const auto& trigger : system.triggered_by) {
            const auto trigger_it = compiled.event_type_index.find(trigger);
            if (trigger_it == compiled.event_type_index.end()) {
                throw std::runtime_error("typed_layer system references unknown event_type trigger: " + trigger);
            }
            compiled_system.trigger_event_type_indices.push_back(trigger_it->second);
        }

        if (system.entity_type_ref.has_value()) {
            const auto entity_type_it = compiled.entity_type_index.find(*system.entity_type_ref);
            if (entity_type_it == compiled.entity_type_index.end()) {
                throw std::runtime_error("typed_layer system references unknown entity_type: " + *system.entity_type_ref);
            }
            compiled_system.entity_type_index = entity_type_it->second;
        }
        if (system.relation_type_ref.has_value()) {
            const auto relation_type_it = compiled.relation_type_index.find(*system.relation_type_ref);
            if (relation_type_it == compiled.relation_type_index.end()) {
                throw std::runtime_error("typed_layer system references unknown relation_type: " + *system.relation_type_ref);
            }
            compiled_system.relation_type_index = relation_type_it->second;
        }
        compiled_system.where = system.where;

        for (const auto& write : system.writes) {
            CompiledTypedSystemWrite compiled_write;
            auto parts = split_dotted_path(write.target);
            if (parts.size() == 3) {
                const auto role_opt = parse_optional_prefixed_role(parts[0]);
                if (!role_opt.has_value()) {
                    throw std::runtime_error("Invalid typed_layer write target role: " + parts[0]);
                }
                compiled_write.role = *role_opt;
                parts.erase(parts.begin());
            }
            if (parts.size() != 2) {
                throw std::runtime_error("typed_layer write target must be component.field: " + write.target);
            }

            const auto component_it = compiled.component_type_index.find(parts[0]);
            if (component_it == compiled.component_type_index.end()) {
                throw std::runtime_error("typed_layer write target references unknown component_type: " + parts[0]);
            }
            compiled_write.component_type_index = component_it->second;

            auto& component_type = compiled.component_types[compiled_write.component_type_index];
            const auto field_it = component_type.field_index.find(parts[1]);
            if (field_it == component_type.field_index.end()) {
                throw std::runtime_error("typed_layer write target references unknown field: " + parts[1]);
            }
            compiled_write.field_index = field_it->second;
            compiled_write.field_type = component_type.fields[compiled_write.field_index].type;
            compiled_write.expr = write.expr;
            compiled_write.when = write.when;
            compiled_system.writes.push_back(std::move(compiled_write));
        }

        for (const auto& create_relation : system.create_relations) {
            CompiledTypedSystemCreateRelation compiled_create;
            const auto relation_it = compiled.relation_type_index.find(create_relation.relation_type_ref);
            if (relation_it == compiled.relation_type_index.end()) {
                throw std::runtime_error("typed_layer create_relation references unknown relation_type: " +
                                         create_relation.relation_type_ref);
            }
            compiled_create.relation_type_index = relation_it->second;
            compiled_create.source_role = parse_typed_entity_role(create_relation.source);
            compiled_create.target_role = parse_typed_entity_role(create_relation.target);
            compiled_create.expires_after = create_relation.expires_after;
            compiled_create.payload_exprs = create_relation.payload_exprs;
            compiled_create.when = create_relation.when;
            compiled_system.create_relations.push_back(std::move(compiled_create));
        }

        for (const auto& emit_event : system.emit_events) {
            CompiledTypedSystemEmitEvent compiled_emit;
            const auto event_it = compiled.event_type_index.find(emit_event.event_type_ref);
            if (event_it == compiled.event_type_index.end()) {
                throw std::runtime_error("typed_layer emit_event references unknown event_type: " + emit_event.event_type_ref);
            }
            compiled_emit.event_type_index = event_it->second;
            compiled_emit.timestamp = emit_event.timestamp;
            compiled_emit.priority = emit_event.priority;
            compiled_emit.payload_exprs = emit_event.payload_exprs;
            compiled_emit.when = emit_event.when;
            compiled_system.emit_events.push_back(std::move(compiled_emit));
        }

        compiled.systems.push_back(std::move(compiled_system));
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

    // Import any statically linked extension propagation functions.
    {
        const auto ext_registry = extensions::make_default_registry();
        for (const auto& [id, fn] : ext_registry.propagation_functions().functions()) {
            register_propagation_function(id, fn);
        }
        for (const auto& [alias, canonical] : ext_registry.propagation_functions().aliases()) {
            if (const auto* fn = ext_registry.propagation_functions().find(canonical); fn != nullptr) {
                register_propagation_function(alias, *fn);
            }
        }
    }
    
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
    compiled.typed_layer = compile_typed_layer(scenario);
    
    // Copy registered functions
    compiled.propagation_functions = registered_functions_;

    // Capture expression function registry for the typed runtime.
    {
        const auto ext_registry = extensions::make_default_registry();
        compiled.expression_functions = ext_registry.expression_functions();
    }
    
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
    const auto linear_scale = [](double& target, const double& source, double weight) {
        target = source * weight;
    };
    register_propagation_function("linear_scale", linear_scale);
    register_propagation_function("core::linear_scale", linear_scale);
    
    const auto apply_discount = [](double& target, const double& source, double weight) {
        target = target * (1.0 + source * weight);
    };
    register_propagation_function("apply_discount", apply_discount);
    register_propagation_function("core::apply_discount", apply_discount);
    
    const auto additive = [](double& target, const double& source, double weight) {
        target += source * weight;
    };
    register_propagation_function("additive", additive);
    register_propagation_function("core::additive", additive);
    
    const auto max_propagate = [](double& target, const double& source, double weight) {
        target = std::max(target, source * weight);
    };
    register_propagation_function("max_propagate", max_propagate);
    register_propagation_function("core::max_propagate", max_propagate);
    
    const auto min_propagate = [](double& target, const double& source, double weight) {
        target = std::min(target, source * weight);
    };
    register_propagation_function("min_propagate", min_propagate);
    register_propagation_function("core::min_propagate", min_propagate);
}

} // namespace noisiax::compiler
