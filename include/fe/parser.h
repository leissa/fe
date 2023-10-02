#pragma once

#include <optional>

#include "fe/loc.h"
#include "fe/ring.h"

namespace fe {

/// The blueprint for a [recursive descent](https://en.wikipedia.org/wiki/Recursive_descent_parser)/
/// [ascent parser](https://en.wikipedia.org/wiki/Recursive_ascent_parser) using a @p K lookahead of @p Tok%ens.
template<class Tok, class Tag, size_t K, class S>
class Parser {
private:
    S& self() { return *static_cast<S*>(this); }
    const S& self() const { return *static_cast<const S*>(this); }

protected:
    Parser() {
        //for (size_t i = 0; i != K; ++i) ahead_[i] = self().lexer().lex();
    }

    class Tracker {
    public:
        Tracker(Parser& parser, const Pos& pos)
            : parser_(parser)
            , pos_(pos) {}

        Loc loc() const { return {parser_.prev_.path, pos_, parser_.prev_.finis}; }

    private:
        Parser& parser_;
        Pos pos_;
    };

    /// Factory method to build a Parser::Tracker.
    Tracker tracker() { return Tracker(*this, ahead().loc().begin); }

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
