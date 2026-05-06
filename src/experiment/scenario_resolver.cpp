#include "noisiax/experiment/experiment.hpp"

#include "noisiax/serialization/yaml_serializer.hpp"
#include "noisiax/validation/scenario_validator.hpp"

#include <yaml-cpp/yaml.h>

#include <cctype>
#include <filesystem>
#include <fstream>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace noisiax::experiment {
namespace {

namespace fs = std::filesystem;

uint64_t fnv1a_64(std::string_view text) {
    constexpr uint64_t kOffsetBasis = 14695981039346656037ULL;
    constexpr uint64_t kPrime = 1099511628211ULL;
    uint64_t hash = kOffsetBasis;
    for (const unsigned char c : text) {
        hash ^= static_cast<uint64_t>(c);
        hash *= kPrime;
    }
    return hash;
}

std::string to_hex_u64(uint64_t value) {
    std::ostringstream oss;
    oss << std::hex;
    oss.width(16);
    oss.fill('0');
    oss << value;
    return oss.str();
}

std::string read_file_or_throw(const fs::path& path) {
    std::ifstream in(path);
    if (!in) {
        throw std::runtime_error("Failed to open file: " + path.string());
    }
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

bool is_valid_namespace(std::string_view ns) {
    if (ns.empty()) {
        return false;
    }
    const unsigned char first = static_cast<unsigned char>(ns.front());
    if (std::isalpha(first) == 0 && ns.front() != '_') {
        return false;
    }
    for (const char c : ns) {
        const unsigned char uc = static_cast<unsigned char>(c);
        if (std::isalnum(uc) == 0 && c != '_') {
            return false;
        }
    }
    return true;
}

std::string prefix_id(const std::string& ns, const std::string& id) {
    if (ns.empty()) {
        return id;
    }
    return ns + "__" + id;
}

std::string rewrite_simple_identifiers(
    std::string_view expr,
    const std::unordered_map<std::string, std::string>& replacements) {

    std::string out;
    out.reserve(expr.size());

    std::size_t i = 0;
    while (i < expr.size()) {
        const char c = expr[i];
        if (std::isalpha(static_cast<unsigned char>(c)) != 0 || c == '_') {
            const std::size_t start = i++;
            while (i < expr.size()) {
                const char cc = expr[i];
                if (std::isalnum(static_cast<unsigned char>(cc)) != 0 || cc == '_') {
                    ++i;
                    continue;
                }
                break;
            }
            const std::string token(expr.substr(start, i - start));
            const auto it = replacements.find(token);
            if (it != replacements.end()) {
                out.append(it->second);
            } else {
                out.append(token);
            }
            continue;
        }
        out.push_back(c);
        ++i;
    }

    return out;
}

std::string rewrite_dotted_identifiers(
    std::string_view expr,
    const std::unordered_map<std::string, std::string>& first_segment_replacements) {

    std::string out;
    out.reserve(expr.size());

    std::size_t i = 0;
    while (i < expr.size()) {
        const char c = expr[i];
        if (std::isalpha(static_cast<unsigned char>(c)) != 0 || c == '_') {
            const std::size_t start = i++;
            while (i < expr.size()) {
                const char cc = expr[i];
                if (std::isalnum(static_cast<unsigned char>(cc)) != 0 || cc == '_') {
                    ++i;
                    continue;
                }
                break;
            }

            std::string name(expr.substr(start, i - start));
            while (i < expr.size() && expr[i] == '.') {
                const std::size_t dot = i;
                if (dot + 1 >= expr.size()) {
                    break;
                }
                const char next = expr[dot + 1];
                if (std::isalpha(static_cast<unsigned char>(next)) == 0 && next != '_') {
                    break;
                }
                i += 2;
                const std::size_t seg_start = dot + 1;
                while (i < expr.size()) {
                    const char seg_c = expr[i];
                    if (std::isalnum(static_cast<unsigned char>(seg_c)) != 0 || seg_c == '_') {
                        ++i;
                        continue;
                    }
                    break;
                }
                name.push_back('.');
                name.append(expr.substr(seg_start, i - seg_start));
            }

            const std::size_t dot_pos = name.find('.');
            const std::string_view head = (dot_pos == std::string::npos) ? std::string_view(name)
                                                                          : std::string_view(name).substr(0, dot_pos);
            const auto it = first_segment_replacements.find(std::string(head));
            if (it != first_segment_replacements.end()) {
                out.append(it->second);
                if (dot_pos != std::string::npos) {
                    out.append(name.substr(dot_pos));
                }
            } else {
                out.append(name);
            }
            continue;
        }

        out.push_back(c);
        ++i;
    }

    return out;
}

void prefix_agent_layer(schema::AgentLayerDefinition& layer,
                        const std::string& ns,
                        std::unordered_set<std::string>& out_location_ids,
                        std::unordered_set<std::string>& out_item_ids,
                        std::unordered_set<std::string>& out_shop_ids,
                        std::unordered_set<std::string>& out_agent_ids,
                        std::unordered_set<std::string>& out_policy_ids) {
    std::unordered_map<std::string, std::string> location_map;
    std::unordered_map<std::string, std::string> item_map;
    std::unordered_map<std::string, std::string> shop_map;
    std::unordered_map<std::string, std::string> agent_map;
    std::unordered_map<std::string, std::string> policy_map;

    for (const auto& loc : layer.locations) {
        location_map.emplace(loc.location_id, prefix_id(ns, loc.location_id));
    }
    for (const auto& item : layer.items) {
        item_map.emplace(item.item_id, prefix_id(ns, item.item_id));
    }
    for (const auto& shop : layer.shops) {
        shop_map.emplace(shop.shop_id, prefix_id(ns, shop.shop_id));
    }
    for (const auto& agent : layer.agents) {
        agent_map.emplace(agent.agent_id, prefix_id(ns, agent.agent_id));
    }
    for (const auto& policy : layer.policies) {
        policy_map.emplace(policy.policy_id, prefix_id(ns, policy.policy_id));
    }

    for (auto& loc : layer.locations) {
        loc.location_id = location_map.at(loc.location_id);
        out_location_ids.insert(loc.location_id);
    }
    for (auto& item : layer.items) {
        item.item_id = item_map.at(item.item_id);
        out_item_ids.insert(item.item_id);
    }
    for (auto& shop : layer.shops) {
        shop.shop_id = shop_map.at(shop.shop_id);
        if (!shop.location_ref.empty() && location_map.find(shop.location_ref) != location_map.end()) {
            shop.location_ref = location_map.at(shop.location_ref);
        }
        for (auto& entry : shop.inventory) {
            if (!entry.item_id.empty() && item_map.find(entry.item_id) != item_map.end()) {
                entry.item_id = item_map.at(entry.item_id);
            }
        }
        out_shop_ids.insert(shop.shop_id);
    }
    for (auto& agent : layer.agents) {
        agent.agent_id = agent_map.at(agent.agent_id);
        if (!agent.start_location_ref.empty() && location_map.find(agent.start_location_ref) != location_map.end()) {
            agent.start_location_ref = location_map.at(agent.start_location_ref);
        }
        if (!agent.policy_ref.empty() && policy_map.find(agent.policy_ref) != policy_map.end()) {
            agent.policy_ref = policy_map.at(agent.policy_ref);
        }
        if (!agent.preferences.empty()) {
            std::map<std::string, double> rewritten;
            for (const auto& [key, value] : agent.preferences) {
                auto it = item_map.find(key);
                rewritten.emplace((it != item_map.end()) ? it->second : key, value);
            }
            agent.preferences = std::move(rewritten);
        }
        out_agent_ids.insert(agent.agent_id);
    }
    for (auto& policy : layer.policies) {
        policy.policy_id = policy_map.at(policy.policy_id);
        out_policy_ids.insert(policy.policy_id);
    }
}

void prefix_typed_layer(schema::TypedLayerDefinition& layer,
                        const std::string& ns,
                        std::unordered_set<std::string>& out_component_type_ids,
                        std::unordered_set<std::string>& out_entity_type_ids,
                        std::unordered_set<std::string>& out_entity_ids,
                        std::unordered_set<std::string>& out_relation_type_ids,
                        std::unordered_set<std::string>& out_event_type_ids,
                        std::unordered_set<std::string>& out_system_ids) {
    std::unordered_map<std::string, std::string> component_map;
    std::unordered_map<std::string, std::string> entity_type_map;
    std::unordered_map<std::string, std::string> entity_map;
    std::unordered_map<std::string, std::string> relation_type_map;
    std::unordered_map<std::string, std::string> event_type_map;
    std::unordered_map<std::string, std::string> system_map;

    for (const auto& comp : layer.component_types) {
        component_map.emplace(comp.component_type_id, prefix_id(ns, comp.component_type_id));
    }
    for (const auto& et : layer.entity_types) {
        entity_type_map.emplace(et.entity_type_id, prefix_id(ns, et.entity_type_id));
    }
    for (const auto& ent : layer.entities) {
        entity_map.emplace(ent.entity_id, prefix_id(ns, ent.entity_id));
    }
    for (const auto& rel : layer.relation_types) {
        relation_type_map.emplace(rel.relation_type_id, prefix_id(ns, rel.relation_type_id));
    }
    for (const auto& evt : layer.event_types) {
        event_type_map.emplace(evt.event_type_id, prefix_id(ns, evt.event_type_id));
    }
    for (const auto& sys : layer.systems) {
        system_map.emplace(sys.system_id, prefix_id(ns, sys.system_id));
    }

    for (auto& comp : layer.component_types) {
        comp.component_type_id = component_map.at(comp.component_type_id);
        out_component_type_ids.insert(comp.component_type_id);
    }

    for (auto& et : layer.entity_types) {
        et.entity_type_id = entity_type_map.at(et.entity_type_id);
        for (auto& comp_ref : et.components) {
            auto it = component_map.find(comp_ref);
            if (it != component_map.end()) {
                comp_ref = it->second;
            }
        }
        out_entity_type_ids.insert(et.entity_type_id);
    }

    for (auto& ent : layer.entities) {
        ent.entity_id = entity_map.at(ent.entity_id);
        auto it = entity_type_map.find(ent.entity_type_ref);
        if (it != entity_type_map.end()) {
            ent.entity_type_ref = it->second;
        }

        if (!ent.components.empty()) {
            std::map<std::string, std::map<std::string, schema::TypedScalarValue>> rewritten;
            for (auto& [comp_id, fields] : ent.components) {
                auto comp_it = component_map.find(comp_id);
                const std::string new_comp = (comp_it != component_map.end()) ? comp_it->second : comp_id;
                rewritten.emplace(new_comp, std::move(fields));
            }
            ent.components = std::move(rewritten);
        }

        out_entity_ids.insert(ent.entity_id);
    }

    for (auto& rel : layer.relation_types) {
        rel.relation_type_id = relation_type_map.at(rel.relation_type_id);
        out_relation_type_ids.insert(rel.relation_type_id);
    }

    for (auto& rel : layer.relations) {
        if (relation_type_map.find(rel.relation_type_ref) != relation_type_map.end()) {
            rel.relation_type_ref = relation_type_map.at(rel.relation_type_ref);
        }
        if (entity_map.find(rel.source_entity_ref) != entity_map.end()) {
            rel.source_entity_ref = entity_map.at(rel.source_entity_ref);
        }
        if (entity_map.find(rel.target_entity_ref) != entity_map.end()) {
            rel.target_entity_ref = entity_map.at(rel.target_entity_ref);
        }
    }

    for (auto& evt : layer.event_types) {
        evt.event_type_id = event_type_map.at(evt.event_type_id);
        out_event_type_ids.insert(evt.event_type_id);
    }

    for (auto& init : layer.initial_events) {
        if (event_type_map.find(init.event_type_ref) != event_type_map.end()) {
            init.event_type_ref = event_type_map.at(init.event_type_ref);
        }
    }

    auto rewrite_expr = [&](std::string& expr) {
        expr = rewrite_dotted_identifiers(expr, component_map);
    };

    for (auto& sys : layer.systems) {
        sys.system_id = system_map.at(sys.system_id);
        out_system_ids.insert(sys.system_id);

        for (auto& trig : sys.triggered_by) {
            auto it = event_type_map.find(trig);
            if (it != event_type_map.end()) {
                trig = it->second;
            }
        }

        if (sys.entity_type_ref.has_value()) {
            auto it = entity_type_map.find(*sys.entity_type_ref);
            if (it != entity_type_map.end()) {
                sys.entity_type_ref = it->second;
            }
        }
        if (sys.relation_type_ref.has_value()) {
            auto it = relation_type_map.find(*sys.relation_type_ref);
            if (it != relation_type_map.end()) {
                sys.relation_type_ref = it->second;
            }
        }

        if (sys.where.has_value()) {
            rewrite_expr(*sys.where);
        }

        for (auto& write : sys.writes) {
            rewrite_expr(write.target);
            rewrite_expr(write.expr);
            if (write.when.has_value()) {
                rewrite_expr(*write.when);
            }
        }

        for (auto& create_relation : sys.create_relations) {
            if (relation_type_map.find(create_relation.relation_type_ref) != relation_type_map.end()) {
                create_relation.relation_type_ref = relation_type_map.at(create_relation.relation_type_ref);
            }
            rewrite_expr(create_relation.source);
            rewrite_expr(create_relation.target);
            if (create_relation.expires_after.has_value()) {
                rewrite_expr(*create_relation.expires_after);
            }
            for (auto& [_, expr] : create_relation.payload_exprs) {
                rewrite_expr(expr);
            }
            if (create_relation.when.has_value()) {
                rewrite_expr(*create_relation.when);
            }
        }

        for (auto& emit_event : sys.emit_events) {
            if (event_type_map.find(emit_event.event_type_ref) != event_type_map.end()) {
                emit_event.event_type_ref = event_type_map.at(emit_event.event_type_ref);
            }
            if (emit_event.timestamp.has_value()) {
                rewrite_expr(*emit_event.timestamp);
            }
            for (auto& [_, expr] : emit_event.payload_exprs) {
                rewrite_expr(expr);
            }
            if (emit_event.when.has_value()) {
                rewrite_expr(*emit_event.when);
            }
        }
    }
}

void prefix_fragment(schema::ScenarioDefinition& fragment, const std::string& ns) {
    if (!is_valid_namespace(ns)) {
        throw std::runtime_error("Invalid import namespace: '" + ns + "'");
    }

    std::unordered_map<std::string, std::string> assumption_map;
    std::unordered_map<std::string, std::string> entity_map;
    std::unordered_map<std::string, std::string> variable_map;
    std::unordered_map<std::string, std::string> edge_map;
    std::unordered_map<std::string, std::string> constraint_map;
    std::unordered_map<std::string, std::string> event_map;
    std::unordered_map<std::string, std::string> eval_map;

    for (const auto& a : fragment.assumptions) {
        assumption_map.emplace(a.assumption_id, prefix_id(ns, a.assumption_id));
    }
    for (const auto& e : fragment.entities) {
        entity_map.emplace(e.entity_id, prefix_id(ns, e.entity_id));
    }
    for (const auto& v : fragment.variables) {
        variable_map.emplace(v.variable_id, prefix_id(ns, v.variable_id));
    }
    for (const auto& edge : fragment.dependency_edges) {
        edge_map.emplace(edge.edge_id, prefix_id(ns, edge.edge_id));
    }
    for (const auto& c : fragment.constraints) {
        constraint_map.emplace(c.constraint_id, prefix_id(ns, c.constraint_id));
    }
    for (const auto& ev : fragment.events) {
        event_map.emplace(ev.event_id, prefix_id(ns, ev.event_id));
    }
    for (const auto& ec : fragment.evaluation_criteria) {
        eval_map.emplace(ec.criterion_id, prefix_id(ns, ec.criterion_id));
    }

    for (auto& a : fragment.assumptions) {
        a.assumption_id = assumption_map.at(a.assumption_id);
    }
    for (auto& e : fragment.entities) {
        e.entity_id = entity_map.at(e.entity_id);
    }
    for (auto& v : fragment.variables) {
        v.variable_id = variable_map.at(v.variable_id);
        if (!v.entity_ref.empty() && entity_map.find(v.entity_ref) != entity_map.end()) {
            v.entity_ref = entity_map.at(v.entity_ref);
        }
    }
    for (auto& edge : fragment.dependency_edges) {
        edge.edge_id = edge_map.at(edge.edge_id);
        if (variable_map.find(edge.source_variable) != variable_map.end()) {
            edge.source_variable = variable_map.at(edge.source_variable);
        }
        if (variable_map.find(edge.target_variable) != variable_map.end()) {
            edge.target_variable = variable_map.at(edge.target_variable);
        }
    }
    for (auto& c : fragment.constraints) {
        c.constraint_id = constraint_map.at(c.constraint_id);
        for (auto& var : c.affected_variables) {
            auto it = variable_map.find(var);
            if (it != variable_map.end()) {
                var = it->second;
            }
        }
        if (!c.constraint_expression.empty()) {
            c.constraint_expression = rewrite_simple_identifiers(c.constraint_expression, variable_map);
        }
    }
    for (auto& ev : fragment.events) {
        ev.event_id = event_map.at(ev.event_id);
        if (!ev.trigger_condition.empty()) {
            ev.trigger_condition = rewrite_simple_identifiers(ev.trigger_condition, variable_map);
        }
        for (auto& [key, value] : ev.event_payload) {
            if (key == "variable_id" || key == "target_variable") {
                auto it = variable_map.find(value);
                if (it != variable_map.end()) {
                    value = it->second;
                }
            }
        }
    }
    for (auto& ec : fragment.evaluation_criteria) {
        ec.criterion_id = eval_map.at(ec.criterion_id);
        for (auto& var : ec.input_variables) {
            auto it = variable_map.find(var);
            if (it != variable_map.end()) {
                var = it->second;
            }
        }
    }

    if (fragment.agent_layer.has_value()) {
        std::unordered_set<std::string> loc_ids;
        std::unordered_set<std::string> item_ids;
        std::unordered_set<std::string> shop_ids;
        std::unordered_set<std::string> agent_ids;
        std::unordered_set<std::string> policy_ids;
        prefix_agent_layer(*fragment.agent_layer, ns, loc_ids, item_ids, shop_ids, agent_ids, policy_ids);
    }

    if (fragment.typed_layer.has_value()) {
        std::unordered_set<std::string> comp_ids;
        std::unordered_set<std::string> entity_type_ids;
        std::unordered_set<std::string> entity_ids;
        std::unordered_set<std::string> relation_type_ids;
        std::unordered_set<std::string> event_type_ids;
        std::unordered_set<std::string> system_ids;
        prefix_typed_layer(*fragment.typed_layer, ns, comp_ids, entity_type_ids, entity_ids, relation_type_ids, event_type_ids, system_ids);
    }
}

template <typename T, typename GetId>
void append_unique(std::vector<T>& dst,
                   const std::vector<T>& src,
                   std::unordered_set<std::string>& seen,
                   const std::string& kind,
                   GetId get_id) {
    for (const auto& item : src) {
        const std::string id = get_id(item);
        if (!id.empty() && !seen.insert(id).second) {
            throw std::runtime_error("Duplicate resolved " + kind + " id: " + id);
        }
        dst.push_back(item);
    }
}

void merge_agent_layer(schema::AgentLayerDefinition& dst, const schema::AgentLayerDefinition& src) {
    if (!(dst.world == src.world)) {
        throw std::runtime_error("agent_layer.world conflict during composition");
    }

    std::unordered_set<std::string> location_ids;
    std::unordered_set<std::string> item_ids;
    std::unordered_set<std::string> shop_ids;
    std::unordered_set<std::string> agent_ids;
    std::unordered_set<std::string> policy_ids;

    for (const auto& loc : dst.locations) location_ids.insert(loc.location_id);
    for (const auto& item : dst.items) item_ids.insert(item.item_id);
    for (const auto& shop : dst.shops) shop_ids.insert(shop.shop_id);
    for (const auto& agent : dst.agents) agent_ids.insert(agent.agent_id);
    for (const auto& pol : dst.policies) policy_ids.insert(pol.policy_id);

    append_unique(dst.locations, src.locations, location_ids, "agent_layer.location", [](const schema::LocationDescriptor& v) {
        return v.location_id;
    });
    append_unique(dst.items, src.items, item_ids, "agent_layer.item", [](const schema::ItemDescriptor& v) {
        return v.item_id;
    });
    append_unique(dst.shops, src.shops, shop_ids, "agent_layer.shop", [](const schema::ShopDescriptor& v) {
        return v.shop_id;
    });
    append_unique(dst.agents, src.agents, agent_ids, "agent_layer.agent", [](const schema::AgentDescriptor& v) {
        return v.agent_id;
    });
    append_unique(dst.policies, src.policies, policy_ids, "agent_layer.policy", [](const schema::PolicyDescriptor& v) {
        return v.policy_id;
    });
}

void merge_typed_layer(schema::TypedLayerDefinition& dst, const schema::TypedLayerDefinition& src) {
    if (!(dst.world == src.world)) {
        throw std::runtime_error("typed_layer.world conflict during composition");
    }

    std::unordered_set<std::string> comp_ids;
    std::unordered_set<std::string> entity_type_ids;
    std::unordered_set<std::string> entity_ids;
    std::unordered_set<std::string> relation_type_ids;
    std::unordered_set<std::string> event_type_ids;
    std::unordered_set<std::string> system_ids;

    for (const auto& v : dst.component_types) comp_ids.insert(v.component_type_id);
    for (const auto& v : dst.entity_types) entity_type_ids.insert(v.entity_type_id);
    for (const auto& v : dst.entities) entity_ids.insert(v.entity_id);
    for (const auto& v : dst.relation_types) relation_type_ids.insert(v.relation_type_id);
    for (const auto& v : dst.event_types) event_type_ids.insert(v.event_type_id);
    for (const auto& v : dst.systems) system_ids.insert(v.system_id);

    append_unique(dst.component_types, src.component_types, comp_ids, "typed_layer.component_type", [](const schema::ComponentTypeDefinition& v) {
        return v.component_type_id;
    });
    append_unique(dst.entity_types, src.entity_types, entity_type_ids, "typed_layer.entity_type", [](const schema::TypedEntityTypeDefinition& v) {
        return v.entity_type_id;
    });
    append_unique(dst.entities, src.entities, entity_ids, "typed_layer.entity", [](const schema::TypedEntityInstanceDefinition& v) {
        return v.entity_id;
    });
    append_unique(dst.relation_types, src.relation_types, relation_type_ids, "typed_layer.relation_type", [](const schema::RelationTypeDefinition& v) {
        return v.relation_type_id;
    });
    dst.relations.insert(dst.relations.end(), src.relations.begin(), src.relations.end());
    append_unique(dst.event_types, src.event_types, event_type_ids, "typed_layer.event_type", [](const schema::TypedEventTypeDefinition& v) {
        return v.event_type_id;
    });
    dst.initial_events.insert(dst.initial_events.end(), src.initial_events.begin(), src.initial_events.end());
    append_unique(dst.systems, src.systems, system_ids, "typed_layer.system", [](const schema::TypedSystemDefinition& v) {
        return v.system_id;
    });
}

void merge_scenario(schema::ScenarioDefinition& dst, const schema::ScenarioDefinition& src) {
    std::unordered_set<std::string> assumption_ids;
    std::unordered_set<std::string> entity_ids;
    std::unordered_set<std::string> variable_ids;
    std::unordered_set<std::string> edge_ids;
    std::unordered_set<std::string> constraint_ids;
    std::unordered_set<std::string> event_ids;
    std::unordered_set<std::string> eval_ids;

    for (const auto& v : dst.assumptions) assumption_ids.insert(v.assumption_id);
    for (const auto& v : dst.entities) entity_ids.insert(v.entity_id);
    for (const auto& v : dst.variables) variable_ids.insert(v.variable_id);
    for (const auto& v : dst.dependency_edges) edge_ids.insert(v.edge_id);
    for (const auto& v : dst.constraints) constraint_ids.insert(v.constraint_id);
    for (const auto& v : dst.events) event_ids.insert(v.event_id);
    for (const auto& v : dst.evaluation_criteria) eval_ids.insert(v.criterion_id);

    append_unique(dst.assumptions, src.assumptions, assumption_ids, "assumption", [](const schema::Assumption& v) {
        return v.assumption_id;
    });
    append_unique(dst.entities, src.entities, entity_ids, "entity", [](const schema::EntityDescriptor& v) {
        return v.entity_id;
    });
    append_unique(dst.variables, src.variables, variable_ids, "variable", [](const schema::VariableDescriptor& v) {
        return v.variable_id;
    });
    append_unique(dst.dependency_edges, src.dependency_edges, edge_ids, "dependency_edge", [](const schema::DependencyEdge& v) {
        return v.edge_id;
    });
    append_unique(dst.constraints, src.constraints, constraint_ids, "constraint", [](const schema::ConstraintRule& v) {
        return v.constraint_id;
    });
    append_unique(dst.events, src.events, event_ids, "event", [](const schema::EventDescriptor& v) {
        return v.event_id;
    });
    append_unique(dst.evaluation_criteria, src.evaluation_criteria, eval_ids, "evaluation_criterion", [](const schema::EvaluationCriterion& v) {
        return v.criterion_id;
    });

    if (src.agent_layer.has_value()) {
        if (!dst.agent_layer.has_value()) {
            dst.agent_layer = src.agent_layer;
        } else {
            merge_agent_layer(*dst.agent_layer, *src.agent_layer);
        }
    }

    if (src.typed_layer.has_value()) {
        if (!dst.typed_layer.has_value()) {
            dst.typed_layer = src.typed_layer;
        } else {
            merge_typed_layer(*dst.typed_layer, *src.typed_layer);
        }
    }
}

std::vector<ImportSpec> parse_imports(const YAML::Node& root) {
    std::vector<ImportSpec> imports;
    if (!root || !root["imports"]) {
        return imports;
    }
    const auto node = root["imports"];
    if (!node.IsSequence()) {
        throw std::runtime_error("imports must be a YAML sequence");
    }

    for (const auto& item : node) {
        if (!item.IsMap()) {
            throw std::runtime_error("imports entries must be mapping nodes");
        }
        if (!item["path"]) {
            throw std::runtime_error("imports entry missing required field: path");
        }
        ImportSpec spec;
        spec.path = item["path"].as<std::string>();
        if (item["namespace"]) {
            spec.namespace_prefix = item["namespace"].as<std::string>();
        }
        imports.push_back(std::move(spec));
    }

    return imports;
}

struct ResolveContext {
    std::unordered_set<std::string> visiting;
    std::unordered_map<std::string, schema::ScenarioDefinition> resolved_cache;
    CompositionReport report;
};

schema::ScenarioDefinition resolve_definition(const fs::path& input_path,
                                             const ResolveOptions& options,
                                             ResolveContext& ctx) {
    std::error_code ec;
    const fs::path abs = fs::absolute(input_path, ec).lexically_normal();
    const std::string canonical_path = abs.string();

    if (ctx.visiting.find(canonical_path) != ctx.visiting.end()) {
        throw std::runtime_error("Circular import detected at: " + canonical_path);
    }

    if (auto it = ctx.resolved_cache.find(canonical_path); it != ctx.resolved_cache.end()) {
        return it->second;
    }

    ctx.visiting.insert(canonical_path);
    ctx.report.resolved_files.push_back(canonical_path);

    const std::string raw = read_file_or_throw(abs);
    YAML::Node root = YAML::Load(raw);
    const auto imports = parse_imports(root);

    serialization::YamlSerializer serializer;
    // Scenario files may include a v4-only top-level "imports" stanza. The core
    // ScenarioDefinition schema stays strict; resolve-time composition is the
    // only place that understands "imports".
    YAML::Node sanitized;
    if (root && root.IsMap() && root["imports"]) {
        sanitized = YAML::Node(YAML::NodeType::Map);
        for (const auto& entry : root) {
            const std::string key = entry.first.as<std::string>();
            if (key == "imports") {
                continue;
            }
            sanitized[key] = entry.second;
        }
    } else {
        sanitized = YAML::Clone(root);
    }
    YAML::Emitter emitter;
    emitter.SetIndent(2);
    emitter << sanitized;
    schema::ScenarioDefinition scenario = serializer.deserialize(emitter.c_str());

    for (const auto& imp : imports) {
        ctx.report.imports.push_back(imp);
        fs::path child = imp.path;
        if (child.is_relative()) {
            child = abs.parent_path() / child;
        }
        schema::ScenarioDefinition resolved_child = resolve_definition(child, options, ctx);
        if (imp.namespace_prefix.has_value()) {
            prefix_fragment(resolved_child, *imp.namespace_prefix);
        }
        merge_scenario(scenario, resolved_child);
    }

    ctx.visiting.erase(canonical_path);
    ctx.resolved_cache.emplace(canonical_path, scenario);
    return scenario;
}

}  // namespace

ResolvedScenario resolve_scenario(const std::string& path, const ResolveOptions& options) {
    ResolveContext ctx;

    const fs::path input(path);
    const std::string raw = read_file_or_throw(input);
    ctx.report.source_hash = to_hex_u64(fnv1a_64(raw));

    schema::ScenarioDefinition resolved = resolve_definition(input, options, ctx);

    if (options.validate_resolved) {
        validation::ScenarioValidator validator;
        const auto validation_report = validator.validate(resolved);
        if (!validation_report.overall_passed) {
            std::ostringstream oss;
            oss << "Resolved scenario validation failed";
            for (const auto& error : validation_report.errors) {
                oss << "\n- " << error;
            }
            throw std::runtime_error(oss.str());
        }
    }

    serialization::YamlSerializer serializer;
    ResolvedScenario result;
    result.scenario = std::move(resolved);
    result.canonical_yaml = serializer.serialize(result.scenario);
    result.source_hash = ctx.report.source_hash;
    result.resolved_hash = to_hex_u64(fnv1a_64(result.canonical_yaml));
    result.report = std::move(ctx.report);
    result.report.resolved_hash = result.resolved_hash;
    result.report.source_hash = result.source_hash;
    result.report.success = true;
    return result;
}

}  // namespace noisiax::experiment
