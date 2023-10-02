#include <sstream>

#include <doctest/doctest.h>

#include <fe/lexer.h>
#include <fe/parser.h>

using fe::Loc;
using fe::Pos;
using fe::Sym;

std::ostream& operator<<(std::ostream& os, const Pos pos) {
    if (pos.row) {
        if (pos.col) return os << pos.row << ':' << pos.col;
        return os << pos.row;
    }
    return os << "<unknown position>";
}

std::ostream& operator<<(std::ostream& os, const Loc loc) {
    if (loc) {
        os << (loc.path ? *loc.path : "<unknown file>") << ':' << loc.begin;
        if (loc.begin != loc.finis) {
            if (loc.begin.row != loc.finis.row)
                os << '-' << loc.finis;
            else
                os << '-' << loc.finis.col;
        }
        return os;
    }
    return os << "<unknown location>";
}

#define LET_KEY(m)        \
    m(K_let,    "let")    \
    m(K_return, "return")

#define LET_MISC(m)            \
    m(M_id,  "<identifier>")   \
    m(M_lit, "<literal>")

#define LET_TOK(m)      \
    m(D_paren_l,   "(") \
    m(D_paren_r,   ")") \
    m(T_semicolon, ";") \
    m(T_lambda,    "λ") \
    m(T_EoF,       "<end of file>")

#define LET_OP(m)             \
    m(O_add, "+", Add, true)  \
    m(O_sub, "-", Add, true)  \
    m(O_mul, "*", Mul, true)  \
    m(O_div, "/", Mul, true)  \
    m(O_ass, "=", ASS, false)

class Tok {
public:
    enum Tag {
#define CODE(t, str) t,
        LET_KEY(CODE)
        LET_MISC(CODE)
        LET_TOK(CODE)
#undef CODE
#define CODE(t, str, prec, left_assoc) t,
        LET_OP(CODE)
#undef CODE
    };

    enum Prec {
        Err, Bot, Ass, Add, Mul
    };

    Tok(Loc loc, Tag tag)
        : loc_(loc)
        , tag_(tag)
    {}
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
        if (tag_ == M_id)  return sym_;
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

struct Driver : public fe::SymPool {
public:
    /// @name diagnostics
    ///@{
    template<class... Args>
    std::ostream& note(Loc loc, const char* fmt, Args&&... args) {
        std::cerr << loc << ": note: ";
        return errf(fmt, std::forward<Args&&>(args)...) << std::endl;
    }

    template<class... Args>
    std::ostream& warn(Loc loc, const char* fmt, Args&&... args) {
        ++num_errors_;
        std::cerr << loc << ": warning: ";
        return errf(fmt, std::forward<Args&&>(args)...) << std::endl;
    }

    template<class... Args>
    std::ostream& err(Loc loc, const char* fmt, Args&&... args) {
        ++num_warnings_;
        std::cerr << loc << ": error: ";
        return errf(fmt, std::forward<Args&&>(args)...) << std::endl;
    }

    unsigned num_errors() const { return num_errors_; }
    unsigned num_warnings() const { return num_warnings_; }
    ///@}

private:
    unsigned num_errors_   = 0;
    unsigned num_warnings_ = 0;
};

template<size_t K = 1>
class Lexer : public fe::Lexer<K> {
public:
    using fe::Lexer<K>::ahead;
    using fe::Lexer<K>::accept;
    using fe::Lexer<K>::accept_if;
    using fe::Lexer<K>::next;

    using fe::Lexer<K>::loc_;
    using fe::Lexer<K>::peek_;
    using fe::Lexer<K>::str_;

    Lexer(Driver& driver, std::istream& istream, const std::filesystem::path* path = nullptr)
        : fe::Lexer<K>(istream, path)
        , driver_(driver)
    {}

    Tok lex() {
        while (true) {
            loc_.begin = peek_;
            str_.clear();

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

            std::cerr << "invalid input character: '" << (char)ahead(0) << '\'' << std::endl;
            next();
        }
    }

private:
    Driver& driver_;
};

class Parser : public fe::Parser<Tok, Tok::Tag, 1, Parser> {
};

template<size_t K>
void test_lexer() {
    Driver driver;
    std::istringstream is(" test  abc    def if  \nwhile λ foo   ");
    Lexer lexer(driver, is);

    auto t1 = lexer.lex();
    auto t2 = lexer.lex();
    auto t3 = lexer.lex();
    auto t4 = lexer.lex();
    auto t5 = lexer.lex();
    auto t6 = lexer.lex();
    auto t7 = lexer.lex();
    auto t8 = lexer.lex();
    auto t9 = lexer.lex();
    std::ostringstream os;
    os << t1 << t2 << t3 << t4 << t5 << t6 << t7 << t8 << t9;
    CHECK(os.str() == "testabcdefifwhileλfoo<end of file><end of file>");

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
