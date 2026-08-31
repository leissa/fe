#pragma once

#include <concepts>

#include <algorithm>
#include <deque>
#include <format>

#include "fe/error.h"
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
/// @p S must provide the Lexer to pull from and somewhere to report to:
/// ```
/// class MyParser : public fe::Parser<Tok, Tok::Tag, K, MyParser> {
///     Lexer& lexer();                                ///< Parser::lex pulls the next Tok%en from here.
///     fe::Error& error();                            ///< Where the default diagnostics below go.
///
///     friend fe::Parser<Tok, Tok::Tag, K, MyParser>; ///< Otherwise, these may be private.
/// };
/// ```
/// Parser::syntax_err and Parser::unanchored_err come with a default; declare either in @p S to word it differently.
/// @warning Declaring *any* `syntax_err` in @p S hides all of them, so keep one that takes a `Tag`.
template<class Tok, class Tag, size_t K, class S>
requires std::is_default_constructible_v<Tok>
      && (std::is_convertible_v<Tok, bool> || std::is_constructible_v<bool, Tok>)class Parser {
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

        Anchor(Parser& parser, Tag tag)
            : parser_(parser) {
            parser_.anchors_.emplace_back(tag);
        }

        ~Anchor() { parser_.anchors_.pop_back(); }

    private:
        Parser& parser_;
    };

    /// Factory method to build a Parser::Anchor; Parser::expect @p tag yourself at the end of the scope.
    /// Use like this:
    /// ```
    /// if (accept(Tag::D_paren_l)) {
    ///     auto _    = this->anchor(Tag::D_paren_r);
    ///     auto expr = parse_expr();
    ///     expect(Tag::D_paren_r, "parenthesized expression");
    ///     return expr;
    /// }
    /// ```
    [[nodiscard]] Anchor anchor(Tag tag) { return {*this, tag}; }

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

    /// @name Diagnostics
    /// The defaults @p S may replace with one of its own.
    ///@{
    /// Parser::expect did not find @p tag while parsing @p ctxt.
    void syntax_err(Tag tag, std::string_view ctxt) {
        static_assert(
            requires(S& s) { s.error(); },
            "provide `fe::Error& error()` in your parser - or a `syntax_err` of your own");
        self().error().error(ahead().loc(), "expected {}, got `{}` while parsing {}", tag2str_(tag), ahead(), ctxt);
    }

    /// Parser::recover discarded @p tok while parsing @p ctxt.
    void unanchored_err(Tok tok, std::string_view ctxt) {
        static_assert(
            requires(S& s) { s.error(); },
            "provide `fe::Error& error()` in your parser - or an `unanchored_err` of your own");
        self().error().error(tok.loc(), "ignoring unmatched `{}` while parsing {}", tok, ctxt);
    }
    ///@}

    /// Spells @p tag out via `Tok::tag2str` if there is one - a bare enumerator would render as its number.
    static auto tag2str_(Tag tag) {
        if constexpr (requires { Tok::tag2str(tag); })
            return std::format("`{}`", Tok::tag2str(tag));
        else
            return std::format("`{}`", tag);
    }

    Ring<Tok, K> ahead_;
    Loc curr_;
    std::deque<Tag> anchors_;
};

} // namespace fe
