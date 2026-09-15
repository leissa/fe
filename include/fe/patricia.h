#pragma once

#include <concepts>
#include <cstddef>
#include <cstdint>

#include <algorithm>
#include <array>
#include <bit>
#include <functional>
#include <initializer_list>
#include <iostream>
#include <iterator>
#include <optional>
#include <ostream>
#include <print>
#include <ranges>
#include <type_traits>

#ifdef FE_ABSL
#    include <absl/container/flat_hash_set.h>
#else
#    include <unordered_set>
#endif

#include "fe/arena.h"
#include "fe/assert.h"
#include "fe/hash.h"
#include "fe/span.h"
#include "fe/vector.h"

// MSVC implements the standard attribute as a no-op and offers this one instead.
#if defined(_MSC_VER) && !defined(__clang__)
#    define FE_NO_UNIQUE_ADDRESS [[msvc::no_unique_address]]
#else
#    define FE_NO_UNIQUE_ADDRESS [[no_unique_address]]
#endif

namespace fe {

/// The value type that turns a Patricia map into a set: it occupies no space in a `Patricia::Entry`.
struct Unit {
    constexpr bool operator==(const Unit&) const noexcept = default;
};

/// How Patricia hash-conses a value; specialize this for a value type of your own.
template<class V>
struct PatriciaVal {
    static constexpr bool eq(const V& v1, const V& v2) noexcept { return v1 == v2; }
    static size_t hash(const V& v) noexcept { return std::hash<V>()(v); }
};

template<>
struct PatriciaVal<Unit> {
    static constexpr bool eq(Unit, Unit) noexcept { return true; }
    static constexpr size_t hash(Unit) noexcept { return 0; }
};

/// Hash-consed, immutable maps from an unsigned @p K to @p V.
/// A [Patricia tree](https://dl.acm.org/doi/10.1145/321479.321481) - a prefix trie over the big-endian bits of
/// the key, as in Okasaki and Gill's [Fast Mergeable Integer Maps](https://ku-fpg.github.io/papers/Okasaki-98-IntMap/)
/// - in the four flavours below, picked by size alone; equal contents are therefore *pointer-equal*.
///
/// | Flavour | Holds |
/// |---------|-------|
/// | empty   | nothing; a `Map` that converts to `false` |
/// | `Leaf`  | exactly one Patricia::Entry |
/// | `Arr`   | 2 to @p N entries, sorted by key |
/// | `Br`    | more than @p N entries: a prefix, its branching bit, and two non-empty children |
///
/// The flavour is picked for *every* subtree, so a branch's children are again leaves, arrays, or branches - which
/// collapses the bottom @f$\log_2 N@f$ levels of the trie into one array each.
/// Patricia shape is canonical (the branching bit of a key set is just the highest bit its minimum and maximum
/// disagree on), so this keeps one key set mapped to exactly one representation.
///
/// Keys are ordered as **unsigned**, i.e. iteration yields `0` first and `K(-1)` last.
/// @p VT tells Patricia how to hash-cons a @p V; @p V is `Unit` for a set - see PatriciaSet.
/// @note All operations yield a **new** Map; none of them modify their input.
/// Nothing is ever freed - the Arena%s release everything at once when the Patricia dies.
template<class D, class KT, class K, size_t N>
class PatriciaPtr;

template<class V, class K = uint64_t, size_t N = 8, class VT = PatriciaVal<V>>
class Patricia {
    static_assert(std::unsigned_integral<K>, "Patricia keys are ordered as unsigned");
    static_assert(N >= 2, "an array node holds 2 to N entries");
    static_assert(std::is_trivially_copyable_v<V> && std::is_trivially_destructible_v<V>
                      && std::is_default_constructible_v<V>,
                  "the Arena never destroys - a value must be trivially copyable/destructible");

public:
    /// One key/value pair; `sizeof(Entry) == sizeof(K)` for a set.
    struct Entry {
        K key;
        FE_NO_UNIQUE_ADDRESS V val;
    };

    /// @name Combiners
    /// What to do when both operands of an operation bind the same key.
    /// A combiner may also yield a `std::optional<V>` and drop the entry with `std::nullopt`.
    ///@{
    struct Fst {
        constexpr V operator()(K, const V& v1, const V&) const noexcept { return v1; } ///< The left one wins.
    };
    struct Snd {
        constexpr V operator()(K, const V&, const V& v2) const noexcept { return v2; } ///< The right one wins.
    };
    ///@}

private:
    /// @name Okasaki Bit Twiddling
    ///@{
    /// Bits strictly above the branching bit @p m; `0` if @p m is the top bit.
    static constexpr K above(K m) noexcept { return K(K(0) - K(m << 1)); }
    /// Is @p k left of the branching bit @p m?
    static constexpr bool zero_bit(K k, K m) noexcept { return (k & m) == 0; }
    /// @p k with the branching bit @p m and everything below it cleared; @p k itself for the leaf mask `0`.
    static constexpr K mask_of(K k, K m) noexcept { return m == 0 ? k : K(k & above(m)); }
    /// The highest bit @p k1 and @p k2 disagree on; `0` iff they are equal.
    static constexpr K branching_bit(K k1, K k2) noexcept { return std::bit_floor(K(k1 ^ k2)); }
    /// Does @p k live under the prefix @p p with branching bit @p m?
    static constexpr bool match_prefix(K k, K p, K m) noexcept { return mask_of(k, m) == p; }
    ///@}

    /// The entry bound to @p key - or `nullptr`.
    /// A block holds at most @p N entries, so a scan beats a binary search; the *insertion* point still wants
    /// `std::lower_bound`, since a miss would scan the whole block.
    static constexpr const Entry* find(View<Entry> es, K key) noexcept {
        for (const auto& e : es)
            if (e.key == key) return &e;
        return nullptr;
    }

    /// Map tags a node pointer in its two low bits, so no node may be less aligned than that.
    /// Only Leaf can fall short - Arr and Br carry a `size_t` and are aligned enough by themselves.
    static constexpr size_t Leaf_Align = std::max(alignof(Entry), size_t(4));

    /// Exactly one Entry.
    struct alignas(Leaf_Align) Leaf {
        constexpr Leaf(Entry entry) noexcept
            : entry(entry) {}

        Entry entry;
    };

    /// 2 to @p N entries, sorted by key.
    struct Arr {
        constexpr Arr(size_t hash, uint32_t size) noexcept
            : hash(hash)
            , size(size) {}

        size_t hash;
        uint32_t size;
        Entry entries[];
    };

    /// More than @p N entries.
    struct Br {
        constexpr Br(K prefix, K mask, size_t size, uintptr_t l, uintptr_t r) noexcept
            : prefix(prefix)
            , mask(mask)
            , size(size)
            , l(l)
            , r(r) {}

        K prefix;    ///< What all keys below agree on; Br::mask and everything under it cleared.
        K mask;      ///< The branching bit.
        size_t size; ///< `> N`.
        uintptr_t l; ///< Tagged Map word of the child with Br::mask clear; never empty.
        uintptr_t r; ///< Tagged Map word of the child with Br::mask set; never empty.
    };

    static_assert(alignof(Leaf) >= 4 && alignof(Arr) >= 4 && alignof(Br) >= 4);

public:
    /// An immutable map - really just a tagged pointer, so copy it around freely.
    class Map {
    private:
        enum class Tag : uintptr_t { Null = 0, Leaf = 1, Arr = 2, Br = 3 };

        static constexpr uintptr_t Tag_Mask = 0b11;

        constexpr explicit Map(uintptr_t ptr) noexcept
            : ptr_(ptr) {}
        constexpr Map(const Leaf* n) noexcept
            : ptr_(uintptr_t(n) | uintptr_t(Tag::Leaf)) {}
        constexpr Map(const Arr* n) noexcept
            : ptr_(uintptr_t(n) | uintptr_t(Tag::Arr)) {}
        constexpr Map(const Br* n) noexcept
            : ptr_(uintptr_t(n) | uintptr_t(Tag::Br)) {}

        constexpr Tag tag() const noexcept { return Tag(ptr_ & Tag_Mask); }
        template<class T>
        constexpr const T* ptr() const noexcept {
            return std::bit_cast<const T*>(ptr_ & ~Tag_Mask);
        }
        // clang-format off
        constexpr const Leaf* isa_leaf() const noexcept { return tag() == Tag::Leaf ? ptr<Leaf>() : nullptr; }
        constexpr const Arr*  isa_arr () const noexcept { return tag() == Tag::Arr  ? ptr<Arr >() : nullptr; }
        constexpr const Br*   isa_br  () const noexcept { return tag() == Tag::Br   ? ptr<Br  >() : nullptr; }
        constexpr Map left () const noexcept { return Map(ptr<Br>()->l); }
        constexpr Map right() const noexcept { return Map(ptr<Br>()->r); }
        // clang-format on

        /// The entries this very node stores; empty unless it is a `Leaf` or an `Arr`.
        constexpr View<Entry> block() const noexcept {
            if (auto n = isa_leaf()) return View<Entry>(&n->entry, size_t(1));
            if (auto n = isa_arr()) return View<Entry>(n->entries, size_t(n->size));
            return {};
        }

        /// What all keys agree on - the key itself for a `Leaf`.
        constexpr K prefix() const noexcept {
            if (auto n = isa_br()) return n->prefix;
            auto es = block();
            return mask_of(es.front().key, branching_bit(es.front().key, es.back().key));
        }

    public:
        /// Yields the Entry%s in ascending key order.
        /// @note The trie is at most `8 * sizeof(K)` deep, so the path stack sits inside the iterator.
        class iterator {
        public:
            /// @name Iterator Properties
            ///@{
            using iterator_category = std::forward_iterator_tag;
            using difference_type   = std::ptrdiff_t;
            using value_type        = Entry;
            using pointer           = const Entry*;
            using reference         = const Entry&;
            ///@}

            /// @name Construction
            ///@{
            iterator() noexcept = default;
            ///@}

            /// @name Dereference
            ///@{
            reference operator*() const noexcept { return *i_; }
            pointer operator->() const noexcept { return i_; }
            ///@}

            /// @name Increment
            ///@{
            iterator& operator++() noexcept { return ++i_ == e_ ? advance() : *this; }

            iterator operator++(int) noexcept {
                auto res = *this;
                this->operator++();
                return res;
            }
            ///@}

            /// @name Comparisons
            ///@{
            bool operator==(const iterator& other) const noexcept { return i_ == other.i_; }
            bool operator==(std::default_sentinel_t) const noexcept { return i_ == nullptr; }
            ///@}

        private:
            explicit iterator(uintptr_t w) noexcept {
                if (w != 0) descend(w);
            }

            /// Walks to the leftmost block below @p w, remembering the branches on the way.
            void descend(uintptr_t w) noexcept {
                for (; Tag(w & Tag_Mask) == Tag::Br; w = path_[depth_++]->l) {
                    // A branch's mask strictly decreases downwards and the deepest one still spans > N keys.
                    assert(depth_ < path_.size());
                    path_[depth_] = std::bit_cast<const Br*>(w & ~Tag_Mask);
                    left_ |= uint64_t(1) << depth_;
                }

                if (Tag(w & Tag_Mask) == Tag::Leaf) {
                    i_ = &std::bit_cast<const Leaf*>(w & ~Tag_Mask)->entry;
                    e_ = i_ + 1;
                } else {
                    auto n = std::bit_cast<const Arr*>(w & ~Tag_Mask);
                    i_     = n->entries;
                    e_     = n->entries + n->size;
                }
            }

            /// Climbs to the nearest branch we are still in the left child of and takes its right one.
            iterator& advance() noexcept {
                for (; depth_ != 0; --depth_) {
                    auto d = depth_ - 1;
                    if (left_ & (uint64_t(1) << d)) {
                        left_ &= ~(uint64_t(1) << d);
                        descend(path_[d]->r);
                        return *this;
                    }
                }

                i_ = e_ = nullptr;
                return *this;
            }

            const Entry* i_ = nullptr;
            const Entry* e_ = nullptr;
            std::array<const Br*, 8 * sizeof(K)> path_{};
            uint64_t left_ = 0; ///< One bit per level: are we in `path_[level]`'s left child?
            size_t depth_  = 0;

            friend class Map;
        };

        /// @name Construction
        ///@{
        constexpr Map() noexcept = default; ///< The empty map.
        ///@}

        /// @name Getters
        ///@{
        size_t size() const noexcept {
            if (auto n = isa_arr()) return n->size;
            if (auto n = isa_br()) return n->size;
            return isa_leaf() ? 1 : 0;
        }

        constexpr bool empty() const noexcept { return ptr_ == 0; }
        constexpr explicit operator bool() const noexcept { return !empty(); } ///< Not empty?
        ///@}

        /// @name Lookup
        ///@{

        /// The Entry @p key is bound to - or `nullptr`.
        /// @note The Entry lives in the Arena and hence stays valid as long as the Patricia does.
        const Entry* find(K key) const noexcept {
            for (auto t = *this;;) {
                if (auto n = t.isa_br()) {
                    if (!match_prefix(key, n->prefix, n->mask)) return nullptr;
                    t = zero_bit(key, n->mask) ? t.left() : t.right();
                } else {
                    return Patricia::find(t.block(), key);
                }
            }
        }

        bool contains(K key) const noexcept { return find(key) != nullptr; }

        const Entry* min() const noexcept { return edge(false); } ///< Smallest key - or `nullptr`.
        const Entry* max() const noexcept { return edge(true); }  ///< Largest key - or `nullptr`.

        /// Is @f$this \cap other \neq \emptyset@f$ (comparing keys)?
        [[nodiscard]] bool has_intersection(Map other) const noexcept {
            if (this->empty() || other.empty()) return false;
            if (*this == other) return true;

            auto n1 = this->isa_br();
            auto n2 = other.isa_br();

            if (!n1) {
                for (const auto& e : this->block())
                    if (other.contains(e.key)) return true;
                return false;
            }

            if (!n2) {
                for (const auto& e : other.block())
                    if (this->contains(e.key)) return true;
                return false;
            }

            if (n1->mask == n2->mask && n1->prefix == n2->prefix)
                return this->left().has_intersection(other.left()) || this->right().has_intersection(other.right());

            if (n1->mask > n2->mask && match_prefix(n2->prefix, n1->prefix, n1->mask))
                return zero_bit(n2->prefix, n1->mask) ? this->left().has_intersection(other)
                                                      : this->right().has_intersection(other);

            if (n2->mask > n1->mask && match_prefix(n1->prefix, n2->prefix, n2->mask))
                return zero_bit(n1->prefix, n2->mask) ? this->has_intersection(other.left())
                                                      : this->has_intersection(other.right());

            return false;
        }

        /// Are all keys of `this` also keys of @p other?
        [[nodiscard]] bool subset_of(Map other) const noexcept {
            if (*this == other || this->empty()) return true;
            if (this->size() > other.size()) return false;

            auto n1 = this->isa_br();
            auto n2 = other.isa_br();

            if (!n1) {
                for (const auto& e : this->block())
                    if (!other.contains(e.key)) return false;
                return true;
            }

            if (!n2) return false; // a Br holds more than N entries, so the size check above already caught this

            if (n1->mask == n2->mask && n1->prefix == n2->prefix)
                return this->left().subset_of(other.left()) && this->right().subset_of(other.right());

            if (n2->mask > n1->mask && match_prefix(n1->prefix, n2->prefix, n2->mask))
                return zero_bit(n1->prefix, n2->mask) ? this->subset_of(other.left()) : this->subset_of(other.right());

            return false;
        }
        ///@}

        /// @name Iterators
        /// Ascending by key, compared as **unsigned**.
        ///@{
        iterator begin() const noexcept { return iterator(ptr_); }
        std::default_sentinel_t end() const noexcept { return {}; }

        /// Like iterating, but without the iterator's path stack.
        template<class F>
        void for_each(F&& f) const {
            each(f);
        }
        ///@}

        /// @name Comparisons
        /// Everything is hash-consed, so this compares contents in `O(1)`.
        ///@{
        constexpr bool operator==(Map other) const noexcept { return this->ptr_ == other.ptr_; }
        ///@}

        /// @name Output
        ///@{
        std::ostream& stream(std::ostream& os) const {
            os << '{';
            auto sep = "";
            for (const auto& e : *this) {
                os << sep << +e.key;
                if constexpr (requires { os << std::declval<const V&>(); }) os << ": " << e.val;
                sep = ", ";
            }
            return os << '}';
        }

        void dump() const { stream(std::cout) << std::endl; }

        void dot(std::ostream& os) const {
            std::print(os, "digraph {{\nordering=out;\nnode [shape=box,style=filled];\n");
            dot(os, *this);
            std::print(os, "}}\n");
        }
        ///@}

    private:
        const Entry* edge(bool last) const noexcept {
            auto t = *this;
            while (auto n = t.isa_br())
                t = last ? t.right() : t.left();
            auto es = t.block();
            return es.empty() ? nullptr : (last ? &es.back() : &es.front());
        }

        template<class F>
        void each(F& f) const {
            if (isa_br()) {
                left().each(f);
                right().each(f);
            } else {
                for (const auto& e : block())
                    std::invoke(f, e);
            }
        }

        static void dot(std::ostream& os, Map map) {
            if (auto n = map.isa_br()) {
                std::print(os, "n{} [label=\"{:#x}/{:#x}\"];\n", map.ptr_, uint64_t(n->prefix), uint64_t(n->mask));
                for (auto child : {map.left(), map.right()}) {
                    std::print(os, "n{} -> n{};\n", map.ptr_, child.ptr_);
                    dot(os, child);
                }
            } else {
                std::print(os, "n{} [label=\"", map.ptr_);
                map.stream(os);
                std::print(os, "\"];\n");
            }
        }

        uintptr_t ptr_ = 0;

        friend class Patricia;
        template<class, class, class, size_t>
        friend class PatriciaPtr;
        friend std::ostream& operator<<(std::ostream& os, Map map) { return map.stream(os); }
    };

    using Set = Map; ///< What you want to spell when @p V is `Unit`.

    static_assert(std::forward_iterator<typename Map::iterator>);
    static_assert(std::ranges::range<Map>);

    /// @name Construction
    ///@{
    Patricia& operator=(const Patricia&) = delete;

    explicit Patricia(size_t page_size = Arena::Default_Page_Size)
        : leaf_arena_(page_size)
        , arr_arena_(page_size)
        , br_arena_(page_size) {}
    Patricia(const Patricia&) = delete;
    Patricia(Patricia&& other)
        : Patricia() {
        swap(*this, other);
    }
    ///@}

    /// @name Create
    /// @note A key bound more than once keeps its **last** binding, just like repeated Patricia::insert calls.
    ///@{
    template<std::ranges::input_range R>
    requires std::convertible_to<std::ranges::range_reference_t<R>, Entry> [[nodiscard]] Map create(R&& r) {
        auto v = Vector<Entry>();
        for (const Entry& e : r)
            v.emplace_back(e);
        return build(v);
    }

    [[nodiscard]] Map create(std::initializer_list<Entry> list) {
        return create(View<Entry>(list.begin(), list.size()));
    }

    /// Creates a *set* from plain keys.
    template<std::ranges::input_range R>
    requires(std::is_same_v<V, Unit> && std::convertible_to<std::ranges::range_reference_t<R>, K>)
    [[nodiscard]] Map create(R&& r) {
        auto v = Vector<Entry>();
        for (K key : r)
            v.emplace_back(Entry{key, Unit()});
        return build(v);
    }

    [[nodiscard]] Map create(std::initializer_list<K> list) requires(std::is_same_v<V, Unit>) {
        return create(View<K>(list.begin(), list.size()));
    }
    ///@}

    /// @name Map Operations
    /// @note These operations do **not** modify their input; they yield a **new** Map.
    ///@{

    /// Yields @f$t[key \mapsto val]@f$; @p f settles a clash as `f(key, old, val)`.
    template<class F = Snd>
    [[nodiscard]] Map insert(Map t, K key, V val, F f = {}) {
        if (auto n = t.isa_leaf()) {
            if (n->entry.key != key) {
                Entry es[2];
                if (key < n->entry.key)
                    es[0] = Entry{key, val}, es[1] = n->entry;
                else
                    es[0] = n->entry, es[1] = Entry{key, val};
                return arr(View<Entry>(es, size_t(2)));
            }

            auto v = apply(f, key, n->entry.val, val);
            if (!v) return {};
            return VT::eq(*v, n->entry.val) ? t : leaf(Entry{key, *v});
        }

        if (t.isa_arr()) {
            auto es  = t.block();
            auto i   = std::ranges::lower_bound(es, key, {}, &Entry::key);
            auto pos = size_t(i - es.begin());
            auto buf = std::array<Entry, N + 1>();

            if (i != es.end() && i->key == key) {
                auto v = apply(f, key, i->val, val);
                if (!v) { // dropped: copy over, skip i
                    auto o = std::ranges::copy(es.first(pos), buf.begin()).out;
                    o      = std::ranges::copy(es.subspan(pos + 1), o).out;
                    return make(View<Entry>(buf.data(), size_t(o - buf.begin())));
                }
                if (VT::eq(*v, i->val)) return t;
                std::ranges::copy(es, buf.begin());
                buf[pos].val = *v;
                return arr(View<Entry>(buf.data(), es.size()));
            }

            auto o = std::ranges::copy(es.first(pos), buf.begin()).out;
            *o++   = Entry{key, val};
            o      = std::ranges::copy(es.subspan(pos), o).out;
            return make(View<Entry>(buf.data(), size_t(o - buf.begin())));
        }

        if (auto n = t.isa_br()) {
            if (!match_prefix(key, n->prefix, n->mask)) return join(t, leaf(Entry{key, val}));

            if (zero_bit(key, n->mask)) {
                auto l = insert(t.left(), key, val, f);
                return l == t.left() ? t : br(n->prefix, n->mask, l, t.right());
            }

            auto r = insert(t.right(), key, val, f);
            return r == t.right() ? t : br(n->prefix, n->mask, t.left(), r);
        }

        return leaf(Entry{key, val});
    }

    /// Yields @f$t \cup \{key\}@f$.
    [[nodiscard]] Map insert(Map t, K key) requires(std::is_same_v<V, Unit>) { return insert(t, key, Unit()); }

    /// Yields @f$t \setminus \{key\}@f$.
    [[nodiscard]] Map erase(Map t, K key) {
        if (auto n = t.isa_leaf()) return n->entry.key == key ? Map() : t;

        if (t.isa_arr()) {
            auto es = t.block();
            auto i  = find(es, key);
            if (!i) return t;

            auto pos = size_t(i - es.data());
            auto buf = std::array<Entry, N>();
            auto o   = std::ranges::copy(es.first(pos), buf.begin()).out;
            o        = std::ranges::copy(es.subspan(pos + 1), o).out;
            return make(View<Entry>(buf.data(), size_t(o - buf.begin())));
        }

        if (auto n = t.isa_br()) {
            if (!match_prefix(key, n->prefix, n->mask)) return t;

            if (zero_bit(key, n->mask)) {
                auto l = erase(t.left(), key);
                return l == t.left() ? t : br(n->prefix, n->mask, l, t.right());
            }

            auto r = erase(t.right(), key);
            return r == t.right() ? t : br(n->prefix, n->mask, t.left(), r);
        }

        return t;
    }

    /// Yields @f$t_1 \cup t_2@f$; @p f settles a clash as `f(key, from_t1, from_t2)`.
    /// @attention The `t1 == t2` shortcut assumes @p f is idempotent, i.e. `f(key, v, v) == v`.
    template<class F = Fst>
    [[nodiscard]] Map merge(Map t1, Map t2, F f = {}) {
        if (t1 == t2 || t2.empty()) return t1;
        if (t1.empty()) return t2;

        auto n1 = t1.isa_br();
        auto n2 = t2.isa_br();

        if (!n1 && !n2) { // both hold at most N entries: one linear pass beats inserting them one by one
            auto es1 = t1.block();
            auto es2 = t2.block();
            auto buf = std::array<Entry, 2 * N>();
            auto i1 = es1.begin(), i2 = es2.begin();
            auto o = buf.begin();

            while (i1 != es1.end() && i2 != es2.end()) {
                if (i1->key < i2->key) {
                    *o++ = *i1++;
                } else if (i2->key < i1->key) {
                    *o++ = *i2++;
                } else {
                    if (auto v = apply(f, i1->key, i1->val, i2->val)) *o++ = Entry{i1->key, *v};
                    ++i1, ++i2;
                }
            }

            o = std::ranges::copy(std::ranges::subrange(i1, es1.end()), o).out;
            o = std::ranges::copy(std::ranges::subrange(i2, es2.end()), o).out;
            return make(View<Entry>(buf.data(), size_t(o - buf.begin())));
        }

        if (!n1) return insert_all(t2, t1, f, true);
        if (!n2) return insert_all(t1, t2, f, false);

        if (n1->mask == n2->mask && n1->prefix == n2->prefix)
            return br(n1->prefix, n1->mask, merge(t1.left(), t2.left(), f), merge(t1.right(), t2.right(), f));

        if (n1->mask > n2->mask && match_prefix(n2->prefix, n1->prefix, n1->mask))
            return zero_bit(n2->prefix, n1->mask) ? br(n1->prefix, n1->mask, merge(t1.left(), t2, f), t1.right())
                                                  : br(n1->prefix, n1->mask, t1.left(), merge(t1.right(), t2, f));

        if (n2->mask > n1->mask && match_prefix(n1->prefix, n2->prefix, n2->mask))
            return zero_bit(n1->prefix, n2->mask) ? br(n2->prefix, n2->mask, merge(t1, t2.left(), f), t2.right())
                                                  : br(n2->prefix, n2->mask, t2.left(), merge(t1, t2.right(), f));

        return join(t1, t2);
    }

    /// Yields @f$t_1 \cap t_2@f$; @p f settles a clash as `f(key, from_t1, from_t2)`.
    /// @attention The `t1 == t2` shortcut assumes @p f is idempotent, i.e. `f(key, v, v) == v`.
    template<class F = Fst>
    [[nodiscard]] Map intersect(Map t1, Map t2, F f = {}) {
        if (t1 == t2) return t1;
        if (t1.empty() || t2.empty()) return {};

        auto n1 = t1.isa_br();
        auto n2 = t2.isa_br();

        if (!n1) return filter(t1, t2, f, false);
        if (!n2) return filter(t2, t1, f, true);

        if (n1->mask == n2->mask && n1->prefix == n2->prefix)
            return br(n1->prefix, n1->mask, intersect(t1.left(), t2.left(), f), intersect(t1.right(), t2.right(), f));

        if (n1->mask > n2->mask && match_prefix(n2->prefix, n1->prefix, n1->mask))
            return zero_bit(n2->prefix, n1->mask) ? intersect(t1.left(), t2, f) : intersect(t1.right(), t2, f);

        if (n2->mask > n1->mask && match_prefix(n1->prefix, n2->prefix, n2->mask))
            return zero_bit(n1->prefix, n2->mask) ? intersect(t1, t2.left(), f) : intersect(t1, t2.right(), f);

        return {};
    }

    /// Yields @f$t_1 \setminus t_2@f$ - the entries of @p t1 whose key @p t2 does not bind.
    [[nodiscard]] Map diff(Map t1, Map t2) {
        if (t1 == t2) return {};
        if (t1.empty() || t2.empty()) return t1;

        auto n1 = t1.isa_br();
        auto n2 = t2.isa_br();

        if (!n1) {
            auto buf = std::array<Entry, N>();
            auto o   = buf.begin();
            for (const auto& e : t1.block())
                if (!t2.contains(e.key)) *o++ = e;
            auto size = size_t(o - buf.begin());
            return size == t1.size() ? t1 : make(View<Entry>(buf.data(), size));
        }

        if (!n2) {
            auto res = t1;
            for (const auto& e : t2.block())
                res = erase(res, e.key);
            return res;
        }

        if (n1->mask == n2->mask && n1->prefix == n2->prefix)
            return br(n1->prefix, n1->mask, diff(t1.left(), t2.left()), diff(t1.right(), t2.right()));

        if (n1->mask > n2->mask && match_prefix(n2->prefix, n1->prefix, n1->mask))
            return zero_bit(n2->prefix, n1->mask) ? br(n1->prefix, n1->mask, diff(t1.left(), t2), t1.right())
                                                  : br(n1->prefix, n1->mask, t1.left(), diff(t1.right(), t2));

        if (n2->mask > n1->mask && match_prefix(n1->prefix, n2->prefix, n2->mask))
            return zero_bit(n1->prefix, n2->mask) ? diff(t1, t2.left()) : diff(t1, t2.right());

        return t1;
    }
    ///@}

    friend void swap(Patricia& p1, Patricia& p2) noexcept {
        using std::swap;
        // clang-format off
        swap(p1.leaf_arena_, p2.leaf_arena_);
        swap(p1.arr_arena_,  p2.arr_arena_);
        swap(p1.br_arena_,   p2.br_arena_);
        swap(p1.leafs_,      p2.leafs_);
        swap(p1.arrs_,       p2.arrs_);
        swap(p1.brs_,        p2.brs_);
        // clang-format on
    }

private:
    /// @name Combiner Plumbing
    ///@{
    /// Swaps the two values, so that a combiner written for `(t1, t2)` survives being applied the other way round.
    template<class F>
    struct Flip {
        F& f;
        auto operator()(K key, const V& v1, const V& v2) const { return std::invoke(f, key, v2, v1); }
    };

    /// Normalizes a combiner that yields a plain `V` to one that yields a `std::optional<V>`.
    template<class F>
    static std::optional<V> apply(F& f, K key, const V& v1, const V& v2) {
        if constexpr (std::is_same_v<std::invoke_result_t<F&, K, const V&, const V&>, std::optional<V>>)
            return std::invoke(f, key, v1, v2);
        else
            return std::optional<V>(std::invoke(f, key, v1, v2));
    }

    /// @p small holds at most @p N entries, so folding them into @p t one by one is bounded work.
    template<class F>
    Map insert_all(Map t, Map small, F& f, bool flip) {
        for (const auto& e : small.block()) {
            if (flip) {
                auto g = Flip<F>{f};
                t      = insert(t, e.key, e.val, g);
            } else {
                t = insert(t, e.key, e.val, f);
            }
        }
        return t;
    }

    /// The entries of @p small that @p other binds as well.
    template<class F>
    Map filter(Map small, Map other, F& f, bool flip) {
        auto buf = std::array<Entry, N>();
        auto o   = buf.begin();

        for (const auto& e : small.block()) {
            if (auto x = other.find(e.key)) {
                auto v = flip ? apply(f, e.key, x->val, e.val) : apply(f, e.key, e.val, x->val);
                if (v) *o++ = Entry{e.key, *v};
            }
        }

        return make(View<Entry>(buf.data(), size_t(o - buf.begin())));
    }
    ///@}

    /// @name Node Construction
    /// This is where the four flavours and the hash-consing live.
    ///@{

    /// Sorts @p v, drops all but the last binding of each key, and hands the result to Patricia::make.
    Map build(Vector<Entry>& v) {
        std::ranges::stable_sort(v, {}, &Entry::key);

        auto o = v.begin();
        for (auto i = v.begin(), e = v.end(); i != e;) {
            auto j = i;
            while (j != e && j->key == i->key)
                ++j;
            *o++ = j[-1];
            i    = j;
        }

        return make(View<Entry>(v.data(), size_t(o - v.begin())));
    }

    /// The canonical Map for the sorted, duplicate-free @p es.
    Map make(View<Entry> es) {
        if (es.empty()) return {};
        if (es.size() == 1) return leaf(es.front());
        if (es.size() <= N) return arr(es);

        auto m = branching_bit(es.front().key, es.back().key);
        auto p = mask_of(es.front().key, m);
        auto mid
            = size_t(std::ranges::partition_point(es, [m](const Entry& e) { return zero_bit(e.key, m); }) - es.begin());
        return br(p, m, make(es.first(mid)), make(es.subspan(mid)));
    }

    /// Joins two Map%s whose key ranges are disjoint.
    Map join(Map t1, Map t2) {
        auto p1 = t1.prefix();
        auto p2 = t2.prefix();
        auto m  = branching_bit(p1, p2);
        auto p  = mask_of(p1, m);
        return zero_bit(p1, m) ? br(p, m, t1, t2) : br(p, m, t2, t1);
    }

    Map leaf(Entry e) {
        auto state    = leaf_arena_.state();
        auto node     = new (leaf_arena_.allocate<Leaf>(1)) Leaf(e);
        auto [i, ins] = leafs_.emplace(node);
        if (!ins) leaf_arena_.deallocate(state);
        return Map(*i);
    }

    Map arr(View<Entry> es) {
        assert(2 <= es.size() && es.size() <= N);
        auto state = arr_arena_.state();
        auto buff  = arr_arena_.allocate(sizeof(Arr) + es.size() * sizeof(Entry), alignof(Arr));
        auto node  = new (buff) Arr(hash_entries(es), uint32_t(es.size()));
        std::uninitialized_copy(es.begin(), es.end(), node->entries);
        auto [i, ins] = arrs_.emplace(node);
        if (!ins) arr_arena_.deallocate(state);
        return Map(*i);
    }

    /// A branch - unless @p l and @p r together still fit into one array node.
    Map br(K prefix, K mask, Map l, Map r) {
        if (l.empty()) return r;
        if (r.empty()) return l;

        auto size = l.size() + r.size();
        if (size <= N) { // both children are a Leaf or an Arr, and l's keys all precede r's: concatenate
            auto buf = std::array<Entry, N>();
            auto o   = std::ranges::copy(l.block(), buf.begin()).out;
            o        = std::ranges::copy(r.block(), o).out;
            return arr(View<Entry>(buf.data(), size));
        }

        auto state    = br_arena_.state();
        auto node     = new (br_arena_.allocate<Br>(1)) Br(prefix, mask, size, l.ptr_, r.ptr_);
        auto [i, ins] = brs_.emplace(node);
        if (!ins) br_arena_.deallocate(state);
        return Map(*i);
    }
    ///@}

    /// @name Hash-Consing
    ///@{
    static size_t hash_key(size_t h, K key) noexcept {
        h = hash_combine(h, size_t(key));
        if constexpr (sizeof(K) > sizeof(size_t)) h = hash_combine(h, size_t(uint64_t(key) >> 32));
        return h;
    }

    static size_t hash_entry(size_t h, const Entry& e) noexcept {
        return hash_combine(hash_key(h, e.key), VT::hash(e.val));
    }

    static size_t hash_entries(View<Entry> es) noexcept {
        auto h = hash_begin();
        for (const auto& e : es)
            h = hash_entry(h, e);
        return h;
    }

    struct LeafHash {
        size_t operator()(const Leaf* n) const noexcept { return hash_entry(hash_begin(), n->entry); }
    };

    struct LeafEq {
        bool operator()(const Leaf* n1, const Leaf* n2) const noexcept {
            return n1->entry.key == n2->entry.key && VT::eq(n1->entry.val, n2->entry.val);
        }
    };

    struct ArrHash {
        size_t operator()(const Arr* n) const noexcept { return n->hash; }
    };

    struct ArrEq {
        bool operator()(const Arr* n1, const Arr* n2) const noexcept {
            if (n1->size != n2->size) return false;
            for (uint32_t i = 0; i != n1->size; ++i)
                if (n1->entries[i].key != n2->entries[i].key || !VT::eq(n1->entries[i].val, n2->entries[i].val))
                    return false;
            return true;
        }
    };

    struct BrHash {
        size_t operator()(const Br* n) const noexcept {
            auto h = hash_key(hash_key(hash_begin(), n->prefix), n->mask);
            return hash_combine(hash_combine(h, n->l), n->r);
        }
    };

    struct BrEq {
        bool operator()(const Br* n1, const Br* n2) const noexcept {
            // The children are canonical already, so comparing their words settles the whole subtree.
            return n1->prefix == n2->prefix && n1->mask == n2->mask && n1->l == n2->l && n1->r == n2->r;
        }
    };

#ifdef FE_ABSL
    template<class T, class H, class E>
    using Pool = absl::flat_hash_set<const T*, H, E>;
#else
    template<class T, class H, class E>
    using Pool = std::unordered_set<const T*, H, E>;
#endif
    ///@}

    // One Arena per node kind, so that rolling a speculative allocation back on a pool hit is always LIFO.
    Arena leaf_arena_;
    Arena arr_arena_;
    Arena br_arena_;
    Pool<Leaf, LeafHash, LeafEq> leafs_;
    Pool<Arr, ArrHash, ArrEq> arrs_;
    Pool<Br, BrHash, BrEq> brs_;
};

/// A Patricia set of `uint64_t`; spell its immutable value type `PatriciaSet::Set`.
using PatriciaSet = Patricia<Unit>;

/// Hash-consed sets of `D*`, ordered by an unsigned id the elements carry themselves.
/// A thin adaptor over a Patricia whose keys are those ids and whose values are the `D*` they belong to; so a Set
/// iterates `D*` in ascending id order and equal sets are *pointer-equal*.
/// @attention Key::key *is* the identity: two elements sharing an id are the same element to a Set.
/// @p KT is a *key* trait:
/// ```
/// struct Key {
///     static K key(const D*) noexcept;                      ///< Unique id; orders the elements.
///     static std::ostream& stream(std::ostream&, const D*);  ///< Optional; defaults to Key::key.
/// };
/// ```
/// @attention Every `D` must be at least 8-byte aligned; Set stores a singleton inline in the pointer.
template<class D, class KT, class K = uint32_t, size_t N = 8>
class PatriciaPtr {
private:
    /// The key already pins down the pointer, so it need not contribute to the hash.
    struct Val {
        static constexpr bool eq(D* d1, D* d2) noexcept { return d1 == d2; }
        static constexpr size_t hash(D*) noexcept { return 0; }
    };

    using P     = Patricia<D*, K, N, Val>;
    using Entry = typename P::Entry;

public:
    /// An immutable set - really just a tagged pointer, so copy it around freely.
    class Set {
    private:
        /// `Patricia::Map` tags the low *two* bits and every node it points at is 8-byte aligned, so bit 2 is ours:
        /// a Set with it set is the element itself, and a singleton therefore costs no node at all.
        static constexpr uintptr_t Uniq = 0b100;

        constexpr Set(typename P::Map map) noexcept
            : word_(map.ptr_) {}

        constexpr bool is_uniq() const noexcept { return word_ & Uniq; }
        constexpr D* uniq() const noexcept { return std::bit_cast<D*>(word_ & ~Uniq); }
        /// @attention Only meaningful if !is_uniq().
        constexpr typename P::Map map() const noexcept { return typename P::Map(word_); }

    public:
        /// Yields the `D*` in ascending Key::key order.
        class iterator {
        public:
            /// @name Iterator Properties
            ///@{
            using iterator_category = std::forward_iterator_tag;
            using difference_type   = std::ptrdiff_t;
            using value_type        = D*;
            using pointer           = D*;
            using reference         = D*;
            ///@}

            /// @name Construction
            ///@{
            iterator() noexcept = default;
            ///@}

            /// @name Dereference
            ///@{
            reference operator*() const noexcept { return uniq_ ? uniq_ : i_->val; }
            pointer operator->() const noexcept { return this->operator*(); }
            ///@}

            /// @name Increment
            ///@{
            iterator& operator++() noexcept {
                if (uniq_)
                    uniq_ = nullptr;
                else
                    ++i_;
                return *this;
            }

            iterator operator++(int) noexcept {
                auto res = *this;
                this->operator++();
                return res;
            }
            ///@}

            /// @name Comparisons
            ///@{
            bool operator==(const iterator& other) const noexcept { return uniq_ == other.uniq_ && i_ == other.i_; }
            ///@}

        private:
            explicit iterator(D* uniq) noexcept
                : uniq_(uniq) {}
            explicit iterator(typename P::Map::iterator i) noexcept
                : i_(i) {}

            D* uniq_ = nullptr;
            typename P::Map::iterator i_;

            friend class Set;
        };

        /// @name Construction
        ///@{
        constexpr Set() noexcept = default; ///< The empty set.

        /// The singleton @f$\{d\}@f$ - stored inline, so this allocates nothing.
        constexpr explicit Set(D* d) noexcept
            : word_(std::bit_cast<uintptr_t>(d) | Uniq) {
            assert((std::bit_cast<uintptr_t>(d) & uintptr_t(0b111)) == 0 && "a D must be at least 8-byte aligned");
        }
        ///@}

        /// @name Getters
        ///@{
        size_t size() const noexcept { return is_uniq() ? size_t(1) : map().size(); }
        constexpr bool empty() const noexcept { return word_ == 0; }
        constexpr explicit operator bool() const noexcept { return word_ != 0; } ///< Not empty?

        D* min() const noexcept { return is_uniq() ? uniq() : edge(map().min()); } ///< Smallest key - or `nullptr`.
        D* max() const noexcept { return is_uniq() ? uniq() : edge(map().max()); } ///< Largest key - or `nullptr`.
        ///@}

        /// @name Check Membership
        ///@{
        bool contains(D* d) const noexcept {
            if (is_uniq()) return KT::key(uniq()) == KT::key(d);
            return map().contains(KT::key(d));
        }

        /// Is @f$this \cap other \neq \emptyset@f$?
        [[nodiscard]] bool has_intersection(Set other) const noexcept {
            if (this->is_uniq()) return other.contains(this->uniq());
            if (other.is_uniq()) return this->contains(other.uniq());
            return map().has_intersection(other.map());
        }

        /// Is @f$this \subseteq other@f$?
        [[nodiscard]] bool subset_of(Set other) const noexcept {
            if (this->empty()) return true;
            if (this->is_uniq()) return other.contains(this->uniq());
            if (other.is_uniq()) return false; // a non-uniq, non-empty Set holds at least two elements
            return map().subset_of(other.map());
        }
        ///@}

        /// @name Iterators
        ///@{
        iterator begin() const noexcept {
            if (is_uniq()) return iterator(uniq());
            return iterator(map().begin());
        }
        iterator end() const noexcept { return {}; }

        /// Like iterating, but without the iterator's path stack.
        template<class F>
        void for_each(F&& f) const {
            if (is_uniq())
                std::invoke(f, uniq());
            else
                map().for_each([&f](const Entry& e) { std::invoke(f, e.val); });
        }
        ///@}

        /// @name Comparisons
        /// Everything is hash-consed and a singleton is always Uniq, so this compares contents in `O(1)`.
        ///@{
        constexpr bool operator==(Set other) const noexcept { return this->word_ == other.word_; }
        ///@}

        /// @name Output
        ///@{
        std::ostream& stream(std::ostream& os) const {
            os << '{';
            auto sep = "";
            for (auto d : *this) {
                os << sep;
                if constexpr (requires { KT::stream(os, d); })
                    KT::stream(os, d);
                else
                    os << +KT::key(d);
                sep = ", ";
            }
            return os << '}';
        }

        void dump() const { stream(std::cout) << std::endl; }

        void dot(std::ostream& os) const {
            if (is_uniq())
                stream(os);
            else
                map().dot(os);
        }
        ///@}

    private:
        static D* edge(const Entry* e) noexcept { return e ? e->val : nullptr; }

        uintptr_t word_ = 0;

        friend class PatriciaPtr;
        friend std::ostream& operator<<(std::ostream& os, Set set) { return set.stream(os); }
    };

    static_assert(std::forward_iterator<typename Set::iterator>);
    static_assert(std::ranges::range<Set>);

    /// @name Construction
    ///@{
    PatriciaPtr& operator=(const PatriciaPtr&) = delete;

    explicit PatriciaPtr(size_t page_size = Arena::Default_Page_Size)
        : p_(page_size) {}
    PatriciaPtr(const PatriciaPtr&) = delete;
    PatriciaPtr(PatriciaPtr&& other)
        : PatriciaPtr() {
        swap(*this, other);
    }
    ///@}

    /// @name Set Operations
    /// @note These operations do **not** modify their input; they yield a **new** Set.
    ///@{
    [[nodiscard]] static Set singleton(D* d) noexcept { return Set(d); } ///< Yields @f$\{d\}@f$.

    /// Creates a Set with all elements in @p r.
    template<std::ranges::input_range R>
    requires std::convertible_to<std::ranges::range_reference_t<R>, D*> [[nodiscard]] Set create(R&& r) {
        auto v = Vector<Entry>();
        for (D* d : r)
            v.emplace_back(Entry{KT::key(d), d});
        return wrap(p_.create(v));
    }

    /// Creates a Set with all elements in `[begin, end)`.
    template<std::input_iterator I, std::sentinel_for<I> S>
    [[nodiscard]] Set create(I begin, S end) {
        return create(std::ranges::subrange(begin, end));
    }

    /// Creates a Set with all elements in @p list.
    [[nodiscard]] Set create(std::initializer_list<D*> list) {
        return create(View<D* const>(list.begin(), list.size()));
    }

    /// Yields @f$s \cup \{d\}@f$.
    [[nodiscard]] Set insert(Set s, D* d) {
        if (s.empty()) return singleton(d);

        if (s.is_uniq()) {
            auto u = s.uniq();
            if (KT::key(u) == KT::key(d)) return singleton(d);
            Entry es[2];
            if (KT::key(d) < KT::key(u))
                es[0] = Entry{KT::key(d), d}, es[1] = Entry{KT::key(u), u};
            else
                es[0] = Entry{KT::key(u), u}, es[1] = Entry{KT::key(d), d};
            return wrap(p_.create(View<Entry>(es, size_t(2))));
        }

        return wrap(p_.insert(s.map(), KT::key(d), d));
    }

    /// Yields @f$s \setminus \{d\}@f$.
    [[nodiscard]] Set erase(Set s, D* d) {
        if (s.empty()) return s;
        if (s.is_uniq()) return KT::key(s.uniq()) == KT::key(d) ? Set() : s;
        return wrap(p_.erase(s.map(), KT::key(d)));
    }

    /// Yields @f$s_1 \cup s_2@f$.
    [[nodiscard]] Set merge(Set s1, Set s2) {
        if (s1 == s2 || s2.empty()) return s1;
        if (s1.empty()) return s2;
        if (s1.is_uniq()) return insert(s2, s1.uniq());
        if (s2.is_uniq()) return insert(s1, s2.uniq());
        return wrap(p_.merge(s1.map(), s2.map()));
    }

    /// Yields @f$s_1 \cap s_2@f$.
    [[nodiscard]] Set intersect(Set s1, Set s2) {
        if (s1 == s2) return s1;
        if (s1.empty() || s2.empty()) return {};
        if (s1.is_uniq()) return s2.contains(s1.uniq()) ? s1 : Set();
        if (s2.is_uniq()) return s1.contains(s2.uniq()) ? s2 : Set();
        return wrap(p_.intersect(s1.map(), s2.map()));
    }

    /// Yields @f$s_1 \setminus s_2@f$.
    [[nodiscard]] Set diff(Set s1, Set s2) {
        if (s1 == s2) return {};
        if (s1.empty() || s2.empty()) return s1;
        if (s1.is_uniq()) return s2.contains(s1.uniq()) ? Set() : s1;
        if (s2.is_uniq()) return erase(s1, s2.uniq());
        return wrap(p_.diff(s1.map(), s2.map()));
    }
    ///@}

    friend void swap(PatriciaPtr& p1, PatriciaPtr& p2) noexcept {
        using std::swap;
        swap(p1.p_, p2.p_);
    }

private:
    /// Canonicity: a one-element Set is *always* Uniq, so a `Leaf` must never surface as a whole Set.
    static Set wrap(typename P::Map map) noexcept {
        if (auto n = map.isa_leaf()) return singleton(n->entry.val);
        return Set(map);
    }

    P p_;
};

} // namespace fe

#undef FE_NO_UNIQUE_ADDRESS
