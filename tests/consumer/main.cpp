#include <sstream>
#include <string_view>

#include <fe/driver.h>
#include <fe/error.h>
#include <fe/loc.h>

using namespace fe;

// Renders one diagnostic - which dispatches through fe::Diag's vtable and pulls in the Snippet.
int main() {
    Driver driver;
    auto src = driver.src().add("consumer.fe", "let x = 23;\n").first;
    auto loc = Loc(src, Pos(4), Pos(4));

    driver.error().e(loc, "cannot bind `{}`", driver.sym("x")).n("just a smoke test");

    auto os = std::ostringstream();
    try {
        driver.error().ack(os);
    } catch (const Error::Bail& bail) {
        return std::string_view(bail.what()).find("cannot bind") == std::string_view::npos ? 1 : 0;
    }
    return 1;
}
