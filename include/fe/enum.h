#pragma once

#include <compare>

#include <type_traits>

namespace fe {

/// @name is_enum
///@{
template<typename T> struct is_bit_enum : std::false_type {};
template<class T> inline constexpr bool is_bit_enum_v = is_bit_enum<T>::value;
template<class E>
concept BitEnum = std::is_enum_v<E> && is_bit_enum_v<E>;
///@}

/// @name Bit operations for enum classs
/// Provides all kind of bit and comparison operators for an `enum class` @p E.
/// Note that the bit operators return @p E's underlying type and not the original `enum` @p E.
/// This is because the result may not be a valid `enum` value.
/// For the same reason, it doesn't make sense to declare operators such as `&=`.
/// Use like this:
/// ```
/// using fe::operator&;
/// using fe::operator|;
/// using fe::operator^;
/// using fe::operator<=>;
/// using fe::operator==;
/// using fe::operator!=;
///
/// enum class MyEnum : unsigned {
///     A = 1 << 0,
///     B = 1 << 1,
///     C = 1 << 2,
/// };
///
/// template<> struct fe::is_bit_enum<MyEnum> : std::true_type { };
/// ```
///@{
// clang-format off
template <BitEnum E> constexpr auto operator&(E                         x,                        E  y) { return std::underlying_type_t<E>(x)  &  std::underlying_type_t<E>(y); }
template <BitEnum E> constexpr auto operator&(std::underlying_type_t<E> x,                        E  y) { return                           x   &  std::underlying_type_t<E>(y); }
template <BitEnum E> constexpr auto operator&(                       E  x, std::underlying_type_t<E> y) { return std::underlying_type_t<E>(x)  &                            y ; }
template <BitEnum E> constexpr auto operator|(                       E  x,                        E  y) { return std::underlying_type_t<E>(x)  |  std::underlying_type_t<E>(y); }
template <BitEnum E> constexpr auto operator|(std::underlying_type_t<E> x,                        E  y) { return                           x   |  std::underlying_type_t<E>(y); }
template <BitEnum E> constexpr auto operator|(                       E  x, std::underlying_type_t<E> y) { return std::underlying_type_t<E>(x)  |                            y ; }
template <BitEnum E> constexpr auto operator^(                       E  x,                        E  y) { return std::underlying_type_t<E>(x)  ^  std::underlying_type_t<E>(y); }
template <BitEnum E> constexpr auto operator^(std::underlying_type_t<E> x,                        E  y) { return                           x   ^  std::underlying_type_t<E>(y); }
template <BitEnum E> constexpr auto operator^(                       E  x, std::underlying_type_t<E> y) { return std::underlying_type_t<E>(x)  ^                            y ; }
template <BitEnum E> constexpr std::strong_ordering operator<=>(std::underlying_type_t<E> x, E y) { return x <=> std::underlying_type_t<E>(y); }
template <BitEnum E> constexpr std::strong_ordering operator<=>(E x, std::underlying_type_t<E> y) { return std::underlying_type_t<E>(x) <=> y; }
template <BitEnum E> constexpr bool operator==(std::underlying_type_t<E> x, E y) { return x == std::underlying_type_t<E>(y); }
template <BitEnum E> constexpr bool operator!=(std::underlying_type_t<E> x, E y) { return x != std::underlying_type_t<E>(y); }
template <BitEnum E> constexpr bool operator==(E x, std::underlying_type_t<E> y) { return std::underlying_type_t<E>(x) == y; }
template <BitEnum E> constexpr bool operator!=(E x, std::underlying_type_t<E> y) { return std::underlying_type_t<E>(x) != y; }
// clang-format on

///@}

} // namespace fe
