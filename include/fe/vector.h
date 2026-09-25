#pragma once

#include <algorithm>
#include <functional>
#include <memory>
#include <ranges>
#include <type_traits>

#include <ankerl/svector.h>

#include "fe/span.h"

namespace fe {

/// Aims at 4 words (i.e., 4 * sizeof(size_t)) of inlined storage; svector's one-byte tag makes the object a word
/// larger.
template<class T>
static constexpr size_t Default_Inlined_Size = std::max((size_t)1, 4 * sizeof(size_t) / sizeof(T));

/// This is a thin wrapper for
/// [`ankerl::svector<T, N, A>`](https://github.com/martinus/svector)
/// which is a drop-in replacement for [`std::vector<T, A>`](https://en.cppreference.com/w/cpp/container/vector).
/// In addition, there are generator-like/lambda-based constructors and conversions to Span available.
template<class T, size_t N = Default_Inlined_Size<T>, class A = std::allocator<T>>
class Vector : public ankerl::svector<T, N, A> {
public:
    using Base = ankerl::svector<T, N, A>;

    /// @name Constructors
    ///@{
    using Base::Base;
    template<class F>
    constexpr explicit Vector(size_t size, F&& f) requires(std::is_invocable_r_v<T, F, size_t>)
        : Base(size) {
        for (size_t i = 0; i != size; ++i)
            (*this)[i] = std::invoke(f, i);
    }

    template<std::ranges::forward_range R, class F>
    constexpr explicit Vector(R&& range, F&& f)
        requires(std::is_invocable_r_v<T, F, decltype(*std::ranges::begin(range))>
                 && !std::is_same_v<std::decay_t<R>, Vector>)
        : Base(std::ranges::distance(range)) {
        auto ri = std::ranges::begin(range);
        for (auto& elem : *this)
            elem = std::invoke(f, *ri++);
    }
    ///@}

    /// @name Span
    ///@{
    constexpr auto span() noexcept { return Span{Base::data(), Base::size()}; }
    constexpr auto span() const noexcept { return Span{Base::data(), Base::size()}; }
    constexpr auto view() const noexcept { return span(); }
    ///@}
};

static_assert(std::ranges::contiguous_range<Vector<int>>);

} // namespace fe
