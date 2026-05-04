#include "noisiax/noisiax.hpp"

#include <iostream>
#include <stdexcept>
#include <string>

namespace {

void print_usage(const char* program_name) {
    std::cerr << "Usage:\n"
              << "  " << program_name << " validate <scenario.yaml>\n"
              << "  " << program_name << " compile <scenario.yaml>\n"
              << "  " << program_name << " run <scenario.yaml>\n";
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
        return 0;
    } catch (const std::exception& ex) {
        std::cerr << "Compilation failed: " << ex.what() << "\n";
        return 1;
    }
}

int handle_run(const std::string& filepath) {
    try {
        const auto report = noisiax::run_scenario(filepath);
        noisiax::serialization::ReportSerializer serializer;
        std::cout << serializer.generate_summary(report);
        return report.success ? 0 : 1;
    } catch (const std::exception& ex) {
        std::cerr << "Runtime failed: " << ex.what() << "\n";
        return 1;
    }
}

}  // namespace

int main(int argc, char** argv) {
    if (argc != 3) {
        print_usage(argv[0]);
        return 1;
    }

    const std::string command = argv[1];
    const std::string filepath = argv[2];

    if (command == "validate") {
        return handle_validate(filepath);
    }
    if (command == "compile") {
        return handle_compile(filepath);
    }
    if (command == "run") {
        return handle_run(filepath);
    }

    print_usage(argv[0]);
    return 1;
}
