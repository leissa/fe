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
    void start() { loc_ = peek().anew_begin(); }

    /// @name Text
    /// What has been lexed since Lexer::start.
    /// The whole source sits in Lexer::buf_, so Lexer::loc_ already *is* the token and Lexer::view costs nothing.
    ///@{
    std::string_view view() const { return buf_.substr(loc_.begin.offset, loc_.size()); }

    /// Lexer::view, case-folded - what a case-insensitive language like FORTRAN or SQL wants to intern.
    /// @note Byte-wise, which is all it takes: only ASCII folds, and no UTF-8 sequence spells it.
    std::string lower() const { return fold(fe::utf8::tolower); }
    std::string upper() const { return fold(fe::utf8::toupper); }

    // Transform view() via @p f.
    std::string fold(char32_t (*f)(char32_t) noexcept) const {
        std::string res(view());
        for (auto& c : res)
            c = (char)f((char32_t)(uint8_t)c);
        return res;
    }
    ///@}

    /// @name Accept
    ///@{

    /// Get next `char32_t` in Lexer::buf_ and extend Lexer::loc_ to cover it.
    /// @returns utf8::Invalid on an invalid UTF-8 sequence.
    char32_t next() {
        loc_.end = ahead_[0].end;
        return ahead_.put(decode()).c;
    }

    /// Accept next character in Lexer::buf_ and Lexer::next it, if @p pred holds.
    template<class Pred>
    bool accept(Pred pred) {
        if (!pred(ahead())) return false;
        self().next();
        return true;
    }

    // clang-format off
    bool accept(char32_t c) { return accept([c](char32_t d) { return c == d; }); }
    bool accept(char     c) { return accept((char32_t)c); }
    bool accept(char8_t  c) { return accept((char32_t)c); }
    // clang-format on

    /// Lexer::next as long as @p pred holds.
    /// An ASCII run is taken straight out of Lexer::buf_ - no code point is decoded and the lookahead
    /// is re-primed once at the end, which is what makes scanning an identifier or a stretch of white
    /// space cost a compare per byte.
    /// A character beyond ASCII falls back to Lexer::next, so @p pred may match one.
    /// @note Only worth it if the lexed text is expected to be long such as identifiers or comments.
    /// @returns the run just consumed.
    template<class Pred>
    std::string_view accept_while(Pred pred) {
        auto begin = ahead_[0].begin.offset;

        while (true) {
            auto run = ahead_[0].begin.offset;
            for (; run != buf_.size() && (uint8_t)buf_[run] < 0x80 && pred((char32_t)(uint8_t)buf_[run]); ++run) {}

            if (run != ahead_[0].begin.offset) {
                loc_.end = Pos((uint32_t)run);
                cursor_  = run;
                for (size_t i = 0; i != K; ++i)
                    ahead_.put(decode());
            }

            auto c = ahead();
            if (c < 0x80 || c == utf8::EoF || !pred(c)) break;
            self().next();
        }

        return buf_.substr(begin, loc_.end.offset - begin);
    }

    /// Lexer::next up to - but not including - the next byte that is @p a or @p b.
    /// The stop bytes are a *set*: whichever comes first ends the run, and this is no substring search.
    /// Stops at the end of Lexer::buf_ if none of them ever shows up.
    /// No UTF-8 sequence spells an ASCII byte, so such a run needs no decoding at all:
    /// this is how to skip a comment or a string literal,
    /// where Lexer::accept_while pays a compare - and Lexer::next a whole utf8::decode - per character.
    /// @note Nothing in the run is validated as UTF-8 - malformed bytes cannot spell @p a or @p b either.
    /// @returns the run just consumed.
    std::string_view accept_while_none_of(char8_t a, char8_t b) {
        assert(a < 0x80 && b < 0x80 && "only an ASCII byte can be searched for without decoding");
        auto begin = ahead_[0].begin.offset;
        auto run   = begin;

        for (auto e = buf_.size(); run != e; ++run)
            if (auto c = (char8_t)buf_[run]; c == a || c == b) break;

        return skip_to(begin, run);
    }

    /// As Lexer::accept_while_none_of(char8_t, char8_t), but a single stop byte, which one `memchr` finds outright.
    std::string_view accept_while_none_of(char8_t a) {
        assert(a < 0x80 && "only an ASCII byte can be searched for without decoding");
        auto begin = ahead_[0].begin.offset;
        auto pos   = buf_.find((char)a, begin);
        return skip_to(begin, pos == std::string_view::npos ? buf_.size() : pos);
    }

    std::string_view accept_while_none_of(char a) { return accept_while_none_of((char8_t)a); }
    std::string_view accept_while_none_of(char a, char b) { return accept_while_none_of((char8_t)a, (char8_t)b); }

    /// Lexer::next up to - but not including - the next occurrence of @p seq.
    /// Where Lexer::accept_while_none_of takes a set of bytes, this one takes a *sequence*:
    /// only @p seq, spelled in exactly that order, ends the run - which is what closes a `/*` comment.
    /// Stops at the end of Lexer::buf_ if @p seq never shows up.
    /// @note Nothing in the run is decoded or validated as UTF-8.
    /// @returns the run just consumed.
    std::string_view accept_until(std::string_view seq) {
        assert(!seq.empty() && "an empty sequence matches at once, so the lexer would not advance");
        auto begin = ahead_[0].begin.offset;
        auto pos   = buf_.find(seq, begin);
        return skip_to(begin, pos == std::string_view::npos ? buf_.size() : pos);
    }
    ///@}

    /// @name Recover
    /// Lexer::next input that cannot be part of a token, report it, and keep the current lexer going.
    /// Invoke after Lexer::start, so Lexer::loc_ spans exactly what was discarded.
    ///@{
    /// A whole run of malformed UTF-8, if any, reported as one `S::utf8_err`.
    /// Check this *before* your token dispatch: utf8::Invalid is no code point and matches no rule of yours.
    bool recover_utf8() {
        if (!accept(utf8::Invalid)) return false;
        while (accept(utf8::Invalid)) {}
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
    /// Each yields the Error it reported into, so a Note can be chained.
    ///@{
    fe::Error& error() { return self().driver().error(); }
    const fe::Error& error() const { return self().driver().error(); }

    /// Lexer::recover_utf8 discarded the malformed bytes at Lexer::loc_.
    fe::Error& utf8_err() {
        static_assert(
            requires(S& s) { s.driver(); },
            "provide `fe::Driver& driver()` in your lexer - or a `utf8_err` of your own");
        return error().e(loc_, "invalid UTF-8 sequence");
    }

    /// Lexer::recover_char discarded @p c at Lexer::loc_.
    fe::Error& char_err(char32_t c) {
        static_assert(
            requires(S& s) { s.driver(); },
            "provide `fe::Driver& driver()` in your lexer - or a `char_err` of your own");
        return error().e(loc_, "invalid input character `{}`", utf8::Char32(c));
    }
    ///@}

    std::string_view buf_;
    const Src* src_;
    size_t cursor_ = 0; ///< Byte offset of the first not yet decoded character.
    Ring<Ahead, K> ahead_;
    Loc loc_; ///< Loc%ation of the token we are currently constructing - see Lexer::view.

private:
    /// Consume Lexer::buf_ up to byte offset @p run, without decoding a thing.
    /// @returns that range.
    std::string_view skip_to(size_t begin, size_t run) {
        if (run != begin) {
            loc_.end = Pos((uint32_t)run);
            cursor_  = run;
            for (size_t i = 0; i != K; ++i)
                ahead_.put(decode());
        }

        return buf_.substr(begin, run - begin);
    }

    Ahead decode() {
        auto begin = cursor_;
        auto c     = utf8::decode(buf_, cursor_);
        return {c, Pos((uint32_t)begin), Pos((uint32_t)cursor_)};
    }
};

} // namespace fe
