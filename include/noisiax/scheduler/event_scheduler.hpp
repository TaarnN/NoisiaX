#pragma once

#include "noisiax/schema/scenario_schema.hpp"
#include "noisiax/compiler/scenario_compiler.hpp"
#include "noisiax/engine/simulation_engine.hpp"
#include <queue>
#include <string>
#include <functional>

namespace noisiax::scheduler {

/**
 * @brief Deterministic event scheduler with stable ordering
 * 
 * Ordering semantics: (timestamp, priority, handle_lexicographic)
 * - Primary sort: ascending timestamp
 * - Secondary sort: descending priority (higher priority first)
 * - Tertiary sort: lexicographic handle for deterministic tie-breaking
 */
class EventScheduler {
public:
    using EventCallback = std::function<void(const schema::EventDescriptor&, engine::SimulationState&)>;
    
    EventScheduler() = default;
    ~EventScheduler() = default;
    
    /**
     * @brief Initialize scheduler from compiled scenario
     * @param compiled The compiled scenario artifact
     */
    void initialize(const compiler::CompiledScenario& compiled);
    
    /**
     * @brief Register a callback for an event type
     * @param event_type The event type to handle
     * @param callback The callback function
     */
    void register_callback(const std::string& event_type, EventCallback callback);
    
    /**
     @brief Get the next scheduled event (without removing it)
     * @return Pointer to the next event, or nullptr if queue is empty
     */
    const compiler::ScheduledEvent* peek_next_event() const;
    
    /**
     * @brief Pop and return the next scheduled event
     * @return The next event, or std::nullopt if queue is empty
     */
    std::optional<compiler::ScheduledEvent> pop_next_event();
    
    /**
     * @brief Schedule a new event dynamically
     * @param event The event to schedule
     */
    void schedule_event(compiler::ScheduledEvent event);
    
    /**
     * @brief Check if there are more events to process
     */
    bool has_events() const;
    
    /**
     * @brief Get the number of pending events
     */
    std::size_t pending_count() const;
    
    /**
     * @brief Process all events up to a given timestamp
     * @param max_time Maximum timestamp to process
     * @param state The simulation state
     * @return Number of events processed
     */
    std::size_t process_events_until(double max_time, engine::SimulationState& state);
    
    /**
     * @brief Process the next single event
     * @param state The simulation state
     * @return true if an event was processed, false if queue was empty
     */
    bool process_next_event(engine::SimulationState& state);
    
    /**
     * @brief Clear all pending events
     */
    void clear();
    
    /**
     * @brief Get event history for replay verification
     */
    const std::vector<std::string>& event_history() const;
    
private:
    // Priority queue with custom comparator for deterministic ordering
    struct EventComparator {
        bool operator()(const compiler::ScheduledEvent& a, const compiler::ScheduledEvent& b) const {
            // Invert for min-heap behavior with std::priority_queue
            return b < a;  // Uses ScheduledEvent::operator<
        }
    };
    
    std::priority_queue<compiler::ScheduledEvent, 
                        std::vector<compiler::ScheduledEvent>,
                        EventComparator> event_queue_;
    
    std::map<std::string, EventCallback> callbacks_;
    std::vector<std::string> event_history_;  // For replay verification
    
    // Statistics
    std::size_t total_processed_ = 0;
    std::size_t total_skipped_ = 0;
};

} // namespace noisiax::scheduler
