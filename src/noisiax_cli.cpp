#include "noisiax/noisiax.hpp"
#include "noisiax/experiment/experiment.hpp"

#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <variant>

namespace {

void print_usage(const char* program_name) {
    std::cerr << "Usage:\n"
              << "  " << program_name << " validate <scenario.yaml>\n"
              << "  " << program_name << " compile <scenario.yaml>\n"
              << "  " << program_name << " resolve <scenario.yaml> [--output canonical.yaml]\n"
              << "  " << program_name << " run <scenario.yaml> [--trace none|events|decisions|full]"
              << " [--output result.json] [--max-time <float>] [--max-events <int>] [--seed <u64>]\n"
              << "  " << program_name << " experiment <experiment.yaml> --output-dir <dir> [--trace none|events|decisions|full]"
              << " [--max-time <float>] [--max-events <int>]\n";
}

std::string escape_json(const std::string& input) {
    std::ostringstream oss;
    for (const char c : input) {
        switch (c) {
            case '"': oss << "\\\""; break;
            case '\\': oss << "\\\\"; break;
            case '\b': oss << "\\b"; break;
            case '\f': oss << "\\f"; break;
            case '\n': oss << "\\n"; break;
            case '\r': oss << "\\r"; break;
            case '\t': oss << "\\t"; break;
            default:
                if (static_cast<unsigned char>(c) < 0x20) {
                    oss << "\\u00";
                    oss << std::hex << static_cast<int>(c);
                } else {
                    oss << c;
                }
        }
    }
    return oss.str();
}

std::string trace_level_to_string(noisiax::TraceLevel level) {
    switch (level) {
        case noisiax::TraceLevel::NONE: return "none";
        case noisiax::TraceLevel::EVENTS: return "events";
        case noisiax::TraceLevel::DECISIONS: return "decisions";
        case noisiax::TraceLevel::FULL: return "full";
    }
    return "none";
}

noisiax::TraceLevel parse_trace_level(const std::string& text) {
    if (text == "none") return noisiax::TraceLevel::NONE;
    if (text == "events") return noisiax::TraceLevel::EVENTS;
    if (text == "decisions") return noisiax::TraceLevel::DECISIONS;
    if (text == "full") return noisiax::TraceLevel::FULL;
    throw std::runtime_error("Invalid --trace value: " + text);
}

std::string run_result_to_json(const noisiax::RunResult& result, const noisiax::RunOptions& options) {
    std::ostringstream oss;

    auto typed_scalar_to_json = [&](const noisiax::schema::TypedScalarValue& value) -> std::string {
        return std::visit([&](auto&& arg) -> std::string {
            using T = std::decay_t<decltype(arg)>;
            if constexpr (std::is_same_v<T, int64_t>) {
                return std::to_string(arg);
            } else if constexpr (std::is_same_v<T, double>) {
                std::ostringstream num;
                num << std::fixed << std::setprecision(6) << arg;
                return num.str();
            } else if constexpr (std::is_same_v<T, bool>) {
                return arg ? "true" : "false";
            } else if constexpr (std::is_same_v<T, std::string>) {
                return "\"" + escape_json(arg) + "\"";
            }
            return "null";
        }, value);
    };

    oss << "{\n";
    oss << "  \"scenario_id\": \"" << escape_json(result.report.scenario_id) << "\",\n";
    oss << "  \"success\": " << (result.report.success ? "true" : "false") << ",\n";
    oss << "  \"trace_level\": \"" << trace_level_to_string(options.trace_level) << "\",\n";

    oss << "  \"errors\": [";
    for (std::size_t i = 0; i < result.report.errors.size(); ++i) {
        if (i > 0) oss << ", ";
        oss << "\"" << escape_json(result.report.errors[i]) << "\"";
    }
    oss << "],\n";

    oss << "  \"warnings\": [";
    for (std::size_t i = 0; i < result.report.warnings.size(); ++i) {
        if (i > 0) oss << ", ";
        oss << "\"" << escape_json(result.report.warnings[i]) << "\"";
    }
    oss << "],\n";

    oss << "  \"statistics\": {";
    bool first_stat = true;
    for (const auto& [key, value] : result.report.statistics) {
        if (!first_stat) oss << ", ";
        first_stat = false;
        oss << "\"" << escape_json(key) << "\": \"" << escape_json(value) << "\"";
    }
    oss << "},\n";

    oss << "  \"events\": [";
    for (std::size_t i = 0; i < result.events.size(); ++i) {
        const auto& event = result.events[i];
        if (i > 0) oss << ",";
        oss << "\n    {";
        oss << "\"event_id\": " << event.event_id << ", ";
        oss << "\"event_type\": \"" << escape_json(event.event_type) << "\", ";
        oss << "\"event_handle\": \"" << escape_json(event.event_handle) << "\", ";
        oss << "\"timestamp\": " << event.timestamp << ", ";
        oss << "\"priority\": " << event.priority << ", ";
        oss << "\"actor_id\": \"" << escape_json(event.actor_id) << "\", ";
        oss << "\"shop_id\": \"" << escape_json(event.shop_id) << "\", ";
        oss << "\"item_id\": \"" << escape_json(event.item_id) << "\"";
        if (!event.causal_parent_event_ids.empty()) {
            oss << ", \"causal_parent_event_ids\": [";
            for (std::size_t p = 0; p < event.causal_parent_event_ids.size(); ++p) {
                if (p > 0) oss << ", ";
                oss << event.causal_parent_event_ids[p];
            }
            oss << "]";
        }
        if (!event.random_draws.empty()) {
            oss << ", \"random_draws\": [";
            for (std::size_t r = 0; r < event.random_draws.size(); ++r) {
                const auto& draw = event.random_draws[r];
                if (r > 0) oss << ", ";
                oss << "{"
                    << "\"stream_key\": \"" << escape_json(draw.stream_key) << "\", "
                    << "\"draw_index\": " << draw.draw_index << ", "
                    << "\"raw_u64\": " << draw.raw_u64 << ", "
                    << "\"normalized\": " << draw.normalized << ", "
                    << "\"interpreted_result\": \"" << escape_json(draw.interpreted_result) << "\"}";
            }
            oss << "]";
        }
        oss << "}";
    }
    if (!result.events.empty()) {
        oss << "\n  ";
    }
    oss << "],\n";

    oss << "  \"decisions\": [";
    for (std::size_t i = 0; i < result.decisions.size(); ++i) {
        const auto& decision = result.decisions[i];
        if (i > 0) oss << ",";
        oss << "\n    {";
        oss << "\"decision_id\": " << decision.decision_id << ", ";
        oss << "\"event_id\": " << decision.event_id << ", ";
        oss << "\"decision_type\": \"" << escape_json(decision.decision_type) << "\", ";
        oss << "\"agent_id\": \"" << escape_json(decision.agent_id) << "\", ";
        oss << "\"random_draw\": " << decision.random_draw << ", ";
        oss << "\"random_draw_scaled\": " << decision.random_draw_scaled << ", ";
        oss << "\"selected_item_id\": \"" << escape_json(decision.selected_item_id) << "\", ";
        oss << "\"selected_shop_id\": \"" << escape_json(decision.selected_shop_id) << "\"";
        if (!decision.causal_parent_event_ids.empty()) {
            oss << ", \"causal_parent_event_ids\": [";
            for (std::size_t p = 0; p < decision.causal_parent_event_ids.size(); ++p) {
                if (p > 0) oss << ", ";
                oss << decision.causal_parent_event_ids[p];
            }
            oss << "]";
        }
        oss << "}";
    }
    if (!result.decisions.empty()) {
        oss << "\n  ";
    }
    oss << "],\n";

    oss << "  \"state_changes\": [";
    for (std::size_t i = 0; i < result.state_changes.size(); ++i) {
        const auto& change = result.state_changes[i];
        if (i > 0) oss << ",";
        oss << "\n    {";
        oss << "\"event_id\": " << change.event_id << ", ";
        oss << "\"entity_type\": \"" << escape_json(change.entity_type) << "\", ";
        oss << "\"entity_id\": \"" << escape_json(change.entity_id) << "\", ";
        oss << "\"field_name\": \"" << escape_json(change.field_name) << "\", ";
        oss << "\"old_value\": \"" << escape_json(change.old_value) << "\", ";
        oss << "\"new_value\": \"" << escape_json(change.new_value) << "\"";
        oss << "}";
    }
    if (!result.state_changes.empty()) {
        oss << "\n  ";
    }
    oss << "],\n";

    oss << "  \"final_state\": {\n";
    oss << "    \"agents\": [";
    for (std::size_t i = 0; i < result.final_state.agents.size(); ++i) {
        const auto& agent = result.final_state.agents[i];
        if (i > 0) oss << ", ";
        oss << "{";
        oss << "\"agent_id\": \"" << escape_json(agent.agent_id) << "\", ";
        oss << "\"money_spent\": " << agent.money_spent << ", ";
        oss << "\"time_walking\": " << agent.time_walking << ", ";
        oss << "\"time_talking\": " << agent.time_talking << ", ";
        oss << "\"time_queueing\": " << agent.time_queueing << ", ";
        oss << "\"observed_purchases\": " << agent.observed_purchases << ", ";
        oss << "\"influence_received\": " << agent.influence_received;
        oss << "}";
    }
    oss << "],\n";

    oss << "    \"shops\": [";
    for (std::size_t i = 0; i < result.final_state.shops.size(); ++i) {
        const auto& shop = result.final_state.shops[i];
        if (i > 0) oss << ", ";
        oss << "{";
        oss << "\"shop_id\": \"" << escape_json(shop.shop_id) << "\", ";
        oss << "\"queue_size\": " << shop.queue_size;
        if (!shop.remaining_stock.empty()) {
            oss << ", \"remaining_stock\": {";
            bool first = true;
            for (const auto& [item_id, stock] : shop.remaining_stock) {
                if (!first) oss << ", ";
                first = false;
                oss << "\"" << escape_json(item_id) << "\": " << stock;
            }
            oss << "}";
        }
        oss << "}";
    }
    oss << "]\n";
    oss << "  }";

    if (result.typed_final_state.has_value() || !result.typed_system_traces.empty()) {
        oss << ",\n";

        oss << "  \"typed_system_traces\": [";
        for (std::size_t i = 0; i < result.typed_system_traces.size(); ++i) {
            const auto& trace = result.typed_system_traces[i];
            if (i > 0) oss << ",";
            oss << "\n    {";
            oss << "\"event_id\": " << trace.event_id << ", ";
            oss << "\"system_id\": \"" << escape_json(trace.system_id) << "\", ";
            oss << "\"timestamp\": " << trace.timestamp;

            if (!trace.matched_entity_ids.empty()) {
                oss << ", \"matched_entity_ids\": [";
                for (std::size_t m = 0; m < trace.matched_entity_ids.size(); ++m) {
                    if (m > 0) oss << ", ";
                    oss << "\"" << escape_json(trace.matched_entity_ids[m]) << "\"";
                }
                oss << "]";
            }

            if (!trace.writes.empty()) {
                oss << ", \"writes\": [";
                for (std::size_t w = 0; w < trace.writes.size(); ++w) {
                    const auto& write = trace.writes[w];
                    if (w > 0) oss << ", ";
                    oss << "{"
                        << "\"entity_id\": \"" << escape_json(write.entity_id) << "\", "
                        << "\"component_type_id\": \"" << escape_json(write.component_type_id) << "\", "
                        << "\"field_name\": \"" << escape_json(write.field_name) << "\", "
                        << "\"old_value\": \"" << escape_json(write.old_value) << "\", "
                        << "\"new_value\": \"" << escape_json(write.new_value) << "\""
                        << "}";
                }
                oss << "]";
            }

            if (!trace.created_relations.empty()) {
                oss << ", \"created_relations\": [";
                for (std::size_t r = 0; r < trace.created_relations.size(); ++r) {
                    const auto& rel = trace.created_relations[r];
                    if (r > 0) oss << ", ";
                    oss << "{"
                        << "\"relation_type_id\": \"" << escape_json(rel.relation_type_id) << "\", "
                        << "\"source_entity_id\": \"" << escape_json(rel.source_entity_id) << "\", "
                        << "\"target_entity_id\": \"" << escape_json(rel.target_entity_id) << "\""
                        << "}";
                }
                oss << "]";
            }

            if (!trace.emitted_events.empty()) {
                oss << ", \"emitted_events\": [";
                for (std::size_t e = 0; e < trace.emitted_events.size(); ++e) {
                    const auto& emitted = trace.emitted_events[e];
                    if (e > 0) oss << ", ";
                    oss << "{"
                        << "\"event_type_id\": \"" << escape_json(emitted.event_type_id) << "\", "
                        << "\"event_id\": " << emitted.event_id
                        << "}";
                }
                oss << "]";
            }

            if (!trace.random_draws.empty()) {
                oss << ", \"random_draws\": [";
                for (std::size_t d = 0; d < trace.random_draws.size(); ++d) {
                    const auto& draw = trace.random_draws[d];
                    if (d > 0) oss << ", ";
                    oss << "{"
                        << "\"stream_key\": \"" << escape_json(draw.stream_key) << "\", "
                        << "\"draw_index\": " << draw.draw_index << ", "
                        << "\"raw_u64\": " << draw.raw_u64 << ", "
                        << "\"normalized\": " << draw.normalized << ", "
                        << "\"interpreted_result\": \"" << escape_json(draw.interpreted_result) << "\"}";
                }
                oss << "]";
            }

            oss << "}";
        }
        if (!result.typed_system_traces.empty()) {
            oss << "\n  ";
        }
        oss << "],\n";

        oss << "  \"typed_final_state\": ";
        if (!result.typed_final_state.has_value()) {
            oss << "null\n";
        } else {
            const auto& snapshot = *result.typed_final_state;
            oss << "{\n";
            oss << "    \"state_fingerprint\": \"" << escape_json(snapshot.state_fingerprint) << "\",\n";

            oss << "    \"summary\": {";
            bool first_summary = true;
            for (const auto& [key, value] : snapshot.summary) {
                if (!first_summary) oss << ", ";
                first_summary = false;
                oss << "\"" << escape_json(key) << "\": \"" << escape_json(value) << "\"";
            }
            oss << "},\n";

            oss << "    \"entities\": [";
            for (std::size_t i = 0; i < snapshot.entities.size(); ++i) {
                const auto& entity = snapshot.entities[i];
                if (i > 0) oss << ", ";
                oss << "{";
                oss << "\"entity_id\": \"" << escape_json(entity.entity_id) << "\", ";
                oss << "\"entity_type_id\": \"" << escape_json(entity.entity_type_id) << "\"";
                if (!entity.components.empty()) {
                    oss << ", \"components\": {";
                    bool first_component = true;
                    for (const auto& comp : entity.components) {
                        if (!first_component) oss << ", ";
                        first_component = false;
                        oss << "\"" << escape_json(comp.component_type_id) << "\": {";
                        bool first_field = true;
                        for (const auto& [field, val] : comp.fields) {
                            if (!first_field) oss << ", ";
                            first_field = false;
                            oss << "\"" << escape_json(field) << "\": " << typed_scalar_to_json(val);
                        }
                        oss << "}";
                    }
                    oss << "}";
                }
                oss << "}";
            }
            oss << "],\n";

            oss << "    \"relations\": [";
            for (std::size_t i = 0; i < snapshot.relations.size(); ++i) {
                const auto& rel = snapshot.relations[i];
                if (i > 0) oss << ", ";
                oss << "{";
                oss << "\"relation_type_id\": \"" << escape_json(rel.relation_type_id) << "\", ";
                oss << "\"source_entity_id\": \"" << escape_json(rel.source_entity_id) << "\", ";
                oss << "\"target_entity_id\": \"" << escape_json(rel.target_entity_id) << "\"";
                if (rel.expires_at.has_value()) {
                    oss << ", \"expires_at\": " << *rel.expires_at;
                }
                if (!rel.payload.empty()) {
                    oss << ", \"payload\": {";
                    bool first_field = true;
                    for (const auto& [field, val] : rel.payload) {
                        if (!first_field) oss << ", ";
                        first_field = false;
                        oss << "\"" << escape_json(field) << "\": " << typed_scalar_to_json(val);
                    }
                    oss << "}";
                }
                oss << "}";
            }
            oss << "]\n";

            oss << "  }\n";
        }
    } else {
        oss << "\n";
    }

    oss << "}\n";
    return oss.str();
}

int handle_validate(const std::string& filepath) {
    const auto report = noisiax::validate_scenario(filepath);
    noisiax::serialization::ReportSerializer serializer;
    std::cout << serializer.generate_summary(report);
    return report.success ? 0 : 1;
}

int handle_compile(const std::string& filepath) {
    try {
        const auto compiled = noisiax::compile_scenario(filepath);
        std::cout << "Compilation succeeded\n";
        std::cout << "scenario_id: " << compiled.scenario_id << "\n";
        std::cout << "variables: " << compiled.total_variables << "\n";
        std::cout << "dependencies: " << compiled.total_dependencies << "\n";
        std::cout << "constraints: " << compiled.total_constraints << "\n";
        std::cout << "events: " << compiled.total_events << "\n";
        std::cout << "agent_layer: " << (compiled.agent_layer.has_value() ? "enabled" : "disabled") << "\n";
        std::cout << "typed_layer: " << (compiled.typed_layer.has_value() ? "enabled" : "disabled") << "\n";
        return 0;
    } catch (const std::exception& ex) {
        std::cerr << "Compilation failed: " << ex.what() << "\n";
        return 1;
    }
}

int handle_run(int argc, char** argv) {
    try {
        const std::string filepath = argv[2];
        noisiax::RunOptions options;
        std::string output_path;
        bool use_detailed = false;

        for (int i = 3; i < argc; ++i) {
            const std::string arg = argv[i];
            if (arg == "--trace") {
                if (i + 1 >= argc) {
                    throw std::runtime_error("--trace requires a value");
                }
                options.trace_level = parse_trace_level(argv[++i]);
                use_detailed = true;
                continue;
            }
            if (arg == "--output") {
                if (i + 1 >= argc) {
                    throw std::runtime_error("--output requires a file path");
                }
                output_path = argv[++i];
                use_detailed = true;
                continue;
            }
            if (arg == "--max-time") {
                if (i + 1 >= argc) {
                    throw std::runtime_error("--max-time requires a value");
                }
                options.max_time = std::stod(argv[++i]);
                use_detailed = true;
                continue;
            }
            if (arg == "--max-events") {
                if (i + 1 >= argc) {
                    throw std::runtime_error("--max-events requires a value");
                }
                options.max_events = static_cast<std::size_t>(std::stoull(argv[++i]));
                use_detailed = true;
                continue;
            }
            if (arg == "--seed") {
                if (i + 1 >= argc) {
                    throw std::runtime_error("--seed requires a value");
                }
                options.seed_override = static_cast<uint64_t>(std::stoull(argv[++i]));
                use_detailed = true;
                continue;
            }
            throw std::runtime_error("Unknown option: " + arg);
        }

        noisiax::serialization::ReportSerializer serializer;
        if (!use_detailed) {
            const auto report = noisiax::run_scenario(filepath);
            std::cout << serializer.generate_summary(report);
            return report.success ? 0 : 1;
        }

        const auto run_result = noisiax::run_scenario_detailed(filepath, options);
        std::cout << serializer.generate_summary(run_result.report);

        if (!output_path.empty()) {
            std::ofstream output_file(output_path);
            if (!output_file.is_open()) {
                throw std::runtime_error("Unable to open output path: " + output_path);
            }
            output_file << run_result_to_json(run_result, options);
            output_file.close();
            std::cout << "Detailed output written to " << output_path << "\n";
        }

        return run_result.report.success ? 0 : 1;
    } catch (const std::exception& ex) {
        std::cerr << "Runtime failed: " << ex.what() << "\n";
        return 1;
    }
}

int handle_resolve(int argc, char** argv) {
    try {
        const std::string filepath = argv[2];
        std::string output_path;

        for (int i = 3; i < argc; ++i) {
            const std::string arg = argv[i];
            if (arg == "--output") {
                if (i + 1 >= argc) {
                    throw std::runtime_error("--output requires a file path");
                }
                output_path = argv[++i];
                continue;
            }
            throw std::runtime_error("Unknown option: " + arg);
        }

        const auto resolved = noisiax::experiment::resolve_scenario(filepath);
        if (output_path.empty()) {
            std::cout << resolved.canonical_yaml;
            return 0;
        }

        std::ofstream output_file(output_path);
        if (!output_file.is_open()) {
            throw std::runtime_error("Unable to open output path: " + output_path);
        }
        output_file << resolved.canonical_yaml;
        output_file.close();
        std::cout << "Resolved scenario written to " << output_path << "\n";
        return 0;
    } catch (const std::exception& ex) {
        std::cerr << "Resolve failed: " << ex.what() << "\n";
        return 1;
    }
}

int handle_experiment(int argc, char** argv) {
    try {
        const std::string filepath = argv[2];
        noisiax::experiment::ExperimentOptions options;

        for (int i = 3; i < argc; ++i) {
            const std::string arg = argv[i];
            if (arg == "--output-dir") {
                if (i + 1 >= argc) {
                    throw std::runtime_error("--output-dir requires a directory path");
                }
                options.output_dir = argv[++i];
                continue;
            }
            if (arg == "--trace") {
                if (i + 1 >= argc) {
                    throw std::runtime_error("--trace requires a value");
                }
                options.run_options.trace_level = parse_trace_level(argv[++i]);
                continue;
            }
            if (arg == "--max-time") {
                if (i + 1 >= argc) {
                    throw std::runtime_error("--max-time requires a value");
                }
                options.run_options.max_time = std::stod(argv[++i]);
                continue;
            }
            if (arg == "--max-events") {
                if (i + 1 >= argc) {
                    throw std::runtime_error("--max-events requires a value");
                }
                options.run_options.max_events = static_cast<std::size_t>(std::stoull(argv[++i]));
                continue;
            }
            throw std::runtime_error("Unknown option: " + arg);
        }

        if (options.output_dir.empty()) {
            throw std::runtime_error("--output-dir is required");
        }

        const auto result = noisiax::experiment::run_experiment(filepath, options);
        std::cout << "Experiment completed\n";
        std::cout << "experiment_id: " << result.experiment_id << "\n";
        std::cout << "output_dir: " << result.output_dir << "\n";
        std::cout << "runs: " << result.runs.size() << "\n";
        return 0;
    } catch (const std::exception& ex) {
        std::cerr << "Experiment failed: " << ex.what() << "\n";
        return 1;
    }
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 3) {
        print_usage(argv[0]);
        return 1;
    }

    const std::string command = argv[1];
    if (command == "validate") {
        return handle_validate(argv[2]);
    }
    if (command == "compile") {
        return handle_compile(argv[2]);
    }
    if (command == "resolve") {
        return handle_resolve(argc, argv);
    }
    if (command == "run") {
        return handle_run(argc, argv);
    }
    if (command == "experiment") {
        return handle_experiment(argc, argv);
    }

    print_usage(argv[0]);
    return 1;
}
