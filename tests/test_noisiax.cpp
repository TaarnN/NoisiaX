#include "noisiax/noisiax.hpp"

#include <cassert>

int main() {
  assert(noisiax::name() == "NoisiaX");
  assert(noisiax::version() == "0.0.1");
  return 0;
}
