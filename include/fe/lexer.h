#pragma once

#include <cstddef>

#include <string>
#include <string_view>

#include "fe/driver.h"
#include "fe/loc.h"
#include "fe/ring.h"
#include "fe/src.h"
#include "fe/utf8.h"

namespace fe {

/// The blueprint for a lexer with a buffer of @p K tokens to peek into the future (Lexer::ahead).
/// You can "override" Lexer::next via CRTP (@p S is the child).
/// The whole source has to sit in @p buf: a Pos is an index into it, so there is nothing left to
/// keep track of - Lexer::next just hands out the byte range the code point it consumed occupied.
/// @p S must provide somewhere to report to:
/// ```
/// class MyLexer : public fe::Lexer<K, MyLexer> {
///     fe::Driver& driver();                ///< The default diagnostic below lands in its Driver::error.
///
///     friend fe::Lexer<K, MyLexer>;        ///< Otherwise, this may be private.
/// };
/// ```
/// Lexer::utf8_err and Lexer::char_err come with a default; declare either in @p S to word it differently.
template<size_t K, class S>
class Lexer {
private:
    S& self() { return *static_cast<S*>(this); }
    const S& self() const { return *static_cast<const S*>(this); }

public:
    Lexer(std::string_view buf)
        : Lexer(buf, nullptr) {}
    Lexer(const Src& src)
        : Lexer(src.buf(), &src) {}

protected:
    /// Delegate here to funnel both of the above into a single ctor of your own.
    Lexer(std::string_view buf, const Src* src)
        : buf_(buf)
        , src_(src) {
        if (buf_.starts_with(utf8::Bom)) cursor_ = utf8::Bom.size();
        for (size_t i = 0; i != K; ++i)
            ahead_[i] = decode();
        start();
    }

    /// A decoded code point together with the byte range it occupies.
    struct Ahead {
        char32_t c = utf8::EoF;
        Pos begin, end;
    };

    char32_t ahead(size_t i = 0) const { return ahead_[i].c; }

    /// Loc%ation of the next character to be consumed (Lexer::ahead()); empty once the buffer is exhausted.
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

    /// @name Recover
    /// Lexer::next input that cannot be part of a token, report it, and keep the current lexer going.
    /// Invoke after Lexer::start, so Lexer::loc_ spans exactly what was discarded.
    ///@{
    /// A whole run of malformed UTF-8, if any, reported as one `S::utf8_err`.
    /// Check this *before* your token dispatch: utf8::Invalid is no code point and matches no rule of yours.
    bool recover_utf8() {
        if (!accept<Append::Off>(utf8::Invalid)) return false;
        while (accept<Append::Off>(utf8::Invalid)) {}
        self().utf8_err();
        return true;
    }

    /// One character, reported as `S::char_err`.
    /// This is the last resort of your token dispatch: nothing in your language starts with it.
    /// @warning Never at utf8::EoF - accept that first or your lexer will spin.
    void recover_char() {
        auto c = ahead();
        self().next();
        self().char_err(c);
    }
    ///@}

    /// @name Diagnostics
    /// The defaults @p S may replace with one of its own.
    ///@{
    /// Lexer::recover_utf8 discarded the malformed bytes at Lexer::loc_.
    void utf8_err() {
        static_assert(
            requires(S& s) { s.driver(); },
            "provide `fe::Driver& driver()` in your lexer - or a `utf8_err` of your own");
        self().driver().error(loc_, "invalid UTF-8 sequence");
    }

    /// Lexer::recover_char discarded @p c at Lexer::loc_.
    void char_err(char32_t c) {
        static_assert(
            requires(S& s) { s.driver(); },
            "provide `fe::Driver& driver()` in your lexer - or a `char_err` of your own");
        self().driver().error(loc_, "invalid input character `{}`", utf8::Char32(c));
    }
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
