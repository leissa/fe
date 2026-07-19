#include <doctest/doctest.h>
#include <fe/cast.h>

namespace {

// dynamic_cast flavor: a plain polymorphic hierarchy.
struct Poly : fe::RuntimeCast<Poly> {
    virtual ~Poly() = default;
};

struct PolyA : Poly {};
struct PolyB : Poly {};

// Nodeable flavor: dispatch on node() without RTTI.
enum class Kind { A, B };

struct Expr : fe::RuntimeCast<Expr> {
    Expr(Kind kind)
        : kind_(kind) {}

    Kind node() const { return kind_; }

    Kind kind_;
};

struct ExprA : Expr {
    static constexpr Kind Node = Kind::A;

    ExprA()
        : Expr(Node) {}
};

struct ExprB : Expr {
    static constexpr Kind Node = Kind::B;

    ExprB()
        : Expr(Node) {}
};

static_assert(fe::Nodeable<ExprA>);
static_assert(!fe::Nodeable<PolyA>);

} // namespace

TEST_CASE("RuntimeCast") {
    SUBCASE("isa/as via dynamic_cast") {
        PolyA a;
        Poly* p = &a;
        CHECK(p->isa<PolyA>() == &a);
        CHECK(p->isa<PolyB>() == nullptr);
        CHECK(p->as<PolyA>() == &a);

        const Poly* cp = &a;
        CHECK(cp->isa<PolyA>() == &a);
        CHECK(cp->isa<PolyB>() == nullptr);
        CHECK(cp->as<PolyA>() == &a);
    }

    SUBCASE("isa/as via node()") {
        ExprA a;
        Expr* e = &a;
        CHECK(e->isa<ExprA>() == &a);
        CHECK(e->isa<ExprB>() == nullptr);
        CHECK(e->as<ExprA>() == &a);

        const Expr* ce = &a;
        CHECK(ce->isa<ExprA>() == &a);
        CHECK(ce->isa<ExprB>() == nullptr);
        CHECK(ce->as<ExprA>() == &a);
    }
}
