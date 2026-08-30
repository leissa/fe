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
    auto val = std::move(s.top());
    s.pop();
    return val;
}

template<Queuelike Q>
[[nodiscard]] typename Q::value_type pop(Q& q) {
    auto val = std::move(q.front());
    q.pop();
    return val;
}

/// Yields pointer to element (or the element itself if it is already a pointer), if found and `nullptr` otherwise.
/// Constness of @p container carries over to the result.
/// @warning If the element is **not** already a pointer, this lookup will simply take the address of this element.
/// This means that, e.g., a rehash of an `absl::flat_hash_map` will invalidate this pointer.
template<class C, class K>
[[nodiscard]] auto lookup(C& container, const K& key) {
    auto i = container.find(key);
    if constexpr (std::is_pointer_v<typename C::mapped_type>)
        return i != container.end() ? i->second : nullptr;
    else
        return i != container.end() ? &i->second : nullptr;
}

/// Looks up @p key in @p container, asserts that it exists, and returns a reference to the mapped value.
template<class C, class K>
[[nodiscard]] decltype(auto) assert_lookup(C& container, const K& key) {
    auto i = container.find(key);
    assert(i != container.end());
    return (i->second);
}

/// Invokes `emplace` on @p container, asserts that insertion actually happened, and returns the iterator.
template<class C, class... Args>
auto assert_emplace(C& container, Args&&... args) {
    auto [i, ins] = container.emplace(std::forward<Args>(args)...);
    assert_unused(ins);
    return i;
}
///@}

} // namespace fe
