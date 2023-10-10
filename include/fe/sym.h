#pragma once

#include <cassert>
#include <cstring>

#include <iostream>
#include <string>

#ifdef FE_ABSL
#    include <absl/container/flat_hash_map.h>
#    include <absl/container/flat_hash_set.h>
#else
#    include <unordered_map>
#    include <unordered_set>
#endif

#include "fe/arena.h"

namespace fe {

/// A Sym%bol just wraps a pointer to Sym::String, so pass Sym itself around as value.
/// Sym is compatible with:
/// * recommended: `std::string_view` (via Sym::view)
/// * null-terminated C-strings (via Sym::c_str)
///
/// This means that retrieving a `std::string_view` or a null-terminated C-string is basically free.
/// You can also obtain a `std::string` (via Sym::str), but this involves a copy.
/// With the exception of the empty string, you should only create Sym%bols via SymPool::sym.
/// This in turn will toss all Sym%bols into a big hash set.
/// This makes Sym::operator== and Sym::operator!= an O(1) operation.
/// The empty string is internally handled as `nullptr`.
/// Thus, you can create a Sym%bol representing an empty string without having access to the SymPool.
/// @note The empty `std::string`/`std::string_view`, `nullptr`, and `"\0"` are all identified as Sym::Sym().
class Sym {
public:
    struct String {
        String() = default;
        String(size_t size)
            : size(size) {}
        size_t size;
        char chars[]; // This is actually a C-only features, but all C++ compilers support that anyway.

        struct Equal {
            bool operator()(const String* d1, const String* d2) const {
                bool res = d1->size == d2->size;
                for (size_t i = 0, e = d1->size; res && i != e; ++i) res &= d1->chars[i] == d2->chars[i];
                return res;
            }
        };

        struct Hash {
            size_t operator()(const String* s) const {
                return std::hash<std::string_view>()(std::string_view(s->chars, s->size));
            }
        };

#ifdef FE_ABSL
        template<class H> friend H AbslHashValue(H h, const String* string) {
            return H::combine(std::move(h), std::string_view(string->chars, string->size));
        }
#endif
    };

    static_assert(sizeof(String) == sizeof(size_t), "String.chars should be 0");

private:
    Sym(const String* string)
        : string_(string) {}

public:
    Sym() = default;

    /// @name Getters
    ///@{
    size_t size() const { return string_ ? string_->size : 0; }
    bool empty() const { return string_ == nullptr; }
    ///@}

    /// @name Access
    ///@{
    char operator[](size_t i) const {
        assert(i < size());
        return c_str()[i];
    }
    char front() const { return (*this)[0]; }
    char back() const { return (*this)[size() - 1]; }
    ///@}

    /// @name Iterators
    ///@{
    auto begin() const { return c_str(); }
    auto end() const { return c_str() + size(); }
    auto cbegin() const { return begin(); }
    auto cend() const { return end(); }
    auto rbegin() const { return std::reverse_iterator(end()); }
    auto rend() const { return std::reverse_iterator(begin()); }
    auto crbegin() const { return rbegin(); }
    auto crend() const { return rend(); }
    ///@}

    /// @name Comparisons
    ///@{
    auto operator<=>(Sym other) const { return this->view() <=> other.view(); }
    bool operator==(Sym other) const { return this->string_ == other.string_; }
    bool operator!=(Sym other) const { return this->string_ != other.string_; }
    auto operator<=>(char c) const {
        if ((*this).size() == 0) return std::strong_ordering::less;
        auto cmp = (*this)[0] <=> c;
        if ((*this).size() == 1) return cmp;
        return cmp == 0 ? std::strong_ordering::greater : cmp;
    }
    auto operator==(char c) const { return (*this) <=> c == 0; }
    auto operator!=(char c) const { return (*this) <=> c != 0; }
    ///@}

    /// @name Conversions
    ///@{
    const char* c_str() const { return string_ ? string_->chars : empty_; }
    operator const char*() const { return c_str(); }

    std::string_view view() const {
        return string_ ? std::string_view(string_->chars, string_->size) : std::string_view();
    }
    operator std::string_view() const { return view(); }

    std::string str() const { return std::string(view()); } ///< This involves a copy.
    explicit operator std::string() const { return str(); } ///< `explicit` as this involves a copy.

    explicit operator bool() const { return string_; } ///< Is not empty?
    ///@}

#ifdef FE_ABSL
    template<class H> friend H AbslHashValue(H h, Sym sym) { return H::combine(std::move(h), sym.string_); }
#endif
    friend struct ::std::hash<fe::Sym>;
    friend std::ostream& operator<<(std::ostream& o, Sym sym) { return o << *sym; }

private:
    static constexpr const char* empty_ = "";
    const String* string_               = nullptr;

    friend class SymPool;
};

#ifndef DOXYGEN
} // namespace fe

template<> struct std::hash<fe::Sym> {
    size_t operator()(fe::Sym sym) const { return std::hash<void*>()((void*)sym.string_); }
};

namespace fe {
#endif

/// @name Sym
///@{
/// Set/Map is keyed by pointer - which is hashed in SymPool.
#ifdef FE_ABSL
template<class V> using SymMap = absl::flat_hash_map<Sym, V>;
using SymSet                   = absl::flat_hash_set<Sym>;
#else
template<class V> using SymMap = std::unordered_map<Sym, V>;
using SymSet                   = std::unordered_set<Sym>;
#endif
///@}

/// Hash set where all strings - wrapped in Sym%bol - live in.
/// You can access the SymPool from Driver.
class SymPool {
public:
    using String = Sym::String;

    SymPool(const SymPool&) = delete;
#ifdef FE_ABSL
    SymPool() {}
    SymPool(SymPool&& other)
        : strings_(std::move(other.strings_))
        , pool_(std::move(other.pool_)) {}
#else
    SymPool()
        : pool_(container_.allocator<const String*>()) {}
    SymPool(SymPool&& other)
        : strings_(std::move(other.strings_))
        , container_(std::move(other.container_))
        , pool_(std::move(other.pool_)) {
    }
#endif

    /// @name sym
    ///@{
    Sym sym(std::string_view s) {
        if (s.empty()) return Sym();
        auto state = strings_.state();
        auto ptr   = (String*)strings_.allocate(sizeof(String) + s.size() + 1 /*'\0'*/);
        new (ptr) String(s.size());
        *std::copy(s.begin(), s.end(), ptr->chars) = '\0';
        auto [i, ins] = pool_.emplace(ptr);
        if (ins) return Sym(ptr);
        strings_.deallocate(state);
        return Sym(*i);
    }
    Sym sym(const std::string& s) { return sym((std::string_view)s); }
    /// @p s is a null-terminated C-string.
    Sym sym(const char* s) { return s == nullptr || *s == '\0' ? Sym() : sym(std::string_view(s, strlen(s))); }
    // TODO we can try to fit s in current page and hence eliminate the explicit use of strlen
    ///@}

    friend void swap(SymPool& p1, SymPool& p2) {
        using std::swap;
        // clang-format off
        swap(p1.strings_,   p2.strings_  );
#ifndef FE_ABSL
        swap(p1.container_, p2.container_);
#endif
        swap(p1.pool_,      p2.pool_     );
        // clang-format on
    }

private:
    Arena strings_;
#ifdef FE_ABSL
    absl::flat_hash_set<const String*, absl::Hash<const String*>, String::Equal> pool_;
#else
    Arena container_;
    std::unordered_set<const String*, String::Hash, String::Equal, Arena::Allocator<const String*>> pool_;
#endif
};

} // namespace fe
