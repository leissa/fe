#pragma once

#include <concepts>

#include <algorithm>
#include <type_traits>

#include "fe/assert.h"
#include "fe/error.h"
#include "fe/format.h"
#include "fe/loc.h"
#include "fe/ring.h"
#include "fe/vector.h"

namespace fe {

/// What fe::Parser needs of its Tok%en type: a Tag and a Loc, copied in and out of the lookahead by value.
/// Parser::accept and Parser::expect indicate failure by returning a default-constructed Tok%en,
/// so `Tok()` must be falsy and tagged Tag::Nil.
/// That sentinel is only checkable here if `Tok()` is usable in a constant expression - Parser::init asserts it.
template<class Tok, class Tag>
concept Token = requires(const Tok tok) {
    Tag::Nil;
    { tok.tag() } -> std::convertible_to<Tag>;
    { tok.loc() } -> std::convertible_to<Loc>;
    requires std::equality_comparable<Tag>;
    requires std::semiregular<Tok>;
    requires std::constructible_from<bool, Tok>;
    requires !requires { std::integral_constant<Tag, Tok().tag()>(); } || Tok().tag() == Tag::Nil;
};

/// What fe::Parser needs of its CRTP child @p S beyond fe::Diagnosable: the Lexer to pull Tok%ens from.
template<class S, class Tok>
concept Lexable = requires(S& s) {
    { s.lexer().lex() } -> std::convertible_to<Tok>;
};

/// The blueprint for a [recursive descent](https://en.wikipedia.org/wiki/Recursive_descent_parser)/
/// [ascent parser](https://en.wikipedia.org/wiki/Recursive_ascent_parser) using a @p K lookahead of `Tok`ens.
/// Parser::accept and Parser::expect indicate failure by constructing a @p Tok%en with its default constructor.
/// Hence, @p Tok must be an fe::Token - default-constructible, testable as a `bool`, and tagged Tag::Nil:
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
/// @p S must provide the Lexer to pull from and the Driver to report to - publicly,
/// so that fe::Lexable and fe::Diagnosable hold outside this class too:
/// ```
/// class MyParser : public fe::Parser<Tok, Tok::Tag, K, MyParser> {
/// public:
///     Lexer& lexer();     ///< Parser::lex pulls the next Tok%en from here.
///     fe::Driver& driver(); ///< The default diagnostics below land in its Driver::error.
/// };
/// ```
/// Parser::syntax_err and Parser::unanchored_err come with a default; declare either in @p S to word it differently.
/// Each is one name with one signature, so a declaration in @p S replaces it outright - no `using` of the base needed.
template<class Tok, class Tag, size_t K, class S>
requires Token<Tok, Tag> && (K >= 1) class Parser {
private:
    S& self() { return *static_cast<S*>(this); }
    const S& self() const { return *static_cast<const S*>(this); }

protected:
    /// @name Construction
    ///@{
    void init() {
        static_assert(Lexable<S, Tok>, "provide `Lexer& lexer()` in your parser - its `lex` is what feeds the Parser");
        assert(!Tok() && Tok().tag() == Tag::Nil && "a default-constructed Tok must be the Nil token");
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

    /// One anchor; the *left* end opened the context, the *right* one is what it is still waiting for.
    struct Anchor {
        Tok l_tok; ///< The Tok%en that opened the context; default-constructed unless Parser::anchor was given one.
        Tag r_tag; ///< The closing Tag that context is waiting for.
    };

    /// RAII helper that pushes a Parser::Anchor for its lifetime; use Parser::anchor to build one.
    class ScopedAnchor {
    public:
        ScopedAnchor(const ScopedAnchor&)            = delete;
        ScopedAnchor& operator=(const ScopedAnchor&) = delete;

        ScopedAnchor(Parser& parser, Tok l_tok, Tag r_tag)
            : parser_(parser) {
            parser_.anchors_.emplace_back(std::move(l_tok), r_tag);
        }

        ~ScopedAnchor() { parser_.anchors_.pop_back(); }

    private:
        Parser& parser_;
    };

    /// @name Anchor
    /// An *anchor* is a @p Tag that an enclosing context is waiting for.
    /// E.g., while parsing a parenthesized expression, `)` is anchored:
    /// a nested parser must not swallow it but bail out, so the enclosing context can Parser::expect it.
    /// A `)` that is *not* anchored, however, is simply bogus and Parser::recover discards it.
    ///@{

    /// Factory method to build a Parser::ScopedAnchor; Parser::expect @p r_tag yourself at the end of the scope.
    /// Hand the Tok%en that opened this context to @p l_tok and Parser::syntax_err notes it on a missing @p r_tag,
    /// which is all it takes to point a `)` that never came back at its `(`.
    /// Use like this:
    /// ```
    /// if (auto paren_l = accept(Tag::D_paren_l)) {
    ///     auto _    = this->anchor(Tag::D_paren_r, paren_l);
    ///     auto expr = parse_expr();
    ///     expect(Tag::D_paren_r, "parenthesized expression");
    ///     return expr;
    /// }
    /// ```
    [[nodiscard]] ScopedAnchor anchor(Tok l_tok, Tag r_tag) { return {*this, l_tok, r_tag}; }

    /// The innermost Anchor waiting for @p r_tag - `nullptr` if no enclosing context is.
    /// Scans the innermost anchor first, but *any* enclosing context counts.
    const Anchor* find_anchor(Tag r_tag) const {
        if (r_tag == Tag::Nil) return nullptr;
        auto i
            = std::find_if(anchors_.rbegin(), anchors_.rend(), [r_tag](const Anchor& a) { return a.r_tag == r_tag; });
        return i == anchors_.rend() ? nullptr : &*i;
    }

    /// Is @p r_tag anchored by an enclosing context?
    bool is_anchored(Tag r_tag) const { return find_anchor(r_tag); }

    /// Parser::lex all Tok%ens whose Tag satisfies @p pred and that are not Parser::is_anchored;
    /// report the whole run as a single `S::unanchored_err`.
    /// This turns an otherwise fatal Tok%en into a mere error message and keeps the current parser going.
    /// One mistake discards one run, so one run is one diagnostic: a message per token buries the real error.
    template<std::predicate<Tag> P>
    void recover(P pred, Cite ctxt) {
        auto discard = [this, &pred] { return pred(ahead().tag()) && !is_anchored(ahead().tag()); };
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

    /// What Parser::expect was after - a Tag, or free-form markup.
    /// Both spellings are one parameter rather than two overloads, so that Parser::syntax_err stays a single
    /// signature: a declaration in @p S then replaces it instead of hiding a set of siblings alongside it.
    struct Expected {
        Tag tag = Tag::Nil; ///< Tag::Nil, if the expectation was not a Tag.
        Cited what;         ///< Already rendered as markup - Parser::tag2str_ backticks a Tag.

        Expected(Tag tag)
            : tag(tag)
            , what(tag2str_(tag)) {}
        Expected(Cited what)
            : what(std::move(what)) {}
        Expected(Cite what)
            : what(std::string(what.view())) {}
        template<size_t N>
        Expected(const char (&what)[N])
            : Expected(Cite(what)) {}
    };

    /// @name Diagnostics
    /// The defaults @p S may replace with one of its own.
    /// Each yields the Error it reported into, so a Note can be chained.
    ///@{
    fe::Error& error() { return self().driver().error(); }
    const fe::Error& error() const { return self().driver().error(); }

    /// Parser::expect did not find @p what while parsing @p ctxt; @p got defaults to Parser::ahead.
    /// @p ctxt is markup, so backtick a literal token within it yourself.
    /// A @p what that an enclosing context anchors gets a Note pointing back at the Tok%en that opened it -
    /// the `(` of a `)` that never came - as long as Parser::anchor was handed that token.
    fe::Error& syntax_err(Expected what, Cite ctxt, Tok got = {}) {
        static_assert(Diagnosable<S>, "provide `fe::Driver& driver()` in your parser - or a `syntax_err` of your own");
        static_assert(Formattable<Tok>, "provide a `std::formatter` for your Tok - or a `syntax_err` of your own");
        if (!got) got = ahead();
        auto& err = error().e(got.loc(), "expected {}, got `{}` while parsing {}", what.what, got, ctxt);
        if (auto* a = find_anchor(what.tag); a && a->l_tok)
            err.n(a->l_tok.loc(), "unmatched `{}` opened here", a->l_tok);
        return err;
    }

    /// Parser::recover discarded a run of @p n Tok%ens starting with @p tok and spanning @p loc while parsing @p ctxt.
    fe::Error& unanchored_err(Tok tok, Loc loc, size_t n, Cite ctxt) {
        static_assert(Diagnosable<S>,
                      "provide `fe::Driver& driver()` in your parser - or an `unanchored_err` of your own");
        static_assert(Formattable<Tok>, "provide a `std::formatter` for your Tok - or an `unanchored_err` of your own");
        if (n == 1) return error().e(loc, "ignoring unmatched `{}` while parsing {}", tok, ctxt);
        return error().e(loc, "ignoring {} unmatched tokens starting with `{}` while parsing {}", n, tok, ctxt);
    }
    ///@}

    /// Spells @p tag out via `Tok::tag2str` if there is one - a bare enumerator would render as its number.
    static auto tag2str_(Tag tag) {
        if constexpr (requires { Tok::tag2str(tag); }) {
            return format_cite("`{}`", Tok::tag2str(tag));
        } else {
            static_assert(Formattable<Tag>, "provide `Tok::tag2str` - or a `std::formatter` for your Tag");
            return format_cite("`{}`", tag);
        }
    }

    Ring<Tok, K> ahead_;
    Loc curr_;
    Vector<Anchor> anchors_;
};

} // namespace fe
