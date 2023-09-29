#include <exception>
#include <iostream>
#include <fstream>
#include <filesystem>
#include <vector>

#include <fe/assert.h>

#include <fe/sym.h>
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
    m(T_semicolon, ";")

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
        , tag_(tag) {}
    Tok(Loc loc, Sym sym)
        : loc_(loc)
        , tag_(Tag::M_id) {
        sym_ = sym;
    }
    Tok(Loc loc, uint64_t u64)
        : loc_(loc)
        , tag_(Tag::M_lit) {
        u64_ = u64;
    }

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

class Lexer : public fe::Lexer {
public:
    Lexer(Driver& driver, std::istream& istream, const std::filesystem::path* path)
        : fe::Lexer(istream, path)
        , driver_(driver)
    {}

    Tok lex() {
        while (true) {
            loc_.begin = peek_.pos;
            str_.clear();

            if (accept(0)) {
                std::cerr << "invalid UTF-8 sequence" << std::endl;
                continue;
            }

            if (accept_if(isspace)) continue;

            if (accept('(')) return {loc_, Tok::Tag::D_paren_l};
            if (accept('(')) return {loc_, Tok::Tag::D_paren_r};

            if (accept('+')) return {loc_, Tok::Tag::O_add};
            if (accept('-')) return {loc_, Tok::Tag::O_sub};
            if (accept('*')) return {loc_, Tok::Tag::O_mul};
            if (accept('/')) return {loc_, Tok::Tag::O_div};
            if (accept('=')) return {loc_, Tok::Tag::O_ass};

            if (accept(';')) return {loc_, Tok::Tag::T_semicolon};

            if (accept_if([](char32_t c) { return c == '_' || isalpha(c); })) {
                while (accept_if([](char32_t c) { return c == '_' || c == '.' || isalnum(c); })) {}
                return {loc_, driver_.sym(str_)};
            }

            if (accept_if(isdigit)) {
                while (accept_if(isdigit)) {}
                auto u = strtoull(str_.c_str(), nullptr, 10);
                return {loc_, u};
            }

            std::cerr << "invalid input character" << std::endl;
            next();
        }
    }

private:
    Driver& driver_;
};

class Parser : public fe::Parser<Tok, Tok::Tag, 1, Parser> {
};

int main(int argc, char** argv) {
    try {
        if (argc == 1) {
            std::cerr << argv[0] << ": " << "no input file" << std::endl;
            return EXIT_FAILURE;
        } else if (argc >= 3) {
            std::cerr << argv[0] << ": " << "only specify one input file" << std::endl;
            return EXIT_FAILURE;
        }

        Driver driver;
        std::filesystem::path file(argv[1]);
        std::ifstream ifs(file);
        Lexer lexer(driver, ifs, &file);
        //fe::Arena<8, 128> arena;
        fe::SymPool syms;
        auto hello = syms.sym("hello world");

        //std::vector<int, fe::Arena<8, 128>::Allocator<int>> v(arena);
        //for (int i = 0, e = 10000; i != e; ++i) v.emplace_back(i);

        for (int i = 0; i < 10; ++i) {
            auto tok = lexer.lex();
            std::cout << tok.loc() << ": " << tok.to_string() << std::endl;
        }
    } catch (const std::exception& e) {
        std::cerr << argv[0] << ": " << e.what() << std::endl;
        return EXIT_FAILURE;
    } catch (...) {
        std::cerr << argv[0] << ": unknown exception" << std::endl;
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}
