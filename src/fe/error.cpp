#include "fe/error.h"

#include "fe/driver.h"

namespace fe {

const Diag& Error::diag() const { return driver_->diag(); }

} // namespace fe
