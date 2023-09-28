#pragma once

#include <istream>
#include <filesystem>

#include "fe/loc.h"
#include "fe/utf8.h"

namespace fe {

class Lexer {
public:
    Lexer(std::istream& istream, const std::filesystem::path* path)
        : istream_(istream)
        , loc_(path, {0, 0}) {
        next(); //
        accept(utf8::BOM); // eat utf-8 BOM if present
    }

    Loc loc() const { return loc_; }

protected:
    /// Get next `char32_t` in Lexer::istream_ and increase Lexer::loc_ / Lexer::k_pos_.
    /// @returns `0` on an invalid UTF-8 sequence.
    char32_t next() {
        auto [pos, c32] = peek_;
        peek_.pos       = pos;
        peek_.c32       = utf8::encode(istream_);

        if (peek_.c32 == '\n') {
            ++peek_.pos.row;
            peek_.pos.col = 0;
        } else if (peek_.c32 == utf8::EoF) {
            /* do nothing */
        } else {
            ++peek_.pos.col;
        }

        loc_.finis = pos;
        return c32;
    }

    /// @return `true` if @p pred holds.
    /// In this case invoke Lexer::next() and append to Lexer::str_.
    template<class Pred, bool lower = false>
    bool accept_if(Pred pred) {
        return pred(peek_.c32) ? (str_ += lower ? std::tolower(next()) : next(), true) : false;
    }
    bool accept(char32_t c32) { return peek_.c32 == c32 ? (str_ += next(), true) : false; }

    std::istream& istream_;
    Loc loc_;      ///< Loc%ation of the token we are currently constructing within Lexer::str_,
    struct {
        Pos pos = {1, 1};
        char32_t c32;
    } peek_;
    std::string str_;
};

} // namespace fe
