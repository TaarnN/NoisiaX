#pragma once

#include <string>
#include <vector>

namespace noisiax::extensions {

enum class DiagnosticSeverity {
    ERROR,
    WARNING
};

struct Diagnostic {
    DiagnosticSeverity severity = DiagnosticSeverity::ERROR;
    std::string message;

    bool operator==(const Diagnostic& other) const = default;
};

class DiagnosticSink {
public:
    void error(std::string message) {
        diagnostics_.push_back(Diagnostic{DiagnosticSeverity::ERROR, std::move(message)});
    }

    void warning(std::string message) {
        diagnostics_.push_back(Diagnostic{DiagnosticSeverity::WARNING, std::move(message)});
    }

    bool has_errors() const {
        for (const auto& d : diagnostics_) {
            if (d.severity == DiagnosticSeverity::ERROR) return true;
        }
        return false;
    }

    const std::vector<Diagnostic>& diagnostics() const { return diagnostics_; }

private:
    std::vector<Diagnostic> diagnostics_;
};

}  // namespace noisiax::extensions

