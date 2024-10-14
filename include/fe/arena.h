#pragma once

#include <algorithm>
#include <list>
#include <memory>

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

    /// @name Allocator
    /// An [allocator](https://en.cppreference.com/w/cpp/named_req/Allocator) in order to use this Arena for
    /// [containers](https://en.cppreference.com/w/cpp/named_req/AllocatorAwareContainer).
    /// Construct it via Arena::allocator.
    ///@{
    template<class T> struct Allocator {
        using value_type = T;

        Allocator() = delete;
        template<class U>
        Allocator(const Arena::Allocator<U>& allocator) noexcept
            : arena(allocator.arena) {}
        Allocator(Arena& arena) noexcept
            : arena(arena) {}

        [[nodiscard]] T* allocate(size_t num_elems) { return arena.allocate<T>(num_elems); }

        void deallocate(T*, size_t) noexcept {}

        template<class U> bool operator==(const Allocator<U>& a) const noexcept { return &arena == &a.arena; }
        template<class U> bool operator!=(const Allocator<U>& a) const noexcept { return &arena != &a.arena; }

        Arena& arena;
    };

    /// Create Allocator from Arena.
    template<class T> Allocator<T> allocator() { return Allocator<T>(*this); }
    ///@}

    /// @name Smart Pointer
    /// This is a [std::unique_ptr](https://en.cppreference.com/w/cpp/memory/unique_ptr)
    /// that uses the Arena under the hood
    /// and whose deleter will *only* invoke the destructor but *not* `delete` anything;
    /// memory will be released upon destruction of the Arena.
    ///
    /// Use like this:
    /// ```
    /// auto ptr = arena.mk<Foo>(a, b, c); // new Foo(a, b, c) placed into arena
    /// ```
    ///@{
    template<class T> struct Deleter {
        constexpr Deleter() noexcept = default;
        template<class U> constexpr Deleter(const Deleter<U>&) noexcept {}
        void operator()(T* ptr) { ptr->~T(); }
    };

    template<class T> using Ptr = std::unique_ptr<T, Deleter<T>>;
    template<class T, class... Args> Ptr<T> mk(Args&&... args) {
        auto ptr = new (allocate(sizeof(T))) T(std::forward<Args&&>(args)...);
        return Ptr<T>(ptr, Deleter<T>());
    }
    ///@}

    /// @name Construction/Destruction
    ///@{
    Arena(size_t page_size = Default_Page_Size)
        : page_size_(page_size) {
        pages_.emplace_back(page_size);
    }
    Arena(const Arena&) = delete;
    Arena(Arena&& other) noexcept
        : Arena() {
        swap(*this, other);
    }
    Arena& operator=(Arena) = delete;
    ///@}

    /// @name Allocate
    ///@{

    /// Align next allocate(size_t) to @p a.
    Arena& align(size_t a) { return index_ = (index_ + (a - 1)) & ~(a - 1), *this; }

    /// Get @p n bytes of fresh memory.
    [[nodiscard]] void* allocate(size_t num_bytes) {
        if (index_ + num_bytes > pages_.back().size) {
            pages_.emplace_back(std::max(page_size_, num_bytes));
            index_ = 0;
        }

        auto result = pages_.back().buffer.get() + index_;
        index_ += num_bytes;
        return result;
    }

    template<class T> [[nodiscard]] T* allocate(size_t num_elems) {
        align(alignof(T));
        return static_cast<T*>(allocate(num_elems * std::max(sizeof(T), alignof(T))));
    }
    ///@}

    /// @name Deallocate
    /// Deallocate memory again in reverse order.
    ///@{
    /// Removes @p num_bytes again.
    void deallocate(size_t num_bytes) { index_ -= num_bytes; }

    /// Goes back to @p state in Arena.
    /// Use like this:
    /// ```
    /// auto state = arena.state();
    /// auto ptr   = arena.allocate(n);
    /// if (/* I don't want that */) arena.deallocate(state);
    /// ```
    /// @warning Only use, if you really know what you are doing.
    using State = std::pair<size_t, size_t>;
    State state() const { return {pages_.size(), index_}; }
    void deallocate(State state) {
        if (state.first == pages_.size())
            index_ = state.second; // don't care otherwise
        else
            index_ = 0;
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

private:
    struct Page {
        Page(size_t size)
            : size(size)
            , buffer(std::make_unique<char[]>(size)) {}
        const size_t size;
        std::unique_ptr<char[]> buffer;
    };

    std::list<Page> pages_;
    size_t page_size_;
    size_t index_ = 0;
};

} // namespace fe
