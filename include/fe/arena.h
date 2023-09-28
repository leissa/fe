#pragma once

#include <cassert>
#include <algorithm>
#include <memory>

namespace fe {

template<size_t Align, size_t PageSize = 1024 * 1024>
class Arena {
private:
    struct Page {
        Page(size_t size)
            : buffer(std::make_unique<char[]>(size)) {}

        std::unique_ptr<char> buffer;
        std::unique_ptr<Page> next;
    };

public:
    Arena() {}
        //: root_(new Page) // don't use 'new Page()' - we keep the allocated Page uninitialized
        //, curr_(root_.get()) {}

#if (!defined(_MSC_VER) && defined(NDEBUG))
    struct Lock {
        Lock() { assert((guard_ = !guard_) && "you are not allowed to recursively invoke allocate"); }
        ~Lock() { guard_ = !guard_; }
        static bool guard_;
    };
#else
    struct Lock {
        ~Lock() {}
    };
#endif

    static constexpr size_t align(size_t n) { return (n + (sizeof(void*) - 1)) & ~(sizeof(void*) - 1); }

    void* allocate(size_t num_bytes) {
        Lock lock;
        num_bytes = align(num_bytes);

        if (index_ + num_bytes >= Page::Size) {
            auto zone = new Page;
            curr_->next.reset(zone);
            curr_  = zone;
            index_ = 0;
        }

        auto result = curr_->buffer + index_;
        index_ += num_bytes;
        assert(index_ % Align == 0);
        return result;
    }

    //// @warning Invoke destructor of object beforehand.
    void deallocate(size_t num_bytes) {
        num_bytes = align(num_bytes);
        if (ptrdiff_t(index_ - num_bytes) > 0) // don't care otherwise
            index_ -= num_bytes;
        assert(index_ % Align == 0);
    }

    friend void swap(Arena& a1, Arena& a2) {
        using std::swap;
        // clang-format off
        swap(a1.root_,  a2.root_);
        swap(a1.curr_,  a2.curr_);
        swap(a1.index_, a2.index_);
        // clang-format on
    }

private:
    std::unique_ptr<Page> root_;
    Page* curr_;
    size_t index_ = 0;
};

} // namespace arena
