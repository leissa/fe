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
        for (size_t i = 0; i != K; ++i) ahead_[0] = utf8::decode(istream_);
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

    /// @return `true` if @p pred holds.
    /// In this case invoke Lexer::next() and append to Lexer::str_.
    template<class Pred, bool lower = false>
    bool accept_if(Pred pred) {
        return pred(ahead()) ? (str_ += lower ? std::tolower(next()) : next(), true) : false;
    }
    bool accept(char32_t c32) { return ahead() == c32 ? (str_ += next(), true) : false; }

    std::istream& istream_;
    Ring<char32_t, K> ahead_;
    Loc loc_;  ///< Loc%ation of the token we are currently constructing within Lexer::str_,
    Pos peek_; ///< Pos%ition of ahead_::first;
    std::string str_;
};

} // namespace fe
