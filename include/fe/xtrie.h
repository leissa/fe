#pragma once

#include <cstdint>

#include <algorithm>
#include <array>
#include <bit>
#include <fstream>
#include <iostream>
#include <print>
#include <ranges>
#include <string>

#ifdef FE_ABSL
#    include <absl/container/flat_hash_map.h>
#    include <absl/container/flat_hash_set.h>
#else
#    include <unordered_map>
#    include <unordered_set>
#endif

#include "fe/arena.h"
#include "fe/assert.h"
#include "fe/hash.h"
#include "fe/lct.h"
#include "fe/vector.h"

namespace fe {

/// Hash-consed sets of `D*`.
/// Small sets are sorted arrays, large ones paths in a trie; either way, equal sets are pointer-equal.
/// This is an *IndexedTrie* as described [here](https://dl.acm.org/doi/10.1145/3808286).
/// @p K is a *key* trait that grants access to the two `uint32_t`s XTrie needs on `D`:
/// ```
/// struct Key {
///     static uint32_t gid(const D*) noexcept;               ///< Unique id; orders and hashes the elements.
///     static uint32_t tid(const D*) noexcept;               ///< Trie id; `0` means "not assigned yet".
///     static void set_tid(const D*, uint32_t) noexcept;
///     static std::ostream& stream(std::ostream&, const D*); ///< Optional; defaults to Key::gid.
/// };
/// ```
/// @p N is the maximum size of an array set; bigger sets live in the trie.
template<class D, class K, size_t N = 16>
class XTrie {
private:
    struct Hash {
        constexpr size_t operator()(D* d) const noexcept { return fe::hash(K::gid(d)); }
    };

#ifdef FE_ABSL
    template<class V>
    using Map = absl::flat_hash_map<D*, V, Hash>;
#else
    template<class V>
    using Map  = std::unordered_map<D*, V, Hash>;
#endif

    /// Trie Node.
    class Node : public lct::Node<Node, D*> {
    private:
        using LCT = lct::Node<Node, D*>;

    public:
        constexpr Node(uint32_t id) noexcept
            : parent(nullptr)
            , def(nullptr)
            , size(0)
            , min(uint32_t(-1))
            , id(id) {}

        constexpr Node(Node* parent, D* def, uint32_t id) noexcept
            : parent(parent)
            , def(def)
            , size(parent->size + 1)
            , min(parent->def ? parent->min : K::tid(def))
            , id(id) {
            parent->link(this);
        }

        constexpr bool lt(D* d) const noexcept { return this->is_root() || K::tid(this->def) < K::tid(d); }
        constexpr bool eq(D* d) const noexcept { return this->def == d; }

        void dot(std::ostream& os) {
            using namespace std::string_literals;

            auto node2str = [](const Node* n) {
                return "n_"s + (n->def ? std::to_string(K::tid(n->def)) : "root"s) + "_"s + std::to_string(n->id);
            };

            for (auto stack = Vector<Node*>{this}; !stack.empty();) {
                auto n = stack.back();
                stack.pop_back();

                std::print(os, "{} [tooltip=\"gid: {}, min: {}\"];\n", node2str(n), n->def ? K::gid(n->def) : 0,
                           n->min);

                for (const auto& [_, child] : n->children) {
                    std::print(os, "{} -> {}\n", node2str(n), node2str(child.get()));
                    stack.emplace_back(child.get());
                }
            }
        }

        ///@name Getters
        ///@{
        constexpr bool is_root() const noexcept { return def == nullptr; }

        /// All tids on the path from the trie root to `this` live within `[Node::min, K::tid(Node::def)]`.
        [[nodiscard]] bool contains(D* d) noexcept {
            size_t tid = K::tid(d), lo = min, hi = K::tid(def);
            if (tid == lo || tid == hi) return true;
            return lo < tid && tid < hi && LCT::contains(d);
        }

        using LCT::find;
        ///@}

        Node* const parent;
        D* const def;
        const size_t size;
        /// @note `uint32_t` (not `size_t`) so that it packs into the same 8 bytes as Node::id;
        /// it only ever holds a Key::tid, which is a `uint32_t` itself.
        const uint32_t min;
        uint32_t const id;
        Map<Arena::Ptr<Node>> children;
    };

    struct Data {
        constexpr Data(size_t size) noexcept
            : size(size) {}

        size_t size;
        D* elems[];

        struct Equal {
            constexpr bool operator()(const Data* d1, const Data* d2) const noexcept {
                return d1->size == d2->size && std::equal(d1->begin(), d1->end(), d2->begin());
            }
        };

        struct Hash {
            constexpr size_t operator()(const Data* d) const noexcept {
                auto h = hash_begin();
                for (auto e : *d)
                    h = hash_combine(h, std::bit_cast<uintptr_t>(e));
                return h;
            }
        };

        /// @name Iterators
        ///@{
        constexpr D** begin() noexcept { return elems; }
        constexpr D** end() noexcept { return elems + size; }
        constexpr D* const* begin() const noexcept { return elems; }
        constexpr D* const* end() const noexcept { return elems + size; }
        ///@}

#ifdef FE_ABSL
        template<class H>
        friend constexpr H AbslHashValue(H h, const Data* d) noexcept {
            if (!d) return H::combine(std::move(h), 0);
            return H::combine_contiguous(std::move(h), d->elems, d->size);
        }
#endif
    };

#ifdef FE_ABSL
    using Pool = absl::flat_hash_set<const Data*, absl::Hash<const Data*>, typename Data::Equal>;
#else
    using Pool = std::unordered_set<const Data*, typename Data::Hash, typename Data::Equal>;
#endif

public:
    class Set {
    private:
        enum class Tag : uintptr_t { Null, Uniq, Data, Node };

        constexpr Set(const Data* data) noexcept
            : ptr_(uintptr_t(data) | uintptr_t(Tag::Data)) {} ///< Data Set.
        constexpr Set(Node* node) noexcept
            : ptr_(uintptr_t(node) | uintptr_t(Tag::Node)) {} ///< Node set.

    public:
        class iterator {
        private:
            constexpr iterator(D* d) noexcept
                : tag_(Tag::Uniq)
                , ptr_(std::bit_cast<uintptr_t>(d)) {}
            constexpr iterator(D* const* elems) noexcept
                : tag_(Tag::Data)
                , ptr_(std::bit_cast<uintptr_t>(elems)) {}
            constexpr iterator(Node* node) noexcept
                : tag_(Tag::Node)
                , ptr_(std::bit_cast<uintptr_t>(node)) {}

        public:
            /// @name Iterator Properties
            ///@{
            using iterator_category = std::forward_iterator_tag;
            using difference_type   = std::ptrdiff_t;
            using value_type        = D*;
            using pointer           = D* const*;
            using reference         = D* const&;
            ///@}

            /// @name Construction
            ///@{
            constexpr iterator() noexcept = default;
            ///@}

            /// @name Increment
            /// @note These operations only change the *view* of this Set; the Set itself is **not** modified.
            ///@{
            constexpr iterator& operator++() noexcept {
                // clang-format off
                switch (tag_) {
                    case Tag::Uniq: return clear();
                    case Tag::Data: return ptr_ = std::bit_cast<uintptr_t>(std::bit_cast<D* const*>(ptr_) + 1), *this;
                    case Tag::Node: {
                        auto node = std::bit_cast<Node*>(ptr_);
                        node      = node->parent;
                        if (node->is_root())
                            clear();
                        else
                            ptr_ = std::bit_cast<uintptr_t>(node);
                        return *this;
                    }
                    default: unreachable();
                }
                // clang-format on
            }

            constexpr iterator operator++(int) noexcept {
                auto res = *this;
                this->operator++();
                return res;
            }
            ///@}

            /// @name Comparisons
            ///@{
            constexpr bool operator==(iterator other) const noexcept {
                return this->tag_ == other.tag_ && this->ptr_ == other.ptr_;
            }
            ///@}

            /// @name Dereference
            ///@{
            constexpr value_type operator*() const noexcept {
                switch (tag_) {
                    case Tag::Uniq: return std::bit_cast<D*>(ptr_);
                    case Tag::Data: return *std::bit_cast<D* const*>(ptr_);
                    case Tag::Node: return std::bit_cast<Node*>(ptr_)->def;
                    default: unreachable();
                }
            }

            constexpr value_type operator->() const noexcept { return this->operator*(); }
            ///@}

            constexpr iterator& clear() noexcept { return *this = {}; }

        private:
            Tag tag_       = Tag::Null;
            uintptr_t ptr_ = 0;

            friend class Set;
        };

        /// @name Construction
        ///@{
        constexpr Set(const Set&) noexcept = default;
        constexpr Set(Set&&) noexcept      = default;
        constexpr Set() noexcept           = default; ///< Null set
        constexpr Set(D* d) noexcept
            : ptr_(uintptr_t(d) | uintptr_t(Tag::Uniq)) {} ///< Uniq set.

        constexpr Set& operator=(const Set&) noexcept = default;
        ///@}

        /// @name Getters
        ///@{
        constexpr size_t size() const noexcept {
            if (isa_uniq()) return 1;
            if (auto d = isa_data()) return d->size;
            if (auto n = isa_node()) return n->size;
            return 0; // empty
        }

        /// Is empty?
        constexpr bool empty() const noexcept {
            assert(tag() != Tag::Node || !ptr<Node>()->is_root());
            return ptr_ == 0;
        }

        constexpr explicit operator bool() const noexcept { return !empty(); } ///< Not empty?
        ///@}

        /// @name Check Membership
        ///@{

        /// Is @f$d \in this@f$?.
        bool contains(D* d) const noexcept {
            if (auto u = isa_uniq()) return d == u;

            if (auto data = isa_data()) {
                for (auto e : *data)
                    if (d == e) return true;
                return false;
            }

            if (auto n = isa_node()) return n->contains(d);

            return false;
        }

        /// Is @f$this \cap other \neq \emptyset@f$?.
        [[nodiscard]] bool has_intersection(Set other) const noexcept {
            if (this->empty() || other.empty()) return false;
            if (*this == other) return true;

            auto u1 = this->isa_uniq();
            auto u2 = other.isa_uniq();
            if (u1) return other.contains(u1);
            if (u2) return this->contains(u2);

            auto d1 = this->isa_data();
            auto d2 = other.isa_data();
            if (d1 && d2) {
                for (auto ai = d1->begin(), ae = d1->end(), bi = d2->begin(), be = d2->end(); ai != ae && bi != be;) {
                    if (*ai == *bi) return true;

                    if (K::gid(*ai) < K::gid(*bi))
                        ++ai;
                    else
                        ++bi;
                }

                return false;
            }

            auto n1 = this->isa_node();
            auto n2 = other.isa_node();
            if (n1 && n2) {
                if (n1->min > K::tid(n2->def) || K::tid(n1->def) < n2->min) return false;
                if (n1->def == n2->def) return true;
                if (!n1->lca(n2)->is_root()) return true;

                while (!n1->is_root() && !n2->is_root()) {
                    if (K::tid(n1->def) > K::tid(n2->def)) {
                        if (n1 = n1->find(n2->def); n2->def == n1->def) return true;
                        n1 = n1->parent;
                    } else {
                        if (n2 = n2->find(n1->def); n1->def == n2->def) return true;
                        n2 = n2->parent;
                    }
                }

                return false;
            }

            auto n = n1 ? n1 : n2;
            for (auto e : *(d1 ? d1 : d2))
                if (n->contains(e)) return true;

            return false;
        }
        ///@}

        /// @name Iterators
        ///@{
        constexpr iterator begin() const noexcept {
            if (auto u = isa_uniq()) return {u};
            if (auto d = isa_data()) return {d->begin()};
            if (auto n = isa_node(); n && !n->is_root()) return {n};
            return {};
        }

        constexpr iterator end() const noexcept {
            if (auto data = isa_data()) return iterator(data->end());
            return {};
        }
        ///@}

        /// @name Comparisons
        ///@{
        constexpr bool operator==(Set other) const noexcept { return this->ptr_ == other.ptr_; }
        ///@}

        /// @name Output
        ///@{
        std::ostream& stream(std::ostream& os) const {
            os << '{';
            auto sep = "";
            for (auto d : *this) {
                os << sep;
                if constexpr (requires { K::stream(os, d); })
                    K::stream(os, d);
                else
                    os << K::gid(d);
                sep = ", ";
            }
            return os << '}';
        }

        void dump() const { stream(std::cout) << std::endl; }
        ///@}

    private:
        constexpr Tag tag() const noexcept { return Tag(ptr_ & uintptr_t(0b11)); }
        template<class T>
        constexpr T* ptr() const noexcept {
            return std::bit_cast<T*>(ptr_ & ~uintptr_t(0b11));
        }
        // clang-format off
        constexpr D*    isa_uniq() const noexcept { return tag() == Tag::Uniq ? ptr<D   >() : nullptr; }
        constexpr Data* isa_data() const noexcept { return tag() == Tag::Data ? ptr<Data>() : nullptr; }
        constexpr Node* isa_node() const noexcept { return tag() == Tag::Node ? ptr<Node>() : nullptr; }
        // clang-format on

        uintptr_t ptr_ = 0;

        friend class XTrie;
        friend std::ostream& operator<<(std::ostream& os, Set set) { return set.stream(os); }
    };

    static_assert(std::forward_iterator<typename Set::iterator>);
    static_assert(std::ranges::range<Set>);

    /// @name Construction
    ///@{
    XTrie& operator=(const XTrie&) = delete;

    constexpr XTrie() noexcept
        : root_(make_node()) {}
    constexpr XTrie(const XTrie&) noexcept = delete;
    constexpr XTrie(XTrie&& other) noexcept
        : XTrie() {
        swap(*this, other);
    }
    ///@}

    /// @name Set Operations
    /// @note These operations do **not** modify the input set(s); they create a **new** Set.
    ///@{

    /// Create a Set with all elements in `[begin, end)`.
    /// @attention Reorders `[begin, end)` in place.
    template<std::random_access_iterator I>
    [[nodiscard]] Set create(I begin, I end) {
        std::sort(begin, end, gid_lt);
        auto u    = std::unique(begin, end);
        auto size = size_t(std::distance(begin, u));

        if (size == 0) return {};
        if (size == 1) return {*begin};

        if (size <= N) {
            auto [data, state] = allocate(size);
            std::copy(begin, u, data->begin());
            return unify(data, state);
        }

        return create_trie(begin, u);
    }

    /// Create a Set wih all elements in @p r.
    template<std::ranges::input_range R>
    [[nodiscard]] Set create(R&& r) {
        auto v = fe::Vector<D*>(std::ranges::begin(r), std::ranges::end(r));
        return create(v.begin(), v.end());
    }

    /// Create a Set wih all elements in @p list.
    [[nodiscard]] Set create(std::initializer_list<D*> list) {
        auto v = fe::Vector<D*>(list);
        return create(v.begin(), v.end());
    }

    /// Yields @f$s \cup \{d\}@f$.
    [[nodiscard]] Set insert(Set s, D* d) {
        if (auto u = s.isa_uniq()) {
            if (d == u) return {d};

            auto [data, state] = allocate(2);
            if (K::gid(d) < K::gid(u))
                data->elems[0] = d, data->elems[1] = u;
            else
                data->elems[0] = u, data->elems[1] = d;
            return unify(data, state);
        }

        if (auto src = s.isa_data()) {
            auto size = src->size;
            assert(size <= N);

            for (auto e : *src)
                if (d == e) return s; // already here

            if (size == N) { // one more element is too much for a Data set: switch over to the trie
                // Use the data arena as scratch space for the N + 1 elements; since create_trie only draws from
                // node_arena_, it is ours to throw away again afterwards.
                auto [scratch, state] = allocate(N + 1);
                auto o                = std::copy(src->begin(), src->end(), scratch->begin());
                *o++                  = d;
#ifndef NDEBUG
                auto scratch_state = data_arena_.state();
#endif
                auto res = create_trie(scratch->begin(), o);
                assert(scratch_state == data_arena_.state() && "create_trie must only draw from node_arena_");
                data_arena_.deallocate(state);
                return res;
            }

            auto [dst, state] = allocate(size + 1);
            auto i            = std::upper_bound(src->begin(), src->end(), d, gid_lt); // where d belongs
            auto o            = std::copy(src->begin(), i, dst->begin());
            *o++              = d;
            std::copy(i, src->end(), o);
            return unify(dst, state);
        }

        if (auto n = s.isa_node()) {
            if (n->contains(d)) return n;
            return insert(n, d);
        }

        return {d};
    }

    /// Yields @f$s_1 \cup s_2@f$.
    [[nodiscard]] Set merge(Set s1, Set s2) {
        if (s1.empty() || s1 == s2) return s2;
        if (s2.empty()) return s1;

        if (auto u = s1.isa_uniq()) return insert(s2, u);
        if (auto u = s2.isa_uniq()) return insert(s1, u);

        auto d1 = s1.isa_data();
        auto d2 = s2.isa_data();
        if (d1 && d2) {
            // Both operands are ordered by gid and duplicate-free, so a linear merge yields the union directly -
            // no sort, no std::unique pass.
            // Its final size is only known afterwards, so allocate the upper bound `d1->size + d2->size` and merge
            // straight into it; every dropped duplicate leaves one slot of excess at the tail that unify releases
            // again.
            auto [data, state] = allocate(d1->size + d2->size);
            auto i1 = d1->begin(), e1 = d1->end();
            auto i2 = d2->begin(), e2 = d2->end();
            auto o = data->begin();

            while (i1 != e1 && i2 != e2) {
                auto g1 = K::gid(*i1);
                auto g2 = K::gid(*i2);
                if (g1 < g2)
                    *o++ = *i1++;
                else if (g2 < g1)
                    *o++ = *i2++;
                else
                    *o++ = *i1++, ++i2; // drop the duplicate
            }
            o = std::copy(i1, e1, o);
            o = std::copy(i2, e2, o);

            auto size = size_t(o - data->begin());
            if (size > N) { // too big for a Data set: switch over to the trie
#ifndef NDEBUG
                auto scratch_state = data_arena_.state();
#endif
                auto res = create_trie(data->begin(), o); // only draws from node_arena_ ...
                assert(scratch_state == data_arena_.state() && "create_trie must only draw from node_arena_");
                data_arena_.deallocate(state); // ... so data is ours to throw away again
                return res;
            }

            auto excess = data->size - size; // data->size is still the upper bound we allocated
            data->size  = size;
            return unify(data, state, excess);
        }

        auto n1 = s1.isa_node();
        auto n2 = s2.isa_node();
        if (n1 && n2) {
            if (n1->is_descendant_of(n2)) return n1;
            if (n2->is_descendant_of(n1)) return n2;
            return merge(n1, n2);
        }

        auto n = n1 ? n1 : n2;
        for (auto d : *(d1 ? d1 : d2))
            if (!n->contains(d)) n = insert(n, d);
        return n;
    }

    /// Yields @f$s \setminus \{d\}@f$.
    [[nodiscard]] Set erase(Set s, D* d) {
        if (auto u = s.isa_uniq()) return d == u ? Set() : s;

        if (auto data = s.isa_data()) {
            auto b = data->begin(), e = data->end();
            auto i = std::find(b, e, d);
            if (i == e) return s; // not in here

            auto size = data->size - 1;
            if (size == 0) return {};
            if (size == 1) return {i == b ? b[1] : b[0]};

            assert(size <= N);
            auto [new_data, state] = allocate(size);
            std::copy(i + 1, e, std::copy(b, i, new_data->begin())); // copy over, skip i
            return unify(new_data, state);
        }

        if (auto n = s.isa_node()) {
            if (!n->contains(d)) return n;

            auto res = erase(n, d);
            if (res->size > N) return res;

            auto v = std::array<D*, N>();
            auto o = v.begin();
            for (auto i = res; !i->is_root(); i = i->parent)
                *o++ = i->def;
            return create(v.begin(), o);
        }

        return {};
    }
    ///@}

    /// @name DOT output
    void dot() {
        auto of = std::ofstream("trie.dot");
        dot(of);
    }

    void dot(std::ostream& os) const {
        std::print(os, "digraph {{\n");
        std::print(os, "ordering=out;\n");
        std::print(os, "node [shape=box,style=filled];\n");
        root()->dot(os);
        std::print(os, "}}\n");
    }

    friend void swap(XTrie& s1, XTrie& s2) noexcept {
        using std::swap;
        // clang-format off
        swap(s1.data_arena_,  s2.data_arena_);
        swap(s1.node_arena_,  s2.node_arena_);
        swap(s1.pool_,        s2.pool_);
        swap(s1.root_,        s2.root_);
        swap(s1.tid_counter_, s2.tid_counter_);
        swap(s1.id_counter_ , s2.id_counter_ );
        // clang-format on
    }

private:
    D* set_tid(D* d) noexcept {
        assert(K::tid(d) == 0);
        K::set_tid(d, tid_counter_++);
        return d;
    }

    /// Data sets are ordered by Key::gid.
    static constexpr bool gid_lt(D* d1, D* d2) noexcept { return K::gid(d1) < K::gid(d2); }

    /// @name Data helpers
    ///@{
    std::pair<Data*, Arena::State> allocate(size_t size) {
        auto bytes = sizeof(Data) + size * sizeof(D*);
        auto state = data_arena_.state();
        auto buff  = data_arena_.allocate(bytes, alignof(Data));
        auto data  = new (buff) Data(size);
        return {data, state};
    }

    /// Hash-conses @p data; rolls the arena back to @p state, if an equal Data is already pooled.
    /// Pass the number of trailing elements allocated but not used as @p excess to release them again.
    Set unify(Data* data, Arena::State state, size_t excess = 0) {
        assert(data->size != 0);
        auto [i, ins] = pool_.emplace(data);
        if (ins) {
            data_arena_.deallocate(excess * sizeof(D*)); // data is the arena's most recent allocation
            return Set(data);
        }

        data_arena_.deallocate(state);
        return Set(*i);
    }
    ///@}

    /// Builds a trie Set from the *unique* elements in `[begin, end)`; reorders them in place.
    /// @attention Must only ever draw from node_arena_.
    /// Two callers use data_arena_ as scratch space and rewind it afterwards; drawing from data_arena_ in here
    /// would pop pages that are still live - possibly including Data that pool_ still points at.
    /// Both call sites assert this.
    template<class I>
    [[nodiscard]] Set create_trie(I begin, I end) {
        // Sorting is a performance optimization, not a correctness requirement:
        // insert() restores the canonical increasing-tid path from any insertion order, but only an element whose
        // tid exceeds the current tip mounts in O(1) - otherwise it walks up and re-mounts the suffix in O(depth).
        // Feeding the elements in ascending tid order therefore turns O(k * depth) into O(k) mounts.
        // A tid of 0 goes last because set_tid hands out the next - and hence maximal - counter value.
        std::sort(begin, end,
                  [](D* d1, D* d2) { return K::tid(d1) != 0 && (K::tid(d2) == 0 || K::tid(d1) < K::tid(d2)); });

        auto res = root();
        for (auto i = begin; i != end; ++i)
            res = insert(res, *i);
        return res;
    }

    // Trie helpers
    constexpr Node* root() const noexcept { return root_.get(); }
    Arena::Ptr<Node> make_node() { return node_arena_.mk<Node>(id_counter_++); }
    Arena::Ptr<Node> make_node(Node* parent, D* def) { return node_arena_.mk<Node>(parent, def, id_counter_++); }

    /// Mounts all Node::def%s stacked up in @p path back onto @p n - deepest one last.
    [[nodiscard]] Node* remount(Node* n, View<D*> path) {
        for (auto d : path | std::views::reverse)
            n = mount(n, d);
        return n;
    }

    [[nodiscard]] Node* mount(Node* parent, D* d) {
        assert(K::tid(d) != 0);
        auto [i, ins] = parent->children.emplace(d, nullptr);
        if (ins) i->second = make_node(parent, d);
        return i->second.get();
    }

    /// The path from @p n up to the insertion point is re-mounted on top of the new node; keep it in @p path instead
    /// of in the call stack, which the depth of a trie path may well outgrow.
    [[nodiscard]] Node* insert(Node* n, D* d) {
        if (K::tid(d) == 0) return mount(n, set_tid(d));

        auto path = Vector<D*>();
        for (; !n->is_root() && n->def != d && K::tid(d) < K::tid(n->def); n = n->parent)
            path.emplace_back(n->def);

        return remount(n->def == d ? n : mount(n, d), path);
    }

    [[nodiscard]] Node* merge(Node* n, Node* m) {
        auto path = Vector<D*>();
        while (n != m && !n->is_root() && !m->is_root()) {
            auto tn = K::tid(n->def), tm = K::tid(m->def);
            if (tn < tm)
                path.emplace_back(m->def), m = m->parent;
            else if (tn > tm)
                path.emplace_back(n->def), n = n->parent;
            else
                path.emplace_back(n->def), n = n->parent, m = m->parent;
        }

        return remount(n == m || m->is_root() ? n : m, path);
    }

    [[nodiscard]] Node* erase(Node* n, D* d) {
        auto path = Vector<D*>();
        for (; K::tid(d) <= K::tid(n->def) && n->def != d; n = n->parent)
            path.emplace_back(n->def);

        return remount(n->def == d ? n->parent : n, path);
    }

    Arena node_arena_;
    Arena data_arena_;
    Pool pool_;
    uint32_t tid_counter_ = 1;
    uint32_t id_counter_  = 0;
    /// @note Must be declared after id_counter_, as the constructor draws the root's Node::id from it.
    Arena::Ptr<Node> root_;
};

} // namespace fe
