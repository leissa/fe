#pragma once

#include <cassert>
#include <vector>

namespace fe {

constexpr size_t Default_Page_Size = 1024 * 1024; ///< 1MB.

/// An arena pre-allocates so-called *pages* of size @p p.
/// You can use Arena::alloc to obtain memory from this.
/// When a page runs out of memory, the next page will be (pre-)allocated.
/// You cannot directly release memory obtained via this method.
/// Instead, *all* memory acquired via this Arena will be released as soon as this Arena will be destroyed.
/// As an exception, you can Arena::dealloc memory that just as been acquired.
///
/// Use Allocator to adopt it in [containers](https://en.cppreference.com/w/cpp/named_req/AllocatorAwareContainer).
/// Construct it via Arena::allocator.
/// @note The Arena assumes a consistent alignment of @p A for all  allocated objects.
template<size_t A = sizeof(size_t), size_t P = Default_Page_Size>
class Arena {
public:
    /// An [allocator](https://en.cppreference.com/w/cpp/named_req/Allocator).
    /// Construct one via Arena::allocator.
    template<class T>
    struct Allocator {
        using value_type = T;

        Allocator() noexcept = delete;
        template<class U>
        Allocator(const Arena<A, P>::Allocator<U>& allocator) noexcept
            : arena(allocator.arena) {}
        Allocator(Arena<A, P>& arena) noexcept
            : arena(arena) {}

        [[nodiscard]] T* allocate(size_t n) {
            static_assert(alignof(T) <= A, "alignment of Arena too small");
            return (T*)arena.allocate(n * sizeof(T));
        }

        void deallocate(T*, size_t) noexcept {}

        template<class U> bool operator==(const Allocator<U>& a) const noexcept { return &arena == &a.arena; }
        template<class U> bool operator!=(const Allocator<U>& a) const noexcept { return &arena != &a.arena; }

        Arena<A, P>& arena;
    };

    Arena() = default;
    ~Arena() {
        for (auto p : pages_) delete[] p;
    }

    static constexpr size_t align(size_t n) { return (n + (A - 1)) & ~(A - 1); } ///< Align @p n to @p A.

    /// Get @p n bytes of fresh memory.
    [[nodiscard]] void* allocate(size_t n) {
        n = align(n);

        if (index_ + n > P) {
            pages_.emplace_back(new char[std::max(P, n)]);
            index_ = 0;
        }

        auto result = pages_.back() + index_;
        index_ += n;
        assert(index_ % A == 0);
        return result;
    }

    /// Tries to remove @p n bytes again.
    /// If this crosses a page boundary, nothing happens.
    void deallocate(size_t n) {
        n = align(n);
        if (ptrdiff_t(index_ - n) > 0) index_ -= n; // don't care otherwise
        assert(index_ % A == 0);
    }

    /// Create Allocator from Arena.
    template<class T> Allocator<T> allocator() { return Allocator<T>(*this); }

    friend void swap(Arena& a1, Arena& a2) {
        using std::swap;
        swap(a1.pages_, a2.pages_);
        swap(a1.index_, a2.index_);
    }

private:
    std::vector<char*> pages_;
    size_t index_ = P;
};

} // namespace arena
