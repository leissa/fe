#pragma once

#include <cassert>
#include <cstring>

#include <bit>
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

static_assert(std::endian::native == std::endian::little || std::endian::native == std::endian::big,
              "mixed endianess not supported");

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
/// @warning Big endian version has not been tested.
class Sym {
public:
    static constexpr size_t Short_String_Bytes = sizeof(uintptr_t);
    static constexpr size_t Short_String_Mask  = Short_String_Bytes - 1;

    struct String {
        constexpr String() noexcept = default;
        constexpr String(size_t size) noexcept
            : size(size) {}

        size_t size = 0;
        char chars[]; // This is actually a C-only features, but all C++ compilers support that anyway.

        struct Equal {
            constexpr bool operator()(const String* s1, const String* s2) const noexcept {
                bool res = s1->size == s2->size;
                for (size_t i = 0, e = s1->size; res && i != e; ++i)
                    res &= s1->chars[i] == s2->chars[i];
                return res;
            }
        };

        struct Hash {
            size_t operator()(const String* s) const noexcept {
                return std::hash<std::string_view>()(std::string_view(s->chars, s->size));
            }
        };

#ifdef FE_ABSL
        template<class H>
        friend constexpr H AbslHashValue(H h, const String* string) noexcept {
            return H::combine(std::move(h), std::string_view(string->chars, string->size));
        }
#endif
    };

    static_assert(sizeof(String) == sizeof(size_t), "String.chars should be 0");

private:
    constexpr Sym(uintptr_t ptr) noexcept
        : ptr_(ptr) {}

public:
    constexpr Sym() noexcept = default;

    /// @name Getters
    ///@{
    constexpr bool empty() const noexcept { return ptr_ == 0; }
    constexpr size_t size() const noexcept {
        if (empty()) return 0;
        if (auto size = ptr_ & Short_String_Mask) return size;
        return ((const String*)ptr_)->size;
    }
    ///@}

    /// @name Access
    ///@{
    constexpr char operator[](size_t i) const noexcept {
        assert(i < size());
        return c_str()[i];
    }
    constexpr char front() const noexcept { return (*this)[0]; }
    constexpr char back() const noexcept { return (*this)[size() - 1]; }
    ///@}

    /// @name Iterators
    ///@{
    constexpr auto begin() const noexcept { return c_str(); }
    constexpr auto end() const noexcept { return c_str() + size(); }
    constexpr auto cbegin() const noexcept { return begin(); }
    constexpr auto cend() const noexcept { return end(); }
    constexpr auto rbegin() const noexcept { return std::reverse_iterator(end()); }
    constexpr auto rend() const noexcept { return std::reverse_iterator(begin()); }
    constexpr auto crbegin() const noexcept { return rbegin(); }
    constexpr auto crend() const noexcept { return rend(); }
    ///@}

    /// @name Comparisons
    ///@{
    constexpr auto operator<=>(Sym other) const noexcept { return this->view() <=> other.view(); }
    constexpr bool operator==(Sym other) const noexcept { return this->ptr_ == other.ptr_; }
    constexpr bool operator!=(Sym other) const noexcept { return this->ptr_ != other.ptr_; }
    constexpr auto operator<=>(char c) const noexcept {
        auto s = size();
        if (s == 0) return std::strong_ordering::less;
        auto cmp = (*this)[0] <=> c;
        if (s == 1) return cmp;
        return cmp == 0 ? std::strong_ordering::greater : cmp;
    }
    constexpr auto operator==(char c) const noexcept { return (*this) <=> c == 0; }
    constexpr auto operator!=(char c) const noexcept { return (*this) <=> c != 0; }
    ///@}

    /// @name Conversions
    ///@{
    constexpr const char* c_str() const noexcept { return view().data(); }
    constexpr operator const char*() const noexcept { return c_str(); }

    constexpr std::string_view view() const noexcept {
        if (empty()) return {std::bit_cast<const char*>(&ptr_), 0};
        // Little endian: 2 a b 0 register: 0ba2
        // Big endian:    a b 0 2 register: ab02
        uintptr_t offset = std::endian::native == std::endian::little ? 1 : 0;
        if (auto size = ptr_ & Short_String_Mask) return {std::bit_cast<const char*>(&ptr_) + offset, size};
        auto S = std::bit_cast<const String*>(ptr_);
        return std::string_view(S->chars, S->size);
    }
    constexpr operator std::string_view() const noexcept { return view(); }
    constexpr std::string_view operator*() const noexcept { return view(); }
    // Unfortunately, this doesn't work:
    // std::string_view operator->() const { return view(); }

    constexpr std::string str() const noexcept { return std::string(view()); } ///< This involves a copy.
    constexpr explicit operator std::string() const noexcept { return str(); } ///< `explicit` as this involves a copy.
    constexpr explicit operator bool() const noexcept { return ptr_; }         ///< Is not empty?
    ///@}

#ifdef FE_ABSL
    template<class H>
    friend constexpr H AbslHashValue(H h, Sym sym) noexcept {
        return H::combine(std::move(h), sym.ptr_);
    }
#endif
    friend struct ::std::hash<fe::Sym>;
    friend std::ostream& operator<<(std::ostream& o, Sym sym) { return o << sym.view(); }

private:
    // Little endian: 2 a b 0 register: 0ba2
    // Big endian:    a b 0 2 register: ab02
    uintptr_t ptr_ = 0;

    friend class SymPool;
};

#ifndef DOXYGEN
} // namespace fe

template<>
struct std::hash<fe::Sym> {
    size_t operator()(fe::Sym sym) const noexcept { return std::hash<uintptr_t>()(sym.ptr_); }
};

namespace fe {
#endif

/// @name Sym
/// Set/Map is keyed by pointer - which is hashed in SymPool.
///@{
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

/// Hash set where all strings - wrapped in Sym%bol - live in.
/// You can access the SymPool from Driver.
class SymPool {
public:
    using String = Sym::String;

    /// @name Constructor & Destruction
    ///@{
    SymPool(const SymPool&) = delete;
#ifdef FE_ABSL
    SymPool() noexcept {}
#else
    SymPool() noexcept
        : pool_(container_.allocator<const String*>()) {}
#endif
    SymPool(SymPool&& other) noexcept
        : SymPool() {
        swap(*this, other);
    }
    SymPool& operator=(SymPool) = delete;
    ///@}

    /// @name sym
    ///@{
    Sym sym(std::string_view s) {
        if (s.empty()) return Sym();
        auto size = s.size();

        if (size <= Sym::Short_String_Bytes - 2) { // small string: need two more bytes for `\0' and size
            uintptr_t ptr = size;
            // Little endian: 2 a b 0 register: 0ba2
            // Big endian:    a b 0 2 register: ab02
            if constexpr (std::endian::native == std::endian::little)
                for (uintptr_t i = 0, shift = 8; i != size; ++i, shift += 8)
                    ptr |= (uintptr_t(s[i]) << shift);
            else
                for (uintptr_t i = 0, shift = (Sym::Short_String_Bytes - 1) * 8; i != size; ++i, shift -= 8)
                    ptr |= (uintptr_t(s[i]) << shift);
            return Sym(ptr);
        }

        auto state = strings_.state();
        auto ptr   = (String*)strings_.allocate(sizeof(String) + s.size() + 1 /*'\0'*/, Sym::Short_String_Bytes);
        new (ptr) String(s.size());
        *std::copy(s.begin(), s.end(), ptr->chars) = '\0';
        auto [i, ins]                              = pool_.emplace(ptr);
        if (ins) return Sym(std::bit_cast<uintptr_t>(ptr));
        strings_.deallocate(state);
        return Sym(std::bit_cast<uintptr_t>(*i));
    }
    Sym sym(const std::string& s) { return sym((std::string_view)s); }
    /// @p s is a null-terminated C-string.
    constexpr Sym sym(const char* s) {
        return s == nullptr || *s == '\0' ? Sym() : sym(std::string_view(s, strlen(s)));
    }
    // TODO we can try to fit s in current page and hence eliminate the explicit use of strlen
    ///@}

    friend void swap(SymPool& p1, SymPool& p2) noexcept {
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
