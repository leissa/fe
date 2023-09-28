#include <fe/sym.h>
#include <fe/lexer.h>

#include <iostream>

using namespace fe;

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

struct Driver : public SymPool {
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

class Lexer : public ::fe::Lexer {
};

int main() {
}
