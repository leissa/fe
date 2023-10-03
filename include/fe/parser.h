#pragma once

#include <optional>

#include "fe/loc.h"
#include "fe/ring.h"

namespace fe {

/// The blueprint for a [recursive descent](https://en.wikipedia.org/wiki/Recursive_descent_parser)/
/// [ascent parser](https://en.wikipedia.org/wiki/Recursive_ascent_parser) using a @p K lookahead of `Tok`ens.
template<class Tok, class Tag, size_t K, class S> class Parser {
private:
    S& self() { return *static_cast<S*>(this); }
    const S& self() const { return *static_cast<const S*>(this); }

protected:
    void init(const std::filesystem::path* path) {
        ahead_.reset();
        for (size_t i = 0; i != K; ++i) ahead_[i] = self().lexer().lex();
        prev_ = Loc(path, {1, 1});
    }

    /// @name Track Loc%ation in Source File
    ///@{
    /// Use like this:
    /// ```
    /// auto track  = tracker();
    /// auto foo    = parse_foo();
    /// auto bar    = parse_bar();
    /// auto foobar = new FooBar(track, foo, bar);
    /// ```
    class Tracker {
    public:
        Tracker(Loc& prev, Pos pos)
            : prev_(prev)
            , pos_(pos) {}

        Loc loc() const { return {prev_.path, pos_, prev_.finis}; }
        operator Loc() const {  return loc(); }

    private:
        const Loc& prev_;
        Pos pos_;
    };

    /// Factory method to build a Parser::Tracker.
    Tracker tracker() { return {prev_, ahead().loc().begin}; }
    ///@}

    Tok ahead(size_t i = 0) const { return ahead_[i]; }

    /// Invoke Lexer to retrieve next Token.
    Tok lex() {
        auto result = ahead();
        prev_       = result.loc();
        ahead_.put(self().lexer().lex());
        return result;
    }

    /// If Parser::ahead() is a @p tag, Parser::lex(), and return `true`.
    std::optional<Tok> accept(Tag tag) {
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

    /// Consume Parser::ahead which must be a @p tag; asserts otherwise.
    Tok eat([[maybe_unused]] Tag tag) {
        assert(tag == ahead().tag() && "internal parser error");
        return lex();
    }

    Ring<Tok, K> ahead_;
    Loc prev_;
};

} // namespace fe
