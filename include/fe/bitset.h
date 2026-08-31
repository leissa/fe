#pragma once

#include <cassert>
#include <cstddef>
#include <cstdint>

#include <algorithm>
#include <bit>
#include <initializer_list>
#include <iostream>
#include <iterator>
#include <ostream>
#include <ranges>
#include <utility>

#include "fe/algo.h"
#include "fe/hash.h"

namespace fe {

/// A dynamically growing set of bits with small storage optimization.
/// The first Bitset::Inline_Bits bits live inside the Bitset itself; only beyond that it allocates on the heap.
/// Think of a Bitset as an infinite sequence of bits that are zero except for the ones you set:
/// Bitset::set/Bitset::flip grow the storage on demand,
/// while testing or clearing a bit beyond Bitset::capacity needs no storage at all.
/// @note Everything is `constexpr` as long as the Bitset stays inline:
/// the heap words hide in a `uint64_t` and are only recovered by a `std::bit_cast` that a constant evaluation rejects.
class Bitset {
public:
    static constexpr size_t Bits_Per_Word = sizeof(uint64_t) * 8; ///< Number of bits in one word.
    static constexpr size_t Inline_Bits   = Bits_Per_Word;        ///< Number of bits available without allocating.
    static constexpr size_t npos          = size_t(-1);           ///< Returned by Bitset::next if there is no set bit.

    /// Proxy that Bitset::operator[] hands out to read/write the bit it refers to.
    class reference {
    public:
        constexpr reference& operator=(bool b) noexcept {
            bitset_->set(i_, b);
            return *this;
        }
        constexpr reference& operator=(const reference& other) noexcept { return *this = bool(other); }
        constexpr reference& flip() noexcept {
            bitset_->flip(i_);
            return *this;
        }

        constexpr operator bool() const noexcept { return bitset_->test(i_); }
        constexpr bool operator~() const noexcept { return !bitset_->test(i_); }

    private:
        constexpr reference(Bitset* bitset, size_t i) noexcept
            : bitset_(bitset)
            , i_(i) {}

        Bitset* bitset_;
        size_t i_;

        friend class Bitset;
    };

    /// Iterates over the *indices* of all set bits in ascending order.
    class iterator {
    public:
        using iterator_category = std::forward_iterator_tag;
        using value_type        = size_t;
        using difference_type   = std::ptrdiff_t;
        using pointer           = void;
        using reference         = size_t;

        constexpr iterator() noexcept = default;

        constexpr size_t operator*() const noexcept { return i_; }
        constexpr iterator& operator++() noexcept {
            assert(i_ != npos);
            i_ = bitset_->next(i_ + 1);
            return *this;
        }
        constexpr iterator operator++(int) noexcept {
            auto res = *this;
            ++*this;
            return res;
        }
        constexpr bool operator==(iterator other) const noexcept { return i_ == other.i_; }

    private:
        constexpr iterator(const Bitset* bitset, size_t i) noexcept
            : bitset_(bitset)
            , i_(i) {}

        const Bitset* bitset_ = nullptr;
        size_t i_             = npos;

        friend class Bitset;
    };

    /// @name Constructors, Destructor, Assignment
    ///@{
    constexpr Bitset() noexcept = default;
    constexpr Bitset(std::initializer_list<size_t> bits) {
        for (auto i : bits)
            set(i);
    }
    constexpr Bitset(const Bitset& other)
        : num_words_(other.num_words_) {
        if (other.on_heap()) {
            auto heap = new uint64_t[num_words_];
            std::copy_n(other.words(), num_words_, heap);
            data_ = bitcast_resize<uint64_t>(heap);
        } else {
            data_ = other.data_;
        }
    }
    constexpr Bitset(Bitset&& other) noexcept { swap(*this, other); }
    constexpr ~Bitset() noexcept {
        if (on_heap()) delete[] words();
    }
    constexpr Bitset& operator=(Bitset other) noexcept {
        swap(*this, other);
        return *this;
    }
    ///@}

    /// @name Access
    ///@{
    [[nodiscard]] constexpr bool test(size_t i) const noexcept {
        auto w = i / Bits_Per_Word;
        return w < num_words_ && (words()[w] & bit(i)) != 0;
    }
    [[nodiscard]] constexpr bool operator[](size_t i) const noexcept { return test(i); }
    [[nodiscard]] constexpr reference operator[](size_t i) noexcept { return {this, i}; }

    /// Index of the first bit that is set at or after @p i - or npos, if there is none.
    [[nodiscard]] constexpr size_t next(size_t i) const noexcept {
        auto w = i / Bits_Per_Word;
        if (w >= num_words_) return npos;

        auto words = this->words();
        for (auto word = words[w] & (~uint64_t(0) << (i % Bits_Per_Word));; word = words[w]) {
            if (word) return w * Bits_Per_Word + size_t(std::countr_zero(word));
            if (++w == num_words_) return npos;
        }
    }
    ///@}

    /// @name Modifiers
    /// Bitset::set and Bitset::flip grow the storage as needed; the others never allocate.
    ///@{
    constexpr Bitset& set(size_t i) {
        grow(i / Bits_Per_Word + 1);
        words()[i / Bits_Per_Word] |= bit(i);
        return *this;
    }
    constexpr Bitset& set(size_t i, bool b) { return b ? set(i) : clear(i); }
    constexpr Bitset& flip(size_t i) {
        grow(i / Bits_Per_Word + 1);
        words()[i / Bits_Per_Word] ^= bit(i);
        return *this;
    }
    constexpr Bitset& clear(size_t i) noexcept {
        if (auto w = i / Bits_Per_Word; w < num_words_) words()[w] &= ~bit(i);
        return *this;
    }
    /// Clears all bits and releases the heap storage again.
    constexpr Bitset& clear() noexcept {
        if (on_heap()) delete[] words();
        data_      = 0;
        num_words_ = 1;
        return *this;
    }
    ///@}

    /// @name Queries
    ///@{
    [[nodiscard]] constexpr size_t count() const noexcept {
        size_t res = 0;
        for (size_t i = 0, e = num_words_; i != e; ++i)
            res += size_t(std::popcount(words()[i]));
        return res;
    }
    [[nodiscard]] constexpr bool any() const noexcept { return !zeros(words(), num_words_); }
    [[nodiscard]] constexpr bool none() const noexcept { return zeros(words(), num_words_); }
    /// Number of bits available without growing.
    [[nodiscard]] constexpr size_t capacity() const noexcept { return num_words_ * Bits_Per_Word; }
    /// Outgrown the inline storage?
    [[nodiscard]] constexpr bool on_heap() const noexcept { return num_words_ != 1; }
    ///@}

    /// @name Set Operations
    /// Union, intersection, symmetric difference, and difference.
    ///@{
    constexpr Bitset& operator|=(const Bitset& other) {
        auto e = other.used();
        grow(e);
        for (size_t i = 0; i != e; ++i)
            words()[i] |= other.words()[i];
        return *this;
    }
    constexpr Bitset& operator&=(const Bitset& other) noexcept {
        auto e = std::min(num_words_, other.num_words_);
        for (size_t i = 0; i != e; ++i)
            words()[i] &= other.words()[i];
        for (size_t i = e, n = num_words_; i != n; ++i)
            words()[i] = 0;
        return *this;
    }
    constexpr Bitset& operator^=(const Bitset& other) {
        auto e = other.used();
        grow(e);
        for (size_t i = 0; i != e; ++i)
            words()[i] ^= other.words()[i];
        return *this;
    }
    constexpr Bitset& operator-=(const Bitset& other) noexcept {
        for (size_t i = 0, e = std::min(num_words_, other.num_words_); i != e; ++i)
            words()[i] &= ~other.words()[i];
        return *this;
    }
    ///@}

    /// @name Comparisons
    /// The bits beyond Bitset::capacity are all zero, so a different capacity does *not* make two Bitset%s differ.
    ///@{
    [[nodiscard]] constexpr bool operator==(const Bitset& other) const noexcept {
        auto e = std::min(num_words_, other.num_words_);
        for (size_t i = 0; i != e; ++i)
            if (words()[i] != other.words()[i]) return false;
        return zeros(words() + e, num_words_ - e) && zeros(other.words() + e, other.num_words_ - e);
    }

    /// Is every bit set in `this` also set in @p other?
    [[nodiscard]] constexpr bool subset_of(const Bitset& other) const noexcept {
        auto e = std::min(num_words_, other.num_words_);
        for (size_t i = 0; i != e; ++i)
            if (words()[i] & ~other.words()[i]) return false;
        return zeros(words() + e, num_words_ - e);
    }

    /// Do `this` and @p other have at least one bit in common?
    [[nodiscard]] constexpr bool intersects(const Bitset& other) const noexcept {
        for (size_t i = 0, e = std::min(num_words_, other.num_words_); i != e; ++i)
            if (words()[i] & other.words()[i]) return true;
        return false;
    }
    ///@}

    /// @name Iterators
    /// Yield the index of each set bit in ascending order.
    ///@{
    [[nodiscard]] constexpr iterator begin() const noexcept { return {this, next(0)}; }
    [[nodiscard]] constexpr iterator end() const noexcept { return {}; }
    ///@}

    [[nodiscard]] constexpr size_t hash() const noexcept {
        auto res = hash_begin();
        for (size_t i = 0, e = used(); i != e; ++i) {
            auto word = words()[i];
            res       = hash_combine(res, uint32_t(word));
            res       = hash_combine(res, uint32_t(word >> 32));
        }
        return res;
    }

    void dump() const { std::cout << (*this) << std::endl; }

    struct Hash {
        constexpr size_t operator()(const Bitset& bitset) const noexcept { return bitset.hash(); }
    };

#ifdef FE_ABSL
    template<class H>
    friend H AbslHashValue(H h, const Bitset& bitset) {
        return H::combine(std::move(h), bitset.hash());
    }
#endif

    friend constexpr Bitset operator|(Bitset b1, const Bitset& b2) { return std::move(b1 |= b2); }
    friend constexpr Bitset operator&(Bitset b1, const Bitset& b2) { return std::move(b1 &= b2); }
    friend constexpr Bitset operator^(Bitset b1, const Bitset& b2) { return std::move(b1 ^= b2); }
    friend constexpr Bitset operator-(Bitset b1, const Bitset& b2) { return std::move(b1 -= b2); }

    friend std::ostream& operator<<(std::ostream& os, const Bitset& bitset) {
        os << '{';
        for (auto sep = ""; auto i : bitset) {
            os << sep << i;
            sep = ", ";
        }
        return os << '}';
    }

    friend constexpr void swap(Bitset& b1, Bitset& b2) noexcept {
        using std::swap;
        swap(b1.data_, b2.data_);
        swap(b1.num_words_, b2.num_words_);
    }

private:
    static constexpr uint64_t bit(size_t i) noexcept { return uint64_t(1) << (i % Bits_Per_Word); }

    static constexpr bool zeros(const uint64_t* words, size_t num_words) noexcept {
        for (size_t i = 0; i != num_words; ++i)
            if (words[i]) return false;
        return true;
    }

    constexpr uint64_t* words() noexcept { return on_heap() ? bitcast_resize<uint64_t*>(data_) : &data_; }
    constexpr const uint64_t* words() const noexcept {
        return on_heap() ? bitcast_resize<const uint64_t*>(data_) : &data_;
    }

    /// Number of words up to and including the last one that has a bit set.
    constexpr size_t used() const noexcept {
        for (auto i = num_words_; i-- != 0;)
            if (words()[i]) return i + 1;
        return 0;
    }

    constexpr void grow(size_t num_words) {
        if (num_words <= num_words_) return;

        num_words = std::max(num_words, num_words_ * 2);
        auto heap = new uint64_t[num_words]();
        std::copy_n(words(), num_words_, heap);
        if (on_heap()) delete[] words();
        data_      = bitcast_resize<uint64_t>(heap);
        num_words_ = num_words;
    }

    uint64_t data_    = 0; ///< The bits themselves while inline, the heap words otherwise.
    size_t num_words_ = 1; ///< Capacity in words; `1` means inline.
};

static_assert(sizeof(void*) != 8 || sizeof(Bitset) == 16, "Bitset should stay two machine words");
static_assert(std::forward_iterator<Bitset::iterator>);
static_assert(std::ranges::forward_range<Bitset>);

} // namespace fe

#ifndef DOXYGEN
template<>
struct std::hash<fe::Bitset> {
    constexpr size_t operator()(const fe::Bitset& bitset) const noexcept { return bitset.hash(); }
};
#endif
