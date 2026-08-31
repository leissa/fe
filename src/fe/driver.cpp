#include "fe/driver.h"

namespace fe {

// Out-of-line so that no consumer emits fe::Diag's vtable - a data symbol - into a shared library of its own.
Driver::Driver()
    : diag_(std::make_unique<Diag>()) {}

Driver::~Driver() = default;

} // namespace fe
