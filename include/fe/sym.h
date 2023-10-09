#pragma once

#include <cassert>
#include <cstring>

#include <iostream>
#include <string>

#ifdef FE_ABSL
#    include <absl/container/flat_hash_map.h>
#    include <absl/container/flat_hash_set.h>
#    include <absl/container/node_hash_set.h>
#else
#    include <unordered_map>
#    include <unordered_set>
#endif

#include "fe/arena.h"

namespace fe {

/// A Sym%bol just wraps a `const std::string*`, so pass Sym itself around as value.
/// With the exception of the empty string, you should only create Sym%bols via SymPool::sym.
/// This in turn will toss all Sym%bols into a big hash set.
/// This makes Sym::operator== and Sym::operator!= an O(1) operation.
/// The empty string is internally handled as `nullptr`.
/// Thus, you can create a Sym%bol representing an empty string without having access to the SymPool.
/// @note The empty `std::string`, `nullptr`, and `"\0"` are all identified as Sym::Sym().
class Sym {
public:
    struct Data {
        Data() = default;
        Data(size_t size)
            : size(size) {}
        size_t size;
        char chars[]; // This is actually a C-only features, but all C++ compilers support that anyway.

        struct Equal {
            bool operator()(const Data* d1, const Data* d2) const {
                bool res = d1->size == d2->size;
                for (size_t i = 0, e = d1->size; res && i != e; ++i) res &= d1->chars[i] == d2->chars[i];
                return res;
            }
        };

        struct Hash {
            size_t operator()(fe::Sym::Data* data) const {
                return std::hash<std::string_view>()(std::string_view(data->chars, data->size));
            }
        };

#ifdef FE_ABSL
        template<class H> friend H AbslHashValue(H h, Data* data) {
            return H::combine(std::move(h), std::string_view(data->chars, data->size));
        }
#endif
    };

    static_assert(sizeof(Data) == sizeof(size_t), "Data.chars should be 0");

private:
    Sym(const Data* data)
        : data_(data) {}

public:
    Sym() = default;

    /// @name Getters
    ///@{
    size_t size() const { return data_ ? data_->size : 0; }
    bool empty() const { return size() == 0; }
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
    bool operator==(Sym other) const { return this->data_ == other.data_; }
    bool operator!=(Sym other) const { return this->data_ != other.data_; }
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
    const char* c_str() const { return data_ ? data_->chars : empty_str; }
    operator const char*() const { return c_str(); }

    std::string_view view() const { return data_ ? std::string_view(data_->chars, data_->size) : std::string_view(); }
    std::string_view operator*() const { return view(); }
    operator std::string_view() const { return view(); }

    std::string str() const { return std::string(view()); }
    explicit operator std::string() const { return str(); } ///< `explicit` as this involves a copy.

    explicit operator bool() const { return data_; }
    ///@}

#ifdef FE_ABSL
    template<class H> friend H AbslHashValue(H h, Sym sym) { return H::combine(std::move(h), sym.data_); }
#endif
    friend struct ::std::hash<fe::Sym>;
    friend std::ostream& operator<<(std::ostream& o, Sym sym) { return o << *sym; }

private:
    static constexpr const char* empty_str = "";
    const Data* data_                      = nullptr;

    friend class SymPool;
};

#ifndef DOXYGEN
} // namespace fe

template<> struct std::hash<fe::Sym> {
    size_t operator()(fe::Sym sym) const { return std::hash<void*>()((void*)sym.data_); }
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
    SymPool()
        : pool_(arena_.allocator<Sym::Data*>()) {}
    SymPool(SymPool&& other)
        : arena_(std::move(other.arena_))
        , pool_(std::move(other.pool_)) {}
    SymPool(const SymPool&) = delete;

    /// @name sym
    ///@{
    Sym sym(std::string_view s) {
        if (s.empty()) return Sym();
        auto state = arena_.state();
        auto ptr   = (Sym::Data*)arena_.allocate(sizeof(Sym::Data) + s.size() + 1 /*'\0'*/);
        new (ptr) Sym::Data(s.size());
        *std::copy(s.begin(), s.end(), ptr->chars) = '\0';
        auto [i, ins]                              = pool_.emplace(ptr);
        if (ins) return Sym(ptr);
        arena_.deallocate(state);
        return Sym(*i);
    }
    Sym sym(const std::string& s) { return sym((std::string_view)s); }
    /// @p s is a null-terminated C-string.
    Sym sym(const char* s) { return s == nullptr || *s == '\0' ? Sym() : sym(std::string_view(s, strlen(s))); }
    // TODO we can try to fit s in current page and hence eliminate the explicit use of strlen
    ///@}

    friend void swap(SymPool& p1, SymPool& p2) {
        using std::swap;
        swap(p1.pool_, p2.pool_);
    }

private:
    Arena arena_;
#ifdef FE_ABSL
    absl::node_hash_set<Sym::Data*, absl::Hash<Sym::Data*>, Sym::Data::Equal, Arena::Allocator<Sym::Data*>> pool_;
#else
    std::unordered_set<Sym::Data*, Sym::Data::Hash, Sym::Data::Equal, Arena::Allocator<Sym::Data*>> pool_;
#endif
};

} // namespace fe
