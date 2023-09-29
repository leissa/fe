#pragma once

#include "fe/ring.h"

namespace fe {

/// The blueprint for a [recursive descent](https://en.wikipedia.org/wiki/Recursive_descent_parser)/
/// [ascent parser](https://en.wikipedia.org/wiki/Recursive_ascent_parser) using a @p K lookahead of @p T%okens.
template<class T, size_t K>
class Parser {
private:
    Ring<T, K> ahead_;
};

} // namespace fe
