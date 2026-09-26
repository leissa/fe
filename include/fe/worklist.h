#pragma once

#include <cstddef>

#include <ranges>
#include <stack>
#include <type_traits>
#include <utility>

#include "fe/container.h"
#include "fe/vector.h"

namespace fe {

/// A FIFO queue over a Vector.
/// Popped elements stay in place until they make up half of the storage, so `pop` is amortized constant.
template<class T, size_t N = Default_Inlined_Size<T>>
class VectorQueue {
public:
    using value_type = T;

    bool empty() const { return head_ == vec_.size(); }
    size_t size() const { return vec_.size() - head_; }

    T& front() { return vec_[head_]; }
    const T& front() const { return vec_[head_]; }
    T& back() { return vec_.back(); }
    const T& back() const { return vec_.back(); }

    void push(const T& val) { vec_.push_back(val); }
    void push(T&& val) { vec_.push_back(std::move(val)); }
    template<class... Args>
    decltype(auto) emplace(Args&&... args) {
        return vec_.emplace_back(std::forward<Args>(args)...);
    }

    void pop() {
        if (++head_ == vec_.size()) {
            vec_.clear();
            head_ = 0;
        } else if (head_ >= 2 * N && 2 * head_ >= vec_.size()) {
            vec_.erase(vec_.begin(), vec_.begin() + head_);
            head_ = 0;
        }
    }

private:
    Vector<T, N> vec_;
    size_t head_ = 0;
};

/// A worklist that pushes each element at most once.
/// @p Set remembers what has already been pushed and may be a reference to share it with the caller.
/// Use it through the BFSWorklist/DFSWorklist aliases.
template<class Set, class C>
class Worklist {
public:
    using T = typename std::remove_reference_t<Set>::value_type;
    static_assert(std::is_same_v<T, typename C::value_type>);

    /// @name Constructors
    ///@{
    Worklist() = default;
    explicit Worklist(Set set)
        : done_(std::forward<Set>(set)) {}
    Worklist(std::initializer_list<T> init) { push(init); }
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

    T& front() requires Queuelike<C> { return c_.front(); }
    const T& front() const requires Queuelike<C> { return c_.front(); }
    T& back() requires Queuelike<C> { return c_.back(); }
    const T& back() const requires Queuelike<C> { return c_.back(); }
    T& top() requires Stacklike<C> { return c_.top(); }
    const T& top() const requires Stacklike<C> { return c_.top(); }
    ///@}

    void clear() {
        done_.clear();
        c_ = {};
    }

private:
    Set done_;
    C c_;
};

namespace detail {
template<class Set>
using WorklistElem = typename std::remove_reference_t<Set>::value_type;
}

template<class Set>
using BFSWorklist = Worklist<Set, VectorQueue<detail::WorklistElem<Set>>>;
template<class Set>
using DFSWorklist = Worklist<Set, std::stack<detail::WorklistElem<Set>, Vector<detail::WorklistElem<Set>>>>;

} // namespace fe
