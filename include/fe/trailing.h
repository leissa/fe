#pragma once

#include <cstdint>

#include <array>
#include <memory>
#include <ranges>
#include <tuple>
#include <type_traits>

#include "fe/algo.h"
#include "fe/span.h"

namespace fe {

/// CRTP base that gives @p Self one array per element type in `Self::Trail_Types` directly behind it
/// in memory - the C flexible array member, generalized to several arrays of different type.
/// @p Self must be created through Arena::mk or Arena::ref, which allocate the arrays along with
/// @p Self and fill them from the *last* `Self::Trail_Types` arguments, in that order.
/// This base is empty - the element counts live at the front of the trailing block - so that
/// `Self::Trail_Types` may name types only declared inside @p Self.
/// ```
/// class Interval : public Node, public fe::Trailing<Interval> {
/// public:
///     using Trail_Types = std::tuple<const Expr*, const Expr*>;
///     Interval(Loc loc, Tag from, Tag to);          // no parameter for either array
///     auto from_args() const { return trail<0>(); } // fe::View<const Expr*>
///     auto to_args() const { return trail<1>(); }
/// };
///
/// arena.ref<Interval>(loc, from, to, from_args, to_args);
/// ```
/// @warning Only the most derived class may carry them.
/// A class deriving from @p Self would inherit this base and place its arrays at @p Self's offset - on top of its
/// own members - so Arena::mk and Arena::ref reject it.
template<class Self>
class Trailing {
public:
    /// The type this base was instantiated with - @p Self itself, unless a class deriving from it inherited it.
    using Trail_Self = Self;

    /// Element type of the @p I th trailing array.
    /// @p S defers the lookup: `Self` is still incomplete while this base is instantiated.
    template<size_t I, class S = Self>
    using Trail_Type = std::tuple_element_t<I, typename S::Trail_Types>;

    static constexpr size_t num_trail() noexcept { return std::tuple_size_v<typename Self::Trail_Types>; }

    static constexpr size_t trail_align() noexcept { return trail_align(seq()); }

    /// Bytes an @p Self with these element counts occupies, `sizeof(Self)` included.
    template<class C>
    [[nodiscard]] static constexpr size_t trail_bytes(const C& counts) noexcept {
        return trail_begin() + offset(num_trail(), counts);
    }

    /// The @p I th trailing array as a `View<Trail_Type<I>>`.
    template<size_t I>
    [[nodiscard]] auto trail() const noexcept {
        auto block  = (const char*)static_cast<const Self*>(this) + trail_begin();
        auto counts = (const uint32_t*)block;
        return View<Trail_Type<I>>((const Trail_Type<I>*)(block + offset(I, counts)), counts[I]);
    }

private:
    static constexpr auto seq() noexcept { return std::make_index_sequence<num_trail()>(); }
    static constexpr size_t trail_begin() noexcept { return pad(sizeof(Self), trail_align()); }

    template<size_t... Is>
    static constexpr size_t trail_align(std::index_sequence<Is...>) noexcept {
        return std::max({alignof(uint32_t), alignof(Trail_Type<Is>)...});
    }

    /// Byte offset of the @p n th array from the block, or the block's size for `n == num_trail()`.
    template<class C>
    static constexpr size_t offset(size_t n, const C& counts) noexcept {
        return offset(n, counts, seq());
    }

    template<class C, size_t... Is>
    static constexpr size_t offset(size_t n, const C& counts, std::index_sequence<Is...>) noexcept {
        constexpr size_t sizes[]  = {sizeof(Trail_Type<Is>)...};
        constexpr size_t aligns[] = {alignof(Trail_Type<Is>)...};

        size_t off = sizeof...(Is) * sizeof(uint32_t);
        for (size_t i = 0; i != n; ++i)
            off = pad(off, aligns[i]) + counts[i] * sizes[i];
        return n == sizeof...(Is) ? off : pad(off, aligns[n]);
    }

    /// Arena calls this once @p Self is fully constructed - only then is the downcast valid.
    template<class... Rs>
    void fill_trail(const Rs&... rs) {
        static_assert(sizeof...(Rs) == num_trail(), "one range per trailing array, as the last arguments");

        auto block  = (char*)static_cast<Self*>(this) + trail_begin();
        auto counts = (uint32_t*)block;
        size_t i    = 0;
        ((counts[i++] = (uint32_t)std::ranges::size(rs)), ...);
        fill(std::index_sequence_for<Rs...>(), block, rs...);
    }

    template<size_t... Is, class... Rs>
    void fill(std::index_sequence<Is...>, char* block, const Rs&... rs) {
        static_assert((std::same_as<std::ranges::range_value_t<Rs>, Trail_Type<Is>> && ...),
                      "the trailing ranges must come last, in the order Trail_Types declares them");
        static_assert((std::is_trivially_destructible_v<Trail_Type<Is>> && ...),
                      "a trailing array is never destroyed - the Arena reclaims it");

        (std::uninitialized_copy(std::ranges::begin(rs), std::ranges::end(rs),
                                 (Trail_Type<Is>*)(block + offset(Is, (const uint32_t*)block))),
         ...);
    }

    friend class Arena;
};

/// Does @p T carry trailing arrays?
template<class T>
concept Trailed = std::derived_from<std::remove_cv_t<T>, Trailing<std::remove_cv_t<T>>>;

} // namespace fe
