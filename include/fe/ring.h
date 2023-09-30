#pragma once

#include <cstddef>
#include <array>

#include <fe/assert.h>

namespace fe {

/// A ring buffer with @p N elements.
template<class T, size_t N>
class Ring {
public:
    Ring& put(const T& t) {
        array_[first_] = t;
        first_ = (first_ + 1) % N;
        return *this;
    }

    T& front() { return array_[first_]; }
    const T& front() const { return array_[first_]; }
    T& operator[](size_t i) { assert(i < N); return array_[(first_ + i) % N]; }
    const T& operator[](size_t i) const { assert(i < N); return array_[(first_ + i) % N]; }

private:
    std::array<T, N> array_;
    size_t first_ = 0;
};

/// Specialization if `N == 1`.
template<class T>
class Ring<T, 1> {
public:
    Ring& put(const T& t) { t_ = t; return *this; }

    T& front() { return t_; }
    const T& front() const { return t_; }
    T& operator[](size_t i) { assert_unused(i == 0); return t_; }
    const T& operator[](size_t i) const { assert_unused(i == 0); return t_; }

private:
    T t_;
};

/// Specialization if `N == 2`.
template<class T>
class Ring<T, 2> {
public:
    Ring& put(const T& t) {
        array_[0] = array_[1];
        array_[1] = t;
        return *this;
    }

    T& front() { return array_[0]; }
    const T& front() const { return array_[0]; }
    T& operator[](size_t i) { assert_unused(i < 2); return array_[i]; }
    const T& operator[](size_t i) const { assert_unused(i < 2); return array_[i]; }

private:
    std::array<T, 2> array_;
};

} // namespace fe
