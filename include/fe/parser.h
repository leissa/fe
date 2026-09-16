#pragma once

#include <concepts>

#include <algorithm>
#include <deque>
#include <format>

#include "fe/driver.h"
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
/// @p S must provide the Lexer to pull from and the Driver to report to:
/// ```
/// class MyParser : public fe::Parser<Tok, Tok::Tag, K, MyParser> {
///     Lexer& lexer();                                ///< Parser::lex pulls the next Tok%en from here.
///     fe::Driver& driver();                          ///< The default diagnostics below land in its Driver::error.
///
///     friend fe::Parser<Tok, Tok::Tag, K, MyParser>; ///< Otherwise, these may be private.
/// };
/// ```
/// Parser::syntax_err and Parser::unanchored_err come with a default; declare either in @p S to word it differently.
/// All Parser::syntax_err overloads funnel through the one taking a `what`/`Tok`/`ctxt`; override just that one.
/// @warning Declaring *any* `syntax_err` in @p S hides all of them, so add `using Super::syntax_err;`.
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

        /// If nothing was consumed since this Tracker started (a total parse failure for whatever it was tracking),
        /// @p curr_'s end still precedes @p start_; yield a zero-width Loc at @p start_ instead of a backwards one.
        Loc loc() const { return {curr_.src, start_, start_ <= curr_.end ? curr_.end : start_}; }
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
    Tok expect(Tag tag, Cite ctxt) {
        if (ahead().tag() == tag) return lex();
        self().syntax_err(tag, ctxt);
        return {};
    }

    /// As above but builds the context via fe::format_cite.
    template<class... Args>
    Tok expect(Tag tag, cite_string<Args...> fmt, Args&&... args) {
        if (ahead().tag() == tag) return lex();
        self().syntax_err(tag, format_cite(fmt, std::forward<Args>(args)...));
        return {};
    }

    /// Consume Parser::ahead which must be a @p tag; asserts otherwise.
    Tok eat([[maybe_unused]] Tag tag) {
        assert(tag == ahead().tag() && "internal parser error");
        return lex();
    }
    ///@}

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

    /// @name Anchor
    /// An *anchor* is a @p Tag that an enclosing context is waiting for.
    /// E.g., while parsing a parenthesized expression, `)` is anchored:
    /// a nested parser must not swallow it but bail out, so the enclosing context can Parser::expect it.
    /// A `)` that is *not* anchored, however, is simply bogus and Parser::recover discards it.
    ///@{

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
    /// report the whole run as a single `S::unanchored_err`.
    /// This turns an otherwise fatal Tok%en into a mere error message and keeps the current parser going.
    /// One mistake discards one run, so one run is one diagnostic: a message per token buries the real error.
    template<std::predicate<Tag> P>
    void recover(P pred, Cite ctxt) {
        auto discard = [this, &pred] { return pred(ahead().tag()) && !anchored(ahead().tag()); };
        if (!discard()) return;

        auto first = lex();
        auto loc   = first.loc();
        size_t n   = 1;
        for (; discard(); ++n)
            loc.end = lex().loc().end;

        self().unanchored_err(first, loc, n, ctxt);
    }

    /// As above but only recovers from @p tag.
    void recover(Tag tag, Cite ctxt) {
        recover([tag](Tag t) { return t == tag; }, ctxt);
    }
    ///@}

    /// @name Diagnostics
    /// The defaults @p S may replace with one of its own.
    /// Each yields the Error it reported into, so a Note can be chained.
    ///@{
    fe::Error& error() { return self().driver().error(); }
    const fe::Error& error() const { return self().driver().error(); }

    /// Parser::expect did not find @p what while parsing @p ctxt.
    /// Both are Cite: a context string is *markup*, so backtick a literal token within it yourself.
    fe::Error& syntax_err(Cite what, Tok tok, Cite ctxt) {
        static_assert(
            requires(S& s) { s.driver(); },
            "provide `fe::Driver& driver()` in your parser - or a `syntax_err` of your own");
        return error().e(tok.loc(), "expected {}, got `{}` while parsing {}", what, tok, ctxt);
    }

    /// As above but uses Parser::ahead as @p tok.
    /// @note `decltype(auto)`, so an override of the funnel above may yield something else - or nothing.
    decltype(auto) syntax_err(Cite what, Cite ctxt) { return self().syntax_err(what, ahead(), ctxt); }

    /// As above but spells @p tag out via Parser::tag2str_.
    decltype(auto) syntax_err(Tag tag, Cite ctxt) { return self().syntax_err(tag2str_(tag), ahead(), ctxt); }

    /// Parser::recover discarded a run of @p n Tok%ens starting with @p tok and spanning @p loc while parsing @p ctxt.
    fe::Error& unanchored_err(Tok tok, Loc loc, size_t n, Cite ctxt) {
        static_assert(
            requires(S& s) { s.driver(); },
            "provide `fe::Driver& driver()` in your parser - or an `unanchored_err` of your own");
        if (n == 1) return error().e(loc, "ignoring unmatched `{}` while parsing {}", tok, ctxt);
        return error().e(loc, "ignoring {} unmatched tokens starting with `{}` while parsing {}", n, tok, ctxt);
    }
    ///@}

    /// Spells @p tag out via `Tok::tag2str` if there is one - a bare enumerator would render as its number.
    static auto tag2str_(Tag tag) {
        if constexpr (requires { Tok::tag2str(tag); })
            return format_cite("`{}`", Tok::tag2str(tag));
        else
            return format_cite("`{}`", tag);
    }

    Ring<Tok, K> ahead_;
    Loc curr_;
    std::deque<Tag> anchors_;
};

} // namespace fe
