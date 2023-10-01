#pragma once

#ifdef __clang__
#include <cstring>
#endif
#include <iostream>
#include <string>

#include "fe/config.h"
#include "fe/arena.h"

#ifdef FE_ABSL
#include <absl/container/flat_hash_map.h>
#include <absl/container/flat_hash_set.h>
#include <absl/container/node_hash_set.h>
#else
#include <unordered_set>
#include <unordered_map>
#endif

//#include "fe/arena.h"

namespace fe {
class Sym;
}
template<> struct std::hash<fe::Sym>;

namespace fe {

/// A Sym%bol just wraps a `const std::string*`, so pass Sym itself around as value.
/// With the exception of the empty string, you should only create Sym%bols via SymPool::sym.
/// This in turn will toss all Sym%bols into a big hash set.
/// This makes Sym::operator== and Sym::operator!= an O(1) operation.
/// The empty string is internally handled as `nullptr`.
/// Thus, you can create a Sym%bol representing an empty string without having access to the SymPool.
/// @note The empty `std::string`, `nullptr`, and `"\0"` are all identified as Sym::Sym().
class Sym {
private:
    Sym(const std::string* ptr)
        : ptr_(ptr) {
        assert(ptr == nullptr || !ptr->empty());
    }

public:
    Sym() = default;

    /// @name begin/end
    ///@{
    /// Useful for range-based for.
    /// Will give you `std::string::const_iterator` - yes **const**; you are not supposed to mutate hashed strings.
    auto begin() const { return (*this)->cbegin(); }
    auto end() const { return (*this)->cend(); }
    ///@}

    /// @name Comparisons
    ///@{
    auto operator<=>(Sym other) const {
#ifdef __clang__
        return std::strcmp((*this)->c_str(), other->c_str()) <=> 0; // std::string <=> std::string is causing probls with clang
#else
        return **this <=> *other;
#endif
    }
    bool operator==(Sym other) const { return this->ptr_ == other.ptr_; }
    bool operator!=(Sym other) const { return this->ptr_ != other.ptr_; }
    auto operator<=>(char c) const {
        if ((*this)->size() == 0) return std::strong_ordering::less;
        auto cmp = (*this)[0] <=> c;
        if ((*this)->size() == 1) return cmp;
        return cmp == 0 ? std::strong_ordering::greater : cmp;
    }
    auto operator==(char c) const { return (*this) <=> c == 0; }
    auto operator!=(char c) const { return (*this) <=> c != 0; }
    ///@}

    /// @name Cast Operators
    ///@{
    operator std::string_view() const { return ptr_ ? *ptr_ : std::string_view(); }
    operator const std::string&() const { return *this->operator->(); }
    explicit operator bool() const { return ptr_; }
    ///@}

    /// @name Access Operators
    ///@{
    char operator[](size_t i) const { return ((const std::string&)(*this))[i]; }
    const std::string& operator*() const { return *this->operator->(); }
    const std::string* operator->() const {
        static std::string empty;
        return ptr_ ? ptr_ : &empty;
    }
    ///@}

#ifdef FE_ABSL
    template<class H>
    friend H AbslHashValue(H h, Sym sym) { return H::combine(std::move(h), sym.ptr_); }
#endif
    friend struct ::std::hash<fe::Sym>;

private:
    const std::string* ptr_ = nullptr;

    friend class SymPool;
};

#ifndef DOXYGEN
} // namespace fe

template<>
struct std::hash<fe::Sym> {
    size_t operator()(fe::Sym sym) const { return std::hash<void*>()((void*)sym.ptr_); }
};

namespace fe {
#endif

/// @name Sym
///@{
/// Set/Map is keyed by pointer - which is hashed in SymPool.
#ifdef FE_ABSL
template<class V>
using SymMap = absl::flat_hash_map<Sym, V>;
using SymSet = absl::flat_hash_set<Sym>;
#else
template<class V>
using SymMap = std::unordered_map<Sym, V>;
using SymSet = std::unordered_set<Sym>;
#endif
///@}

/// @name std::ostream operator
///@{
inline std::ostream& operator<<(std::ostream& o, const Sym sym) { return o << *sym; }
///@}

/// Hash set where all strings - wrapped in Sym%bol - live in.
/// You can access the SymPool from Driver.
class SymPool {
public:
    SymPool()
        : pool_(arena_.allocator<std::string>()) {}
    SymPool(SymPool&& other)
        : arena_(std::move(other.arena_))
        , pool_(std::move(other.pool_)) {}
    SymPool(const SymPool&) = delete;

    /// @name sym
    ///@{
    Sym sym(std::string_view s) { return s.empty() ? Sym() : &*pool_.emplace(s).first; }
    Sym sym(const char* s) { return s == nullptr || *s == '\0' ? Sym() : &*pool_.emplace(s).first; }
    Sym sym(std::string s) { return s.empty() ? Sym() : &*pool_.emplace(std::move(s)).first; }
    ///@}

    friend void swap(SymPool& p1, SymPool& p2) {
        using std::swap;
        swap(p1.pool_, p2.pool_);
    }

private:
    Arena<> arena_;
#ifdef FE_ABSL
    absl::node_hash_set<
#else
    std::unordered_set<
#endif
        std::string,
        std::hash<std::string>,
        std::equal_to<std::string>,
        Arena<>::Allocator<std::string>
    > pool_;
};

} // namespace fe
