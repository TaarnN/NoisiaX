#include "noisiax/scheduler/event_scheduler.hpp"

namespace noisiax::scheduler {

void EventScheduler::initialize(const compiler::CompiledScenario& compiled) {
    // Clear existing queue
    clear();
    
    // Add all scheduled events from compiled scenario
    for (const auto& event : compiled.event_queue) {
        schedule_event(event);
    }
}

void EventScheduler::register_callback(const std::string& event_type, EventCallback callback) {
    callbacks_[event_type] = std::move(callback);
}

const compiler::ScheduledEvent* EventScheduler::peek_next_event() const {
    if (event_queue_.empty()) {
        return nullptr;
    }
    return &event_queue_.top();
}

std::optional<compiler::ScheduledEvent> EventScheduler::pop_next_event() {
    if (event_queue_.empty()) {
        return std::nullopt;
    }
    
    compiler::ScheduledEvent event = event_queue_.top();
    event_queue_.pop();
    return event;
}

void EventScheduler::schedule_event(compiler::ScheduledEvent event) {
    event_queue_.push(std::move(event));
}

bool EventScheduler::has_events() const {
    return !event_queue_.empty();
}

std::size_t EventScheduler::pending_count() const {
    return event_queue_.size();
}

std::size_t EventScheduler::process_events_until(double max_time, engine::SimulationState& state) {
    std::size_t processed = 0;
    
    while (has_events()) {
        const auto* next = peek_next_event();
        if (!next || next->timestamp > max_time) {
            break;
        }
        
        if (process_next_event(state)) {
            processed++;
        } else {
            // Skip this event if no callback registered
            pop_next_event();
            total_skipped_++;
        }
    }
    
    return processed;
}

bool EventScheduler::process_next_event(engine::SimulationState& state) {
    auto event_opt = pop_next_event();
    if (!event_opt) {
        return false;
    }
    
    const auto& event = *event_opt;
    
    // Record in history for replay verification
    event_history_.push_back(event.event_handle);
    
    // Find and invoke callback
    auto it = callbacks_.find(event.descriptor.event_type);
    if (it != callbacks_.end()) {
        it->second(event.descriptor, state);
        total_processed_++;
        return true;
    }
    
    return false;
}

void EventScheduler::clear() {
    while (!event_queue_.empty()) {
        event_queue_.pop();
    }
    event_history_.clear();
    total_processed_ = 0;
    total_skipped_ = 0;
}

const std::vector<std::string>& EventScheduler::event_history() const {
    return event_history_;
}

} // namespace noisiax::scheduler
