#include <sstream>

#include <doctest/doctest.h>
#include <fe/driver.h>
#include <fe/error.h>
#include <fe/lexer.h>
#include <fe/parser.h>

using fe::Loc;
using fe::Pos;
using fe::Sym;

namespace utf8 = fe::utf8;

// clang-format off
#define LET_KEY(m)          \
    m(K_let, "let")         \
    m(K_return, "return")

#define LET_MISC(m)         \
    m(M_id, "<identifier>") \
    m(M_lit, "<literal>")

#define LET_TOK(m)          \
    m(D_paren_l, "(")       \
    m(D_paren_r, ")")       \
    m(D_quote_l, "«")       \
    m(D_quote_r, "»")       \
    m(T_semicolon, ";")     \
    m(T_lambda, "λ")        \
    m(EoF, "<end of file>")

#define LET_OP(m)            \
    m(O_add, "+", Add, true) \
    m(O_sub, "-", Add, true) \
    m(O_mul, "*", Mul, true) \
    m(O_div, "/", Mul, true) \
    m(O_ass, "=", ASS, false)
// clang-format on

class Tok {
public:
    enum Tag {
        Nil,
#define CODE(t, str) t,
        LET_KEY(CODE) LET_MISC(CODE) LET_TOK(CODE)
#undef CODE
#define CODE(t, str, prec, left_assoc) t,
            LET_OP(CODE)
#undef CODE
    };

    enum Prec { Err, Bot, Ass, Add, Mul };

    Tok() {}
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
    explicit operator bool() const { return tag_ != Tag::Nil; }

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
        if (tag_ == M_id) return sym_.str();
        if (tag_ == M_lit) return std::to_string(u64_);
        return tag2str(tag_);
    }

    friend std::ostream& operator<<(std::ostream& os, Tok tok) { return os << tok.to_string(); }

private:
    Loc loc_;
    Tag tag_ = Tag::Nil;
    union {
        Sym sym_;
        uint64_t u64_;
    };
};

template<>
struct std::formatter<Tok> : fe::ostream_formatter {};

template<size_t K = 1>
class Lexer : public fe::Lexer<K, Lexer<K>> {
public:
    using fe::Lexer<K, Lexer<K>>::ahead;
    using fe::Lexer<K, Lexer<K>>::accept;
    using fe::Lexer<K, Lexer<K>>::next;

    using fe::Lexer<K, Lexer<K>>::loc_;
    using fe::Lexer<K, Lexer<K>>::peek;
    using fe::Lexer<K, Lexer<K>>::str_;

    Lexer(fe::Driver& driver, fe::Error& err, std::string_view buf)
        : fe::Lexer<K, Lexer<K>>(buf)
        , driver_(driver)
        , err_(err) {}
    Lexer(fe::Driver& driver, fe::Error& err, const fe::Src& src)
        : fe::Lexer<K, Lexer<K>>(src)
        , driver_(driver)
        , err_(err) {}

    Tok lex() {
        while (true) {
            this->start();

            if (accept(utf8::Invalid)) {
                std::cerr << "invalid UTF-8 sequence" << std::endl;
                continue;
            }

            if (accept(utf8::EoF)) return {loc_, Tok::Tag::EoF};
            if (accept(utf8::isspace)) continue;

            if (accept('(')) return {loc_, Tok::Tag::D_paren_l};
            if (accept(')')) return {loc_, Tok::Tag::D_paren_r};
            if (accept(U'«')) return {loc_, Tok::Tag::D_quote_l};
            if (accept(U'»')) return {loc_, Tok::Tag::D_quote_r};

            if (accept('+')) return {loc_, Tok::Tag::O_add};
            if (accept('-')) return {loc_, Tok::Tag::O_sub};
            if (accept('*')) return {loc_, Tok::Tag::O_mul};
            if (accept('/')) return {loc_, Tok::Tag::O_div};
            if (accept('=')) return {loc_, Tok::Tag::O_ass};

            if (accept(';')) return {loc_, Tok::Tag::T_semicolon};

            if (accept(U'λ')) return {loc_, Tok::Tag::T_lambda};

            if (accept([](char32_t c) { return c == '_' || utf8::isalpha(c); })) {
                while (accept([](char32_t c) { return c == '_' || c == '.' || utf8::isalnum(c); })) {}
                return {loc_, driver_.sym(str_)};
            }

            if (accept(utf8::isdigit)) {
                while (accept(utf8::isdigit)) {}
                auto u = strtoull(str_.c_str(), nullptr, 10);
                return {loc_, u};
            }

            err_.error(peek(), "invalid input character: ''{}'", utf8::Char32(ahead()));
            next();
        }
    }

private:
    fe::Driver& driver_;
    fe::Error& err_;
};

/// Minimal precedence-climbing expression parser, mirroring the intended `fe::Parser` usage in the
/// sister `let` project: derive via CRTP, expose `lexer()`/`error()`, drive the parse with
/// the inherited `tracker`/`ahead`/`accept`/`expect`/`eat`/`lex` helpers.
/// `parse` returns the expression as an s-expression string so precedence/associativity are easy to check.
template<size_t K = 1>
class Parser : public fe::Parser<Tok, Tok::Tag, K, Parser<K>> {
public:
    using Super = fe::Parser<Tok, Tok::Tag, K, Parser<K>>;
    using Super::accept;
    using Super::ahead;
    using Super::anchor;
    using Super::curr_;
    using Super::eat;
    using Super::expect;
    using Super::lex;
    using Super::recover;
    using Super::tracker;

    Parser(fe::Driver& driver, fe::Error& err, std::string_view buf)
        : driver_(driver)
        , err_(err)
        , lexer_(driver, err, buf) {
        this->init(); // fill lookahead; must run after lexer_ is constructed
    }
    Parser(fe::Driver& driver, fe::Error& err, const fe::Src& src)
        : driver_(driver)
        , err_(err)
        , lexer_(driver, err, src) {
        this->init(); // fill lookahead; must run after lexer_ is constructed
    }

    Lexer<K>& lexer() { return lexer_; }
    fe::Driver& driver() { return driver_; }
    fe::Error& error() { return err_; } ///< All the Parser's default diagnostics need.

    /// Parse one whole expression and return {s-expression string, its Loc}.
    std::pair<std::string, Loc> parse() {
        auto track = tracker();
        auto str   = parse_expr("expression", Tok::Bot);
        expect(Tok::Tag::EoF, "expression");
        return {str, track.loc()};
    }

private:
    // clang-format off
    static Tok::Prec bin_prec(Tok::Tag t) {
        switch (t) {
            case Tok::O_add: case Tok::O_sub: return Tok::Add;
            case Tok::O_mul: case Tok::O_div: return Tok::Mul;
            case Tok::O_ass:                  return Tok::Ass;
            default:                          return Tok::Err;
        }
    }
    // clang-format on
    static bool left_assoc(Tok::Tag t) { return t != Tok::O_ass; } // '=' is right-associative

    std::string parse_primary(std::string_view ctxt) {
        if (auto tok = accept(Tok::Tag::M_id)) return tok.to_string();
        if (auto tok = accept(Tok::Tag::M_lit)) return tok.to_string();
        if (accept(Tok::Tag::D_paren_l)) {
            auto _   = this->anchor(Tok::Tag::D_paren_r);
            auto str = parse_expr("parenthesized expression", Tok::Bot);
            expect(Tok::Tag::D_paren_r, "parenthesized expression");
            return str;
        }
        this->syntax_err("primary expression", ctxt);
        return "<error>";
    }

    std::string parse_expr(std::string_view ctxt, Tok::Prec curr_prec) {
        recover(Tok::Tag::D_paren_r, ctxt);
        auto lhs = parse_primary(ctxt);
        while (true) {
            recover(Tok::Tag::D_paren_r, ctxt);
            auto tag  = ahead().tag();
            auto prec = bin_prec(tag);
            if (prec <= curr_prec) break;
            auto op   = lex(); // consume the operator
            auto next = left_assoc(tag) ? prec : Tok::Prec(prec - 1);
            auto rhs  = parse_expr("right-hand side", next);
            lhs       = std::format("({} {} {})", op.to_string(), lhs, rhs);
        }
        return lhs;
    }

    friend Super;
    fe::Driver& driver_;
    fe::Error& err_;
    Lexer<K> lexer_;
};

template<size_t K>
void test_parser() {
    auto parse = [](std::string_view src) {
        fe::Driver drv;
        fe::Error err(drv);
        Parser<K> parser(drv, err, src);
        auto [str, loc] = parser.parse();
        return std::tuple{str, loc, err.num_errors()};
    };

    // precedence
    CHECK(std::get<0>(parse("a + b * c")) == "(+ a (* b c))");
    CHECK(std::get<0>(parse("a * b + c")) == "(+ (* a b) c)");
    // left associativity of +,-,*,/
    CHECK(std::get<0>(parse("a - b - c")) == "(- (- a b) c)");
    CHECK(std::get<0>(parse("a / b / c")) == "(/ (/ a b) c)");
    // right associativity of '='
    CHECK(std::get<0>(parse("a = b = c")) == "(= a (= b c))");
    // parentheses override precedence
    CHECK(std::get<0>(parse("(a + b) * c")) == "(* (+ a b) c)");
    // literals
    CHECK(std::get<0>(parse("1 + 2 * 3")) == "(+ 1 (* 2 3))");

    // no errors on the well-formed inputs above
    CHECK(std::get<2>(parse("a + b * c")) == 0);

    // Tracker spans from the first to the last *consumed* token - parse() also consumes EoF,
    // which is the empty Loc at the end of the buffer.
    {
        auto [str, loc, errs] = parse("a + b");
        CHECK(str == "(+ a b)");
        CHECK(errs == 0);
        CHECK(loc == Loc(Pos(0), Pos(5)));
    }

    // expect() reports a syntax error on a missing ')'
    CHECK(std::get<2>(parse("(a + b")) == 1);
    // parse_primary reports an error when no primary is found
    CHECK(std::get<2>(parse("a +")) == 1);

    // an unanchored ')' is skipped and reported - the parser carries on instead of bailing out
    {
        auto [str, loc, errs] = parse("a + b) + c");
        CHECK(str == "(+ (+ a b) c)");
        CHECK(errs == 1);
    }
    {
        auto [str, loc, errs] = parse("(a + b)) * c");
        CHECK(str == "(* (+ a b) c)");
        CHECK(errs == 1);
    }
    // ... whereas an anchored ')' still terminates the parenthesized expression
    CHECK(std::get<2>(parse("(a + b) * c")) == 0);
}

TEST_CASE("Parser") {
    test_parser<1>();
    test_parser<2>();
    test_parser<3>();
}

template<size_t K>
void test_lexer() {
    fe::Driver drv;
    fe::Error err(drv);
    Lexer<K> lexer(drv, err, " test  abc    def if  \nwhile λ foo «n; X»  ");

    auto t1 = lexer.lex();
    auto t2 = lexer.lex();
    auto t3 = lexer.lex();
    auto t4 = lexer.lex();
    auto t5 = lexer.lex();
    auto t6 = lexer.lex();
    auto t7 = lexer.lex();
    auto t8 = lexer.lex();
    auto t9 = lexer.lex();
    auto t0 = lexer.lex();
    auto ta = lexer.lex();
    auto tb = lexer.lex();
    auto tc = lexer.lex();
    auto td = lexer.lex();
    auto s  = std::format("{} {} {} {} {} {} {} {} {} {} {} {} {} {}", t1, t2, t3, t4, t5, t6, t7, t8, t9, t0, ta, tb,
                          tc, td);
    CHECK(s == "test abc def if while λ foo « n ; X » <end of file> <end of file>");

    // A Loc is a half-open byte range, so a multi-byte code point spans more than one offset.
    // clang-format off
    CHECK(t1.loc() == Loc(Pos( 1), Pos( 5))); // test
    CHECK(t2.loc() == Loc(Pos( 7), Pos(10))); // abc
    CHECK(t3.loc() == Loc(Pos(14), Pos(17))); // def
    CHECK(t4.loc() == Loc(Pos(18), Pos(20))); // if
    CHECK(t5.loc() == Loc(Pos(23), Pos(28))); // while
    CHECK(t6.loc() == Loc(Pos(29), Pos(31))); // λ - 2 bytes
    CHECK(t7.loc() == Loc(Pos(32), Pos(35))); // foo
    CHECK(t8.loc() == Loc(Pos(36), Pos(38))); // « - 2 bytes
    CHECK(t9.loc() == Loc(Pos(38), Pos(39))); // n
    CHECK(t0.loc() == Loc(Pos(39), Pos(40))); // ;
    CHECK(ta.loc() == Loc(Pos(41), Pos(42))); // X
    CHECK(tb.loc() == Loc(Pos(42), Pos(44))); // » - 2 bytes
    CHECK(tc.loc() == Loc(Pos(46), Pos(46))); // <end of file> - empty, at the end of the buffer
    CHECK(td.loc() == Loc(Pos(46), Pos(46))); // <end of file>
    // clang-format on
}

TEST_CASE("Lexer") {
    test_lexer<1>();
    test_lexer<2>();
    test_lexer<3>();
}

template<size_t K>
void test_bom() {
    SUBCASE("UTF-8 BOM is skipped") {
        fe::Driver drv;
        fe::Error err(drv);
        Lexer<K> lexer(drv, err, "\xEF\xBB\xBFtest");

        auto tok = lexer.lex();
        CHECK(tok.to_string() == "test");
        CHECK(tok.loc() == Loc(Pos(3), Pos(7))); // the BOM still occupies its 3 bytes
        CHECK(lexer.lex().tag() == Tok::Tag::EoF);
    }

    SUBCASE("buffer starting with a newline") {
        fe::Driver drv;
        fe::Error err(drv);
        Lexer<K> lexer(drv, err, "\nwhile");

        auto tok = lexer.lex();
        CHECK(tok.to_string() == "while");
        CHECK(tok.loc() == Loc(Pos(1), Pos(6)));
    }

    SUBCASE("empty buffer") {
        fe::Driver drv;
        fe::Error err(drv);
        Lexer<K> lexer(drv, err, "");

        auto tok = lexer.lex();
        CHECK(tok.tag() == Tok::Tag::EoF);
        CHECK(tok.loc() == Loc(Pos(0), Pos(0)));
    }
}

TEST_CASE("Lexer buffer starts") {
    test_bom<1>();
    test_bom<2>();
    test_bom<3>();
}

/// Minimal lexer to exercise the Lexer::Append policies of fe::Lexer::accept.
class FoldLexer : public fe::Lexer<1, FoldLexer> {
public:
    using Super  = fe::Lexer<1, FoldLexer>;
    using Append = Super::Append;
    using Super::accept;
    using Super::str_;

    FoldLexer(std::string_view buf)
        : Super(buf) {}

    template<Append append>
    std::string lex_word() {
        while (accept<Append::Off>(utf8::isspace)) {}
        this->start();
        while (accept<append>(utf8::isalpha)) {}
        return str_;
    }
};

TEST_CASE("utf8::num_code_points") {
    CHECK(utf8::num_code_points("") == 0);
    CHECK(utf8::num_code_points("abc") == 3);
    CHECK(utf8::num_code_points("λ") == 1);      // 2 bytes
    CHECK(utf8::num_code_points("→") == 1);      // 3 bytes
    CHECK(utf8::num_code_points("𝔽") == 1);      // 4 bytes
    CHECK(utf8::num_code_points("«3; x»") == 6); // two 2-byte code points plus four ASCII
    // an invalid lead byte counts as one and still advances
    CHECK(utf8::num_code_points("\xff"
                                "a")
          == 2);
    // a str truncated mid-sequence counts its final partial code point instead of running off the end
    CHECK(utf8::num_code_points(std::string_view("λ").substr(0, 1)) == 1);
    CHECK(utf8::num_code_points(std::string_view("aλ").substr(0, 2)) == 2);
}

TEST_CASE("Lexer Append policies") {
    FoldLexer lexer("MiXeD MiXeD MiXeD MiXeD");

    CHECK(lexer.lex_word<FoldLexer::Append::On>() == "MiXeD");
    CHECK(lexer.lex_word<FoldLexer::Append::Lower>() == "mixed");
    CHECK(lexer.lex_word<FoldLexer::Append::Upper>() == "MIXED");
    CHECK(lexer.lex_word<FoldLexer::Append::Off>() == "");
}
