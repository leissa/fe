#pragma once

#include <cassert>
#include <list>
#include <memory>

namespace fe {

template<size_t Align = alignof(size_t), size_t P = 1024 * 1024>
class Arena {
private:
    struct Page {
        Page(size_t n)
            : buffer(std::make_unique_for_overwrite<char[]>(n)) {}

        std::unique_ptr<char> buffer;
        std::unique_ptr<Page> next;
    };

public:
    Arena() = default;

    static constexpr size_t align(size_t n) { return (n + (Align - 1)) & ~(Align - 1); }

    [[nodiscard]] void* alloc(size_t n) {
        n = align(n);

        if (index_ + n > P) {
            pages_.emplace_back(std::max(P, n));
            index_ = 0;
        }

        auto result = pages_.back().buffer + index_;
        index_ += n;
        assert(index_ % Align == 0);
        return result;
    }

    /// @warning Invoke destructor of object beforehand.
    void dealloc(size_t n) {
        n = align(n);
        if (ptrdiff_t(index_ - n) > 0) index_ -= n; // don't care otherwise
        assert(index_ % Align == 0);
    }

    friend void swap(Arena& a1, Arena& a2) {
        using std::swap;
        swap(a1.pages_, a2.pages_);
        swap(a1.index_, a2.index_);
    }

private:
    std::list<Page> pages_;
    size_t index_ = P;
};

template<class T, size_t A = alignof(size_t), size_t P = 1024 * 1024>
struct Allocator {
    using value_type = T;

    Allocator(const Arena<A, P>& arena) noexcept
        : arena_(arena) {}

#if 0
    template<class U>
    constexpr Allocator(const Allocator <U>& other) noexcept
        : arena_(other.arena_) {}
#endif

    [[nodiscard]] T* allocate(size_t n) {
        if (auto p = static_cast<T*>(std::malloc(n * sizeof(T))))
            return p;

        throw std::bad_alloc();
    }

    void deallocate(T* p, std::size_t n) noexcept {
        std::free(p);
    }

private:
    Arena<A, P>& arena_;
};

} // namespace arena
