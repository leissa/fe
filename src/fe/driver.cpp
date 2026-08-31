#include "fe/driver.h"

namespace fe {

// Out-of-line so that no consumer emits fe::CodeDiag's vtable - a data symbol - into a shared library of its own.
Driver::Driver()
    : Driver(std::make_unique<CodeDiag>()) {}

Driver::Driver(std::unique_ptr<Diag> diag)
    : diag_(std::move(diag))
    , error_(*this) {
    assert(diag_ && "a Driver always has a Diag");
}

Driver::~Driver() { assert(error_.empty() && "please Error::ack all diagnostics before destroying this Driver"); }

} // namespace fe
