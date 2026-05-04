#pragma once

#include "noisiax/schema/scenario_schema.hpp"
#include "noisiax/compiler/scenario_compiler.hpp"
#include <vector>
#include <map>
#include <string>
#include <cstdint>
#include <functional>

namespace noisiax::engine {

/**
 * @brief Typed SoA (Structure of Arrays) buffers for simulation state
 * 
 * Provides contiguous memory layouts for different variable types
 * to enable efficient vectorized operations and cache-friendly access.
 */
class SimulationState {
public:
    SimulationState() = default;
    ~SimulationState() = default;
    
    /**
     * @brief Initialize state from compiled scenario
     * @param compiled The compiled scenario artifact
     * @param seed Master seed for deterministic initialization
     */
    void initialize(const compiler::CompiledScenario& compiled, uint64_t seed);
    
    /**
     * @brief Get integer buffer reference
     */
    std::vector<int64_t>& int_buffer();
    const std::vector<int64_t>& int_buffer() const;
    
    /**
     * @brief Get float buffer reference
     */
    std::vector<double>& float_buffer();
    const std::vector<double>& float_buffer() const;
    
    /**
     * @brief Get string buffer reference
     */
    std::vector<std::string>& string_buffer();
    const std::vector<std::string>& string_buffer() const;
    
    /**
     * @brief Get boolean buffer reference
     */
    std::vector<bool>& bool_buffer();
    const std::vector<bool>& bool_buffer() const;
    
    /**
     * @brief Mark a variable as stale (push invalidation)
     * @param variable_id The variable to mark stale
     */
    void mark_stale(const std::string& variable_id);
    
    /**
     * @brief Check if a variable is stale
     * @param variable_id The variable to check
     */
    bool is_stale(const std::string& variable_id) const;
    
    /**
     * @brief Get stale flag map
     */
    const std::map<std::string, bool>& stale_flags() const;
    std::map<std::string, bool>& stale_flags();
    
    /**
     * @brief Get current simulation time
     */
    double current_time() const;
    
    /**
     * @brief Advance simulation time
     * @param new_time The new simulation time
     */
    void set_current_time(double new_time);
    
    /**
     * @brief Create a checkpoint of current state
     * @return Serialized checkpoint data
     */
    std::string create_checkpoint() const;
    
    /**
     * @brief Restore state from checkpoint
     * @param checkpoint_data Serialized checkpoint data
     */
    void restore_checkpoint(const std::string& checkpoint_data);
    
private:
    std::vector<int64_t> int_buffer_;
    std::vector<double> float_buffer_;
    std::vector<std::string> string_buffer_;
    std::vector<bool> bool_buffer_;
    std::map<std::string, bool> stale_flags_;
    double current_time_ = 0.0;
    
    // Mapping from variable_id to buffer index
    std::map<std::string, std::size_t> variable_to_int_index_;
    std::map<std::string, std::size_t> variable_to_float_index_;
    std::map<std::string, std::size_t> variable_to_string_index_;
    std::map<std::string, std::size_t> variable_to_bool_index_;
};

/**
 * @brief Static dependency graph with pre-registered propagation functions
 * 
 * Implements hybrid propagation semantics:
 * - Push invalidation: marks downstream nodes stale without eager recompute
 * - Pull recompute: on access, recomputes minimal upstream set and caches results
 */
class DependencyGraph {
public:
    using PropagationFunc = std::function<void(double&, const double&, double)>;
    
    DependencyGraph() = default;
    ~DependencyGraph() = default;
    
    /**
     * @brief Build graph from compiled scenario
     * @param compiled The compiled scenario artifact
     */
    void build_from_compiled(const compiler::CompiledScenario& compiled);
    
    /**
     * @brief Register a propagation function by ID
     * @param function_id Unique identifier
     * @param func The propagation function
     */
    void register_function(const std::string& function_id, PropagationFunc func);
    
    /**
     * @brief Propagate change from source variable (push invalidation)
     * @param source_id The source variable that changed
     * @param state The simulation state to mark stale
     */
    void push_invalidation(const std::string& source_id, SimulationState& state);
    
    /**
     * @brief Recompute a variable if stale (pull recompute)
     * @param target_id The variable to recompute
     * @param state The simulation state
     * @return true if recomputation occurred
     */
    bool pull_recompute(const std::string& target_id, SimulationState& state);
    
    /**
     * @brief Get all downstream dependents of a variable
     * @param variable_id The source variable
     * @return List of dependent variable IDs
     */
    std::vector<std::string> get_downstream(const std::string& variable_id) const;
    
    /**
     * @brief Get all upstream dependencies of a variable
     * @param variable_id The target variable
     * @return List of dependency variable IDs
     */
    std::vector<std::string> get_upstream(const std::string& variable_id) const;
    
    /**
     * @brief Compute transitive closure of stale variables
     * @param state The simulation state
     * @return Set of all variables that need recomputation
     */
    std::vector<std::string> compute_stale_closure(const SimulationState& state) const;
    
private:
    // Static adjacency list: source -> [(target, function_id, weight)]
    std::map<std::string, std::vector<compiler::AdjacencyEntry>> adjacency_list_;
    
    // Reverse adjacency for pull recompute: target -> [sources]
    std::map<std::string, std::vector<std::string>> reverse_adjacency_;
    
    // Registered propagation functions
    std::map<std::string, PropagationFunc> functions_;
    
    // Helper for cycle-free traversal
    void traverse_downstream(const std::string& start, 
                             std::vector<std::string>& result,
                             std::set<std::string>& visited) const;
    void traverse_upstream(const std::string& start,
                          std::vector<std::string>& result,
                          std::set<std::string>& visited) const;
};

} // namespace noisiax::engine
