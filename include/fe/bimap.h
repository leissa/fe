#pragma once

#include <concepts>
#include <cstddef>

#include <functional>
#include <stdexcept>
#include <type_traits>
#include <utility>

#include <ankerl/unordered_dense.h>

namespace fe {

namespace detail {
template<class H, class E>
concept Transparent = requires {
    typename H::is_transparent;
    typename E::is_transparent;
};

template<class K, class T>
concept NotConvertibleTo = !std::convertible_to<const K&, T>;
} // namespace detail

/// A bijection between @p L and @p R, kept as one `ankerl::unordered_dense::map` per direction.
/// Look up through left() and right(); a transparent hasher and equality enable heterogeneous lookup there as well.
/// Iterating a BiMap yields its `std::pair<L, R>`s in left() order.
/// @note Every pair is stored once per direction, so keep @p L and @p R cheap to copy - a Sym or a pointer, say.
/// Otherwise, or if you need a pair's address to survive an insert, use BiNodeMap.
template<class L,
         class R,
         class LHash = ankerl::unordered_dense::hash<L>,
         class LEq   = std::equal_to<L>,
         class RHash = ankerl::unordered_dense::hash<R>,
         class REq   = std::equal_to<R>>
class BiMap {
public:
    using Left  = ankerl::unordered_dense::map<L, R, LHash, LEq>;
    using Right = ankerl::unordered_dense::map<R, L, RHash, REq>;

    /// @name Getters
    ///@{
    [[nodiscard]] const Left& left() const noexcept { return l2r_; }
    [[nodiscard]] const Right& right() const noexcept { return r2l_; }
    [[nodiscard]] size_t size() const noexcept { return l2r_.size(); }
    [[nodiscard]] bool empty() const noexcept { return l2r_.empty(); }
    [[nodiscard]] auto begin() const noexcept { return l2r_.begin(); }
    [[nodiscard]] auto end() const noexcept { return l2r_.end(); }
    ///@}

    /// @name Lookup
    /// at_left() and at_right() throw `std::out_of_range` if the key is unbound.
    ///@{
    [[nodiscard]] bool contains_left(const auto& l) const { return l2r_.contains(l); }
    [[nodiscard]] bool contains_right(const auto& r) const { return r2l_.contains(r); }
    [[nodiscard]] const R& at_left(const auto& l) const { return l2r_.at(l); }
    [[nodiscard]] const L& at_right(const auto& r) const { return r2l_.at(r); }
    ///@}

    /// @name Modifiers
    /// They take their arguments by value, so a reference into this BiMap cannot dangle halfway through.
    ///@{

    /// Binds @p l to @p r unless either is already bound; yields whether it did.
    bool insert(L l, R r) {
        auto [i, ins] = l2r_.try_emplace(l, r);
        return ins && link(i, std::move(l), std::move(r));
    }

    /// Binds @p l to @p r after unbinding whatever either was bound to.
    void insert_or_assign(L l, R r) {
        erase_left(l);
        erase_right(r);
        link(l2r_.emplace(l, r).first, std::move(l), std::move(r));
    }

    /// Unbinds @p l and its partner; yields whether @p l was bound.
    bool erase_left(const auto& l) {
        auto i = l2r_.find(l);
        if (i == l2r_.end()) return false;
        r2l_.erase(i->second);
        l2r_.erase(i);
        return true;
    }

    /// Unbinds @p r and its partner; yields whether @p r was bound.
    bool erase_right(const auto& r) {
        auto i = r2l_.find(r);
        if (i == r2l_.end()) return false;
        l2r_.erase(i->second);
        r2l_.erase(i);
        return true;
    }

    void reserve(size_t n) {
        l2r_.reserve(n);
        r2l_.reserve(n);
    }

    void clear() noexcept {
        l2r_.clear();
        r2l_.clear();
    }
    ///@}

    friend bool operator==(const BiMap& a, const BiMap& b) { return a.l2r_ == b.l2r_; }

private:
    bool link(typename Left::iterator i, L&& l, R&& r) {
        try {
            if (r2l_.try_emplace(std::move(r), std::move(l)).second) return true;
        } catch (...) {
            l2r_.erase(i);
            throw;
        }
        l2r_.erase(i);
        return false;
    }

    Left l2r_;
    Right r2l_;
};

/// A bijection between @p L and @p R that stores every pair once, in an `ankerl::unordered_dense::segmented_map`.
/// The right side is a set of pointers to those pairs, hashed and compared by their @p R.
/// An insert never moves a pair, and neither does moving the BiNodeMap itself.
/// An erase, however, moves the last pair into the freed slot, as it does in any `ankerl::unordered_dense` map.
/// Its interface is that of BiMap, except that right() has no map to yield; find_right() takes its place.
template<class L,
         class R,
         class LHash = ankerl::unordered_dense::hash<L>,
         class LEq   = std::equal_to<L>,
         class RHash = ankerl::unordered_dense::hash<R>,
         class REq   = std::equal_to<R>>
class BiNodeMap {
public:
    using Left       = ankerl::unordered_dense::segmented_map<L, R, LHash, LEq>;
    using value_type = typename Left::value_type;

private:
    using Ptr = const value_type*;

    struct Hash {
        using is_transparent = void;
        using is_avalanching = std::bool_constant<ankerl::unordered_dense::hash_is_avalanching_v<RHash>>;

        size_t operator()(Ptr p) const { return hash(p->second); }
        template<detail::NotConvertibleTo<Ptr> K>
        size_t operator()(const K& r) const {
            return hash(r);
        }

        RHash hash;
    };

    struct Eq {
        using is_transparent = void;

        bool operator()(Ptr a, Ptr b) const { return eq(a->second, b->second); }
        template<detail::NotConvertibleTo<Ptr> K>
        bool operator()(const K& r, Ptr b) const {
            return eq(r, b->second);
        }

        REq eq;
    };

public:
    BiNodeMap() = default;
    BiNodeMap(const BiNodeMap& other)
        : l2r_(other.l2r_) {
        r2l_.reserve(l2r_.size());
        for (const auto& p : l2r_)
            r2l_.emplace(&p);
    }
    BiNodeMap(BiNodeMap&&) noexcept = default;

    BiNodeMap& operator=(const BiNodeMap& other) {
        *this = BiNodeMap(other);
        return *this;
    }
    BiNodeMap& operator=(BiNodeMap&&) noexcept = default;

    /// @name Getters
    ///@{
    [[nodiscard]] const Left& left() const noexcept { return l2r_; }
    [[nodiscard]] size_t size() const noexcept { return l2r_.size(); }
    [[nodiscard]] bool empty() const noexcept { return l2r_.empty(); }
    [[nodiscard]] auto begin() const noexcept { return l2r_.begin(); }
    [[nodiscard]] auto end() const noexcept { return l2r_.end(); }
    ///@}

    /// @name Lookup
    /// at_left() and at_right() throw `std::out_of_range` if the key is unbound.
    ///@{
    [[nodiscard]] bool contains_left(const auto& l) const { return l2r_.contains(l); }
    [[nodiscard]] bool contains_right(const auto& r) const { return find_right(r) != nullptr; }
    [[nodiscard]] const R& at_left(const auto& l) const { return l2r_.at(l); }
    [[nodiscard]] const L& at_right(const auto& r) const {
        if (auto p = find_right(r)) return p->first;
        throw std::out_of_range("fe::BiNodeMap::at_right");
    }

    /// Yields the pair whose right side is @p r, or `nullptr`.
    template<class K>
    [[nodiscard]] Ptr find_right(const K& r) const {
        if constexpr (std::same_as<K, R> || detail::Transparent<RHash, REq>) {
            auto i = r2l_.find(r);
            return i != r2l_.end() ? *i : nullptr;
        } else {
            const R& key = r;
            return find_right(key);
        }
    }
    ///@}

    /// @name Modifiers
    /// They take their arguments by value, so a reference into this BiNodeMap cannot dangle halfway through.
    ///@{

    /// Binds @p l to @p r unless either is already bound; yields whether it did.
    bool insert(L l, R r) {
        auto [i, ins] = l2r_.try_emplace(std::move(l), std::move(r));
        return ins && index(i);
    }

    /// Binds @p l to @p r after unbinding whatever either was bound to.
    void insert_or_assign(L l, R r) {
        erase_left(l);
        erase_right(r);
        index(l2r_.try_emplace(std::move(l), std::move(r)).first);
    }

    /// Unbinds @p l and its partner; yields whether @p l was bound.
    bool erase_left(const auto& l) {
        auto i = l2r_.find(l);
        if (i == l2r_.end()) return false;
        erase(&*i);
        return true;
    }

    /// Unbinds @p r and its partner; yields whether @p r was bound.
    bool erase_right(const auto& r) {
        auto p = find_right(r);
        if (!p) return false;
        erase(p);
        return true;
    }

    void reserve(size_t n) {
        l2r_.reserve(n);
        r2l_.reserve(n);
    }

    void clear() noexcept {
        r2l_.clear();
        l2r_.clear();
    }
    ///@}

    friend bool operator==(const BiNodeMap& a, const BiNodeMap& b) { return a.l2r_ == b.l2r_; }

private:
    bool index(typename Left::iterator i) {
        try {
            if (r2l_.emplace(&*i).second) return true;
        } catch (...) {
            l2r_.erase(i);
            throw;
        }
        l2r_.erase(i);
        return false;
    }

    void erase(Ptr p) {
        Ptr back = &l2r_.values().back();
        r2l_.erase(p);
        auto j = p != back ? r2l_.find(back) : r2l_.end();
        l2r_.erase(p->first);
        // The former back now lives at p and keeps its R, so only the pointer changes, not its hash.
        if (j != r2l_.end()) const_cast<Ptr&>(*j) = p;
    }

    Left l2r_;
    ankerl::unordered_dense::set<Ptr, Hash, Eq> r2l_;
};

} // namespace fe
