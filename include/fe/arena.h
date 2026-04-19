#pragma once

#include <algorithm>
#include <list>
#include <memory>
#include <memory_resource>
#include <new>
#include <type_traits>
#include <utility>

#include "fe/assert.h"

namespace fe {

/// An arena pre-allocates so-called *pages* of size Arena::page_size_.
/// You can use Arena::allocate to obtain memory from this.
/// When a page runs out of memory, the next page will be (pre-)allocated.
/// You cannot directly release memory obtained via this method.
/// Instead, *all* memory acquired via this Arena will be released as soon as this Arena will be destroyed.
/// As an exception, you can Arena::deallocate memory that just as been acquired.
class Arena {
public:
    static constexpr size_t Default_Page_Size = 1024 * 1024; ///< 1MB.

    /// A [memory resource](https://en.cppreference.com/w/cpp/memory/memory_resource) bridge in order to use this
    /// Arena for [pmr containers](https://en.cppreference.com/w/cpp/memory/polymorphic_allocator).
    /// Access it via Arena::resource.
    class MemoryResource final : public std::pmr::memory_resource {
    public:
        explicit MemoryResource(Arena& arena) noexcept
            : arena_(arena) {}

    private:
        void* do_allocate(size_t bytes, size_t alignment) override { return arena_.allocate(bytes, alignment); }
        void do_deallocate(void*, size_t, size_t) override {}
        bool do_is_equal(const std::pmr::memory_resource& other) const noexcept override {
            if (this == &other) return true;
            auto resource = dynamic_cast<const MemoryResource*>(&other);
            return resource != nullptr && &arena_ == &resource->arena_;
        }

        Arena& arena_;
    };

    /// An [allocator](https://en.cppreference.com/w/cpp/named_req/Allocator) in order to use this Arena for
    /// [containers](https://en.cppreference.com/w/cpp/named_req/AllocatorAwareContainer).
    /// Construct it via Arena::allocator.
    template<class T>
    struct Allocator {
        using value_type = T;

        Allocator() = delete;

        template<class U>
        constexpr Allocator(const Arena::Allocator<U>& allocator) noexcept
            : arena(allocator.arena) {}
        constexpr Allocator(Arena& arena) noexcept
            : arena(arena) {}

        [[nodiscard]] constexpr T* allocate(size_t num_elems) { return arena.allocate<T>(num_elems); }

        constexpr void deallocate(T*, size_t) noexcept {}

        template<class U>
        constexpr bool operator==(const Allocator<U>& a) const noexcept {
            return &arena == &a.arena;
        }
        template<class U>
        constexpr bool operator!=(const Allocator<U>& a) const noexcept {
            return &arena != &a.arena;
        }

        Arena& arena;
    };

    template<class T>
    struct Deleter {
        constexpr Deleter() noexcept = default;
        template<class U, std::enable_if_t<std::is_convertible_v<U*, T*>, int> = 0>
        constexpr Deleter(const Deleter<U>&) noexcept {}

        constexpr void operator()(T* ptr) const noexcept(noexcept(ptr->~T())) { ptr->~T(); }
    };

    template<class T>
    using Ptr   = std::unique_ptr<T, Deleter<T>>;
    using State = std::pair<size_t, size_t>;

    /// @name Construction
    ///@{
    Arena(const Arena&) = delete;
    explicit Arena(size_t page_size = Default_Page_Size)
        : page_size_(page_size) {
        pages_.emplace_back();
    }
    Arena(Arena&& other) noexcept
        : Arena() {
        swap(*this, other);
    }
    Arena& operator=(Arena) = delete;

    /// Create Allocator from Arena.
    template<class T>
    constexpr Allocator<T> allocator() noexcept {
        return Allocator<T>(*this);
    }

    std::pmr::memory_resource* resource() noexcept { return &resource_; }
    const std::pmr::memory_resource* resource() const noexcept { return &resource_; }

    /// This is a [std::unique_ptr](https://en.cppreference.com/w/cpp/memory/unique_ptr)
    /// that uses the Arena under the hood
    /// and whose Deleter will *only* invoke the destructor but *not* `delete` anything;
    /// memory will be released upon destruction of the Arena.
    ///
    /// Use like this:
    /// ```
    /// auto ptr = arena.mk<Foo>(a, b, c); // new Foo(a, b, c) placed into arena
    /// ```
    template<class T, class... Args>
    constexpr Ptr<T> mk(Args&&... args) {
        auto ptr = new (allocate<std::remove_const_t<T>>(1)) T(std::forward<Args>(args)...);
        return Ptr<T>(ptr, Deleter<T>());
    }
    ///@}

    /// @name Allocate
    ///@{

    /// Get @p n bytes of fresh memory.
    [[nodiscard]] constexpr void* allocate(size_t num_bytes, size_t align) {
        if (num_bytes == 0) return nullptr;
        assert(align != 0);

        auto aligned_index = Arena::align(index_, align);
        if (aligned_index + num_bytes > pages_.back().size) {
            pages_.emplace_back(std::max(page_size_, num_bytes), align);
            aligned_index = 0;
        }

        auto result = pages_.back().buffer + aligned_index;
        index_      = aligned_index + num_bytes;
        return result;
    }

    template<class T>
    [[nodiscard]] constexpr T* allocate(size_t num_elems) {
        return static_cast<T*>(allocate(num_elems * sizeof(T), alignof(T)));
    }
    ///@}

    /// @name Deallocate
    /// Deallocate memory again in reverse order.
    /// Use like this:
    /// ```
    /// auto state = arena.state();
    /// auto ptr   = arena.allocate(n);
    /// if (/* I don't want that */) arena.deallocate(state);
    /// ```
    /// @warning Only use, if you really know what you are doing.
    ///@{

    /// Removes @p num_bytes again.
    constexpr void deallocate(size_t num_bytes) noexcept {
        assert(num_bytes <= index_);
        index_ -= num_bytes;
    }
    [[nodiscard]] State state() const noexcept { return {pages_.size(), index_}; }

    void deallocate(State state) noexcept {
        assert(state.first > 0);
        assert(state.first <= pages_.size());
        while (pages_.size() > state.first) pages_.pop_back();
        assert(state.second <= pages_.back().size);
        index_ = state.second;
    }
    ///@}

    friend void swap(Arena& a1, Arena& a2) noexcept {
        using std::swap;
        // clang-format off
        swap(a1.pages_,     a2.pages_);
        swap(a1.page_size_, a2.page_size_);
        swap(a1.index_,     a2.index_);
        // clang-format on
    }

    /// Align @p i to @p a.
    static constexpr size_t align(size_t i, size_t a) noexcept { return (i + (a - 1)) & ~(a - 1); }

private:
    constexpr Arena& align(size_t a) noexcept { return index_ = align(index_, a), *this; }

    struct Page {
        constexpr Page() noexcept = default;
        Page(size_t size, size_t align)
            : size(size)
            , align(align)
            , buffer((char*)::operator new[](size, std::align_val_t(align))) {}
        constexpr ~Page() noexcept {
            if (buffer) ::operator delete[](buffer, std::align_val_t(align));
        }

        const size_t size  = 0;
        const size_t align = 0;
        char* buffer       = nullptr;
    };

    std::list<Page> pages_;
    size_t page_size_;
    size_t index_ = 0;
    MemoryResource resource_{*this};
};

} // namespace fe
