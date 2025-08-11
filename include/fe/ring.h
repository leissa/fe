#pragma once

#include <cstddef>

#include <algorithm>
#include <array>
#include <initializer_list>

#include <fe/assert.h>

namespace fe {

/// A ring buffer with @p N elements.
template<class T, size_t N>
class Ring {
public:
    /// @name Construction
    ///@{
    Ring(std::initializer_list<T> list) { std::copy(list.begin(), list.end(), array_); }
    Ring() noexcept   = default;
    Ring(const Ring&) = default;
    Ring(Ring&& other) noexcept
        : Ring() {
        swap(*this, other);
    }
    Ring& operator=(Ring other) noexcept { return swap(*this, other), *this; }
    ///@}

    /// @name Access
    ///@{
    T& front() { return array_[first_]; }
    const T& front() const { return array_[first_]; }
    T& operator[](size_t i) {
        assert(i < N);
        return array_[(first_ + i) % N];
    }
    const T& operator[](size_t i) const {
        assert(i < N);
        return array_[(first_ + i) % N];
    }
    ///@}

    /// @name Modifiers
    ///@{
    void reset() { first_ = 0; }

    /// Puts @p item into buffer.
    /// @returns item that falls out.
    T put(T item) {
        auto res       = array_[first_];
        array_[first_] = item;
        first_         = (first_ + 1) % N;
        return res;
    }
    ///@}

    friend void swap(Ring& r1, Ring& r2) noexcept {
        using std::swap;
        swap(r1.array_, r2.array_);
        swap(r1.first_, r2.first_);
    }

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
        : item_(*list.begin()) {}
    Ring()            = default; // no noexcept: we don't know whether T's default constructor throws
    Ring(const Ring&) = default;
    Ring(Ring&& other)
        : Ring() {
        swap(*this, other);
    }
    Ring& operator=(Ring other) noexcept { return swap(*this, other), *this; }
    ///@}

    /// @name Access
    ///@{
    T& front() { return item_; }
    const T& front() const { return item_; }
    T& operator[](size_t i) {
        assert_unused(i == 0);
        return item_;
    }
    const T& operator[](size_t i) const {
        assert_unused(i == 0);
        return item_;
    }
    ///@}

    /// @name Modifiers
    ///@{
    void reset() {}
    T put(T item) {
        auto res = item_;
        item_    = item;
        return res;
    }
    ///@}

    friend void swap(Ring& r1, Ring& r2) noexcept {
        using std::swap;
        swap(r1.item_, r2.item_);
    }

private:
    T item_;
};

/// Specialization if `N == 2`; doesn't need a ring, we just copy.
template<class T>
class Ring<T, 2> {
public:
    /// @name Construction
    ///@{
    Ring(std::initializer_list<T> list) { std::copy(list.begin(), list.end(), array_); }
    Ring() noexcept   = default;
    Ring(const Ring&) = default;
    Ring(Ring&& other) noexcept
        : Ring() {
        swap(*this, other);
    }
    Ring& operator=(Ring other) noexcept { return swap(*this, other), *this; }
    ///@}

    /// @name Access
    ///@{
    T& front() { return array_[0]; }
    const T& front() const { return array_[0]; }
    T& operator[](size_t i) {
        assert_unused(i < 2);
        return array_[i];
    }
    const T& operator[](size_t i) const {
        assert_unused(i < 2);
        return array_[i];
    }
    ///@}

    /// @name Modifiers
    ///@{
    void reset() {}
    T put(T item) {
        auto res  = array_[0];
        array_[0] = array_[1];
        array_[1] = item;
        return res;
    }
    ///@}

    friend void swap(Ring& r1, Ring& r2) noexcept {
        using std::swap;
        swap(r1.array_, r2.array_);
    }

private:
    std::array<T, 2> array_;
};

} // namespace fe
