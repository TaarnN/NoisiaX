#pragma once

#include "noisiax/extensions/extension_api.hpp"

#include <memory>

namespace noisiax::extensions {

std::unique_ptr<INoisiaXExtension> make_acme_market_extension();

}  // namespace noisiax::extensions

