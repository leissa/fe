#include <exception>
#include <iostream>
#include <fstream>
#include <filesystem>

#include <fe/sym.h>
#include <fe/lexer.h>

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

#define LET_MISC(m)             \
    m(M_id,  "<identifier>")    \
    m(M_lit, "<literal>")

#define LET_OP(m)             \
    m(O_add, "+", Add, true)  \
    m(O_add, "-", Add, true)  \
    m(O_add, "*", Mul, true)  \
    m(O_add, "/", Mul, true)  \
    m(O_ass, "=", Mul, false)

class Tok {
public:
    enum Tag {
#define CODE(t, str) t,
        LET_KEY(CODE)
        LET_MISC(CODE)
#undef CODE
    };

    Tok(Loc loc, Tag tag)
        : loc_(loc)
        , tag_(tag) {}
    Tok(Loc loc, Sym sym)
        : loc_(loc)
        , tag_(Tag::M_id) {
        sym_ = sym;
    }
    Tok(Loc loc, uint32_t u)
        : loc_(loc)
        , tag_(Tag::M_id) {
        u_ = u;
    }

    Tag tag() const { return tag_; }
    Loc loc() const { return loc_; }

private:
    Loc loc_;
    Tag tag_;
    union {
        Sym sym_;
        uint32_t u_;
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

            if (accept_if([](char32_t c) { return c == '_' || isalpha(c); })) {
                while (accept_if([](char32_t c) { return c == '_' || c == '.' || isalnum(c); })) {}
                return {loc_, driver_.sym(str_)};
            }

            std::cerr << "invalid input character" << std::endl;
            next();
        }
    }

private:
    Driver& driver_;
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
        fe::Arena<8, 128> arena;
        fe::SymPool syms;
        auto hello = syms.sym("hello world");

        std::vector<int, fe::Arena<8, 128>::Allocator<int>> v(arena);
        for (int i = 0, e = 10000; i != e; ++i)
            v.emplace_back(i);
    } catch (const std::exception& e) {
        std::cerr << argv[0] << ": " << e.what() << std::endl;
        return EXIT_FAILURE;
    } catch (...) {
        std::cerr << argv[0] << ": unknown exception" << std::endl;
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}
