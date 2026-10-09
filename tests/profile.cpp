#include <sstream>

#include <doctest/doctest.h>
#include <fe/profile.h>

TEST_CASE("Profiler") {
    fe::Profiler prof;
    prof.stop();
    prof.count("ignored");
    CHECK(prof.empty());

    prof.start("outer");
    prof.count("n");
    prof.count("n", 2);
    prof.start("in\"ner\\\x01");
    prof.stop();
    prof.stop();

    const auto& spans = prof.spans();
    REQUIRE(spans.size() == 2);
    CHECK(spans[0].depth == 0);
    CHECK(spans[0].parent == fe::Profiler::No_Parent);
    CHECK(spans[1].depth == 1);
    CHECK(spans[1].parent == 0);
    REQUIRE(spans[0].counters.size() == 1);
    CHECK(spans[0].counters[0].second == 3);
    CHECK(spans[1].counters.empty());

    std::ostringstream os;
    prof.chrome_trace(os);
    CHECK(os.str().find(R"(in\"ner\\\u0001)") != std::string::npos);
}
