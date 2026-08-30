#pragma once

#include <queue>
#include <ranges>
#include <stack>
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

/// A worklist that pushes each element at most once.
/// @p Set remembers what has already been pushed and may be a reference to share it with the caller.
/// Use it through the UniqueQueue/UniqueStack aliases.
template<class Set, template<class> class C>
class Unique {
public:
    using T = typename std::remove_reference_t<Set>::value_type;

    /// @name Constructors
    ///@{
    Unique() = default;
    explicit Unique(Set set)
        : done_(std::forward<Set>(set)) {}
    Unique(std::initializer_list<T> init) { push(init); }
    ///@}

    /// @name push
    ///@{
    bool push(T val) {
        if (done_.emplace(val).second) {
            c_.emplace(std::move(val));
            return true;
        }
        return false;
    }
    template<std::ranges::input_range R>
    void push(R&& r) {
        for (auto&& val : r)
            push(val);
    }
    ///@}

    /// @name Access
    ///@{
    bool empty() const { return c_.empty(); }
    size_t size() const { return c_.size(); }
    T pop() { return fe::pop(c_); }

    T& front() requires Queuelike<C<T>> { return c_.front(); }
    const T& front() const requires Queuelike<C<T>> { return c_.front(); }
    T& back() requires Queuelike<C<T>> { return c_.back(); }
    const T& back() const requires Queuelike<C<T>> { return c_.back(); }
    T& top() requires Stacklike<C<T>> { return c_.top(); }
    const T& top() const requires Stacklike<C<T>> { return c_.top(); }
    ///@}

    void clear() {
        done_.clear();
        c_ = {};
    }

private:
    Set done_;
    C<T> c_;
};

template<class Set>
using UniqueQueue = Unique<Set, std::queue>;
template<class Set>
using UniqueStack = Unique<Set, std::stack>;

} // namespace fe
