#pragma once

#include <cstddef>

#include <string>
#include <string_view>

#include "fe/loc.h"
#include "fe/ring.h"
#include "fe/utf8.h"

namespace fe {

/// The blueprint for a lexer with a buffer of @p K tokens to peek into the future (Lexer::ahead).
/// You can "override" Lexer::next via CRTP (@p S is the child).
/// The whole source has to sit in @p buf: a Pos is an index into it, so there is nothing left to
/// keep track of - Lexer::next just hands out the byte range the code point it consumed occupied.
template<size_t K, class S>
class Lexer {
private:
    S& self() { return *static_cast<S*>(this); }
    const S& self() const { return *static_cast<const S*>(this); }

public:
    Lexer(std::string_view buf, const Src* src = nullptr)
        : buf_(buf)
        , src_(src) {
        if (buf_.starts_with(utf8::Bom)) cursor_ = utf8::Bom.size();
        for (size_t i = 0; i != K; ++i)
            ahead_[i] = decode();
        start();
    }

protected:
    /// A decoded code point together with the byte range it occupies.
    struct Ahead {
        char32_t c = utf8::EoF;
        Pos begin, end;
    };

    char32_t ahead(size_t i = 0) const { return ahead_[i].c; }

    /// Loc%ation of the next character to be consumed (Lexer::ahead()); empty once the stream is exhausted.
    Loc peek() const { return {src_, ahead_[0].begin, ahead_[0].end}; }

    /// Invoke before assembling the next token.
    void start() {
        loc_ = peek().anew_begin();
        str_.clear();
    }

    /// Get next `char32_t` in Lexer::buf_ and extend Lexer::loc_ to cover it.
    /// @returns utf8::Invalid on an invalid UTF-8 sequence.
    char32_t next() {
        loc_.end = ahead_[0].end;
        return ahead_.put(decode()).c;
    }

    /// @name Accept
    /// Accept next character in Lexer::buf_, depending on some condition.
    ///@{
    /// What should happen to the accepted char?
    /// Normalize identifiers via Append::Lower or Append::Upper for case-insensitive languages like FORTRAN or SQL.
    enum class Append {
        Off,   ///< Do not append accepted char to Lexer::str_.
        On,    ///< Append accepted char as is to Lexer::str_.
        Lower, ///< Append accepted char via fe::utf8::tolower` to Lexer::str_.
        Upper, ///< Append accepted char via fe::utf8::toupper` to Lexer::str_.
    };

    /// @returns `true` if @p pred holds.
    /// In this case invoke Lexer::next() and append to Lexer::str_, if @p append.
    template<Append append = Append::On, class Pred>
    bool accept(Pred pred) {
        if (pred(ahead())) {
            auto c = self().next();
            if constexpr (append != Append::Off) {
                if constexpr (append == Append::Lower) c = fe::utf8::tolower(c);
                if constexpr (append == Append::Upper) c = fe::utf8::toupper(c);
                str_ += c;
            }
            return true;
        }
        return false;
    }

    // clang-format off
    template<Append append = Append::On> bool accept(char32_t c) { return accept<append>([c](char32_t d) { return c == d; }); }
    template<Append append = Append::On> bool accept(char     c) { return accept<append>((char32_t)c); }
    template<Append append = Append::On> bool accept(char8_t  c) { return accept<append>((char32_t)c); }
    // clang-format on
    ///@}

    std::string_view buf_;
    const Src* src_;
    size_t cursor_ = 0; ///< Byte offset of the first not yet decoded character.
    Ring<Ahead, K> ahead_;
    Loc loc_; ///< Loc%ation of the token we are currently constructing within Lexer::str_,
    std::string str_;

private:
    Ahead decode() {
        auto begin = cursor_;
        auto c     = utf8::decode(buf_, cursor_);
        return {c, Pos((uint32_t)begin), Pos((uint32_t)cursor_)};
    }
};

} // namespace fe
