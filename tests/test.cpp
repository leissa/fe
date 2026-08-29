#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN

#include <stack>
#include <unordered_map>
#include <unordered_set>

#include <doctest/doctest.h>
#include <fe/algo.h>
#include <fe/arena.h>
#include <fe/container.h>
#include <fe/dbg.h>
#include <fe/enum.h>
#include <fe/log.h>
#include <fe/ring.h>
#include <fe/span.h>
#include <fe/sym.h>
#include <fe/term.h>
#include <fe/utf8.h>
#include <fe/vector.h>

using namespace std::literals;

TEST_CASE("Arena") {
    fe::Arena arena;
    std::vector<int, fe::Arena::Allocator<int>> v(arena.allocator<int>());
    for (int i = 0, e = 10000; i != e; ++i)
        v.emplace_back(i);

    SUBCASE("pmr containers can use the arena resource directly") {
        std::pmr::vector<int> pv(arena.resource());
        for (int i = 0, e = 1000; i != e; ++i)
            pv.emplace_back(i);
        CHECK(pv.front() == 0);
        CHECK(pv.back() == 999);
    }

    SUBCASE("allocations that only fit before alignment use a fresh page") {
        fe::Arena small(64);
        auto first = small.allocate(57, 1);
        REQUIRE(first != nullptr);
        CHECK(small.state() == fe::Arena::State{2, 57});

        auto second = small.allocate(7, 8);
        REQUIRE(second != nullptr);
        CHECK(small.state() == fe::Arena::State{3, 7});
    }

    SUBCASE("mk constructs in the arena and Ptr only destructs") {
        struct Probe {
            Probe(int& live)
                : live(live) {
                ++live;
            }
            ~Probe() { --live; }
            int& live;
        };

        int live = 0;
        {
            auto probe = arena.mk<Probe>(live);
            CHECK(live == 1);
        }
        CHECK(live == 0);
    }

    SUBCASE("Ptr<Derived> converts to Ptr<Base>") {
        struct Base {
            virtual ~Base() = default;
        };
        struct Derived : Base {
            Derived(int& live)
                : live(live) {
                ++live;
            }
            ~Derived() override { --live; }
            int& live;
        };

        int live = 0;
        {
            fe::Arena::Ptr<Base> base = arena.mk<Derived>(live);
            CHECK(live == 1);
        }
        CHECK(live == 0);
    }

    SUBCASE("zero-byte allocations yield nullptr") { CHECK(arena.allocate(0, 1) == nullptr); }

    SUBCASE("align rounds up to the requested alignment") {
        CHECK(fe::Arena::align(0, 8) == 0);
        CHECK(fe::Arena::align(1, 8) == 8);
        CHECK(fe::Arena::align(8, 8) == 8);
        CHECK(fe::Arena::align(9, 8) == 16);
        CHECK(fe::Arena::align(15, 4) == 16);
    }

    SUBCASE("moving an Arena keeps its allocations alive") {
        fe::Arena a;
        auto p = a.allocate<int>(1);
        *p     = 42;

        fe::Arena b(std::move(a));
        CHECK(*p == 42);
        CHECK(a.state() == fe::Arena::State{1, 0}); // moved-from Arena is freshly usable again
        CHECK(a.allocate<int>(1) != nullptr);
    }

    SUBCASE("restoring state drops newer pages and restores the old offset") {
        fe::Arena small(64);
        auto first = small.allocate(60, 1);
        REQUIRE(first != nullptr);
        auto checkpoint = small.state();
        CHECK(checkpoint == fe::Arena::State{2, 60});

        auto second = small.allocate(16, 1);
        REQUIRE(second != nullptr);
        CHECK(small.state() == fe::Arena::State{3, 16});

        small.deallocate(checkpoint);
        CHECK(small.state() == checkpoint);

        auto third = small.allocate(4, 4);
        REQUIRE(third != nullptr);
        CHECK(small.state() == fe::Arena::State{2, 64});
    }
}

TEST_CASE("Ring") {
    fe::Ring<int, 1> ring1;
    ring1[0] = 0;
    CHECK(ring1.front() == 0);
    auto res = ring1.put(1);
    CHECK(res == 0);
    CHECK(ring1.front() == 1);

    fe::Ring<int, 2> ring2;
    ring2[0] = 0;
    ring2[1] = 1;
    CHECK(ring2.front() == 0);
    CHECK(ring2[0] == 0);
    CHECK(ring2[1] == 1);
    res = ring2.put(2);
    CHECK(res == 0);
    CHECK(ring2.front() == 1);
    CHECK(ring2[0] == 1);
    CHECK(ring2[1] == 2);
    res = ring2.put(3);
    CHECK(res == 1);
    CHECK(ring2.front() == 2);
    CHECK(ring2[0] == 2);
    CHECK(ring2[1] == 3);

    fe::Ring<int, 3> ring3;
    ring3[0] = 0;
    ring3[1] = 1;
    ring3[2] = 2;
    CHECK(ring3.front() == 0);
    CHECK(ring3[0] == 0);
    CHECK(ring3[1] == 1);
    CHECK(ring3[2] == 2);
    res = ring3.put(3);
    CHECK(res == 0);
    CHECK(ring3.front() == 1);
    CHECK(ring3[0] == 1);
    CHECK(ring3[1] == 2);
    CHECK(ring3[2] == 3);
    res = ring3.put(4);
    CHECK(res == 1);
    CHECK(ring3.front() == 2);
    CHECK(ring3[0] == 2);
    CHECK(ring3[1] == 3);
    CHECK(ring3[2] == 4);
    res = ring3.put(5);
    CHECK(res == 2);
    CHECK(ring3.front() == 3);
    CHECK(ring3[0] == 3);
    CHECK(ring3[1] == 4);
    CHECK(ring3[2] == 5);

    SUBCASE("initializer_list construction") {
        fe::Ring<int, 1> r1{7};
        CHECK(r1.front() == 7);

        fe::Ring<int, 2> r2{1, 2};
        CHECK(r2[0] == 1);
        CHECK(r2[1] == 2);

        fe::Ring<int, 3> r3{1, 2, 3};
        CHECK(r3[0] == 1);
        CHECK(r3[1] == 2);
        CHECK(r3[2] == 3);
    }

    SUBCASE("reset rewinds to the physical start") {
        fe::Ring<int, 3> r3{1, 2, 3};
        r3.put(4); // array is now {4, 2, 3} with first_ == 1
        CHECK(r3.front() == 2);
        r3.reset();
        CHECK(r3.front() == 4);
        CHECK(r3[1] == 2);
        CHECK(r3[2] == 3);
    }

    SUBCASE("move and swap") {
        fe::Ring<int, 3> a{1, 2, 3};
        fe::Ring<int, 3> b{4, 5, 6};
        swap(a, b);
        CHECK(a[0] == 4);
        CHECK(b[0] == 1);

        auto c = std::move(a);
        CHECK(c[0] == 4);
        CHECK(c[2] == 6);
    }
}

TEST_CASE("Sym") {
    fe::SymPool syms;

    CHECK(syms.sym("a").view() == "a"s);
    CHECK(syms.sym("ab").view() == "ab"s);
    CHECK(syms.sym("abc").view() == "abc"s);
    CHECK(syms.sym("abcd").view() == "abcd"s);
    CHECK(syms.sym("abcde").view() == "abcde"s);
    CHECK(syms.sym("abcdef").view() == "abcdef"s);
    CHECK(syms.sym("abcdefg").view() == "abcdefg"s);
    CHECK(syms.sym("abcdefgh").view() == "abcdefgh"s);
    CHECK(syms.sym("abcdefghi").view() == "abcdefghi"s);
    CHECK(syms.sym("abcdefghij").view() == "abcdefghij"s);

    CHECK(syms.sym("a") == syms.sym("a"s));
    CHECK(syms.sym("ab") == syms.sym("ab"s));
    CHECK(syms.sym("abc") == syms.sym("abc"s));
    CHECK(syms.sym("abcd") == syms.sym("abcd"s));
    CHECK(syms.sym("abcde") == syms.sym("abcde"s));
    CHECK(syms.sym("abcdef") == syms.sym("abcdef"s));
    CHECK(syms.sym("abcdefg") == syms.sym("abcdefg"s));
    CHECK(syms.sym("abcdefgh") == syms.sym("abcdefgh"s));
    CHECK(syms.sym("abcdefghi") == syms.sym("abcdefghi"s));
    CHECK(syms.sym("abcdefghij") == syms.sym("abcdefghij"s));

    auto b  = syms.sym("b");
    auto bc = syms.sym("bc");
    CHECK(b == 'b');
    CHECK(b != 'a');
    CHECK(b <= 'b');
    CHECK(b >= 'b');
    CHECK(b < 'c');
    CHECK(b > 'a');
    CHECK(bc < 'c');
    CHECK(bc > 'a');
    CHECK(bc > 'b');
    auto xyz = syms.sym("xyz");
    std::string zyx;
    for (auto i = xyz.rbegin(), e = xyz.rend(); i != e; ++i)
        zyx += *i;
    CHECK(zyx == "zyx");
    CHECK(xyz.front() == 'x');
    CHECK(xyz.back() == 'z');
    CHECK(xyz.size() == 3);
    auto empty = fe::Sym();
    CHECK(empty.empty());
    CHECK(empty.size() == 0);
    CHECK(!empty);

    SUBCASE("empty symbol from empty inputs") {
        CHECK(syms.sym("") == fe::Sym());
        CHECK(syms.sym(std::string()) == fe::Sym());
        CHECK(syms.sym(std::string_view()) == fe::Sym());
        CHECK(syms.sym((const char*)nullptr) == fe::Sym());
        CHECK(syms.sym("").view() == std::string_view());
    }

    SUBCASE("long symbols are interned once") {
        auto s1 = syms.sym("this-is-a-rather-long-symbol");
        auto s2 = syms.sym("this-is-a-rather-long-symbol"s);
        CHECK(s1 == s2);
        CHECK(s1.c_str() == s2.c_str()); // same interned storage
        CHECK(s1.c_str()[s1.size()] == '\0');
        CHECK(s1.str() == "this-is-a-rather-long-symbol"s);
    }

    SUBCASE("comparison with strings and string_views") {
        auto abc = syms.sym("abc");
        CHECK(abc == "abc"sv);
        CHECK(abc == "abc"s);
        CHECK("abc"sv == abc);
        CHECK(abc < "abd"sv);
        CHECK(abc > "abb"sv);
        CHECK(syms.sym("a") < syms.sym("b"));
        CHECK(syms.sym("ab") < syms.sym("b"));
        CHECK(syms.sym("ab") < syms.sym("abc"));
    }

    SUBCASE("SymMap/SymSet lookup with interned symbols") {
        fe::SymMap<int> map;
        map[syms.sym("key")] = 42;
        auto it              = map.find(syms.sym("key"));
        REQUIRE(it != map.end());
        CHECK(it->second == 42);
        CHECK(!map.contains(syms.sym("missing")));

        fe::SymSet set;
        set.insert(syms.sym("elem"));
        CHECK(set.contains(syms.sym("elem")));
        CHECK(!set.contains(fe::Sym()));
    }

    SUBCASE("moving a SymPool keeps interned symbols valid") {
        fe::SymPool p1;
        auto sym = p1.sym("a-rather-long-symbol-name");

        fe::SymPool p2(std::move(p1));
        CHECK(p2.sym("a-rather-long-symbol-name") == sym);
        CHECK(sym.view() == "a-rather-long-symbol-name"sv);
    }
}

TEST_CASE("utf8") {
    std::ostringstream oss;
    fe::utf8::encode(oss, U'a');
    fe::utf8::encode(oss, U'£');
    fe::utf8::encode(oss, U'λ');
    fe::utf8::encode(oss, U'𐄂');
    fe::utf8::encode(oss, U'𐀮');
    CHECK(oss.str() == "a£λ𐄂𐀮");
    CHECK(fe::utf8::any('a', 'b', 'c')('a'));
    CHECK(fe::utf8::any('a', 'b', 'c')('b'));
    CHECK(fe::utf8::any('a', 'b', 'c')('c'));
    CHECK(fe::utf8::any('a', 'b', 'c')('x') == false);

    SUBCASE("decode preserves U+0000") {
        std::istringstream nul(std::string("\0", 1));
        CHECK(fe::utf8::decode(nul) == fe::utf8::Null);
    }

    SUBCASE("decode rejects overlong encodings") {
        std::istringstream overlong2("\xc0\x80");
        std::istringstream overlong3("\xe0\x80\x80");
        std::istringstream overlong4("\xf0\x80\x80\x80");
        CHECK(fe::utf8::decode(overlong2) == fe::utf8::Invalid);
        CHECK(fe::utf8::decode(overlong3) == fe::utf8::Invalid);
        CHECK(fe::utf8::decode(overlong4) == fe::utf8::Invalid);
    }

    SUBCASE("decode rejects surrogate code points") {
        std::istringstream surrogate("\xed\xa0\x80");
        CHECK(fe::utf8::decode(surrogate) == fe::utf8::Invalid);
    }

    SUBCASE("decode rejects values above the Unicode range") {
        std::istringstream too_large("\xf4\x90\x80\x80");
        std::istringstream invalid_lead("\xf5\x80\x80\x80");
        CHECK(fe::utf8::decode(too_large) == fe::utf8::Invalid);
        CHECK(fe::utf8::decode(invalid_lead) == fe::utf8::Invalid);
    }

    SUBCASE("decode rejects truncated sequences") {
        std::istringstream trunc2("\xc3");
        std::istringstream trunc3("\xe2\x82");
        CHECK(fe::utf8::decode(trunc2) == fe::utf8::Invalid);
        CHECK(fe::utf8::decode(trunc3) == fe::utf8::Invalid);
    }

    SUBCASE("num_bytes inspects the lead byte") {
        CHECK(fe::utf8::num_bytes('a') == 1);
        CHECK(fe::utf8::num_bytes(0xc3) == 2);
        CHECK(fe::utf8::num_bytes(0xe2) == 3);
        CHECK(fe::utf8::num_bytes(0xf0) == 4);
        CHECK(fe::utf8::num_bytes(0x80) == 0); // continuation byte
        CHECK(fe::utf8::num_bytes(0xff) == 0);
    }

    SUBCASE("encode rejects out-of-range code points") {
        std::ostringstream oss2;
        CHECK(!fe::utf8::encode(oss2, char32_t(0x110000)));
        CHECK(oss2.str().empty());
    }

    SUBCASE("encode/decode roundtrip") {
        for (char32_t c : {U'$', U'¢', U'ह', U'€', U'𐍈', char32_t(0x10ffff)}) {
            std::ostringstream os;
            REQUIRE(fe::utf8::encode(os, c));
            std::istringstream is(os.str());
            CHECK(fe::utf8::decode(is) == c);
        }
    }

    SUBCASE("classification and case folding") {
        CHECK(fe::utf8::isascii('a'));
        CHECK(!fe::utf8::isascii(U'λ'));
        CHECK(fe::utf8::isrange(U'5', '0', '9'));
        CHECK(fe::utf8::isrange('0', '9')(U'5'));
        CHECK(!fe::utf8::isrange('0', '9')(U'a'));
        CHECK(fe::utf8::isodigit('7'));
        CHECK(!fe::utf8::isodigit('8'));
        CHECK(fe::utf8::isbdigit('1'));
        CHECK(!fe::utf8::isbdigit('2'));
        CHECK(fe::utf8::tolower(U'A') == U'a');
        CHECK(fe::utf8::toupper(U'a') == U'A');
        CHECK(fe::utf8::tolower(U'Λ') == U'Λ'); // non-ASCII passes through unchanged
        CHECK(fe::utf8::toupper(U'λ') == U'λ');
    }

    SUBCASE("Char32 streams as UTF-8") {
        CHECK(std::format("{}", fe::utf8::Char32(U'λ')) == "λ");
        CHECK(std::format("{}", fe::utf8::Char32('a')) == "a");
    }
}

enum class MyEnum : unsigned {
    A = 1 << 0,
    B = 1 << 1,
    C = 1 << 2,
};

template<>
struct fe::is_bit_enum<MyEnum> : std::true_type {};

TEST_CASE("enum") {
    static_assert(fe::to_underlying(MyEnum::A & MyEnum::A) == 1);
    static_assert(fe::to_underlying(MyEnum::A & MyEnum::B) == 0);
    static_assert(fe::to_underlying(MyEnum::A | MyEnum::B) == 3);
    static_assert(fe::to_underlying(MyEnum::A ^ MyEnum::A) == 0);
    static_assert(fe::to_underlying(~MyEnum::A & (MyEnum::A | MyEnum::B)) == 2);
    static_assert(fe::has_flag(MyEnum::A | MyEnum::B, MyEnum::A));
    static_assert(!fe::has_flag(MyEnum::A | MyEnum::B, MyEnum::C));

    auto e = MyEnum::A;
    e |= MyEnum::B;
    CHECK(fe::to_underlying(e) == 3);
    e &= MyEnum::B;
    CHECK(fe::to_underlying(e) == 2);
    e ^= MyEnum::B | MyEnum::C;
    CHECK(fe::to_underlying(e) == 4);
}

TEST_CASE("term") {
    struct ModeGuard {
        ~ModeGuard() { fe::term::set_mode(old_); }
        fe::term::Mode old_ = fe::term::mode();
    } guard;

    SUBCASE("auto mode suppresses colors for non-terminal streams") {
        std::ostringstream oss;
        fe::term::set_mode(fe::term::Mode::Auto);
        oss << fe::term::FG::Red << "x" << fe::term::FG::Reset;
        CHECK(oss.str() == "x");
    }

    SUBCASE("always mode emits ANSI sequences") {
        std::ostringstream oss;
        fe::term::set_mode(fe::term::Mode::Always);
        oss << fe::term::FG::Red << "x" << fe::term::FG::Reset;
        CHECK(oss.str() == "\033[31mx\033[39m");
    }

    SUBCASE("never mode suppresses colors") {
        std::ostringstream oss;
        fe::term::set_mode(fe::term::Mode::Never);
        oss << fe::term::FG::Green << "x" << fe::term::FG::Reset;
        CHECK(oss.str() == "x");
    }

    SUBCASE("always mode emits ANSI sequences via std::format") {
        fe::term::set_mode(fe::term::Mode::Always);
        CHECK(std::format("{}x{}", fe::term::FG::Red, fe::term::FG::Reset) == "\033[31mx\033[39m");
    }

    SUBCASE("auto mode suppresses colors via std::format") {
        fe::term::set_mode(fe::term::Mode::Auto);
        CHECK(std::format("{}x{}", fe::term::FG::Red, fe::term::FG::Reset) == "x");
    }

    SUBCASE("resolve_mode decides auto mode based on the given stream") {
        std::ostringstream oss;
        fe::term::set_mode(fe::term::Mode::Auto);
        fe::term::resolve_mode(oss); // not a terminal
        CHECK(fe::term::mode() == fe::term::Mode::Never);
        CHECK(std::format("{}x{}", fe::term::FG::Red, fe::term::FG::Reset) == "x");
    }

    SUBCASE("resolve_mode leaves explicit modes untouched") {
        fe::term::set_mode(fe::term::Mode::Always);
        fe::term::resolve_mode();
        CHECK(fe::term::mode() == fe::term::Mode::Always);

        fe::term::set_mode(fe::term::Mode::Never);
        fe::term::resolve_mode();
        CHECK(fe::term::mode() == fe::term::Mode::Never);
    }

    // use_color is the very predicate operator<< branches on, so the two must never disagree:
    // a caller pairing a plain-text fallback with color would otherwise emit both or neither.
    SUBCASE("use_color agrees with what operator<< emits") {
        for (auto mode : {fe::term::Mode::Always, fe::term::Mode::Never, fe::term::Mode::Auto}) {
            std::ostringstream oss;
            fe::term::set_mode(mode);
            oss << fe::term::FG::Red;
            CHECK(fe::term::use_color(oss) == !oss.str().empty());
        }
    }
}

TEST_CASE("format") {
    std::vector<int> v0;
    std::vector<int> v1 = {23};
    std::vector<int> v2 = {23, 42};
    std::vector<int> v3 = {23, 42, 17};
    CHECK(std::format("{}", fe::Join(v0)) == "");
    CHECK(std::format("{}", fe::Join(v1)) == "23");
    CHECK(std::format("{}", fe::Join(v2)) == "23, 42");
    CHECK(std::format("{}", fe::Join(v3)) == "23, 42, 17");

    SUBCASE("Join with custom separator and format spec") {
        CHECK(std::format("{}", fe::Join(v2, " | ")) == "23 | 42");
        CHECK(std::format("{:#x}", fe::Join(v2)) == "0x17, 0x2a"); // spec applies to each element
        CHECK(std::format("{}", fe::Join(v0, " | ")) == "");

        std::ostringstream oss;
        oss << fe::Join(v3, "-");
        CHECK(oss.str() == "23-42-17");
    }

    SUBCASE("StreamFn") {
        auto chained = fe::StreamFn([](std::ostream& os) -> std::ostream& { return os << "hi"; });
        auto plain   = fe::StreamFn([](std::ostream& os) { os << "ho"; });

        std::ostringstream oss;
        oss << chained << ' ' << plain;
        CHECK(oss.str() == "hi ho");
        CHECK(std::format("{} {}", chained, plain) == "hi ho");
    }

    SUBCASE("Tab") {
        fe::Tab tab;
        CHECK(tab.indent() == 0);
        CHECK(std::format("{}|", tab) == "|");

        ++tab;
        CHECK(std::format("{}|", tab) == "\t|");

        tab += 2;
        CHECK(tab.indent() == 3);
        --tab;
        tab -= 1;
        CHECK(tab.indent() == 1);

        auto more = tab + 2; // creates a new Tab
        CHECK(more.indent() == 3);
        CHECK(tab.indent() == 1);
        CHECK((more - 3).indent() == 0);

        auto spaces = ++fe::Tab::spaces();
        CHECK(std::format("{}|", spaces) == "    |");
    }
}

TEST_CASE("Span") {
    SUBCASE("structured binding writes through") {
        int a[3]        = {0, 1, 2};
        auto s          = fe::Span(a);
        auto& [x, y, z] = s;
        y               = 23;
        CHECK(a[1] == 23);
        CHECK(x == 0);
        CHECK(z == 2);
    }

    int a[9] = {0, 1, 2, 3, 4, 5, 6, 7, 8};
    int* p   = a;

    auto check = [](auto span, int b, int e) {
        CHECK(span.front() == b);
        CHECK(span.back() == e - 1);
        CHECK(span.size() == size_t(e - b));
    };

    SUBCASE("constness follows the source") {
        auto vec         = std::vector(a, a + 9);
        const auto& cvec = vec;
        auto vs          = fe::Span(vec);
        auto vv          = fe::View<int>(vec);
        auto vw          = fe::Span(cvec);
        static_assert(std::is_same_v<decltype(vs), fe::Span<int, std::dynamic_extent>>);
        static_assert(std::is_same_v<decltype(vv), fe::Span<const int, std::dynamic_extent>>);
        static_assert(std::is_same_v<decltype(vw), fe::Span<const int, std::dynamic_extent>>);
        static_assert(fe::Vectorlike<std::vector<int>>);
        check(vs, 0, 9);
    }

    SUBCASE("initializer_list") {
        auto init = fe::Span<const int>({1, 2, 3});
        CHECK(init.size() == 3);
        CHECK(init[2] == 3);
    }

    auto s_0_9 = fe::Span(a);
    auto d_0_9 = fe::Span(p, 9);
    static_assert(std::is_same_v<decltype(s_0_9), fe::Span<int, 9>>, "dynamic_extent broken");
    static_assert(std::is_same_v<decltype(d_0_9), fe::Span<int, std::dynamic_extent>>, "dynamic_extent broken");
    check(s_0_9, 0, 9);
    check(d_0_9, 0, 9);

    SUBCASE("subspan") {
        auto s_7_9 = s_0_9.subspan<7>();
        auto s_2_6 = s_0_9.subspan<2, 4>();
        auto s_1_4 = d_0_9.subspan<1, 3>();
        auto d_2_9 = d_0_9.subspan<2>();
        auto d_7_9 = s_0_9.subspan(7);
        auto d_2_6 = s_0_9.subspan(2, 4);
        static_assert(std::is_same_v<decltype(s_7_9), fe::Span<int, 2>>, "dynamic_extent broken");
        static_assert(std::is_same_v<decltype(s_2_6), fe::Span<int, 4>>, "dynamic_extent broken");
        static_assert(std::is_same_v<decltype(s_1_4), fe::Span<int, 3>>, "dynamic_extent broken");
        static_assert(std::is_same_v<decltype(d_2_9), fe::Span<int, std::dynamic_extent>>, "dynamic_extent broken");
        static_assert(std::is_same_v<decltype(d_7_9), fe::Span<int, std::dynamic_extent>>, "dynamic_extent broken");
        static_assert(std::is_same_v<decltype(d_2_6), fe::Span<int, std::dynamic_extent>>, "dynamic_extent broken");

        check(s_7_9, 7, 9);
        check(s_2_6, 2, 6);
        check(s_1_4, 1, 4);
        check(d_2_9, 2, 9);
        check(d_7_9, 7, 9);
        check(d_2_6, 2, 6);
    }

    SUBCASE("rsubspan") {
        auto s_0_2 = s_0_9.rsubspan<7>();
        auto s_3_7 = s_0_9.rsubspan<2, 4>();
        auto s_5_8 = d_0_9.rsubspan<1, 3>();
        auto d_0_6 = d_0_9.rsubspan<3>();
        auto d_0_2 = s_0_9.rsubspan(7);
        auto d_3_7 = s_0_9.rsubspan(2, 4);
        static_assert(std::is_same_v<decltype(s_0_2), fe::Span<int, 2>>, "dynamic_extent broken");
        static_assert(std::is_same_v<decltype(s_3_7), fe::Span<int, 4>>, "dynamic_extent broken");
        static_assert(std::is_same_v<decltype(s_5_8), fe::Span<int, 3>>, "dynamic_extent broken");
        static_assert(std::is_same_v<decltype(d_0_6), fe::Span<int, std::dynamic_extent>>, "dynamic_extent broken");
        static_assert(std::is_same_v<decltype(d_0_2), fe::Span<int, std::dynamic_extent>>, "dynamic_extent broken");
        static_assert(std::is_same_v<decltype(d_3_7), fe::Span<int, std::dynamic_extent>>, "dynamic_extent broken");

        check(s_0_2, 0, 2);
        check(s_3_7, 3, 7);
        check(s_5_8, 5, 8);
        check(d_0_6, 0, 6);
        check(d_0_2, 0, 2);
        check(d_3_7, 3, 7);
    }

    SUBCASE("span<N> keeps the size static") {
        auto first3 = s_0_9.span<3>();
        static_assert(std::is_same_v<decltype(first3), fe::Span<int, 3>>);
        check(first3, 0, 3);
    }
}

TEST_CASE("Vector") {
    fe::Vector<int> v = {1, 2, 3};
    CHECK(v.span().size() == 3);
    CHECK(v.view()[2] == 3);

    SUBCASE("generator constructors") {
        fe::Vector<int> squares(5, [](size_t i) { return int(i * i); });
        CHECK(squares.size() == 5);
        CHECK(squares[4] == 16);

        std::vector<int> src = {1, 2, 3};
        fe::Vector<int> twice(src, [](int i) { return 2 * i; });
        CHECK(twice.size() == 3);
        CHECK(twice[2] == 6);
    }

    SUBCASE("insert_range/append_range") {
        std::vector<int> more = {4, 5};
        v.append_range(more);
        CHECK(v.size() == 5);
        CHECK(v.back() == 5);
        v.insert_range(v.begin(), more);
        CHECK(v.front() == 4);
    }

    SUBCASE("erase/erase_if and swap") {
        fe::Vector<int> w = {1, 2, 3, 2, 1};
        CHECK(erase(w, 2) == 2);
        CHECK(w.size() == 3);
        CHECK(erase_if(w, [](int i) { return i == 1; }) == 2);
        CHECK(w.size() == 1);

        fe::Vector<int> x = {7};
        swap(w, x);
        CHECK(w.front() == 7);
        CHECK(x.front() == 3);
    }
}

TEST_CASE("algo") {
    CHECK(fe::pad(0, 8) == 0);
    CHECK(fe::pad(1, 8) == 8);
    CHECK(fe::pad(8, 8) == 8);
    CHECK(fe::pad(9, 8) == 16);

    CHECK(fe::bitcast_resize<uint32_t>(uint16_t(23)) == 23);
    CHECK(fe::bitcast_resize<uint8_t>(uint32_t(0xff23)) == 0x23);

    CHECK(fe::subview("hello world", 6) == "world");
    CHECK(fe::subview("hello world", 0, 5) == "hello");

    auto str = std::string("a.b.c");
    fe::find_and_replace(str, ".", "::");
    CHECK(str == "a::b::c");

    SUBCASE("binary_find below and above the linear-scan threshold") {
        auto lt = std::less<int>();
        for (int n : {8, 32}) {
            std::vector<int> v;
            for (int i = 0; i != n; ++i)
                v.emplace_back(2 * i);
            CHECK(fe::binary_find(v.begin(), v.end(), 0, lt) == v.begin());
            CHECK(*fe::binary_find(v.begin(), v.end(), 2 * (n - 1), lt) == 2 * (n - 1));
            CHECK(fe::binary_find(v.begin(), v.end(), 1, lt) == v.end());
            CHECK(fe::binary_find(v.begin(), v.end(), 2 * n, lt) == v.end());
        }
    }
}

TEST_CASE("container") {
    SUBCASE("pop") {
        std::stack<int> s;
        s.push(1);
        s.push(2);
        CHECK(fe::pop(s) == 2);
        CHECK(fe::pop(s) == 1);
        CHECK(s.empty());

        std::queue<int> q;
        q.push(1);
        q.push(2);
        CHECK(fe::pop(q) == 1);
        CHECK(fe::pop(q) == 2);
        CHECK(q.empty());
    }

    SUBCASE("lookup") {
        std::unordered_map<int, int> map = {
            {1, 23}
        };
        CHECK(*fe::lookup(map, 1) == 23);
        CHECK(fe::lookup(map, 2) == nullptr);
        CHECK(fe::assert_lookup(map, 1) == 23);

        int i                              = 23;
        std::unordered_map<int, int*> ptrs = {
            {1, &i}
        };
        CHECK(fe::lookup(ptrs, 1) == &i);
        CHECK(fe::lookup(ptrs, 2) == nullptr);

        CHECK(fe::assert_emplace(map, 2, 42)->second == 42);
    }

    SUBCASE("UniqueQueue") {
        fe::UniqueQueue<std::unordered_set<int>> queue;
        CHECK(queue.empty());
        CHECK(queue.push(1));
        CHECK(!queue.push(1));
        CHECK(queue.push(2));
        CHECK(queue.front() == 1);
        CHECK(queue.back() == 2);
        CHECK(queue.pop() == 1);
        CHECK(queue.pop() == 2);
        CHECK(queue.empty());
        CHECK(!queue.push(1)); // still done

        queue.clear();
        CHECK(queue.push(1));
    }
}

TEST_CASE("Dbg") {
    fe::SymPool syms;
    auto a   = syms.sym("a");
    auto loc = fe::Loc(fe::Pos(0), fe::Pos(1));
    auto dbg = fe::Dbg(loc, a);

    CHECK(dbg.sym() == a);
    CHECK(dbg.loc() == loc);
    CHECK(dbg);
    CHECK(dbg == fe::Dbg(loc, a));
    CHECK(dbg != fe::Dbg(loc, syms.sym("b")));
    CHECK(std::format("{}", dbg) == "a");

    SUBCASE("anonymous") {
        CHECK(fe::Dbg().is_anon());
        CHECK(fe::Dbg(syms.sym("_")).is_anon());
        CHECK(!dbg.is_anon());
        CHECK(!fe::Dbg(loc));
    }

    SUBCASE("Hash/Eq") {
        std::unordered_map<fe::Dbg, int, fe::Dbg::Hash, fe::Dbg::Eq> map;
        map[dbg] = 23;
        CHECK(map.find(fe::Dbg(loc, a))->second == 23);
        CHECK(!map.contains(fe::Dbg(a)));
    }
}

TEST_CASE("Log") {
    using Level = fe::Log::Level;
    // The expectations go through std::format like Log::emit does, so they hold in any term::Mode.
    auto expect = [](Level level, const auto& where, std::string_view msg) {
        return std::format("{}{}:{}{}:{} {}\n", fe::Log::level2color(level), fe::Log::level2acro(level),
                           fe::term::FG::Gray, where, fe::term::FG::Reset, msg);
    };

    std::ostringstream oss;
    fe::Log log;
    CHECK(!log);

    log.set(&oss).set(Level::Info);
    CHECK(log);
    CHECK(log.level() == Level::Info);

    log.log(Level::Info, "foo.cpp", 23, "hi {}", 42);
    CHECK(oss.str() == expect(Level::Info, "foo.cpp:23", "hi 42"));

    SUBCASE("levels above the maximum are dropped") {
        oss.str({});
        log.log(Level::Debug, "foo.cpp", 23, "nope");
        CHECK(oss.str().empty());
    }

    SUBCASE("a Loc names the place instead") {
        auto loc = fe::Loc(fe::Pos(0), fe::Pos(1));
        oss.str({});
        log.log(Level::Error, loc, "oops");
        CHECK(oss.str() == expect(Level::Error, loc, "oops"));
    }

    SUBCASE("acronyms and colors") {
        CHECK(fe::Log::level2acro(Level::Error) == 'E');
        CHECK(fe::Log::level2acro(Level::Trace) == 'T');
        CHECK(fe::Log::level2color(Level::Error) == fe::term::FG::Red);
        CHECK(fe::Log::level2color(Level::Trace) == fe::term::FG::Magenta);
    }
}
