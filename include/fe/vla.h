#pragma once

#include <cstdint>

#include <memory>
#include <ranges>
#include <tuple>
#include <type_traits>

#include "fe/algo.h"
#include "fe/span.h"

namespace fe {

/// CRTP base that gives @p Self one [VLA](https://en.wikipedia.org/wiki/Variable-length_array) per element type in
/// `Self::VLA_Types` directly behind it in memory - the C flexible array member, generalized to several arrays of
/// different type.
/// @p Self must be created through Arena::mk or Arena::ref, which allocate the arrays along with
/// @p Self and fill them from the *last* `Self::VLA_Types` arguments, in that order.
/// This base is empty - the element counts live at the front of the VLA block - so that
/// `Self::VLA_Types` may name types only declared inside @p Self.
/// ```
/// class Interval : public Node, public fe::VLA<Interval> {
/// public:
///     using VLA_Types = std::tuple<const Expr*, const Expr*>;
///     Interval(Loc loc, Tag from, Tag to);          // no parameter for either array
///     auto from_args() const { return vla<0>(); } // fe::View<const Expr*>
///     auto to_args() const { return vla<1>(); }
/// };
///
/// arena.ref<Interval>(loc, from, to, from_args, to_args);
/// ```
/// @warning Only the most derived class may carry them.
/// A class deriving from @p Self would inherit this base and place its arrays at @p Self's offset - on top of its
/// own members - so Arena::mk and Arena::ref reject it.
template<class Self>
class VLA {
public:
    /// The type this base was instantiated with - @p Self itself, unless a class deriving from it inherited it.
    using VLA_Self = Self;

    /// Element type of the @p I th VLA.
    /// @p S defers the lookup: `Self` is still incomplete while this base is instantiated.
    template<size_t I, class S = Self>
    using VLA_Type = std::tuple_element_t<I, typename S::VLA_Types>;

    static constexpr size_t num_vlas() noexcept { return std::tuple_size_v<typename Self::VLA_Types>; }

    static constexpr size_t vla_align() noexcept { return vla_align(seq()); }

    /// Bytes an @p Self with these element counts occupies, `sizeof(Self)` included.
    template<class C>
    [[nodiscard]] static constexpr size_t vla_bytes(const C& counts) noexcept {
        return vla_begin() + offset(num_vlas(), counts);
    }

    /// The @p I th VLA as a `Span<VLA_Type<I>>`.
    template<size_t I, size_t N = std::dynamic_extent>
    [[nodiscard]] auto vla() noexcept {
        auto block  = (char*)static_cast<Self*>(this) + vla_begin();
        auto counts = (uint32_t*)block;
        if constexpr (N == std::dynamic_extent)
            return Span<VLA_Type<I>>((VLA_Type<I>*)(block + offset(I, counts)), counts[I]);
        else
            return Span<VLA_Type<I>, N>((VLA_Type<I>*)(block + offset(I, counts)));
    }

    /// The @p I th VLA as a `View<VLA_Type<I>>` (`const` version).
    template<size_t I, size_t N = std::dynamic_extent>
    [[nodiscard]] auto vla() const noexcept {
        auto block  = (const char*)static_cast<const Self*>(this) + vla_begin();
        auto counts = (const uint32_t*)block;
        if constexpr (N == std::dynamic_extent)
            return View<VLA_Type<I>>((const VLA_Type<I>*)(block + offset(I, counts)), counts[I]);
        else
            return View<VLA_Type<I>, N>((const VLA_Type<I>*)(block + offset(I, counts)));
    }

private:
    static constexpr auto seq() noexcept { return std::make_index_sequence<num_vlas()>(); }
    static constexpr size_t vla_begin() noexcept { return pad(sizeof(Self), vla_align()); }

    template<size_t... Is>
    static constexpr size_t vla_align(std::index_sequence<Is...>) noexcept {
        return std::max({alignof(uint32_t), alignof(VLA_Type<Is>)...});
    }

    /// Byte offset of the @p n th array from the block, or the block's size for `n == num_vlas()`.
    template<class C>
    static constexpr size_t offset(size_t n, const C& counts) noexcept {
        return offset(n, counts, seq());
    }

    template<class C, size_t... Is>
    static constexpr size_t offset(size_t n, const C& counts, std::index_sequence<Is...>) noexcept {
        constexpr size_t sizes[]  = {sizeof(VLA_Type<Is>)...};
        constexpr size_t aligns[] = {alignof(VLA_Type<Is>)...};

        size_t off = sizeof...(Is) * sizeof(uint32_t);
        for (size_t i = 0; i != n; ++i)
            off = pad(off, aligns[i]) + counts[i] * sizes[i];
        return n == sizeof...(Is) ? off : pad(off, aligns[n]);
    }

    /// Arena calls this once @p Self is fully constructed - only then is the downcast valid.
    template<class... Rs>
    void fill_vla(const Rs&... rs) {
        static_assert(sizeof...(Rs) == num_vlas(), "one range per VLA, as the last arguments");

        auto block  = (char*)static_cast<Self*>(this) + vla_begin();
        auto counts = (uint32_t*)block;
        size_t i    = 0;
        ((counts[i++] = (uint32_t)std::ranges::size(rs)), ...);
        fill(std::index_sequence_for<Rs...>(), block, rs...);
    }

    template<size_t... Is, class... Rs>
    void fill(std::index_sequence<Is...>, char* block, const Rs&... rs) {
        static_assert((std::same_as<std::ranges::range_value_t<Rs>, VLA_Type<Is>> && ...),
                      "the VLA ranges must come last, in the order VLA_Types declares them");
        static_assert((std::is_trivially_destructible_v<VLA_Type<Is>> && ...),
                      "a VLA is never destroyed - the Arena reclaims it");

        (std::uninitialized_copy(std::ranges::begin(rs), std::ranges::end(rs),
                                 (VLA_Type<Is>*)(block + offset(Is, (const uint32_t*)block))),
         ...);
    }

    friend class Arena;
};

/// Does @p T carry VLAs?
template<class T>
concept VLAed = std::derived_from<std::remove_cv_t<T>, VLA<std::remove_cv_t<T>>>;

} // namespace fe
