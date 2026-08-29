#pragma once

#include <queue>
#include <type_traits>
#include <utility>

#include "fe/assert.h"

namespace fe {

/// @name Helpers for Containers
///@{
template<class S>
auto pop(S& s) -> decltype(s.top(), typename S::value_type()) {
    auto val = s.top();
    s.pop();
    return val;
}

template<class Q>
auto pop(Q& q) -> decltype(q.front(), typename Q::value_type()) {
    auto val = q.front();
    q.pop();
    return val;
}

/// Yields pointer to element (or the element itself if it is already a pointer), if found and `nullptr` otherwise.
/// @warning If the element is **not** already a pointer, this lookup will simply take the address of this element.
/// This means that, e.g., a rehash of an `absl::flat_hash_map` will invalidate this pointer.
template<class C, class K>
auto lookup(const C& container, const K& key) {
    auto i = container.find(key);
    if constexpr (std::is_pointer_v<typename C::mapped_type>)
        return i != container.end() ? i->second : nullptr;
    else
        return i != container.end() ? &i->second : nullptr;
}

/// Looks up @p key in @p container, asserts that it exists, and returns the mapped value.
template<class C, class K>
auto assert_lookup(C& container, const K& key) {
    auto i = container.find(key);
    assert(i != container.end());
    return i->second;
}

/// Invokes `emplace` on @p container, asserts that insertion actually happened, and returns the iterator.
template<class C, class... Args>
auto assert_emplace(C& container, Args&&... args) {
    auto [i, ins] = container.emplace(std::forward<Args>(args)...);
    assert_unused(ins);
    return i;
}
///@}

/// A `std::queue` that pushes each element at most once.
template<class Set>
class UniqueQueue {
public:
    using T = typename std::remove_reference_t<Set>::value_type;

    UniqueQueue() = default;
    UniqueQueue(Set set)
        : done_(set) {}

    bool push(T val) {
        if (done_.emplace(val).second) {
            queue_.emplace(val);
            return true;
        }
        return false;
    }

    bool empty() const { return queue_.empty(); }
    T pop() { return fe::pop(queue_); }
    T& front() { return queue_.front(); }
    T& back() { return queue_.back(); }
    void clear() {
        done_.clear();
        queue_ = {};
    }

private:
    Set done_;
    std::queue<T> queue_;
};

} // namespace fe
