#pragma once

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
/// template<> struct fe::is_bit_enum<MyEnum> : std::true_type {};
/// ```
template<typename T>
struct is_bit_enum : std::false_type {};

template<typename E>
concept BitEnum = std::is_enum_v<E> && is_bit_enum<E>::value;

template<fe::BitEnum E> constexpr auto to_underlying(E e) noexcept { return static_cast<std::underlying_type_t<E>>(e); }
template<fe::BitEnum E> constexpr bool has_flag(E value, E flag) noexcept { return (value & flag) == flag; }

} // namespace fe

// clang-format off
template<fe::BitEnum E> constexpr E operator|(E a, E b) noexcept { return static_cast<E>(fe::to_underlying(a) | fe::to_underlying(b)); }
template<fe::BitEnum E> constexpr E operator&(E a, E b) noexcept { return static_cast<E>(fe::to_underlying(a) & fe::to_underlying(b)); }
template<fe::BitEnum E> constexpr E operator^(E a, E b) noexcept { return static_cast<E>(fe::to_underlying(a) ^ fe::to_underlying(b)); }
template<fe::BitEnum E> constexpr E operator~(E a) noexcept { return static_cast<E>(~fe::to_underlying(a)); }
template<fe::BitEnum E> constexpr E& operator|=(E& a, E b) noexcept { return a = (a | b); }
template<fe::BitEnum E> constexpr E& operator&=(E& a, E b) noexcept { return a = (a & b); }
template<fe::BitEnum E> constexpr E& operator^=(E& a, E b) noexcept { return a = (a ^ b); }
// clang-format on
