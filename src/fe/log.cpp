#include "fe/log.h"

namespace fe {

// clang-format off
char Log::level2acro(Level level) {
    switch (level) {
        case Level::T: return 'T';
        case Level::D: return 'D';
        case Level::V: return 'V';
        case Level::I: return 'I';
        case Level::W: return 'W';
        case Level::E: return 'E';
        default: unreachable();
    }
}

term::FG Log::level2color(Level level) {
    switch (level) {
        case Level::T: return term::FG::Magenta;
        case Level::D: return term::FG::Cyan;
        case Level::V: return term::FG::Blue;
        case Level::I: return term::FG::Green;
        case Level::W: return term::FG::Yellow;
        case Level::E: return term::FG::Red;
        default: unreachable();
    }
}
// clang-format on

} // namespace fe
