#pragma once

#include <cstdint>

#include <bit>
#include <functional>
#include <initializer_list>
#include <iterator>
#include <string>
#include <string_view>
#include <type_traits>
#include <unordered_map>
#include <unordered_set>
#include <utility>

#include "fe/hash.h"

#if !defined(FE_HASH_ABSL) && !defined(FE_HASH_STD) && !defined(FE_HASH_TSL_ROBIN) && !defined(FE_HASH_TSL_HOPSCOTCH) \
    && !defined(FE_HASH_ANKERL) && !defined(FE_HASH_PHMAP) && !defined(FE_HASH_EMHASH) && !defined(FE_HASH_BOOST)
#    ifdef FE_ABSL
#        define FE_HASH_ABSL
#    else
#        define FE_HASH_STD
#    endif
#endif

#if defined(FE_HASH_ABSL)
#    include <absl/container/flat_hash_map.h>
#    include <absl/container/flat_hash_set.h>
#    include <absl/container/node_hash_map.h>
#    include <absl/container/node_hash_set.h>
#elif defined(FE_HASH_TSL_ROBIN)
#    include <tsl/robin_map.h>
#    include <tsl/robin_set.h>
#elif defined(FE_HASH_TSL_HOPSCOTCH)
#    include <tsl/hopscotch_map.h>
#    include <tsl/hopscotch_set.h>
#elif defined(FE_HASH_ANKERL)
#    include <ankerl/unordered_dense.h>
#elif defined(FE_HASH_PHMAP)
#    include <parallel_hashmap/phmap.h>
#elif defined(FE_HASH_EMHASH)
#    include <emhash/hash_set8.hpp>
#    include <emhash/hash_table8.hpp>
#elif defined(FE_HASH_BOOST)
#    include <boost/unordered/unordered_flat_map.hpp>
#    include <boost/unordered/unordered_flat_set.hpp>
#    include <boost/unordered/unordered_node_map.hpp>
#    include <boost/unordered/unordered_node_set.hpp>
#endif

namespace fe {

/// Default hasher for the non-abseil backends; all results go through fe::hash, so they avalanche.
template<class T>
struct DefaultHash {
    using is_avalanching = std::true_type;

    size_t operator()(const T& t) const noexcept {
        if constexpr (std::is_pointer_v<T>)
            return hash(std::bit_cast<uintptr_t>(t));
        else if constexpr (std::is_integral_v<T> || std::is_enum_v<T>)
            return hash(size_t(t));
        else if constexpr (std::is_convertible_v<const T&, std::string_view>)
            return hash_begin(std::string_view(t));
        else
            return hash(std::hash<T>{}(t));
    }
};

template<class A, class B>
struct DefaultHash<std::pair<A, B>> {
    using is_avalanching = std::true_type;

    size_t operator()(const std::pair<A, B>& p) const noexcept {
        return hash(DefaultHash<A>{}(p.first) ^ (DefaultHash<B>{}(p.second) * fnv1_prime));
    }
};

/// Transparent, like abseil's default string hash, so `std::string_view` lookups work.
template<>
struct DefaultHash<std::string> {
    using is_avalanching = std::true_type;
    using is_transparent = void;

    size_t operator()(std::string_view sv) const noexcept { return hash_begin(sv); }
};

template<class K>
struct DefaultEq : std::equal_to<K> {};
template<>
struct DefaultEq<std::string> : std::equal_to<> {};

/// Tells ankerl/boost that @p H needs no extra mixing - true for all hashers built on fe::hash.
template<class H>
struct Avalanching : H {
    using is_avalanching = std::true_type;
};

#if defined(FE_HASH_ABSL)
template<class K>
using DefHash = absl::container_internal::hash_default_hash<K>;
template<class K>
using DefEq = absl::container_internal::hash_default_eq<K>;
#else
template<class K>
using DefHash = DefaultHash<K>;
template<class K>
using DefEq = DefaultEq<K>;
#endif

#if defined(FE_HASH_TSL_ROBIN) || defined(FE_HASH_TSL_HOPSCOTCH)
namespace detail {
/// tsl's map iterators yield `const std::pair<K, V>&`; this adapter yields the usual `std::pair<const K, V>&`.
template<class Base>
class TslMap : public Base {
public:
    using key_type    = typename Base::key_type;
    using mapped_type = typename Base::mapped_type;
    using value_type  = std::pair<const key_type, mapped_type>;

    template<class It, bool Const>
    class Iter {
    public:
        using iterator_category = std::forward_iterator_tag;
        using value_type        = TslMap::value_type;
        using difference_type   = std::ptrdiff_t;
        using reference         = std::conditional_t<Const, const value_type&, value_type&>;
        using pointer           = std::conditional_t<Const, const value_type*, value_type*>;

        Iter() = default;
        Iter(It it)
            : it(it) {}
        template<class I, bool C>
        requires(Const && !C) Iter(Iter<I, C> other)
            : it(other.it) {}

        reference operator*() const {
            return reinterpret_cast<reference>(const_cast<std::pair<key_type, mapped_type>&>(*it));
        }
        pointer operator->() const { return &**this; }
        Iter& operator++() { return ++it, *this; }
        Iter operator++(int) {
            auto res = *this;
            ++it;
            return res;
        }
        friend bool operator==(const Iter& a, const Iter& b) { return a.it == b.it; }

        It it;
    };

    using iterator       = Iter<typename Base::iterator, false>;
    using const_iterator = Iter<typename Base::const_iterator, true>;

    using Base::Base;
    using Base::erase;

    iterator begin() { return Base::begin(); }
    iterator end() { return Base::end(); }
    const_iterator begin() const { return Base::begin(); }
    const_iterator end() const { return Base::end(); }
    const_iterator cbegin() const { return Base::cbegin(); }
    const_iterator cend() const { return Base::cend(); }

    iterator find(const key_type& key) { return Base::find(key); }
    const_iterator find(const key_type& key) const { return Base::find(key); }
    template<class K>
    iterator find(const K& key) {
        return Base::find(key);
    }
    template<class K>
    const_iterator find(const K& key) const {
        return Base::find(key);
    }

    template<class... Args>
    std::pair<iterator, bool> emplace(Args&&... args) {
        auto [i, ins] = Base::emplace(std::forward<Args>(args)...);
        return {i, ins};
    }
    template<class K, class... Args>
    std::pair<iterator, bool> try_emplace(K&& key, Args&&... args) {
        auto [i, ins] = Base::try_emplace(std::forward<K>(key), std::forward<Args>(args)...);
        return {i, ins};
    }
    template<class K, class V>
    std::pair<iterator, bool> insert_or_assign(K&& key, V&& val) {
        auto [i, ins] = Base::insert_or_assign(std::forward<K>(key), std::forward<V>(val));
        return {i, ins};
    }
    std::pair<iterator, bool> insert(const value_type& v) { return emplace(v.first, v.second); }
    std::pair<iterator, bool> insert(value_type&& v) { return emplace(std::move(v.first), std::move(v.second)); }
    template<class P>
    requires std::is_constructible_v<typename Base::value_type, P&&> std::pair<iterator, bool> insert(P&& p) {
        auto [i, ins] = Base::insert(std::forward<P>(p));
        return {i, ins};
    }
    template<class I>
    void insert(I first, I last) {
        for (; first != last; ++first)
            emplace(*first);
    }
    void insert(std::initializer_list<value_type> list) { insert(list.begin(), list.end()); }

    iterator erase(iterator i) { return Base::erase(i.it); }
    iterator erase(const_iterator i) { return Base::erase(i.it); }
};
} // namespace detail
#endif

#if defined(FE_HASH_EMHASH)
namespace detail {
/// emhash8::HashSet::emplace only takes a finished value.
template<class Base>
class EmSet : public Base {
public:
    using Base::Base;

    template<class... Args>
    auto emplace(Args&&... args) {
        return Base::insert(typename Base::value_type(std::forward<Args>(args)...));
    }
};
} // namespace detail
#endif

// clang-format off
#if defined(FE_HASH_ABSL)
template<class K, class V, class H = DefHash<K>, class E = DefEq<K>> using HashMap = absl::flat_hash_map<K, V, H, E>;
template<class K,          class H = DefHash<K>, class E = DefEq<K>> using HashSet = absl::flat_hash_set<K,    H, E>;
template<class K, class V, class H = DefHash<K>, class E = DefEq<K>> using NodeMap = absl::node_hash_map<K, V, H, E>;
template<class K,          class H = DefHash<K>, class E = DefEq<K>> using NodeSet = absl::node_hash_set<K,    H, E>;
#elif defined(FE_HASH_PHMAP)
template<class K, class V, class H = DefHash<K>, class E = DefEq<K>> using HashMap = phmap::flat_hash_map<K, V, H, E>;
template<class K,          class H = DefHash<K>, class E = DefEq<K>> using HashSet = phmap::flat_hash_set<K,    H, E>;
template<class K, class V, class H = DefHash<K>, class E = DefEq<K>> using NodeMap = phmap::node_hash_map<K, V, H, E>;
template<class K,          class H = DefHash<K>, class E = DefEq<K>> using NodeSet = phmap::node_hash_set<K,    H, E>;
#elif defined(FE_HASH_BOOST)
template<class K, class V, class H = DefHash<K>, class E = DefEq<K>> using HashMap = boost::unordered_flat_map<K, V, Avalanching<H>, E>;
template<class K,          class H = DefHash<K>, class E = DefEq<K>> using HashSet = boost::unordered_flat_set<K,    Avalanching<H>, E>;
template<class K, class V, class H = DefHash<K>, class E = DefEq<K>> using NodeMap = boost::unordered_node_map<K, V, Avalanching<H>, E>;
template<class K,          class H = DefHash<K>, class E = DefEq<K>> using NodeSet = boost::unordered_node_set<K,    Avalanching<H>, E>;
#else
#    if defined(FE_HASH_TSL_ROBIN)
template<class K, class V, class H = DefHash<K>, class E = DefEq<K>> using HashMap = detail::TslMap<tsl::robin_map<K, V, H, E>>;
template<class K,          class H = DefHash<K>, class E = DefEq<K>> using HashSet = tsl::robin_set<K,    H, E>;
#    elif defined(FE_HASH_TSL_HOPSCOTCH)
template<class K, class V, class H = DefHash<K>, class E = DefEq<K>> using HashMap = detail::TslMap<tsl::hopscotch_map<K, V, H, E>>;
template<class K,          class H = DefHash<K>, class E = DefEq<K>> using HashSet = tsl::hopscotch_set<K,    H, E>;
#    elif defined(FE_HASH_ANKERL)
template<class K, class V, class H = DefHash<K>, class E = DefEq<K>> using HashMap = ankerl::unordered_dense::map<K, V, Avalanching<H>, E>;
template<class K,          class H = DefHash<K>, class E = DefEq<K>> using HashSet = ankerl::unordered_dense::set<K,    Avalanching<H>, E>;
#    elif defined(FE_HASH_EMHASH)
template<class K, class V, class H = DefHash<K>, class E = DefEq<K>> using HashMap = emhash8::HashMap<K, V, H, E>;
template<class K,          class H = DefHash<K>, class E = DefEq<K>> using HashSet = detail::EmSet<emhash8::HashSet<K, H, E>>;
#    else
template<class K, class V, class H = DefHash<K>, class E = DefEq<K>> using HashMap = std::unordered_map<K, V, H, E>;
template<class K,          class H = DefHash<K>, class E = DefEq<K>> using HashSet = std::unordered_set<K,    H, E>;
#    endif
template<class K, class V, class H = DefHash<K>, class E = DefEq<K>> using NodeMap = std::unordered_map<K, V, H, E>;
template<class K,          class H = DefHash<K>, class E = DefEq<K>> using NodeSet = std::unordered_set<K,    H, E>;
#endif
// clang-format on

} // namespace fe
