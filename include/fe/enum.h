#pragma once

#include <compare>

#include <initializer_list>
#include <type_traits>

namespace fe {

/// @name Bit operations for enum classs
/// Provides all kind of bit and comparison operators for an `enum class` @p E.
/// Use like this:
/// ```
/// enum class MyEnum : unsigned {
///     A = 1 << 0,
///     B = 1 << 1,
///     C = 1 << 2,
/// };
///
/// using MyFlags = BitEnum<MyEnum>;
/// ```
///@{
template<class E> class BitEnum {
private:
    using U = std::underlying_type_t<E>;

public:
    constexpr BitEnum() noexcept
        : flags_(static_cast<U>(0)) {}
    constexpr explicit BitEnum(E e) noexcept
        : flags_(U(e)) {}
    constexpr BitEnum(std::initializer_list<E> list) noexcept
        : BitEnum() {
        for (auto e : list) flags_ |= U(e);
    }
    constexpr explicit BitEnum(U flags) noexcept //< Construct from raw values.
        : flags_(flags) {}

    /// @name Getters/Conversion
    ///@{
    constexpr bool is_set(E e) const noexcept { return (flags_ & U(e)) == U(e); }
    constexpr explicit operator bool() const noexcept { return flags_ != static_cast<U>(0); }
    constexpr explicit operator U() const noexcept { return flags_; } ///< Retrieve the raw underlying flags.
    ///@}

    // clang-format off
    /// @name Setters
    ///@{
    constexpr BitEnum set(E e) noexcept { flags_ |= U(e); return *this; }
    constexpr BitEnum unset(E e) noexcept { flags_ &= ~U(e); return *this; }
    constexpr void clear() noexcept { flags_ = U(0); }
    ///@}

    /// @name Operators
    ///@{
    friend constexpr BitEnum operator&(BitEnum x, BitEnum y) noexcept { return BitEnum(U(x) & U(y)); }
    friend constexpr BitEnum operator&(BitEnum x, E       y) noexcept { return BitEnum(U(x) & U(y)); }
    friend constexpr BitEnum operator&(E       x, BitEnum y) noexcept { return BitEnum(U(x) & U(y)); }

    friend constexpr BitEnum operator|(BitEnum x, BitEnum y) noexcept { return BitEnum(U(x) | U(y)); }
    friend constexpr BitEnum operator|(BitEnum x, E       y) noexcept { return BitEnum(U(x) | U(y)); }
    friend constexpr BitEnum operator|(E       x, BitEnum y) noexcept { return BitEnum(U(x) | U(y)); }

    friend constexpr BitEnum operator^(BitEnum x, BitEnum y) noexcept { return BitEnum(U(x) ^ U(y)); }
    friend constexpr BitEnum operator^(BitEnum x, E       y) noexcept { return BitEnum(U(x) ^ U(y)); }
    friend constexpr BitEnum operator^(E       x, BitEnum y) noexcept { return BitEnum(U(x) ^ U(y)); }

    friend constexpr bool operator==(BitEnum x, BitEnum y) noexcept { return U(x) == U(y); }
    friend constexpr bool operator==(E       x, BitEnum y) noexcept { return U(x) == U(y); }
    friend constexpr bool operator==(BitEnum x, E       y) noexcept { return U(x) == U(y); }

    friend constexpr bool operator!=(BitEnum x, BitEnum y) noexcept { return U(x) != U(y); }
    friend constexpr bool operator!=(E       x, BitEnum y) noexcept { return U(x) != U(y); }
    friend constexpr bool operator!=(BitEnum x, E       y) noexcept { return U(x) != U(y); }

    friend constexpr std::strong_ordering operator<=>(BitEnum x, BitEnum y) noexcept { return U(x) <=> U(y); }
    friend constexpr std::strong_ordering operator<=>(BitEnum x, E       y) noexcept { return U(x) <=> U(y); }
    friend constexpr std::strong_ordering operator<=>(E       x, BitEnum y) noexcept { return U(x) <=> U(y); }

    friend constexpr BitEnum operator~(BitEnum x) { return BitEnum(~U(x)); }
    ///@}
    // clang-format on

private:
    U flags_;
};
///@}

} // namespace fe
