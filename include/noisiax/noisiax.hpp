#pragma once

#include <string_view>

namespace noisiax {

[[nodiscard]] constexpr std::string_view version() noexcept {
  return "0.0.1";
}

[[nodiscard]] std::string_view name() noexcept;

}  // namespace noisiax
