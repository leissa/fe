#pragma once

#include <concepts>

#include <algorithm>
#include <deque>
#include <format>

#include "fe/loc.h"
#include "fe/ring.h"

namespace fe {

/// The blueprint for a [recursive descent](https://en.wikipedia.org/wiki/Recursive_descent_parser)/
/// [ascent parser](https://en.wikipedia.org/wiki/Recursive_ascent_parser) using a @p K lookahead of `Tok`ens.
/// Parser::accept and Parser::expect indicate failure by constructing a @p Tok%en with its default constructor.
/// Hence, @p Tok must be default-constructible *and* testable as a `bool` (to check for that failure):
/// ```
/// class Tok {
/// public:
///     enum class Tag {
///         Nil,
///         // ...
///     };
///     Tok() {} // default constructor yields the "failure" token
///     // ...
///     explicit operator bool() const { return tag_ != Tag::Nil; }
///     // ...
/// };
///
/// // Your Parser:
/// if (auto tok = accept(Tok::Tag::My_Tag)) {
///     do_something(tok);
/// }
/// ```
/// The Parser itself does not report anything; @p S must provide the Lexer to pull from and the diagnostics:
/// ```
/// class MyParser : public fe::Parser<Tok, Tok::Tag, K, MyParser> {
///     Lexer& lexer();                                  ///< Parser::lex pulls the next Tok%en from here.
///     void syntax_err(Tag, std::string_view ctxt);     ///< Parser::expect did not find its Tag.
///     void unanchored_err(Tok, std::string_view ctxt); ///< Parser::recover discarded this Tok%en.
///
///     friend fe::Parser<Tok, Tok::Tag, K, MyParser>;   ///< Otherwise, these may be private.
/// };
/// ```
template<class Tok, class Tag, size_t K, class S>
requires std::is_default_constructible_v<Tok>
      && (std::is_convertible_v<Tok, bool> || std::is_constructible_v<bool, Tok>)
class Parser {
private:
    S& self() { return *static_cast<S*>(this); }
    const S& self() const { return *static_cast<const S*>(this); }

protected:
    /// @name Construction
    ///@{
    void init() {
        ahead_.reset();
        for (size_t i = 0; i != K; ++i)
            ahead_[i] = self().lexer().lex();
        curr_ = ahead().loc().anew_begin();
    }
    ///@}

    /// @name Tracker
    /// Track Loc%ation in the source file.
    /// Use like this:
    /// ```
    /// auto track  = tracker();
    /// auto foo    = parse_foo();
    /// auto bar    = parse_bar();
    /// auto foobar = new FooBar(track, foo, bar);
    /// ```
    ///@{
    class Tracker {
    public:
        Tracker(Pos start, Loc& curr)
            : start_(start)
            , curr_(curr) {}

        Loc loc() const { return {curr_.src, start_, curr_.end}; }
        Loc operator()() const { return loc(); }
        operator Loc() const { return loc(); }

    private:
        Pos start_;
        const Loc& curr_;
    };

    /// Factory method to build a Parser::Tracker.
    Tracker tracker() { return {ahead().loc().begin, curr_}; }
    Tracker tracker(Pos begin) { return {begin, curr_}; } ///< As above but start tracking at @p begin.
    Tracker tracker(Loc begin) { return {begin.begin, curr_}; }
    ///@}

    /// @name Shift Token
    ///@{
    /// Get lookahead.
    Tok ahead(size_t i = 0) const { return ahead_[i]; }

    /// Invoke Lexer to retrieve next Token.
    Tok lex() {
        auto result = ahead();
        curr_       = result.loc();
        ahead_.put(self().lexer().lex());
        return result;
    }

    /// If Parser::ahead() is a @p tag, consume and return it, otherwise yield `std::nullopt`.
    Tok accept(Tag tag) {
        if (tag != ahead().tag()) return {};
        return lex();
    }

    /// Parser::lex Parser::ahead() which must be a @p tag.
    /// Issue error with @p ctxt otherwise.
    Tok expect(Tag tag, std::string_view ctxt) {
        if (ahead().tag() == tag) return lex();
        self().syntax_err(tag, ctxt);
        return {};
    }

    /// As above but builds the context via std::format.
    template<class... Args>
    Tok expect(Tag tag, std::format_string<Args...> fmt, Args&&... args) {
        if (ahead().tag() == tag) return lex();
        self().syntax_err(tag, std::format(fmt, std::forward<Args>(args)...));
        return {};
    }

    /// Consume Parser::ahead which must be a @p tag; asserts otherwise.
    Tok eat([[maybe_unused]] Tag tag) {
        assert(tag == ahead().tag() && "internal parser error");
        return lex();
    }
    ///@}

    /// @name Anchor
    /// An *anchor* is a @p Tag that an enclosing context is waiting for.
    /// E.g., while parsing a parenthesized expression, `)` is anchored:
    /// a nested parser must not swallow it but bail out, so the enclosing context can Parser::expect it.
    /// A `)` that is *not* anchored, however, is simply bogus and Parser::recover discards it.
    ///@{

    /// RAII helper that anchors a @p Tag for its lifetime; use Parser::anchor to build one.
    class Anchor {
    public:
        Anchor(const Anchor&)            = delete;
        Anchor& operator=(const Anchor&) = delete;

        Anchor(Parser& parser, Tag tag, std::string ctxt = {})
            : parser_(parser)
            , tag_(tag)
            , ctxt_(std::move(ctxt)) {
            parser_.anchors_.emplace_back(tag);
        }

        /// Removes the anchor again and - unless constructed without a @p ctxt - Parser::expect%s @p tag.
        ~Anchor() {
            parser_.anchors_.pop_back();
            if (!ctxt_.empty()) parser_.expect(tag_, ctxt_);
        }

    private:
        Parser& parser_;
        Tag tag_;
        std::string ctxt_;
    };

    /// Factory method to build a Parser::Anchor.
    /// A @p ctxt makes the Parser::Anchor Parser::expect @p tag at the end of the scope; omit it to merely anchor.
    /// Use like this:
    /// ```
    /// if (accept(Tag::D_paren_l)) {
    ///     auto _ = this->anchor(Tag::D_paren_r, "parenthesized expression");
    ///     return parse_expr();
    /// }
    /// ```
    [[nodiscard]] Anchor anchor(Tag tag, std::string_view ctxt = {}) { return {*this, tag, std::string(ctxt)}; }

    /// As above but builds @p ctxt via std::format.
    template<class... Args>
    [[nodiscard]] Anchor anchor(Tag tag, std::format_string<Args...> fmt, Args&&... args) {
        return {*this, tag, std::format(fmt, std::forward<Args>(args)...)};
    }

    /// Is @p tag anchored by an enclosing context?
    /// Scans the innermost anchor first, but *any* enclosing context counts.
    bool anchored(Tag tag) const { return std::find(anchors_.rbegin(), anchors_.rend(), tag) != anchors_.rend(); }

    /// Parser::lex all Tok%ens whose Tag satisfies @p pred and that are not Parser::anchored;
    /// report each one as `S::unanchored_err`.
    /// This turns an otherwise fatal Tok%en into a mere error message and keeps the current parser going.
    template<std::predicate<Tag> P>
    void recover(P pred, std::string_view ctxt) {
        while (pred(ahead().tag()) && !anchored(ahead().tag()))
            self().unanchored_err(lex(), ctxt);
    }

    /// As above but only recovers from @p tag.
    void recover(Tag tag, std::string_view ctxt) {
        recover([tag](Tag t) { return t == tag; }, ctxt);
    }
    ///@}

    Ring<Tok, K> ahead_;
    Loc curr_;
    std::deque<Tag> anchors_;
};

} // namespace fe
