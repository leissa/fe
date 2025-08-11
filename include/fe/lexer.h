#pragma once

#include <filesystem>
#include <istream>

#include "fe/loc.h"
#include "fe/ring.h"
#include "fe/utf8.h"

namespace fe {

/// The blueprint for a lexer with a buffer of @p K tokens to peek into the future (Lexer::ahead).
/// You can "overide" Lexer::next via CRTP (@p S is the child).
template<size_t K, class S>
class Lexer {
private:
    S& self() { return *static_cast<S*>(this); }
    const S& self() const { return *static_cast<const S*>(this); }

public:
    Lexer(std::istream& istream, const std::filesystem::path* path = nullptr)
        : istream_(istream)
        , loc_(path, {0, 0})
        , peek_(1, 1) {
        for (size_t i = 0; i != K; ++i)
            ahead_[i] = utf8::decode(istream_);
        accept(utf8::BOM); // eat UTF-8 BOM, if present
        assert(peek_.col == 1);
    }

protected:
    char32_t ahead(size_t i = 0) const { return ahead_[i]; }

    /// Invoke before assembling the next token.
    void start() {
        loc_.begin = peek_;
        str_.clear();
    }

    /// Get next `char32_t` in Lexer::istream_ and increase Lexer::loc_.
    /// @returns Null on an invalid UTF-8 sequence.
    char32_t next() {
        loc_.finis = peek_;
        auto res   = ahead_.put(utf8::decode(istream_));
        auto c     = ahead_.front(); // char of the peek location

        if (c == '\n') {
            ++peek_.row;
            peek_.col = 0;
        } else if (c == utf8::EoF || c == utf8::BOM) {
            /* do nothing */
        } else {
            ++peek_.col;
        }

        return res;
    }

    /// @name Accept
    /// Accept next character in Lexer::istream_, depending on some condition.
    ///@{
    /// What should happend to the accepted char?
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

    std::istream& istream_;
    Ring<char32_t, K> ahead_;
    Loc loc_;  ///< Loc%ation of the token we are currently constructing within Lexer::str_,
    Pos peek_; ///< Pos%ition of ahead_::first;
    std::string str_;
};

} // namespace fe
