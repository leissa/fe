#pragma once

#include <type_traits>
#include <utility>

#include "fe/assert.h"

namespace fe {

/// Something which behaves like `std::stack` or `std::priority_queue`.
template<class C>
concept Stacklike = requires(C c) {
    c.top();
    c.pop();
};

/// Something which behaves like `std::queue`.
template<class C>
concept Queuelike = requires(C c) {
    c.front();
    c.pop();
};

/// @name Helpers for Containers
///@{
template<Stacklike S>
[[nodiscard]] typename S::value_type pop(S& s) {
    // std::priority_queue::top is const, but pop_heap moves the top out of the heap before it compares anything.
    auto val = std::move(const_cast<typename S::value_type&>(s.top()));
    s.pop();
    return val;
}

template<Queuelike Q>
[[nodiscard]] typename Q::value_type pop(Q& q) {
    auto val = std::move(q.front());
    q.pop();
    return val;
}

/// Yields a pointer to the value mapped to @p key (or the value itself if it is a pointer) or `nullptr` if not found.
/// Constness of @p container carries over to the result.
/// @warning If the mapped value is **not** already a pointer, this lookup will simply take its address.
/// This means that, e.g., a rehash of an `ankerl::unordered_dense::map` will invalidate this pointer.
[[nodiscard]] auto lookup(auto& container, const auto& key) {
    auto i = container.find(key);
    if constexpr (std::is_pointer_v<typename std::remove_cvref_t<decltype(container)>::mapped_type>)
        return i != container.end() ? i->second : nullptr;
    else
        return i != container.end() ? &i->second : nullptr;
}

/// Looks up @p key in @p container, asserts that it exists, and returns a reference to the mapped value.
[[nodiscard]] decltype(auto) assert_lookup(auto& container, const auto& key) {
    auto i = container.find(key);
    assert(i != container.end());
    return (i->second);
}

/// Invokes `emplace` on @p container, asserts that insertion actually happened, and returns the iterator.
auto assert_emplace(auto& container, auto&&... args) {
    auto [i, ins] = container.emplace(std::forward<decltype(args)>(args)...);
    assert_unused(ins);
    return i;
}
///@}

} // namespace fe
