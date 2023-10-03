#include <sstream>

#include <doctest/doctest.h>
#include <fe/driver.h>
#include <fe/lexer.h>
#include <fe/loc.cpp.h>
#include <fe/parser.h>

using fe::Loc;
using fe::Pos;
using fe::Sym;

#define LET_KEY(m) m(K_let, "let") m(K_return, "return")

#define LET_MISC(m) m(M_id, "<identifier>") m(M_lit, "<literal>")

#define LET_TOK(m) m(D_paren_l, "(") m(D_paren_r, ")") m(T_semicolon, ";") m(T_lambda, "λ") m(T_EoF, "<end of file>")

#define LET_OP(m)                                                                                       \
    m(O_add, "+", Add, true) m(O_sub, "-", Add, true) m(O_mul, "*", Mul, true) m(O_div, "/", Mul, true) \
        m(O_ass, "=", ASS, false)

class Tok {
public:
    enum Tag {
#define CODE(t, str) t,
        LET_KEY(CODE) LET_MISC(CODE) LET_TOK(CODE)
#undef CODE
#define CODE(t, str, prec, left_assoc) t,
            LET_OP(CODE)
#undef CODE
    };

    enum Prec { Err, Bot, Ass, Add, Mul };

    Tok() = default;
    Tok(Loc loc, Tag tag)
        : loc_(loc)
        , tag_(tag) {}
    Tok(Loc loc, Sym sym)
        : loc_(loc)
        , tag_(Tag::M_id)
        , sym_(sym) {}
    Tok(Loc loc, uint64_t u64)
        : loc_(loc)
        , tag_(Tag::M_lit)
        , u64_(u64) {}

    Tag tag() const { return tag_; }
    Loc loc() const { return loc_; }

    static const char* tag2str(Tag tag) {
        switch (tag) {
#define CODE(t, str) \
    case Tok::Tag::t: return str;
            LET_KEY(CODE)
            LET_TOK(CODE)
            LET_MISC(CODE)
#undef CODE
#define CODE(t, str, prec, left_assoc) \
    case Tok::Tag::t: return str;
            LET_OP(CODE)
#undef CODE
            default: fe::unreachable();
        }
    }

    std::string to_string() const {
        if (tag_ == M_id) return sym_;
        if (tag_ == M_lit) return std::to_string(u64_);
        return tag2str(tag_);
    }

    friend std::ostream& operator<<(std::ostream& os, Tok tok) { return os << tok.to_string(); }

private:
    Loc loc_;
    Tag tag_;
    union {
        Sym sym_;
        uint64_t u64_;
    };
};

template<> struct std::formatter<Tok> : fe::ostream_formatter {};

template<size_t K = 1> class Lexer : public fe::Lexer<K, Lexer<K>> {
public:
    using fe::Lexer<K, Lexer<K>>::ahead;
    using fe::Lexer<K, Lexer<K>>::accept;
    using fe::Lexer<K, Lexer<K>>::accept_if;
    using fe::Lexer<K, Lexer<K>>::next;

    using fe::Lexer<K, Lexer<K>>::loc_;
    using fe::Lexer<K, Lexer<K>>::peek_;
    using fe::Lexer<K, Lexer<K>>::str_;

    Lexer(fe::Driver& driver, std::istream& istream, const std::filesystem::path* path = nullptr)
        : fe::Lexer<K, Lexer<K>>(istream, path)
        , driver_(driver) {}

    Tok lex() {
        while (true) {
            this->begin();

            if (accept(0)) {
                std::cerr << "invalid UTF-8 sequence" << std::endl;
                continue;
            }

            if (accept(fe::utf8::EoF)) return {loc_, Tok::Tag::T_EoF};
            if (accept_if(isspace)) continue;

            if (accept('(')) return {loc_, Tok::Tag::D_paren_l};
            if (accept('(')) return {loc_, Tok::Tag::D_paren_r};

            if (accept('+')) return {loc_, Tok::Tag::O_add};
            if (accept('-')) return {loc_, Tok::Tag::O_sub};
            if (accept('*')) return {loc_, Tok::Tag::O_mul};
            if (accept('/')) return {loc_, Tok::Tag::O_div};
            if (accept('=')) return {loc_, Tok::Tag::O_ass};

            if (accept(';')) return {loc_, Tok::Tag::T_semicolon};

            if (accept(U'λ')) return {loc_, Tok::Tag::T_lambda};

            if (accept_if([](char32_t c) { return c == '_' || isalpha(c); })) {
                while (accept_if([](char32_t c) { return c == '_' || c == '.' || isalnum(c); })) {}
                return {loc_, driver_.sym(str_)};
            }

            if (accept_if(isdigit)) {
                while (accept_if(isdigit)) {}
                auto u = strtoull(str_.c_str(), nullptr, 10);
                return {loc_, u};
            }

            driver_.err(peek_, "invalid input character: ''{}'", (char)ahead(0));
            next();
        }
    }

private:
    fe::Driver& driver_;
};

class Parser : public fe::Parser<Tok, Tok::Tag, 1, Parser> {};

template<size_t K> void test_lexer() {
    fe::Driver drv;
    std::istringstream is(" test  abc    def if  \nwhile λ foo   ");
    Lexer lexer(drv, is);

    auto t1 = lexer.lex();
    auto t2 = lexer.lex();
    auto t3 = lexer.lex();
    auto t4 = lexer.lex();
    auto t5 = lexer.lex();
    auto t6 = lexer.lex();
    auto t7 = lexer.lex();
    auto t8 = lexer.lex();
    auto t9 = lexer.lex();
    auto s  = std::format("{} {} {} {} {} {} {} {} {}", t1, t2, t3, t4, t5, t6, t7, t8, t9);
    CHECK(s == "test abc def if while λ foo <end of file> <end of file>");

    // clang-format off
    CHECK(t1.loc() == Loc({1,  2}, {1,  5}));
    CHECK(t2.loc() == Loc({1,  8}, {1, 10}));
    CHECK(t3.loc() == Loc({1, 15}, {1, 17}));
    CHECK(t4.loc() == Loc({1, 19}, {1, 20}));
    CHECK(t5.loc() == Loc({2,  1}, {2,  5}));
    CHECK(t6.loc() == Loc({2,  7}, {2,  7}));
    CHECK(t7.loc() == Loc({2,  9}, {2, 11}));
    CHECK(t8.loc() == Loc({2, 14}, {2, 14}));
    CHECK(t9.loc() == Loc({2, 14}, {2, 14}));
    // clang-format on
}

TEST_CASE("Lexer") {
    test_lexer<1>();
    test_lexer<2>();
    test_lexer<3>();
}
