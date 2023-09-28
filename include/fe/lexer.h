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
        accept(utf8::BOM); // eat utf-8 BOM if present
    }

    Loc loc() const { return loc_; }

protected:
    char32_t peek() const { return peek_.c; }
    /// Get next `char32_t` in Lexer::istream_ and increase Lexer::loc_ / Lexer::k_pos_.
    char32_t next() { /* TODO */ return 0; }

    /// @return `true` if @p pred holds.
    /// In this case invoke Lexer::next() and append to Lexer::str_.
    template<class Pred, bool lower = false>
    bool accept_if(Pred pred) {
        return pred(peek()) ? (str_ += lower ? std::tolower(next()) : next(), true) : false;
    }

    bool accept(char32_t val) {
        return accept_if([val](char32_t p) { return p == val; });
    }

    std::istream& istream_;
    Loc loc_;      ///< Loc%ation of the Tok%en we are currently constructing within Lexer::str_,
    struct {
        Pos pos = {1, 1};
        char32_t c;
    } peek_;
    std::string str_;
};

} // namespace fe
