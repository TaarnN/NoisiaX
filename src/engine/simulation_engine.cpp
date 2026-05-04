#include "noisiax/engine/simulation_engine.hpp"
#include <algorithm>
#include <sstream>
#include <stdexcept>
#include <cstring>
#include <cmath>

namespace noisiax::engine {

void SimulationState::initialize(const compiler::CompiledScenario& compiled, uint64_t seed) {
    // Clear existing state
    int_buffer_.clear();
    float_buffer_.clear();
    string_buffer_.clear();
    bool_buffer_.clear();
    stale_flags_.clear();
    variable_to_int_index_.clear();
    variable_to_float_index_.clear();
    variable_to_string_index_.clear();
    variable_to_bool_index_.clear();
    
    current_time_ = 0.0;
    
    std::size_t int_size = 0;
    std::size_t float_size = 0;
    std::size_t string_size = 0;
    std::size_t bool_size = 0;

    // Initialize variable mappings and derive buffer sizes from compiled offsets.
    for (const auto& [var_id, handle] : compiled.parameter_handles) {
        stale_flags_[var_id] = false;
        
        switch (handle.type) {
            case schema::VariableType::INTEGER:
                variable_to_int_index_[var_id] = handle.buffer_offset;
                int_size = std::max(int_size, handle.buffer_offset + 1);
                break;
            case schema::VariableType::FLOAT:
                variable_to_float_index_[var_id] = handle.buffer_offset;
                float_size = std::max(float_size, handle.buffer_offset + 1);
                break;
            case schema::VariableType::STRING:
                variable_to_string_index_[var_id] = handle.buffer_offset;
                string_size = std::max(string_size, handle.buffer_offset + 1);
                break;
            case schema::VariableType::BOOLEAN:
                variable_to_bool_index_[var_id] = handle.buffer_offset;
                bool_size = std::max(bool_size, handle.buffer_offset + 1);
                break;
            default:
                break;
        }
    }

    int_buffer_.assign(int_size, 0);
    float_buffer_.assign(float_size, 0.0);
    string_buffer_.assign(string_size, "");
    bool_buffer_.assign(bool_size, false);
    
    (void)seed;  // Seed can be used for deterministic initialization of default values
}

std::vector<int64_t>& SimulationState::int_buffer() {
    return int_buffer_;
}

const std::vector<int64_t>& SimulationState::int_buffer() const {
    return int_buffer_;
}

std::vector<double>& SimulationState::float_buffer() {
    return float_buffer_;
}

const std::vector<double>& SimulationState::float_buffer() const {
    return float_buffer_;
}

std::vector<std::string>& SimulationState::string_buffer() {
    return string_buffer_;
}

const std::vector<std::string>& SimulationState::string_buffer() const {
    return string_buffer_;
}

std::vector<bool>& SimulationState::bool_buffer() {
    return bool_buffer_;
}

const std::vector<bool>& SimulationState::bool_buffer() const {
    return bool_buffer_;
}

void SimulationState::mark_stale(const std::string& variable_id) {
    stale_flags_[variable_id] = true;
}

void SimulationState::clear_stale(const std::string& variable_id) {
    stale_flags_[variable_id] = false;
}

bool SimulationState::is_stale(const std::string& variable_id) const {
    auto it = stale_flags_.find(variable_id);
    return it != stale_flags_.end() && it->second;
}

const std::map<std::string, bool>& SimulationState::stale_flags() const {
    return stale_flags_;
}

std::map<std::string, bool>& SimulationState::stale_flags() {
    return stale_flags_;
}

double SimulationState::current_time() const {
    return current_time_;
}

void SimulationState::set_current_time(double new_time) {
    current_time_ = new_time;
}

std::string SimulationState::create_checkpoint() const {
    std::ostringstream oss;
    
    // Write current time
    oss.write(reinterpret_cast<const char*>(&current_time_), sizeof(current_time_));
    
    // Write buffer sizes
    std::size_t int_size = int_buffer_.size();
    std::size_t float_size = float_buffer_.size();
    std::size_t string_size = string_buffer_.size();
    std::size_t bool_size = bool_buffer_.size();
    
    oss.write(reinterpret_cast<const char*>(&int_size), sizeof(int_size));
    oss.write(reinterpret_cast<const char*>(&float_size), sizeof(float_size));
    oss.write(reinterpret_cast<const char*>(&string_size), sizeof(string_size));
    oss.write(reinterpret_cast<const char*>(&bool_size), sizeof(bool_size));
    
    // Write buffer contents
    if (!int_buffer_.empty()) {
        oss.write(reinterpret_cast<const char*>(int_buffer_.data()), int_size * sizeof(int64_t));
    }
    if (!float_buffer_.empty()) {
        oss.write(reinterpret_cast<const char*>(float_buffer_.data()), float_size * sizeof(double));
    }
    
    // Write strings with length prefixes
    for (const auto& str : string_buffer_) {
        std::size_t len = str.size();
        oss.write(reinterpret_cast<const char*>(&len), sizeof(len));
        oss.write(str.data(), len);
    }
    
    // Write bools as bytes
    for (bool b : bool_buffer_) {
        char byte = b ? 1 : 0;
        oss.write(&byte, 1);
    }
    
    // Write stale flags count and entries
    std::size_t stale_count = stale_flags_.size();
    oss.write(reinterpret_cast<const char*>(&stale_count), sizeof(stale_count));
    for (const auto& [var_id, is_stale] : stale_flags_) {
        std::size_t id_len = var_id.size();
        oss.write(reinterpret_cast<const char*>(&id_len), sizeof(id_len));
        oss.write(var_id.data(), id_len);
        char flag = is_stale ? 1 : 0;
        oss.write(&flag, 1);
    }
    
    return oss.str();
}

void SimulationState::restore_checkpoint(const std::string& checkpoint_data) {
    std::istringstream iss(checkpoint_data);
    
    // Read current time
    iss.read(reinterpret_cast<char*>(&current_time_), sizeof(current_time_));
    
    // Read buffer sizes
    std::size_t int_size, float_size, string_size, bool_size;
    iss.read(reinterpret_cast<char*>(&int_size), sizeof(int_size));
    iss.read(reinterpret_cast<char*>(&float_size), sizeof(float_size));
    iss.read(reinterpret_cast<char*>(&string_size), sizeof(string_size));
    iss.read(reinterpret_cast<char*>(&bool_size), sizeof(bool_size));
    
    // Resize buffers
    int_buffer_.resize(int_size);
    float_buffer_.resize(float_size);
    string_buffer_.resize(string_size);
    bool_buffer_.resize(bool_size);
    
    // Read buffer contents
    if (int_size > 0) {
        iss.read(reinterpret_cast<char*>(int_buffer_.data()), int_size * sizeof(int64_t));
    }
    if (float_size > 0) {
        iss.read(reinterpret_cast<char*>(float_buffer_.data()), float_size * sizeof(double));
    }
    
    // Read strings with length prefixes
    for (std::size_t i = 0; i < string_size; ++i) {
        std::size_t len;
        iss.read(reinterpret_cast<char*>(&len), sizeof(len));
        string_buffer_[i].resize(len);
        iss.read(&string_buffer_[i][0], len);
    }
    
    // Read bools as bytes
    for (std::size_t i = 0; i < bool_size; ++i) {
        char byte;
        iss.read(&byte, 1);
        bool_buffer_[i] = (byte != 0);
    }
    
    // Read stale flags
    std::size_t stale_count;
    iss.read(reinterpret_cast<char*>(&stale_count), sizeof(stale_count));
    stale_flags_.clear();
    for (std::size_t i = 0; i < stale_count; ++i) {
        std::size_t id_len;
        iss.read(reinterpret_cast<char*>(&id_len), sizeof(id_len));
        std::string var_id(id_len, '\0');
        iss.read(&var_id[0], id_len);
        char flag;
        iss.read(&flag, 1);
        stale_flags_[var_id] = (flag != 0);
    }
}

// DependencyGraph implementation

void DependencyGraph::build_from_compiled(const compiler::CompiledScenario& compiled) {
    adjacency_list_ = compiled.adjacency_lists;
    parameter_handles_ = compiled.parameter_handles;
    
    // Build reverse adjacency
    reverse_adjacency_.clear();
    for (const auto& [source, targets] : adjacency_list_) {
        for (const auto& entry : targets) {
            reverse_adjacency_[entry.target_variable].push_back({source, entry});
        }
    }
    
    // Copy propagation functions
    functions_ = compiled.propagation_functions;
}

void DependencyGraph::register_function(const std::string& function_id, PropagationFunc func) {
    functions_[function_id] = std::move(func);
}

void DependencyGraph::push_invalidation(const std::string& source_id, SimulationState& state) {
    std::vector<std::string> to_visit;
    std::set<std::string> visited;
    
    to_visit.push_back(source_id);
    
    while (!to_visit.empty()) {
        std::string current = to_visit.back();
        to_visit.pop_back();
        
        if (visited.count(current)) continue;
        visited.insert(current);
        
        // Mark as stale
        state.mark_stale(current);
        
        // Add downstream dependents
        auto it = adjacency_list_.find(current);
        if (it != adjacency_list_.end()) {
            for (const auto& entry : it->second) {
                if (!visited.count(entry.target_variable)) {
                    to_visit.push_back(entry.target_variable);
                }
            }
        }
    }
}

bool DependencyGraph::pull_recompute(const std::string& target_id, SimulationState& state) {
    if (!state.is_stale(target_id)) {
        return false;  // Not stale, no recomputation needed
    }

    std::set<std::string> active_stack;
    std::function<bool(const std::string&)> recompute = [&](const std::string& variable_id) -> bool {
        if (!state.is_stale(variable_id)) {
            return false;
        }

        if (active_stack.contains(variable_id)) {
            throw std::runtime_error("Cycle detected during pull recompute at variable: " + variable_id);
        }
        active_stack.insert(variable_id);

        // Recompute upstream dependencies first.
        auto upstream_it = reverse_adjacency_.find(variable_id);
        if (upstream_it != reverse_adjacency_.end()) {
            for (const auto& reverse_entry : upstream_it->second) {
                recompute(reverse_entry.source_variable);
            }
        }

        double target_value = get_numeric_value(variable_id, state);
        bool updated = false;

        // Apply propagation functions from each upstream source into this target.
        if (upstream_it != reverse_adjacency_.end()) {
            for (const auto& reverse_entry : upstream_it->second) {
                const auto& edge = reverse_entry.edge;
                auto function_it = functions_.find(edge.propagation_function_id);
                if (function_it == functions_.end()) {
                    throw std::runtime_error("Missing propagation function: " + edge.propagation_function_id);
                }

                const double source_value = get_numeric_value(reverse_entry.source_variable, state);
                function_it->second(target_value, source_value, edge.weight);
                updated = true;
            }
        }

        if (updated) {
            set_numeric_value(variable_id, target_value, state);
        }

        state.clear_stale(variable_id);
        active_stack.erase(variable_id);
        return true;
    };

    return recompute(target_id);
}

std::vector<std::string> DependencyGraph::get_downstream(const std::string& variable_id) const {
    std::vector<std::string> result;
    std::set<std::string> visited;
    traverse_downstream(variable_id, result, visited);
    return result;
}

std::vector<std::string> DependencyGraph::get_upstream(const std::string& variable_id) const {
    std::vector<std::string> result;
    std::set<std::string> visited;
    traverse_upstream(variable_id, result, visited);
    return result;
}

std::vector<std::string> DependencyGraph::compute_stale_closure(const SimulationState& state) const {
    std::vector<std::string> result;
    
    for (const auto& [var_id, is_stale] : state.stale_flags()) {
        if (is_stale) {
            result.push_back(var_id);
        }
    }
    
    return result;
}

void DependencyGraph::traverse_downstream(const std::string& start,
                                          std::vector<std::string>& result,
                                          std::set<std::string>& visited) const {
    if (visited.count(start)) return;
    visited.insert(start);
    
    auto it = adjacency_list_.find(start);
    if (it != adjacency_list_.end()) {
        for (const auto& entry : it->second) {
            result.push_back(entry.target_variable);
            traverse_downstream(entry.target_variable, result, visited);
        }
    }
}

void DependencyGraph::traverse_upstream(const std::string& start,
                                        std::vector<std::string>& result,
                                        std::set<std::string>& visited) const {
    if (visited.count(start)) return;
    visited.insert(start);
    
    auto it = reverse_adjacency_.find(start);
    if (it != reverse_adjacency_.end()) {
        for (const auto& reverse_entry : it->second) {
            result.push_back(reverse_entry.source_variable);
            traverse_upstream(reverse_entry.source_variable, result, visited);
        }
    }
}

double DependencyGraph::get_numeric_value(const std::string& variable_id, const SimulationState& state) const {
    auto handle_it = parameter_handles_.find(variable_id);
    if (handle_it == parameter_handles_.end()) {
        return 0.0;
    }

    const auto& handle = handle_it->second;
    switch (handle.type) {
        case schema::VariableType::INTEGER:
            if (handle.buffer_offset < state.int_buffer().size()) {
                return static_cast<double>(state.int_buffer()[handle.buffer_offset]);
            }
            break;
        case schema::VariableType::FLOAT:
            if (handle.buffer_offset < state.float_buffer().size()) {
                return state.float_buffer()[handle.buffer_offset];
            }
            break;
        case schema::VariableType::BOOLEAN:
            if (handle.buffer_offset < state.bool_buffer().size()) {
                return state.bool_buffer()[handle.buffer_offset] ? 1.0 : 0.0;
            }
            break;
        default:
            break;
    }

    return 0.0;
}

bool DependencyGraph::set_numeric_value(const std::string& variable_id, double value, SimulationState& state) const {
    auto handle_it = parameter_handles_.find(variable_id);
    if (handle_it == parameter_handles_.end()) {
        return false;
    }

    const auto& handle = handle_it->second;
    switch (handle.type) {
        case schema::VariableType::INTEGER:
            if (handle.buffer_offset < state.int_buffer().size()) {
                state.int_buffer()[handle.buffer_offset] = static_cast<int64_t>(std::llround(value));
                return true;
            }
            break;
        case schema::VariableType::FLOAT:
            if (handle.buffer_offset < state.float_buffer().size()) {
                state.float_buffer()[handle.buffer_offset] = value;
                return true;
            }
            break;
        case schema::VariableType::BOOLEAN:
            if (handle.buffer_offset < state.bool_buffer().size()) {
                state.bool_buffer()[handle.buffer_offset] = (value != 0.0);
                return true;
            }
            break;
        default:
            break;
    }

    return false;
}

} // namespace noisiax::engine
