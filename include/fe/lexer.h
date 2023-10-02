#pragma once

#include <istream>
#include <filesystem>

#include "fe/loc.h"
#include "fe/ring.h"
#include "fe/utf8.h"

namespace fe {

template<size_t K>
class Lexer {
public:
    Lexer(std::istream& istream, const std::filesystem::path* path = nullptr)
        : istream_(istream)
        , loc_(path, {0, 0})
        , peek_(1, 1) {
        for (size_t i = 0; i != K; ++i) ahead_[i] = utf8::decode(istream_);
        accept(utf8::BOM); // eat UTF-8 BOM, if present
    }

    Loc loc() const { return loc_; }

protected:
    char32_t ahead(size_t i = 0) const { return ahead_[i]; }

    /// Get next `char32_t` in Lexer::istream_ and increase Lexer::loc_.
    /// @returns `0` on an invalid UTF-8 sequence.
    char32_t next() {
        loc_.finis = peek_;
        auto res   = ahead();
        auto curr  = ahead_.put(utf8::decode(istream_));

        if (curr == '\n') {
            ++peek_.row, peek_.col = 0;
        } else if (curr == utf8::EoF) {
            /* do nothing */
        } else {
            ++peek_.col;
        }

        return res;
    }

    /// What should happend to the accept%ed char?
    enum class Append {
        Off,   ///< Do not append accepted char to str_.
        On,    ///< Append accepted char as is to str_.
        Lower, ///< Append accepted char via `std::tolower` to str_.
        Upper, ///< Append accepted char via `std::toupper` to str_.
    };

    /// @returns `true` if @p pred holds.
    /// In this case invoke Lexer::next() and append to Lexer::str_, if @p append.
    template<Append append = Append::On, class Pred>
    bool accept_if(Pred pred) {
        if (pred(ahead())) {
            auto c = next();
            if constexpr (append != Append::Off) {
                if constexpr (append == Append::Lower) c = std::tolower(c);
                if constexpr (append == Append::Upper) c = std::toupper(c);
                str_ += c;
            }
            return true;
        }
        return false;
    }
    template<Append append = Append::On>
    bool accept(char32_t c) { return accept_if<append>([c](char32_t d) { return c == d; }); }

    std::istream& istream_;
    Ring<char32_t, K> ahead_;
    Loc loc_;  ///< Loc%ation of the token we are currently constructing within Lexer::str_,
    Pos peek_; ///< Pos%ition of ahead_::first;
    std::string str_;
};

} // namespace fe
