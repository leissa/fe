#pragma once

#include <utility>

namespace fe {

/// RAII guard that restores a value at the end of the scope.
/// This primary template guards whatever @p Get reads and @p Set writes; see fe::term::ScopedMode.
template<class T, auto Get = nullptr, auto Set = nullptr>
class Restore {
public:
    /// Saves what @p Get yields and restores it in the destructor.
    Restore()
        : prev_(Get()) {}

    /// As above but @p Set%s @p value up front.
    explicit Restore(T value)
        : prev_(Get()) {
        Set(std::move(value));
    }

    ~Restore() { Set(std::move(prev_)); }

    Restore(const Restore&)            = delete;
    Restore& operator=(const Restore&) = delete;

private:
    T prev_;
};

/// Restores @p ref to its current value at the end of the scope.
/// The two-argument constructor additionally sets @p ref to @p value up front (like `std::exchange`).
template<class T>
class Restore<T, nullptr, nullptr> {
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

/// The primary template has no two-argument constructor to deduce this one from.
template<class T>
Restore(T&, T) -> Restore<T>;

} // namespace fe
