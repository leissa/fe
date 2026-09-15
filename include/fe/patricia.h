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
#include <memory>
#include <ostream>
#include <print>
#include <ranges>

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

namespace fe {

/// Hash-consed, immutable sets of `D*`, ordered by an unsigned id the elements carry themselves.
/// A [Patricia tree](https://dl.acm.org/doi/10.1145/321479.321481) - a prefix trie over the big-endian bits of
/// the id, as in Okasaki and Gill's [Fast Mergeable Integer Maps](https://ku-fpg.github.io/papers/Okasaki-98-IntMap/)
/// - in the four flavours below, picked by size alone; equal sets are therefore *pointer-equal*.
///
/// | Flavour | Holds |
/// |---------|-------|
/// | empty   | nothing; a Set that converts to `false` |
/// | `Uniq`  | exactly one `D*` - inline in the Set itself, so a singleton costs no node at all |
/// | `Arr`   | 2 to @p N elements, sorted by id |
/// | `Br`    | more than @p N elements: a prefix, its branching bit, and two non-empty children |
///
/// The flavour is picked for *every* subtree, so a branch's children are again single elements, arrays, or
/// branches - which collapses the bottom @f$\log_2 N@f$ levels of the trie into one array each.
/// Patricia shape is canonical (the branching bit of an id set is just the highest bit its minimum and maximum
/// disagree on), so this keeps one element set mapped to exactly one representation.
///
/// Ids are ordered as **unsigned**, i.e. iteration yields `0` first and `K(-1)` last.
/// @p KT is a *key* trait:
/// ```
/// struct Key {
///     static K key(const D*) noexcept;                       ///< Unique id; orders the elements.
///     static std::ostream& stream(std::ostream&, const D*);  ///< Optional; defaults to Key::key.
/// };
/// ```
/// @attention Key::key *is* the identity: two elements sharing an id are the same element to a Set, and which of
/// them survives an operation is unspecified.
/// Every `D` must be at least 4-byte aligned, since a Set tags the two low bits of its word.
/// @note All operations yield a **new** Set; none of them modify their input.
/// Nothing is ever freed - the Arena%s release everything at once when the Patricia dies.
template<class D, class KT, class K = uint32_t, size_t N = 8>
class Patricia {
    static_assert(std::unsigned_integral<K>, "Patricia ids are ordered as unsigned");
    static_assert(N >= 2, "an array node holds 2 to N elements");

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

    /// An element and its id.
    /// The id is redundant - but reading it through `KT::key` would chase the pointer on every comparison.
    struct Entry {
        K key;
        D* d;
    };

    /// 2 to @p N entries, sorted by id.
    struct Arr {
        constexpr Arr(size_t hash, uint32_t size) noexcept
            : hash(hash)
            , size(size) {}

        size_t hash; ///< Cached: the pool rehashes, so recomputing this over all Arr::entries hurts.
        uint32_t size;
        Entry entries[];
    };

    /// More than @p N elements.
    struct Br {
        constexpr Br(K prefix, K mask, size_t size, uintptr_t l, uintptr_t r) noexcept
            : prefix(prefix)
            , mask(mask)
            , size(size)
            , l(l)
            , r(r) {}

        K prefix;    ///< What all ids below agree on; Br::mask and everything under it cleared.
        K mask;      ///< The branching bit.
        size_t size; ///< `> N`.
        uintptr_t l; ///< Tagged Set word of the child with Br::mask clear; never empty.
        uintptr_t r; ///< Tagged Set word of the child with Br::mask set; never empty.
    };

    static_assert(alignof(Arr) >= 4 && alignof(Br) >= 4);

    /// How the id spans of two branches relate - the case analysis every binary operation below walks.
    enum class Rel {
        Same, ///< The same span: their children pair up.
        L1,   ///< The second one lives under the first one's left child.
        R1,   ///< ... under its right child.
        L2,   ///< The first one lives under the second one's left child.
        R2,   ///< ... under its right child.
        None, ///< Disjoint spans.
    };

    static Rel rel(const Br* n1, const Br* n2) noexcept {
        if (n1->mask == n2->mask && n1->prefix == n2->prefix) return Rel::Same;

        if (n1->mask > n2->mask && match_prefix(n2->prefix, n1->prefix, n1->mask))
            return zero_bit(n2->prefix, n1->mask) ? Rel::L1 : Rel::R1;

        if (n2->mask > n1->mask && match_prefix(n1->prefix, n2->prefix, n2->mask))
            return zero_bit(n1->prefix, n2->mask) ? Rel::L2 : Rel::R2;

        return Rel::None;
    }

public:
    /// An immutable set - really just a tagged pointer, so copy it around freely.
    class Set {
    private:
        /// An untagged word *is* the `D*` it holds, so a `Uniq` needs no node - and `0` is the empty Set.
        enum class Tag : uintptr_t { Uniq = 0, Arr = 1, Br = 2 };

        static constexpr uintptr_t Tag_Mask = 0b11;

        constexpr explicit Set(uintptr_t word) noexcept
            : word_(word) {}
        constexpr Set(const Arr* n) noexcept
            : word_(std::bit_cast<uintptr_t>(n) | uintptr_t(Tag::Arr)) {}
        constexpr Set(const Br* n) noexcept
            : word_(std::bit_cast<uintptr_t>(n) | uintptr_t(Tag::Br)) {}

        constexpr Tag tag() const noexcept { return Tag(word_ & Tag_Mask); }
        template<class T>
        constexpr const T* ptr() const noexcept {
            return std::bit_cast<const T*>(word_ & ~Tag_Mask);
        }
        // clang-format off
        /// The one element of a `Uniq` - `nullptr` for every other flavour, the empty Set included.
        constexpr D* isa_uniq() const noexcept { return tag() == Tag::Uniq ? std::bit_cast<D*>(word_) : nullptr; }
        constexpr const Arr* isa_arr() const noexcept { return tag() == Tag::Arr ? ptr<Arr>() : nullptr; }
        constexpr const Br*  isa_br () const noexcept { return tag() == Tag::Br  ? ptr<Br >() : nullptr; }
        constexpr Set left () const noexcept { return Set(ptr<Br>()->l); }
        constexpr Set right() const noexcept { return Set(ptr<Br>()->r); }
        // clang-format on

        /// The entries of an `Arr`; empty for every other flavour - a `Uniq` has no node to point at.
        constexpr View<Entry> entries() const noexcept {
            if (auto n = isa_arr()) return View<Entry>(n->entries, size_t(n->size));
            return {};
        }

        /// What all ids agree on - the id itself for a `Uniq`.
        K prefix() const noexcept {
            if (auto n = isa_br()) return n->prefix;
            if (auto d = isa_uniq()) return KT::key(d);
            auto es = entries();
            return mask_of(es.front().key, branching_bit(es.front().key, es.back().key));
        }

    public:
        /// Yields the `D*` in ascending Key::key order.
        /// @note The trie is at most `8 * sizeof(K)` deep, so the path stack sits inside the iterator.
        class iterator {
        public:
            using iterator_category = std::forward_iterator_tag;
            using difference_type   = std::ptrdiff_t;
            using value_type        = D*;
            using pointer           = D*;
            using reference         = D*;

            iterator() noexcept = default;

            reference operator*() const noexcept { return uniq_ ? uniq_ : i_->d; }
            pointer operator->() const noexcept { return this->operator*(); }

            iterator& operator++() noexcept {
                if (uniq_) return uniq_ = nullptr, advance();
                return ++i_ == e_ ? advance() : *this;
            }

            iterator operator++(int) noexcept {
                auto res = *this;
                this->operator++();
                return res;
            }

            bool operator==(const iterator& other) const noexcept { return i_ == other.i_ && uniq_ == other.uniq_; }

        private:
            explicit iterator(uintptr_t w) noexcept {
                if (w != 0) descend(w);
            }

            /// Walks to the leftmost block below @p w, remembering the branches on the way.
            void descend(uintptr_t w) noexcept {
                for (; Tag(w & Tag_Mask) == Tag::Br; w = path_[depth_++]->l) {
                    // A branch's mask strictly decreases downwards and the deepest one still spans > N ids.
                    assert(depth_ < path_.size());
                    path_[depth_] = std::bit_cast<const Br*>(w & ~Tag_Mask);
                    left_ |= uint64_t(1) << depth_;
                }

                if (Tag(w & Tag_Mask) == Tag::Uniq) {
                    uniq_ = std::bit_cast<D*>(w);
                    i_ = e_ = nullptr;
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

                uniq_ = nullptr;
                i_ = e_ = nullptr;
                return *this;
            }

            D* uniq_        = nullptr; ///< The element of a `Uniq`, which has no node to point into.
            const Entry* i_ = nullptr;
            const Entry* e_ = nullptr;
            std::array<const Br*, 8 * sizeof(K)> path_{};
            uint64_t left_ = 0; ///< One bit per level: are we in `path_[level]`'s left child?
            size_t depth_  = 0;

            friend class Set;
        };

        /// @name Construction
        ///@{
        constexpr Set() noexcept = default; ///< The empty set.

        /// The singleton @f$\{d\}@f$ - stored inline, so this allocates nothing.
        constexpr explicit Set(D* d) noexcept
            : word_(std::bit_cast<uintptr_t>(d)) {
            assert((word_ & Tag_Mask) == 0 && "a D must be at least 4-byte aligned");
        }
        ///@}

        /// @name Getters
        ///@{
        size_t size() const noexcept {
            if (auto n = isa_arr()) return n->size;
            if (auto n = isa_br()) return n->size;
            return empty() ? 0 : 1;
        }

        constexpr bool empty() const noexcept { return word_ == 0; }
        constexpr explicit operator bool() const noexcept { return !empty(); } ///< Not empty?

        D* min() const noexcept { return edge(false); } ///< Smallest id - or `nullptr`.
        D* max() const noexcept { return edge(true); }  ///< Largest id - or `nullptr`.
        ///@}

        /// @name Check Membership
        ///@{
        bool contains(D* d) const noexcept { return lookup(KT::key(d)) != nullptr; }

        /// Is @f$this \cap other \neq \emptyset@f$?
        [[nodiscard]] bool has_intersection(Set other) const noexcept {
            if (this->empty() || other.empty()) return false;
            if (*this == other) return true;

            auto n1 = this->isa_br();
            auto n2 = other.isa_br();

            if (!n1) return this->any_in(other);
            if (!n2) return other.any_in(*this);

            switch (rel(n1, n2)) {
                case Rel::Same:
                    return this->left().has_intersection(other.left()) || this->right().has_intersection(other.right());
                case Rel::L1: return this->left().has_intersection(other);
                case Rel::R1: return this->right().has_intersection(other);
                case Rel::L2: return this->has_intersection(other.left());
                case Rel::R2: return this->has_intersection(other.right());
                case Rel::None: return false;
            }
            unreachable();
        }

        /// Is @f$this \subseteq other@f$?
        [[nodiscard]] bool subset_of(Set other) const noexcept {
            if (*this == other || this->empty()) return true;
            if (this->size() > other.size()) return false;

            auto n1 = this->isa_br();
            auto n2 = other.isa_br();

            if (!n1) return this->all_in(other);
            if (!n2) return false; // a Br holds more than N elements, so the size check above already caught this

            switch (rel(n1, n2)) {
                case Rel::Same: return this->left().subset_of(other.left()) && this->right().subset_of(other.right());
                case Rel::L2: return this->subset_of(other.left());
                case Rel::R2: return this->subset_of(other.right());
                default: return false; // `this` spans ids `other` does not even branch on
            }
        }
        ///@}

        /// @name Iterators
        /// Ascending by id, compared as **unsigned**.
        ///@{
        iterator begin() const noexcept { return iterator(word_); }
        iterator end() const noexcept { return {}; }

        /// Like iterating, but without the iterator's path stack.
        template<class F>
        void for_each(F&& f) const {
            if (isa_br()) {
                left().for_each(f);
                right().for_each(f);
            } else if (auto d = isa_uniq()) {
                std::invoke(f, d);
            } else {
                for (const auto& e : entries())
                    std::invoke(f, e.d);
            }
        }
        ///@}

        /// @name Comparisons
        /// Everything is hash-consed and a singleton is always `Uniq`, so this compares contents in `O(1)`.
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
            std::print(os, "digraph {{\nordering=out;\nnode [shape=box,style=filled];\n");
            dot(os, *this);
            std::print(os, "}}\n");
        }
        ///@}

    private:
        /// The element with @p key - or `nullptr`.
        /// A block holds at most @p N elements, so a scan beats a binary search; the *insertion* point still wants
        /// `std::lower_bound`, since a miss would scan the whole block.
        D* lookup(K key) const noexcept {
            for (auto s = *this;;) {
                if (auto n = s.isa_br()) {
                    if (!match_prefix(key, n->prefix, n->mask)) return nullptr;
                    s = zero_bit(key, n->mask) ? s.left() : s.right();
                } else if (auto d = s.isa_uniq()) {
                    return KT::key(d) == key ? d : nullptr;
                } else {
                    for (const auto& e : s.entries())
                        if (e.key == key) return e.d;
                    return nullptr;
                }
            }
        }

        D* edge(bool last) const noexcept {
            auto s = *this;
            while (auto n = s.isa_br())
                s = last ? s.right() : s.left();
            if (auto d = s.isa_uniq()) return d;
            auto es = s.entries();
            return es.empty() ? nullptr : (last ? es.back().d : es.front().d);
        }

        /// @name Block Predicates
        /// `this` is not a `Br` and hence holds at most @p N elements.
        ///@{
        bool any_in(Set other) const noexcept {
            if (auto d = isa_uniq()) return other.contains(d);
            return std::ranges::any_of(entries(), [other](const Entry& e) { return other.lookup(e.key); });
        }

        bool all_in(Set other) const noexcept {
            if (auto d = isa_uniq()) return other.contains(d);
            return std::ranges::all_of(entries(), [other](const Entry& e) { return other.lookup(e.key); });
        }
        ///@}

        /// Appends the entries this very node stores - none for a `Br` - to @p o.
        template<class O>
        O copy(O o) const noexcept {
            if (auto d = isa_uniq()) return *o++ = Entry{KT::key(d), d}, o;
            return std::ranges::copy(entries(), o).out;
        }

        static void dot(std::ostream& os, Set s) {
            if (auto n = s.isa_br()) {
                std::print(os, "n{} [label=\"{:#x}/{:#x}\"];\n", s.word_, uint64_t(n->prefix), uint64_t(n->mask));
                for (auto child : {s.left(), s.right()}) {
                    std::print(os, "n{} -> n{};\n", s.word_, child.word_);
                    dot(os, child);
                }
            } else {
                std::print(os, "n{} [label=\"", s.word_);
                s.stream(os);
                std::print(os, "\"];\n");
            }
        }

        uintptr_t word_ = 0;

        friend class Patricia;
        friend std::ostream& operator<<(std::ostream& os, Set s) { return s.stream(os); }
    };

    static_assert(std::forward_iterator<typename Set::iterator>);
    static_assert(std::ranges::range<Set>);

    /// @name Construction
    ///@{
    Patricia& operator=(const Patricia&) = delete;

    explicit Patricia(size_t page_size = Arena::Default_Page_Size)
        : arr_arena_(page_size)
        , br_arena_(page_size) {}
    Patricia(const Patricia&) = delete;
    Patricia(Patricia&& other)
        : Patricia() {
        swap(*this, other);
    }
    ///@}

    /// @name Set Operations
    /// @note These operations do **not** modify their input; they yield a **new** Set.
    ///@{

    /// Creates a Set with all elements in @p r.
    template<std::ranges::input_range R>
    requires std::convertible_to<std::ranges::range_reference_t<R>, D*> [[nodiscard]] Set create(R&& r) {
        auto v = Vector<Entry>();
        for (D* d : r)
            v.emplace_back(Entry{KT::key(d), d});
        return build(v);
    }

    [[nodiscard]] Set create(std::initializer_list<D*> list) { return create(View<D*>(list.begin(), list.size())); }

    /// Yields @f$s \cup \{d\}@f$.
    /// @note @p s comes back unchanged if it already holds an element with @p d's id.
    [[nodiscard]] Set insert(Set s, D* d) {
        auto key = KT::key(d);

        if (s.empty()) return Set(d);

        if (auto u = s.isa_uniq()) {
            auto k = KT::key(u);
            if (k == key) return s;
            Entry es[2];
            if (key < k)
                es[0] = Entry{key, d}, es[1] = Entry{k, u};
            else
                es[0] = Entry{k, u}, es[1] = Entry{key, d};
            return arr(View<Entry>(es, size_t(2)));
        }

        if (auto n = s.isa_br()) {
            if (!match_prefix(key, n->prefix, n->mask)) return join(s, Set(d));

            if (zero_bit(key, n->mask)) {
                auto l = insert(s.left(), d);
                return l == s.left() ? s : br(n->prefix, n->mask, l, s.right());
            }

            auto r = insert(s.right(), d);
            return r == s.right() ? s : br(n->prefix, n->mask, s.left(), r);
        }

        auto es = s.entries();
        auto i  = std::ranges::lower_bound(es, key, {}, &Entry::key);
        if (i != es.end() && i->key == key) return s;

        auto pos = size_t(i - es.begin());
        auto buf = std::array<Entry, N + 1>();
        auto o   = std::ranges::copy(es.first(pos), buf.begin()).out;
        *o++     = Entry{key, d};
        o        = std::ranges::copy(es.subspan(pos), o).out;
        return make(View<Entry>(buf.data(), size_t(o - buf.begin())));
    }

    /// Yields @f$s \setminus \{d\}@f$.
    [[nodiscard]] Set erase(Set s, D* d) {
        auto key = KT::key(d);

        if (auto u = s.isa_uniq()) return KT::key(u) == key ? Set() : s;

        if (auto n = s.isa_br()) {
            if (!match_prefix(key, n->prefix, n->mask)) return s;

            if (zero_bit(key, n->mask)) {
                auto l = erase(s.left(), d);
                return l == s.left() ? s : br(n->prefix, n->mask, l, s.right());
            }

            auto r = erase(s.right(), d);
            return r == s.right() ? s : br(n->prefix, n->mask, s.left(), r);
        }

        auto es = s.entries();
        auto i  = std::ranges::find(es, key, &Entry::key);
        if (i == es.end()) return s;

        auto pos = size_t(i - es.begin());
        auto buf = std::array<Entry, N>();
        auto o   = std::ranges::copy(es.first(pos), buf.begin()).out;
        o        = std::ranges::copy(es.subspan(pos + 1), o).out;
        return make(View<Entry>(buf.data(), size_t(o - buf.begin())));
    }

    /// Yields @f$s_1 \cup s_2@f$.
    [[nodiscard]] Set merge(Set s1, Set s2) {
        if (s1 == s2 || s2.empty()) return s1;
        if (s1.empty()) return s2;

        auto n1 = s1.isa_br();
        auto n2 = s2.isa_br();

        if (!n1 && !n2) return merge_blocks(s1, s2);
        if (!n1) return insert_all(s2, s1);
        if (!n2) return insert_all(s1, s2);

        switch (rel(n1, n2)) {
            case Rel::Same: return br(n1->prefix, n1->mask, merge(s1.left(), s2.left()), merge(s1.right(), s2.right()));
            case Rel::L1: return br(n1->prefix, n1->mask, merge(s1.left(), s2), s1.right());
            case Rel::R1: return br(n1->prefix, n1->mask, s1.left(), merge(s1.right(), s2));
            case Rel::L2: return br(n2->prefix, n2->mask, merge(s1, s2.left()), s2.right());
            case Rel::R2: return br(n2->prefix, n2->mask, s2.left(), merge(s1, s2.right()));
            case Rel::None: return join(s1, s2);
        }
        unreachable();
    }

    /// Yields @f$s_1 \cap s_2@f$.
    [[nodiscard]] Set intersect(Set s1, Set s2) {
        if (s1 == s2) return s1;
        if (s1.empty() || s2.empty()) return {};

        auto n1 = s1.isa_br();
        auto n2 = s2.isa_br();

        if (!n1) return filter(s1, s2, true);
        if (!n2) return filter(s2, s1, true);

        switch (rel(n1, n2)) {
            case Rel::Same:
                return br(n1->prefix, n1->mask, intersect(s1.left(), s2.left()), intersect(s1.right(), s2.right()));
            case Rel::L1: return intersect(s1.left(), s2);
            case Rel::R1: return intersect(s1.right(), s2);
            case Rel::L2: return intersect(s1, s2.left());
            case Rel::R2: return intersect(s1, s2.right());
            case Rel::None: return {};
        }
        unreachable();
    }

    /// Yields @f$s_1 \setminus s_2@f$.
    [[nodiscard]] Set diff(Set s1, Set s2) {
        if (s1 == s2) return {};
        if (s1.empty() || s2.empty()) return s1;

        auto n1 = s1.isa_br();
        auto n2 = s2.isa_br();

        if (!n1) return filter(s1, s2, false);
        if (!n2) return erase_all(s1, s2);

        switch (rel(n1, n2)) {
            case Rel::Same: return br(n1->prefix, n1->mask, diff(s1.left(), s2.left()), diff(s1.right(), s2.right()));
            case Rel::L1: return br(n1->prefix, n1->mask, diff(s1.left(), s2), s1.right());
            case Rel::R1: return br(n1->prefix, n1->mask, s1.left(), diff(s1.right(), s2));
            case Rel::L2: return diff(s1, s2.left());
            case Rel::R2: return diff(s1, s2.right());
            case Rel::None: return s1;
        }
        unreachable();
    }
    ///@}

    friend void swap(Patricia& p1, Patricia& p2) noexcept {
        using std::swap;
        // clang-format off
        swap(p1.arr_arena_, p2.arr_arena_);
        swap(p1.br_arena_,  p2.br_arena_);
        swap(p1.arrs_,      p2.arrs_);
        swap(p1.brs_,       p2.brs_);
        // clang-format on
    }

private:
    /// @name Small Cases
    /// At least one operand is not a `Br` and hence holds at most @p N elements.
    ///@{

    /// Both do: one linear pass beats inserting them one by one.
    Set merge_blocks(Set s1, Set s2) {
        if (auto d = s1.isa_uniq()) return insert(s2, d);
        if (auto d = s2.isa_uniq()) return insert(s1, d);

        auto es1 = s1.entries();
        auto es2 = s2.entries();
        auto buf = std::array<Entry, 2 * N>();
        auto i1 = es1.begin(), i2 = es2.begin();
        auto o = buf.begin();

        while (i1 != es1.end() && i2 != es2.end())
            if (i1->key < i2->key)
                *o++ = *i1++;
            else if (i2->key < i1->key)
                *o++ = *i2++;
            else
                *o++ = *i1++, ++i2;

        o = std::ranges::copy(std::ranges::subrange(i1, es1.end()), o).out;
        o = std::ranges::copy(std::ranges::subrange(i2, es2.end()), o).out;
        return make(View<Entry>(buf.data(), size_t(o - buf.begin())));
    }

    /// Folds @p s into @p t one element at a time.
    Set insert_all(Set t, Set s) {
        if (auto d = s.isa_uniq()) return insert(t, d);
        for (const auto& e : s.entries())
            t = insert(t, e.d);
        return t;
    }

    Set erase_all(Set t, Set s) {
        if (auto d = s.isa_uniq()) return erase(t, d);
        for (const auto& e : s.entries())
            t = erase(t, e.d);
        return t;
    }

    /// The elements of @p s that @p other holds as well - or exactly those it does not, for `!in`.
    Set filter(Set s, Set other, bool in) {
        if (auto d = s.isa_uniq()) return other.contains(d) == in ? s : Set();

        auto buf = std::array<Entry, N>();
        auto o   = buf.begin();
        for (const auto& e : s.entries())
            if (bool(other.lookup(e.key)) == in) *o++ = e;
        return make(View<Entry>(buf.data(), size_t(o - buf.begin())));
    }
    ///@}

    /// @name Node Construction
    /// This is where the four flavours and the hash-consing live.
    ///@{

    /// Sorts @p v and drops the duplicate ids, then hands the result to Patricia::make.
    Set build(Vector<Entry>& v) {
        std::ranges::stable_sort(v, {}, &Entry::key);
        auto rest = std::ranges::unique(v, {}, &Entry::key);
        return make(View<Entry>(v.data(), size_t(rest.begin() - v.begin())));
    }

    /// The canonical Set for the sorted, duplicate-free @p es.
    Set make(View<Entry> es) {
        if (es.empty()) return {};
        if (es.size() == 1) return Set(es.front().d);
        if (es.size() <= N) return arr(es);

        auto m = branching_bit(es.front().key, es.back().key);
        auto p = mask_of(es.front().key, m);
        auto mid
            = size_t(std::ranges::partition_point(es, [m](const Entry& e) { return zero_bit(e.key, m); }) - es.begin());
        return br(p, m, make(es.first(mid)), make(es.subspan(mid)));
    }

    /// Joins two Set%s whose id ranges are disjoint.
    Set join(Set s1, Set s2) {
        auto p1 = s1.prefix();
        auto p2 = s2.prefix();
        auto m  = branching_bit(p1, p2);
        auto p  = mask_of(p1, m);
        return zero_bit(p1, m) ? br(p, m, s1, s2) : br(p, m, s2, s1);
    }

    Set arr(View<Entry> es) {
        assert(2 <= es.size() && es.size() <= N);
        auto state = arr_arena_.state();
        auto buff  = arr_arena_.allocate(sizeof(Arr) + es.size() * sizeof(Entry), alignof(Arr));
        auto node  = new (buff) Arr(hash_entries(es), uint32_t(es.size()));
        std::uninitialized_copy(es.begin(), es.end(), node->entries);
        auto [i, ins] = arrs_.emplace(node);
        if (!ins) arr_arena_.deallocate(state);
        return Set(*i);
    }

    /// A branch - unless @p l and @p r together still fit into one array node.
    Set br(K prefix, K mask, Set l, Set r) {
        if (l.empty()) return r;
        if (r.empty()) return l;

        auto size = l.size() + r.size();
        if (size <= N) { // neither child is a Br, and l's ids all precede r's: concatenate
            auto buf = std::array<Entry, N>();
            auto o   = l.copy(buf.begin());
            o        = r.copy(o);
            return arr(View<Entry>(buf.data(), size));
        }

        auto state    = br_arena_.state();
        auto node     = new (br_arena_.allocate<Br>(1)) Br(prefix, mask, size, l.word_, r.word_);
        auto [i, ins] = brs_.emplace(node);
        if (!ins) br_arena_.deallocate(state);
        return Set(*i);
    }
    ///@}

    /// @name Hash-Consing
    /// The id follows from the element, so only the pointers take part.
    ///@{
    static size_t hash_entries(View<Entry> es) noexcept {
        auto h = hash_begin();
        for (const auto& e : es)
            h = hash_combine(h, std::bit_cast<uintptr_t>(e.d));
        return h;
    }

    struct ArrHash {
        size_t operator()(const Arr* n) const noexcept { return n->hash; }
    };

    struct ArrEq {
        bool operator()(const Arr* n1, const Arr* n2) const noexcept {
            if (n1->size != n2->size) return false;
            for (uint32_t i = 0; i != n1->size; ++i)
                if (n1->entries[i].d != n2->entries[i].d) return false;
            return true;
        }
    };

    struct BrHash {
        size_t operator()(const Br* n) const noexcept {
            auto h = hash_combine(hash_combine(hash_begin(), n->prefix), n->mask);
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
    Arena arr_arena_;
    Arena br_arena_;
    Pool<Arr, ArrHash, ArrEq> arrs_;
    Pool<Br, BrHash, BrEq> brs_;
};

} // namespace fe
