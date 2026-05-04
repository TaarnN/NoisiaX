#include "noisiax/engine/agent_runtime.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <iomanip>
#include <limits>
#include <map>
#include <optional>
#include <queue>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace noisiax::engine {
namespace {

enum class RuntimeEventType {
    SIM_START,
    MOVE_START,
    MOVE_ARRIVE,
    OBSERVE_PURCHASE,
    TALK_START,
    TALK_END,
    DECIDE_PURCHASE,
    QUEUE_ENTER,
    PURCHASE,
    INFLUENCE_DECAY,
    SIM_END
};

std::string to_event_type_string(RuntimeEventType type) {
    switch (type) {
        case RuntimeEventType::SIM_START: return "SIM_START";
        case RuntimeEventType::MOVE_START: return "MOVE_START";
        case RuntimeEventType::MOVE_ARRIVE: return "MOVE_ARRIVE";
        case RuntimeEventType::OBSERVE_PURCHASE: return "OBSERVE_PURCHASE";
        case RuntimeEventType::TALK_START: return "TALK_START";
        case RuntimeEventType::TALK_END: return "TALK_END";
        case RuntimeEventType::DECIDE_PURCHASE: return "DECIDE_PURCHASE";
        case RuntimeEventType::QUEUE_ENTER: return "QUEUE_ENTER";
        case RuntimeEventType::PURCHASE: return "PURCHASE";
        case RuntimeEventType::INFLUENCE_DECAY: return "INFLUENCE_DECAY";
        case RuntimeEventType::SIM_END: return "SIM_END";
    }
    return "UNKNOWN";
}

bool trace_level_at_least(TraceLevel current, TraceLevel expected) {
    const auto as_int = [](TraceLevel level) -> int {
        switch (level) {
            case TraceLevel::NONE: return 0;
            case TraceLevel::EVENTS: return 1;
            case TraceLevel::DECISIONS: return 2;
            case TraceLevel::FULL: return 3;
        }
        return 0;
    };
    return as_int(current) >= as_int(expected);
}

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

uint64_t splitmix64(uint64_t value) {
    uint64_t z = value + 0x9E3779B97F4A7C15ULL;
    z = (z ^ (z >> 30U)) * 0xBF58476D1CE4E5B9ULL;
    z = (z ^ (z >> 27U)) * 0x94D049BB133111EBULL;
    return z ^ (z >> 31U);
}

struct RawRandomDraw {
    uint64_t raw_u64 = 0;
    uint64_t draw_index = 0;
    double normalized = 0.0;
};

class DeterministicRng {
public:
    DeterministicRng(uint64_t master_seed, const std::string& scenario_id)
        : master_seed_(master_seed), scenario_hash_(fnv1a_64(scenario_id)) {}

    RawRandomDraw draw(std::string_view stream_key) {
        const std::string key(stream_key);
        uint64_t draw_index = stream_counters_[key]++;
        const uint64_t stream_hash = fnv1a_64(key);

        uint64_t state = splitmix64(master_seed_);
        state = splitmix64(state ^ scenario_hash_);
        state = splitmix64(state ^ stream_hash);
        state = splitmix64(state ^ draw_index);

        RawRandomDraw draw;
        draw.raw_u64 = state;
        draw.draw_index = draw_index;
        draw.normalized = static_cast<double>((state >> 11U) & ((1ULL << 53U) - 1ULL)) *
                          (1.0 / static_cast<double>(1ULL << 53U));
        return draw;
    }

private:
    uint64_t master_seed_ = 0;
    uint64_t scenario_hash_ = 0;
    std::map<std::string, uint64_t> stream_counters_;
};

struct InventoryRuntime {
    int item_index = -1;
    double price = 0.0;
    int64_t stock = 0;
};

struct ShopRuntime {
    int location_index = -1;
    double service_time = 0.0;
    int64_t queue_capacity = 0;
    std::map<int, InventoryRuntime> inventory_by_item;
    std::size_t queue_size = 0;
};

struct MemoryInfluence {
    int item_index = -1;
    std::string tag;
    double strength = 0.0;
    double expires_at = 0.0;
    int source_agent_index = -1;
    uint64_t parent_event_id = 0;
};

struct AgentRuntime {
    int location_index = -1;
    int destination_index = -1;
    double budget = 0.0;
    double movement_speed = 0.0;
    double hunger = 0.0;
    double social_susceptibility = 0.0;
    std::size_t memory_slots = 8;
    std::map<std::string, double> preferences;
    std::vector<MemoryInfluence> memories;
    std::optional<int> intent_item_index;
    std::optional<int> intent_shop_index;
    int last_purchase_item_index = -1;
    bool in_conversation = false;
    int conversation_partner = -1;
    double money_spent = 0.0;
    double time_walking = 0.0;
    double time_talking = 0.0;
    double time_queueing = 0.0;
    std::size_t observed_purchases = 0;
    double influence_received = 0.0;
    std::vector<std::string> visited_locations;
    std::vector<std::string> purchases;
    std::vector<uint64_t> causal_parents;
};

struct RuntimeEvent {
    double timestamp = 0.0;
    int priority = 0;
    std::string handle;
    uint64_t ordinal = 0;
    RuntimeEventType type = RuntimeEventType::SIM_START;
    int agent_index = -1;
    int source_agent_index = -1;
    int shop_index = -1;
    int item_index = -1;
    uint64_t parent_event_id = 0;
    std::map<std::string, std::string> payload;
};

struct RuntimeEventComparator {
    bool operator()(const RuntimeEvent& a, const RuntimeEvent& b) const {
        if (a.timestamp != b.timestamp) return a.timestamp > b.timestamp;
        if (a.priority != b.priority) return a.priority < b.priority;
        if (a.handle != b.handle) return a.handle > b.handle;
        return a.ordinal > b.ordinal;
    }
};

struct CandidateEval {
    int item_index = -1;
    int shop_index = -1;
    double weight = 0.0;
    double base_appeal = 0.0;
    double preference = 0.0;
    double hunger_bonus = 0.0;
    double social_signal = 0.0;
    double tag_association = 0.0;
    double random_jitter = 0.0;
    double price_penalty = 0.0;
    double distance_penalty = 0.0;
    double queue_penalty = 0.0;
    bool affordable = false;
    bool in_stock = false;
};

double distance_between(const schema::LocationDescriptor& a, const schema::LocationDescriptor& b) {
    const double dx = a.x - b.x;
    const double dy = a.y - b.y;
    return std::sqrt((dx * dx) + (dy * dy));
}

std::string to_string_precise(double value) {
    std::ostringstream oss;
    oss << std::fixed << std::setprecision(6) << value;
    return oss.str();
}

std::string fingerprint_agent_state(const std::vector<AgentRuntime>& agents,
                                    const std::vector<ShopRuntime>& shops,
                                    double final_time) {
    constexpr uint64_t kOffsetBasis = 14695981039346656037ULL;
    constexpr uint64_t kPrime = 1099511628211ULL;
    auto mix_bytes = [](uint64_t hash, const unsigned char* bytes, std::size_t size) {
        uint64_t current = hash;
        for (std::size_t i = 0; i < size; ++i) {
            current ^= static_cast<uint64_t>(bytes[i]);
            current *= kPrime;
        }
        return current;
    };

    uint64_t hash = kOffsetBasis;
    for (const auto& agent : agents) {
        hash = mix_bytes(hash, reinterpret_cast<const unsigned char*>(&agent.location_index),
                         sizeof(agent.location_index));
        hash = mix_bytes(hash, reinterpret_cast<const unsigned char*>(&agent.budget), sizeof(agent.budget));
        hash = mix_bytes(hash, reinterpret_cast<const unsigned char*>(&agent.money_spent),
                         sizeof(agent.money_spent));
        hash = mix_bytes(hash, reinterpret_cast<const unsigned char*>(&agent.time_walking),
                         sizeof(agent.time_walking));
        hash = mix_bytes(hash, reinterpret_cast<const unsigned char*>(&agent.time_talking),
                         sizeof(agent.time_talking));
        hash = mix_bytes(hash, reinterpret_cast<const unsigned char*>(&agent.time_queueing),
                         sizeof(agent.time_queueing));
        hash = mix_bytes(hash, reinterpret_cast<const unsigned char*>(&agent.observed_purchases),
                         sizeof(agent.observed_purchases));
        hash = mix_bytes(hash, reinterpret_cast<const unsigned char*>(&agent.influence_received),
                         sizeof(agent.influence_received));
        for (const auto& purchase : agent.purchases) {
            hash = mix_bytes(hash, reinterpret_cast<const unsigned char*>(purchase.data()),
                             purchase.size());
        }
    }
    for (const auto& shop : shops) {
        for (const auto& [item_index, inventory] : shop.inventory_by_item) {
            hash = mix_bytes(hash, reinterpret_cast<const unsigned char*>(&item_index),
                             sizeof(item_index));
            hash = mix_bytes(hash, reinterpret_cast<const unsigned char*>(&inventory.stock),
                             sizeof(inventory.stock));
            hash = mix_bytes(hash, reinterpret_cast<const unsigned char*>(&inventory.price),
                             sizeof(inventory.price));
        }
        hash = mix_bytes(hash, reinterpret_cast<const unsigned char*>(&shop.queue_size),
                         sizeof(shop.queue_size));
    }
    hash = mix_bytes(hash, reinterpret_cast<const unsigned char*>(&final_time), sizeof(final_time));

    std::ostringstream oss;
    oss << std::hex << std::setw(16) << std::setfill('0') << hash;
    return oss.str();
}

RandomDrawTrace make_random_trace(const std::string& stream_key,
                                  const RawRandomDraw& draw,
                                  const std::string& interpreted_result) {
    RandomDrawTrace trace;
    trace.stream_key = stream_key;
    trace.draw_index = draw.draw_index;
    trace.raw_u64 = draw.raw_u64;
    trace.normalized = draw.normalized;
    trace.interpreted_result = interpreted_result;
    return trace;
}

}  // namespace

RunResult run_agent_layer_scenario(const compiler::CompiledScenario& compiled,
                                   const RunOptions& options) {
    RunResult result;
    result.report.scenario_id = compiled.scenario_id;
    result.report.report_type = "RUNTIME";
    result.report.success = true;

    if (!compiled.agent_layer.has_value()) {
        result.report.success = false;
        result.report.errors.push_back("Internal error: run_agent_layer_scenario called without agent_layer");
        return result;
    }

    const auto& layer = *compiled.agent_layer;
    const auto trace_level = options.trace_level;
    const bool record_events = trace_level_at_least(trace_level, TraceLevel::EVENTS);
    const bool record_decisions = trace_level_at_least(trace_level, TraceLevel::DECISIONS);
    const bool record_state_changes = trace_level_at_least(trace_level, TraceLevel::FULL);
    const bool include_causal = options.include_causal_graph;

    const uint64_t runtime_seed = options.seed_override.value_or(compiled.master_seed);
    DeterministicRng rng(runtime_seed, compiled.scenario_id);

    std::vector<AgentRuntime> agents(layer.agents.size());
    std::vector<ShopRuntime> shops(layer.shops.size());

    for (std::size_t i = 0; i < layer.agents.size(); ++i) {
        const auto& src = layer.agents[i];
        auto& dst = agents[i];
        dst.budget = src.budget;
        dst.movement_speed = (src.movement_speed > 0.0) ? src.movement_speed : layer.world.default_walking_speed;
        if (dst.movement_speed <= 0.0) {
            dst.movement_speed = 1.0;
        }
        dst.hunger = src.hunger;
        dst.social_susceptibility = src.social_susceptibility;
        dst.memory_slots = src.memory_slots;
        dst.preferences = src.preferences;
        auto loc_it = layer.location_index.find(src.start_location_ref);
        if (loc_it == layer.location_index.end()) {
            throw std::runtime_error("Unknown agent start location: " + src.start_location_ref);
        }
        dst.location_index = static_cast<int>(loc_it->second);
        dst.destination_index = dst.location_index;
        dst.visited_locations.push_back(src.start_location_ref);
    }

    for (std::size_t i = 0; i < layer.shops.size(); ++i) {
        const auto& src = layer.shops[i];
        auto& dst = shops[i];
        auto loc_it = layer.location_index.find(src.location_ref);
        if (loc_it == layer.location_index.end()) {
            throw std::runtime_error("Unknown shop location: " + src.location_ref);
        }
        dst.location_index = static_cast<int>(loc_it->second);
        dst.service_time = src.service_time;
        dst.queue_capacity = src.queue_capacity;
        dst.queue_size = 0;
        for (const auto& inventory : src.inventory) {
            auto item_it = layer.item_index.find(inventory.item_id);
            if (item_it == layer.item_index.end()) {
                throw std::runtime_error("Unknown inventory item: " + inventory.item_id);
            }
            dst.inventory_by_item[static_cast<int>(item_it->second)] = {
                static_cast<int>(item_it->second),
                inventory.price,
                inventory.stock
            };
        }
    }

    auto max_events = options.max_events;
    if (max_events == 0) {
        max_events = layer.world.max_event_count;
    }
    max_events = std::min(max_events, layer.world.max_event_count);
    if (max_events == 0) {
        max_events = 1;
    }

    const double max_time = (options.max_time > 0.0) ? std::min(options.max_time, layer.world.duration)
                                                      : layer.world.duration;

    uint64_t next_ordinal = 1;
    auto next_handle = [](RuntimeEventType type, uint64_t ordinal) {
        std::ostringstream oss;
        oss << to_event_type_string(type) << "_" << std::setw(8) << std::setfill('0') << ordinal;
        return oss.str();
    };

    std::priority_queue<RuntimeEvent, std::vector<RuntimeEvent>, RuntimeEventComparator> queue;
    auto schedule_event = [&](double timestamp,
                              int priority,
                              RuntimeEventType type,
                              int agent_index,
                              int source_agent_index,
                              int shop_index,
                              int item_index,
                              uint64_t parent_event_id,
                              std::map<std::string, std::string> payload = {}) -> uint64_t {
        RuntimeEvent event;
        event.timestamp = timestamp;
        event.priority = priority;
        event.ordinal = next_ordinal++;
        event.handle = next_handle(type, event.ordinal);
        event.type = type;
        event.agent_index = agent_index;
        event.source_agent_index = source_agent_index;
        event.shop_index = shop_index;
        event.item_index = item_index;
        event.parent_event_id = parent_event_id;
        event.payload = std::move(payload);
        queue.push(std::move(event));
        return next_ordinal - 1;
    };

    schedule_event(0.0, 100, RuntimeEventType::SIM_START, -1, -1, -1, -1, 0);
    schedule_event(max_time, -100, RuntimeEventType::SIM_END, -1, -1, -1, -1, 0);

    auto draw_with_trace = [&](const std::string& stream_key,
                               const std::string& interpretation,
                               std::vector<RandomDrawTrace>& event_draws,
                               std::vector<RandomDrawTrace>* decision_draws = nullptr) {
        const auto draw = rng.draw(stream_key);
        const auto trace = make_random_trace(stream_key, draw, interpretation);
        event_draws.push_back(trace);
        if (decision_draws != nullptr) {
            decision_draws->push_back(trace);
        }
        return draw;
    };

    auto add_state_change = [&](uint64_t event_id,
                                const std::string& entity_type,
                                const std::string& entity_id,
                                const std::string& field_name,
                                const std::string& old_value,
                                const std::string& new_value,
                                const std::vector<uint64_t>& parent_ids) {
        if (!record_state_changes) {
            return;
        }
        StateChangeTrace trace;
        trace.event_id = event_id;
        trace.entity_type = entity_type;
        trace.entity_id = entity_id;
        trace.field_name = field_name;
        trace.old_value = old_value;
        trace.new_value = new_value;
        if (include_causal) {
            trace.causal_parent_event_ids = parent_ids;
        }
        result.state_changes.push_back(std::move(trace));
    };

    auto add_memory = [&](int observer_agent,
                          int item_index,
                          std::string tag,
                          double strength,
                          double expires_at,
                          int source_agent,
                          uint64_t parent_event_id) {
        auto& memory = agents[observer_agent].memories;
        if (memory.size() >= agents[observer_agent].memory_slots) {
            memory.erase(memory.begin());
        }
        memory.push_back(MemoryInfluence{
            .item_index = item_index,
            .tag = std::move(tag),
            .strength = strength,
            .expires_at = expires_at,
            .source_agent_index = source_agent,
            .parent_event_id = parent_event_id
        });
    };

    auto decay_memories = [&](int agent_index, double current_time) {
        auto& memory = agents[agent_index].memories;
        memory.erase(std::remove_if(memory.begin(), memory.end(),
                                    [&](const MemoryInfluence& influence) {
                                        return influence.expires_at <= current_time;
                                    }),
                     memory.end());
    };

    auto find_nearby_agent_same_location = [&](int source_agent) -> int {
        const int location = agents[source_agent].location_index;
        for (int candidate = 0; candidate < static_cast<int>(agents.size()); ++candidate) {
            if (candidate == source_agent) {
                continue;
            }
            if (agents[candidate].location_index == location) {
                return candidate;
            }
        }
        return -1;
    };

    std::size_t processed_events = 0;
    double current_time = 0.0;

    while (!queue.empty()) {
        if (processed_events >= max_events) {
            result.report.success = false;
            result.report.errors.push_back("Runtime halted: max_events limit reached");
            break;
        }

        RuntimeEvent event = queue.top();
        queue.pop();

        if (event.timestamp > max_time) {
            break;
        }
        current_time = event.timestamp;
        ++processed_events;

        std::vector<uint64_t> parent_ids;
        if (include_causal && event.parent_event_id != 0) {
            parent_ids.push_back(event.parent_event_id);
        }
        std::vector<RandomDrawTrace> event_draws;

        switch (event.type) {
            case RuntimeEventType::SIM_START: {
                for (int agent_index = 0; agent_index < static_cast<int>(agents.size()); ++agent_index) {
                    schedule_event(0.0, 10, RuntimeEventType::DECIDE_PURCHASE, agent_index, -1, -1, -1, event.ordinal);
                }
                break;
            }
            case RuntimeEventType::DECIDE_PURCHASE: {
                if (event.agent_index < 0 || event.agent_index >= static_cast<int>(agents.size())) {
                    break;
                }
                auto& agent = agents[event.agent_index];
                decay_memories(event.agent_index, current_time);

                std::vector<CandidateEval> candidates;
                for (int item_index = 0; item_index < static_cast<int>(layer.items.size()); ++item_index) {
                    const auto& item = layer.items[item_index];
                    const double preference = [&]() {
                        const auto preference_it = agent.preferences.find(item.item_id);
                        if (preference_it == agent.preferences.end()) {
                            return 1.0;
                        }
                        return std::max(0.0, preference_it->second);
                    }();

                    double social_signal = 0.0;
                    double tag_association = 0.0;
                    for (const auto& influence : agent.memories) {
                        if (influence.item_index == item_index) {
                            social_signal += influence.strength * agent.social_susceptibility;
                        } else if (!influence.tag.empty() &&
                                   std::find(item.tags.begin(), item.tags.end(), influence.tag) != item.tags.end()) {
                            tag_association += 0.5 * influence.strength * agent.social_susceptibility;
                        }
                    }

                    for (std::size_t shop_index : layer.shops_by_item_index[item_index]) {
                        auto& shop = shops[shop_index];
                        auto inventory_it = shop.inventory_by_item.find(item_index);
                        if (inventory_it == shop.inventory_by_item.end()) {
                            continue;
                        }

                        const auto& inventory = inventory_it->second;
                        const bool in_stock = inventory.stock > 0;
                        const bool affordable = agent.budget >= inventory.price;
                        const auto& source_location = layer.locations[agent.location_index];
                        const auto& target_location = layer.locations[shop.location_index];
                        const double distance = distance_between(source_location, target_location);
                        const double hunger_bonus = agent.hunger * 0.4;
                        const double price_penalty = (inventory.price <= 0.0) ? 0.0 : (inventory.price / 10.0);
                        const double distance_penalty = distance * 0.05;
                        const double queue_penalty = static_cast<double>(shop.queue_size) * 0.2;
                        const auto draw = draw_with_trace(
                            "decision:" + layer.agents[event.agent_index].agent_id,
                            "candidate_jitter",
                            event_draws);
                        const double random_jitter = (draw.normalized * 0.3) - 0.15;

                        const double linear_score =
                            (item.base_appeal * preference) + hunger_bonus + social_signal +
                            tag_association + random_jitter - price_penalty -
                            distance_penalty - queue_penalty;
                        const double weight = std::max(0.001, linear_score);

                        CandidateEval candidate;
                        candidate.item_index = item_index;
                        candidate.shop_index = static_cast<int>(shop_index);
                        candidate.weight = weight;
                        candidate.base_appeal = item.base_appeal;
                        candidate.preference = preference;
                        candidate.hunger_bonus = hunger_bonus;
                        candidate.social_signal = social_signal;
                        candidate.tag_association = tag_association;
                        candidate.random_jitter = random_jitter;
                        candidate.price_penalty = price_penalty;
                        candidate.distance_penalty = distance_penalty;
                        candidate.queue_penalty = queue_penalty;
                        candidate.affordable = affordable;
                        candidate.in_stock = in_stock;
                        candidates.push_back(candidate);
                    }
                }

                std::sort(candidates.begin(), candidates.end(), [&](const CandidateEval& lhs, const CandidateEval& rhs) {
                    const auto& lhs_item = layer.items[static_cast<std::size_t>(lhs.item_index)].item_id;
                    const auto& rhs_item = layer.items[static_cast<std::size_t>(rhs.item_index)].item_id;
                    if (lhs_item != rhs_item) {
                        return lhs_item < rhs_item;
                    }
                    const auto& lhs_shop = layer.shops[static_cast<std::size_t>(lhs.shop_index)].shop_id;
                    const auto& rhs_shop = layer.shops[static_cast<std::size_t>(rhs.shop_index)].shop_id;
                    return lhs_shop < rhs_shop;
                });

                std::vector<CandidateEval> valid;
                valid.reserve(candidates.size());
                for (const auto& candidate : candidates) {
                    if (candidate.affordable && candidate.in_stock) {
                        valid.push_back(candidate);
                    }
                }

                std::optional<CandidateEval> selected;
                double total_weight = 0.0;
                for (const auto& candidate : valid) {
                    total_weight += candidate.weight;
                }

                double draw_value = 0.0;
                double draw_scaled = 0.0;
                std::vector<RandomDrawTrace> decision_draws;
                if (!valid.empty()) {
                    const auto draw = draw_with_trace(
                        "decision:" + layer.agents[event.agent_index].agent_id,
                        "weighted_choice",
                        event_draws,
                        &decision_draws);
                    draw_value = draw.normalized;
                    draw_scaled = draw_value * total_weight;

                    double cumulative = 0.0;
                    for (const auto& candidate : valid) {
                        cumulative += candidate.weight;
                        if (draw_scaled <= cumulative) {
                            selected = candidate;
                            break;
                        }
                    }
                    if (!selected.has_value()) {
                        selected = valid.back();
                    }
                }

                if (record_decisions) {
                    DecisionTrace decision_trace;
                    decision_trace.decision_id = event.ordinal;
                    decision_trace.event_id = event.ordinal;
                    decision_trace.decision_type = "purchase";
                    decision_trace.agent_id = layer.agents[event.agent_index].agent_id;
                    decision_trace.random_draw = draw_value;
                    decision_trace.random_draw_scaled = draw_scaled;
                    for (const auto& candidate : candidates) {
                        DecisionCandidateTrace candidate_trace;
                        candidate_trace.item_id = layer.items[static_cast<std::size_t>(candidate.item_index)].item_id;
                        candidate_trace.shop_id = layer.shops[static_cast<std::size_t>(candidate.shop_index)].shop_id;
                        candidate_trace.weight = candidate.weight;
                        candidate_trace.base_appeal = candidate.base_appeal;
                        candidate_trace.preference = candidate.preference;
                        candidate_trace.hunger_bonus = candidate.hunger_bonus;
                        candidate_trace.social_signal = candidate.social_signal;
                        candidate_trace.tag_association = candidate.tag_association;
                        candidate_trace.random_jitter = candidate.random_jitter;
                        candidate_trace.price_penalty = candidate.price_penalty;
                        candidate_trace.distance_penalty = candidate.distance_penalty;
                        candidate_trace.queue_penalty = candidate.queue_penalty;
                        candidate_trace.affordable = candidate.affordable;
                        candidate_trace.in_stock = candidate.in_stock;
                        decision_trace.candidates.push_back(std::move(candidate_trace));
                    }
                    if (selected.has_value()) {
                        decision_trace.selected_item_id =
                            layer.items[static_cast<std::size_t>(selected->item_index)].item_id;
                        decision_trace.selected_shop_id =
                            layer.shops[static_cast<std::size_t>(selected->shop_index)].shop_id;
                    }
                    if (include_causal) {
                        decision_trace.causal_parent_event_ids = parent_ids;
                    }
                    decision_trace.random_draws = std::move(decision_draws);
                    result.decisions.push_back(std::move(decision_trace));
                }

                if (!selected.has_value()) {
                    schedule_event(current_time + 1.0, 5, RuntimeEventType::DECIDE_PURCHASE,
                                   event.agent_index, -1, -1, -1, event.ordinal);
                    break;
                }

                agent.intent_item_index = selected->item_index;
                agent.intent_shop_index = selected->shop_index;
                schedule_event(current_time, 8, RuntimeEventType::MOVE_START,
                               event.agent_index, -1, selected->shop_index, selected->item_index, event.ordinal);
                break;
            }
            case RuntimeEventType::MOVE_START: {
                if (event.agent_index < 0 || event.shop_index < 0) {
                    break;
                }
                auto& agent = agents[event.agent_index];
                const auto& shop = shops[static_cast<std::size_t>(event.shop_index)];
                const auto& current_location = layer.locations[static_cast<std::size_t>(agent.location_index)];
                const auto& destination = layer.locations[static_cast<std::size_t>(shop.location_index)];
                const double distance = distance_between(current_location, destination);
                const auto jitter_draw = draw_with_trace(
                    "movement:" + layer.agents[event.agent_index].agent_id,
                    "congestion_jitter",
                    event_draws);
                const double jitter = jitter_draw.normalized * 0.5;
                const double travel_time = (distance / std::max(0.1, agent.movement_speed)) + jitter;
                agent.destination_index = shop.location_index;
                schedule_event(current_time + travel_time, 8, RuntimeEventType::MOVE_ARRIVE,
                               event.agent_index, -1, event.shop_index, event.item_index, event.ordinal,
                               {{"travel_time", to_string_precise(travel_time)}});
                break;
            }
            case RuntimeEventType::MOVE_ARRIVE: {
                if (event.agent_index < 0 || event.shop_index < 0) {
                    break;
                }
                auto& agent = agents[event.agent_index];
                const auto old_location = agent.location_index;
                agent.location_index = shops[static_cast<std::size_t>(event.shop_index)].location_index;
                const auto travel_time_it = event.payload.find("travel_time");
                if (travel_time_it != event.payload.end()) {
                    const double travel_time = std::stod(travel_time_it->second);
                    agent.time_walking += travel_time;
                }
                const auto& location_name =
                    layer.locations[static_cast<std::size_t>(agent.location_index)].location_id;
                agent.visited_locations.push_back(location_name);
                add_state_change(event.ordinal,
                                 "agent",
                                 layer.agents[event.agent_index].agent_id,
                                 "location",
                                 layer.locations[static_cast<std::size_t>(old_location)].location_id,
                                 location_name,
                                 parent_ids);

                schedule_event(current_time, 7, RuntimeEventType::QUEUE_ENTER,
                               event.agent_index, -1, event.shop_index, event.item_index, event.ordinal);
                break;
            }
            case RuntimeEventType::QUEUE_ENTER: {
                if (event.agent_index < 0 || event.shop_index < 0 || event.item_index < 0) {
                    break;
                }
                auto& shop = shops[static_cast<std::size_t>(event.shop_index)];
                auto& agent = agents[event.agent_index];
                auto inventory_it = shop.inventory_by_item.find(event.item_index);
                if (inventory_it == shop.inventory_by_item.end() ||
                    inventory_it->second.stock <= 0 ||
                    inventory_it->second.price > agent.budget) {
                    schedule_event(current_time + 0.8, 6, RuntimeEventType::DECIDE_PURCHASE,
                                   event.agent_index, -1, -1, -1, event.ordinal);
                    break;
                }
                if (shop.queue_capacity > 0 &&
                    static_cast<int64_t>(shop.queue_size) >= shop.queue_capacity) {
                    schedule_event(current_time + 0.8, 6, RuntimeEventType::DECIDE_PURCHASE,
                                   event.agent_index, -1, -1, -1, event.ordinal);
                    break;
                }

                shop.queue_size += 1;
                const auto wait_draw = draw_with_trace(
                    "shop_service:" + layer.shops[static_cast<std::size_t>(event.shop_index)].shop_id,
                    "service_jitter",
                    event_draws);
                const double service_delay = shop.service_time + (wait_draw.normalized * 0.7);
                agent.time_queueing += service_delay;
                schedule_event(current_time + service_delay, 7, RuntimeEventType::PURCHASE,
                               event.agent_index, -1, event.shop_index, event.item_index, event.ordinal);
                break;
            }
            case RuntimeEventType::PURCHASE: {
                if (event.agent_index < 0 || event.shop_index < 0 || event.item_index < 0) {
                    break;
                }
                auto& shop = shops[static_cast<std::size_t>(event.shop_index)];
                auto& agent = agents[event.agent_index];
                if (shop.queue_size > 0) {
                    shop.queue_size -= 1;
                }
                auto inventory_it = shop.inventory_by_item.find(event.item_index);
                if (inventory_it == shop.inventory_by_item.end()) {
                    schedule_event(current_time + 1.0, 5, RuntimeEventType::DECIDE_PURCHASE,
                                   event.agent_index, -1, -1, -1, event.ordinal);
                    break;
                }
                auto& inventory = inventory_it->second;
                if (inventory.stock <= 0 || inventory.price > agent.budget) {
                    schedule_event(current_time + 1.0, 5, RuntimeEventType::DECIDE_PURCHASE,
                                   event.agent_index, -1, -1, -1, event.ordinal);
                    break;
                }

                const double old_budget = agent.budget;
                const int64_t old_stock = inventory.stock;
                agent.budget -= inventory.price;
                agent.money_spent += inventory.price;
                inventory.stock -= 1;
                agent.last_purchase_item_index = event.item_index;
                const auto& item_id = layer.items[static_cast<std::size_t>(event.item_index)].item_id;
                agent.purchases.push_back(item_id);

                add_state_change(event.ordinal,
                                 "agent",
                                 layer.agents[event.agent_index].agent_id,
                                 "budget",
                                 to_string_precise(old_budget),
                                 to_string_precise(agent.budget),
                                 parent_ids);
                add_state_change(event.ordinal,
                                 "shop",
                                 layer.shops[static_cast<std::size_t>(event.shop_index)].shop_id,
                                 "stock:" + item_id,
                                 std::to_string(old_stock),
                                 std::to_string(inventory.stock),
                                 parent_ids);

                for (int observer = 0; observer < static_cast<int>(agents.size()); ++observer) {
                    if (observer == event.agent_index) {
                        continue;
                    }
                    if (agents[observer].location_index != agent.location_index) {
                        continue;
                    }
                    schedule_event(current_time + 0.1,
                                   6,
                                   RuntimeEventType::OBSERVE_PURCHASE,
                                   observer,
                                   event.agent_index,
                                   event.shop_index,
                                   event.item_index,
                                   event.ordinal);
                }

                const int talk_partner = find_nearby_agent_same_location(event.agent_index);
                if (talk_partner >= 0 && !agents[event.agent_index].in_conversation &&
                    !agents[talk_partner].in_conversation) {
                    const auto talk_draw = draw_with_trace(
                        "conversation:" + layer.agents[event.agent_index].agent_id + ":" +
                            layer.agents[talk_partner].agent_id,
                        "conversation_chance",
                        event_draws);
                    if (talk_draw.normalized < 0.35) {
                        schedule_event(current_time + 0.2, 6, RuntimeEventType::TALK_START,
                                       event.agent_index, talk_partner, event.shop_index, event.item_index, event.ordinal);
                    }
                }

                if (agent.budget > 0.01) {
                    schedule_event(current_time + 1.0, 5, RuntimeEventType::DECIDE_PURCHASE,
                                   event.agent_index, -1, -1, -1, event.ordinal);
                }
                break;
            }
            case RuntimeEventType::OBSERVE_PURCHASE: {
                if (event.agent_index < 0 || event.item_index < 0 || event.source_agent_index < 0) {
                    break;
                }
                auto& agent = agents[event.agent_index];
                agent.observed_purchases += 1;
                const double base_signal = 0.4 + (agent.social_susceptibility * 0.6);
                agent.influence_received += base_signal;
                add_memory(event.agent_index,
                           event.item_index,
                           "",
                           base_signal,
                           current_time + 5.0,
                           event.source_agent_index,
                           event.ordinal);
                const auto& item = layer.items[static_cast<std::size_t>(event.item_index)];
                for (const auto& tag : item.tags) {
                    add_memory(event.agent_index,
                               -1,
                               tag,
                               base_signal * 0.5,
                               current_time + 5.0,
                               event.source_agent_index,
                               event.ordinal);
                }
                schedule_event(current_time + 5.0, 3, RuntimeEventType::INFLUENCE_DECAY,
                               event.agent_index, -1, -1, -1, event.ordinal);
                if (!agents[event.agent_index].in_conversation) {
                    schedule_event(current_time + 0.6, 5, RuntimeEventType::DECIDE_PURCHASE,
                                   event.agent_index, -1, -1, -1, event.ordinal);
                }
                break;
            }
            case RuntimeEventType::TALK_START: {
                if (event.agent_index < 0 || event.source_agent_index < 0) {
                    break;
                }
                auto& a = agents[event.agent_index];
                auto& b = agents[event.source_agent_index];
                if (a.in_conversation || b.in_conversation) {
                    break;
                }
                a.in_conversation = true;
                b.in_conversation = true;
                a.conversation_partner = event.source_agent_index;
                b.conversation_partner = event.agent_index;
                const auto duration_draw = draw_with_trace(
                    "conversation:" + layer.agents[event.agent_index].agent_id + ":" +
                        layer.agents[event.source_agent_index].agent_id,
                    "conversation_duration",
                    event_draws);
                const double duration = 0.4 + (duration_draw.normalized * 1.2);
                schedule_event(current_time + duration, 6, RuntimeEventType::TALK_END,
                               event.agent_index, event.source_agent_index, event.shop_index, event.item_index, event.ordinal,
                               {{"duration", to_string_precise(duration)}});
                break;
            }
            case RuntimeEventType::TALK_END: {
                if (event.agent_index < 0 || event.source_agent_index < 0) {
                    break;
                }
                auto& a = agents[event.agent_index];
                auto& b = agents[event.source_agent_index];
                const auto duration_it = event.payload.find("duration");
                const double duration = (duration_it == event.payload.end()) ? 0.0 : std::stod(duration_it->second);
                a.time_talking += duration;
                b.time_talking += duration;
                a.in_conversation = false;
                b.in_conversation = false;
                a.conversation_partner = -1;
                b.conversation_partner = -1;

                if (a.last_purchase_item_index >= 0) {
                    const double signal = 0.3 + (b.social_susceptibility * 0.4);
                    add_memory(event.source_agent_index,
                               a.last_purchase_item_index,
                               "",
                               signal,
                               current_time + 5.0,
                               event.agent_index,
                               event.ordinal);
                    schedule_event(current_time + 5.0, 3, RuntimeEventType::INFLUENCE_DECAY,
                                   event.source_agent_index, -1, -1, -1, event.ordinal);
                }

                schedule_event(current_time + 0.4, 5, RuntimeEventType::DECIDE_PURCHASE,
                               event.agent_index, -1, -1, -1, event.ordinal);
                schedule_event(current_time + 0.4, 5, RuntimeEventType::DECIDE_PURCHASE,
                               event.source_agent_index, -1, -1, -1, event.ordinal);
                break;
            }
            case RuntimeEventType::INFLUENCE_DECAY: {
                if (event.agent_index < 0) {
                    break;
                }
                decay_memories(event.agent_index, current_time);
                break;
            }
            case RuntimeEventType::SIM_END:
                break;
        }

        if (record_events) {
            EventTrace trace;
            trace.event_id = event.ordinal;
            trace.event_type = to_event_type_string(event.type);
            trace.event_handle = event.handle;
            trace.timestamp = event.timestamp;
            trace.priority = event.priority;
            if (event.agent_index >= 0) {
                trace.actor_id = layer.agents[static_cast<std::size_t>(event.agent_index)].agent_id;
            }
            if (event.shop_index >= 0) {
                trace.shop_id = layer.shops[static_cast<std::size_t>(event.shop_index)].shop_id;
            }
            if (event.item_index >= 0) {
                trace.item_id = layer.items[static_cast<std::size_t>(event.item_index)].item_id;
            }
            if (include_causal) {
                trace.causal_parent_event_ids = parent_ids;
            }
            trace.payload = event.payload;
            trace.random_draws = std::move(event_draws);
            result.events.push_back(std::move(trace));
        }

        if (event.type == RuntimeEventType::SIM_END) {
            break;
        }
    }

    if (options.include_final_state) {
        for (std::size_t i = 0; i < agents.size(); ++i) {
            AgentFinalState final_agent;
            final_agent.agent_id = layer.agents[i].agent_id;
            final_agent.visited_locations = agents[i].visited_locations;
            final_agent.purchases = agents[i].purchases;
            final_agent.money_spent = agents[i].money_spent;
            final_agent.time_walking = agents[i].time_walking;
            final_agent.time_talking = agents[i].time_talking;
            final_agent.time_queueing = agents[i].time_queueing;
            final_agent.observed_purchases = agents[i].observed_purchases;
            final_agent.influence_received = agents[i].influence_received;
            if (include_causal) {
                final_agent.causal_parent_event_ids = agents[i].causal_parents;
            }
            result.final_state.agents.push_back(std::move(final_agent));
        }
        for (std::size_t i = 0; i < shops.size(); ++i) {
            ShopFinalState final_shop;
            final_shop.shop_id = layer.shops[i].shop_id;
            final_shop.queue_size = shops[i].queue_size;
            for (const auto& [item_index, inventory] : shops[i].inventory_by_item) {
                final_shop.remaining_stock[layer.items[static_cast<std::size_t>(item_index)].item_id] =
                    inventory.stock;
            }
            result.final_state.shops.push_back(std::move(final_shop));
        }
    }

    result.final_state.summary["duration"] = to_string_precise(current_time);
    result.final_state.summary["events_processed"] = std::to_string(processed_events);
    result.final_state.summary["seed"] = std::to_string(runtime_seed);

    result.report.statistics["processed_events"] = std::to_string(processed_events);
    result.report.statistics["remaining_events"] = std::to_string(queue.size());
    result.report.statistics["final_time"] = to_string_precise(current_time);
    result.report.statistics["seed"] = std::to_string(runtime_seed);
    result.report.statistics["state_fingerprint"] =
        fingerprint_agent_state(agents, shops, current_time);
    result.report.statistics["total_agents"] = std::to_string(layer.agents.size());
    result.report.statistics["total_shops"] = std::to_string(layer.shops.size());
    result.report.statistics["total_items"] = std::to_string(layer.items.size());
    result.report.statistics["trace_events"] = std::to_string(result.events.size());
    result.report.statistics["trace_decisions"] = std::to_string(result.decisions.size());
    result.report.statistics["trace_state_changes"] = std::to_string(result.state_changes.size());

    if (result.report.success) {
        result.report.info_messages.push_back("Agent-layer simulation executed successfully");
    }

    return result;
}

}  // namespace noisiax::engine
