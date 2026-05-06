#include "noisiax/extensions/default_registry.hpp"
#include "noisiax/extensions/acme_market_extension.hpp"
#include "noisiax/extensions/civic_resilience_extension.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace noisiax::extensions {
namespace {

double expr_as_number(const ExprValue& value) {
    if (const auto* v = std::get_if<double>(&value)) return *v;
    if (const auto* v_bool = std::get_if<bool>(&value)) return *v_bool ? 1.0 : 0.0;
    throw std::runtime_error("Expected numeric expression value");
}

}  // namespace

ExtensionRegistry make_default_registry() {
    ExtensionRegistry registry;

    // Core propagation functions (canonical IDs) + legacy aliases.
    registry.propagation_functions().register_function("core::linear_scale",
        [](double& target, const double& source, double weight) { target = source * weight; });
    registry.propagation_functions().register_function("core::apply_discount",
        [](double& target, const double& source, double weight) { target = target * (1.0 + source * weight); });
    registry.propagation_functions().register_function("core::additive",
        [](double& target, const double& source, double weight) { target += source * weight; });
    registry.propagation_functions().register_function("core::max_propagate",
        [](double& target, const double& source, double weight) { target = std::max(target, source * weight); });
    registry.propagation_functions().register_function("core::min_propagate",
        [](double& target, const double& source, double weight) { target = std::min(target, source * weight); });

    registry.propagation_functions().register_alias("linear_scale", "core::linear_scale");
    registry.propagation_functions().register_alias("apply_discount", "core::apply_discount");
    registry.propagation_functions().register_alias("additive", "core::additive");
    registry.propagation_functions().register_alias("max_propagate", "core::max_propagate");
    registry.propagation_functions().register_alias("min_propagate", "core::min_propagate");

    // std:: expression functions (canonical IDs) + legacy aliases.
    registry.expression_functions().register_function("std::min", [](const std::vector<ExprValue>& args) -> ExprValue {
        if (args.size() != 2) throw std::runtime_error("std::min(a,b) expects 2 arguments");
        return std::min(expr_as_number(args[0]), expr_as_number(args[1]));
    });
    registry.expression_functions().register_function("std::max", [](const std::vector<ExprValue>& args) -> ExprValue {
        if (args.size() != 2) throw std::runtime_error("std::max(a,b) expects 2 arguments");
        return std::max(expr_as_number(args[0]), expr_as_number(args[1]));
    });
    registry.expression_functions().register_function("std::clamp", [](const std::vector<ExprValue>& args) -> ExprValue {
        if (args.size() != 3) throw std::runtime_error("std::clamp(x,lo,hi) expects 3 arguments");
        const double x = expr_as_number(args[0]);
        const double lo = expr_as_number(args[1]);
        const double hi = expr_as_number(args[2]);
        return std::max(lo, std::min(x, hi));
    });
    registry.expression_functions().register_function("std::abs", [](const std::vector<ExprValue>& args) -> ExprValue {
        if (args.size() != 1) throw std::runtime_error("std::abs(x) expects 1 argument");
        return std::abs(expr_as_number(args[0]));
    });
    registry.expression_functions().register_function("std::sqrt", [](const std::vector<ExprValue>& args) -> ExprValue {
        if (args.size() != 1) throw std::runtime_error("std::sqrt(x) expects 1 argument");
        const double x = expr_as_number(args[0]);
        if (x < 0.0) throw std::runtime_error("std::sqrt(x) expects x >= 0");
        return std::sqrt(x);
    });

    registry.expression_functions().register_alias("min", "std::min");
    registry.expression_functions().register_alias("max", "std::max");
    registry.expression_functions().register_alias("clamp", "std::clamp");
    registry.expression_functions().register_alias("abs", "std::abs");
    registry.expression_functions().register_alias("sqrt", "std::sqrt");
    registry.expression_functions().register_alias("rng", "std::rng_uniform");

    // Statically linked extensions.
    {
        auto acme = make_acme_market_extension();
        const auto* impl = acme.get();
        registry.register_extension(std::move(acme));
        if (impl != nullptr) {
            impl->register_symbols(registry);
        }
    }
    {
        auto civic = make_civic_resilience_extension();
        const auto* impl = civic.get();
        registry.register_extension(std::move(civic));
        if (impl != nullptr) {
            impl->register_symbols(registry);
        }
    }
    return registry;
}

}  // namespace noisiax::extensions
