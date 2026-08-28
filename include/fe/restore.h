#pragma once

#include <utility>

namespace fe {

/// RAII guard that restores @p ref to its current value at the end of the scope.
/// The two-argument constructor additionally sets @p ref to @p value up front (like `std::exchange`).
template<class T>
class Restore {
public:
    /// Saves @p ref and restores it in the destructor.
    explicit Restore(T& ref)
        : ref_(ref)
        , prev_(ref) {}

    /// Saves @p ref, sets it to @p value, and restores the saved value in the destructor.
    Restore(T& ref, T value)
        : ref_(ref)
        , prev_(std::exchange(ref, std::move(value))) {}

    ~Restore() { ref_ = std::move(prev_); }

    Restore(const Restore&)            = delete;
    Restore& operator=(const Restore&) = delete;

private:
    T& ref_;
    T prev_;
};

}
