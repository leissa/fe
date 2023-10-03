#pragma once

#include <cstddef>
#include <array>
#include <algorithm>
#include <initializer_list>

#include <fe/assert.h>

namespace fe {

/// A ring buffer with @p N elements.
template<class T, size_t N>
class Ring {
public:
    /// @name Construction
    ///@{
    Ring(std::initializer_list<T> list) { std::ranges::copy(list, array_); }
    Ring()                       = default;
    Ring(const Ring&)            = default;
    Ring(Ring&&)                 = default;
    Ring& operator=(const Ring&) = default;
    ///@}

    /// @name Access
    ///@{
    T& front() { return array_[first_]; }
    const T& front() const { return array_[first_]; }
    T& operator[](size_t i) { assert(i < N); return array_[(first_ + i) % N]; }
    const T& operator[](size_t i) const { assert(i < N); return array_[(first_ + i) % N]; }
    ///@}

    /// @name Modifiers
    ///@{
    void reset() { first_ = 0; }

    T& put(const T& t) {
        auto& res = array_[first_] = t;
        first_ = (first_ + 1) % N;
        return res;
    }
    ///@}

private:
    std::array<T, N> array_;
    size_t first_ = 0;
};

/// Specialization if `N == 1` - doesn't need a ring.
template<class T>
class Ring<T, 1> {
public:
    /// @name Construction
    ///@{
    Ring(std::initializer_list<T> list)
        : t_(*list.begin()) {}
    Ring()                       = default;
    Ring(const Ring&)            = default;
    Ring(Ring&&)                 = default;
    Ring& operator=(const Ring&) = default;
    ///@}

    /// @name Access
    ///@{
    T& front() { return t_; }
    const T& front() const { return t_; }
    T& operator[](size_t i) { assert_unused(i == 0); return t_; }
    const T& operator[](size_t i) const { assert_unused(i == 0); return t_; }
    ///@}

    /// @name Modifiers
    ///@{
    void reset() {}
    T& put(const T& t) { return t_ = t; }
    ///@}

private:
    T t_;
};

/// Specialization if `N == 2`; doesn't need a ring, we just copy.
template<class T>
class Ring<T, 2> {
public:
    /// @name Construction
    ///@{
    Ring(std::initializer_list<T> list) { std::ranges::copy(list, array_); }
    Ring()                       = default;
    Ring(const Ring&)            = default;
    Ring(Ring&&)                 = default;
    Ring& operator=(const Ring&) = default;
    ///@}

    /// @name Access
    ///@{
    T& front() { return array_[0]; }
    const T& front() const { return array_[0]; }
    T& operator[](size_t i) { assert_unused(i < 2); return array_[i]; }
    const T& operator[](size_t i) const { assert_unused(i < 2); return array_[i]; }
    ///@}

    /// @name Modifiers
    ///@{
    void reset() {}
    T& put(const T& t) {
        array_[0] = array_[1];
        return array_[1] = t;
    }
    ///@}

private:
    std::array<T, 2> array_;
};

} // namespace fe
