#pragma once

#include <cassert>
#include <concepts>

#include <format>
#include <utility>

#include <fe/assert.h>

namespace fe {

template<class T>
concept Nodeable = requires(T n) {
    T::Node;
    n.node();
};

/// Like Nodeable, but for a class that spans a *set* of node kinds instead of a single one.
/// Such a class declares `static constexpr bool isa_node(<node type>)` to decide membership.
/// This keeps RuntimeCast::isa from falling back to a `dynamic_cast` for abstract bases that merely group nodes.
template<class T>
concept NodeSetable = requires(T n) {
    { T::isa_node(n.node()) } -> std::convertible_to<bool>;
};

/// Inherit from this class using [CRTP](https://en.wikipedia.org/wiki/Curiously_recurring_template_pattern),
/// for some nice `dynamic_cast`-style wrappers.
template<class B>
class RuntimeCast {
public:
    // clang-format off
    /// `static_cast` with debug check.
    template<class T> T* as() { assert(isa<T>()); return static_cast<T*>(this); }

    /// `dynamic_cast`.
    /// If @p T isa fe::Nodeable, it will use `node()`, otherwise a `dynamic_cast`.
    template<class T>
    T* isa() {
        if constexpr (Nodeable<T>) {
            return static_cast<B*>(this)->node() == T::Node ? static_cast<T*>(this) : nullptr;
        } else if constexpr (NodeSetable<T>) {
            return T::isa_node(static_cast<B*>(this)->node()) ? static_cast<T*>(this) : nullptr;
        } else {
            return dynamic_cast<T*>(static_cast<B*>(this));
        }
    }

    template<class T> const T*  as() const { return const_cast<RuntimeCast*>(this)->template  as<T   >(); } ///< `const` version.
    template<class T> const T* isa() const { return const_cast<RuntimeCast*>(this)->template isa<T   >(); } ///< `const` version.
    // clang-format on

    /// Like as() but - instead of merely asserting via isa() in `Debug` builds - throws a `std::logic_error` with a
    /// formatted message when the cast fails.
    /// @p fmt / @p args describe what was expected: a plain string works, as does a format string plus arguments.
    /// If `B` is `std::formattable`, the offending object is appended to the message.
    template<class T, class... Args>
    T* expect(std::format_string<Args...> fmt, Args&&... args) {
        if (auto res = isa<T>()) return res;
        auto what = std::format(fmt, std::forward<Args>(args)...);
        if constexpr (std::formattable<const B*, char>)
            throwf("expected {}, but got '{}'", what, static_cast<const B*>(this));
        else
            throwf("expected {}", what);
    }

    /// `const` version.
    template<class T, class... Args>
    const T* expect(std::format_string<Args...> fmt, Args&&... args) const {
        return const_cast<RuntimeCast*>(this)->template expect<T>(fmt, std::forward<Args>(args)...);
    }
};

} // namespace fe
