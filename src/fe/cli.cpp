#include "fe/cli.h"

#include <algorithm>
#include <filesystem>

#include "fe/term.h"

namespace fe {

std::string Cli::Opt::names(bool pad) const {
    if (lname.empty()) return sname;
    if (sname.empty()) return pad ? "    " + lname : lname;
    return sname + ", " + lname;
}

std::string Cli::Opt::spec(bool pad) const {
    auto res = names(pad);
    if (takes_value()) res += std::format("{}<{}>", res.empty() ? "" : " ", hint);
    return res;
}

std::string Cli::usage() const {
    auto s = prog_;
    if (std::ranges::any_of(opts_, [](const Opt& o) { return !o.is_arg(); })) s += " [options]";
    for (const auto& o : opts_)
        if (o.is_arg()) s += std::format(" <{}>", o.hint);
    return s;
}

std::vector<std::string_view> Cli::grps() const {
    std::vector<std::string_view> res;
    for (const auto& o : opts_)
        if (!o.is_arg() && std::ranges::find(res, o.grp) == res.end()) res.emplace_back(o.grp);
    return res;
}

Cli::Opt* Cli::find(std::string_view name) {
    for (auto& o : opts_)
        if (o.sname == name || o.lname == name) return &o;
    return nullptr;
}

std::optional<std::string> Cli::parse(int argc, const char* const* argv) {
    if (prog_.empty() && argc > 0) {
        prog_ = std::filesystem::path(argv[0]).filename().string();
        if (prog_.ends_with(".exe")) prog_.resize(prog_.size() - 4);
    }

    Opt* pos  = nullptr;
    auto next = [&] {
        for (auto& o : opts_)
            if (o.is_arg() && (o.num == 0 || o.multi)) return pos = &o;
        return pos = nullptr;
    };
    next();

    auto set = [](Opt& o, std::string_view name, std::string_view value) -> std::optional<std::string> {
        ++o.num;
        if (auto err = o.set(value); !err.empty()) return std::format("{} '{}': {}", o.kind(), name, err);
        return {};
    };

    bool only_pos = false;
    for (int i = 1; i < argc; ++i) {
        auto s = std::string_view(argv[i]);

        if (!only_pos && s == "--") {
            only_pos = true;
        } else if (!only_pos && s.starts_with("--")) {
            auto eq   = s.find('=');
            auto name = s.substr(0, eq);
            auto o    = find(name);
            if (!o) return std::format("unknown option '{}'", name);

            if (!o->takes_value()) {
                if (eq != std::string_view::npos) return std::format("option '{}' does not take a value", name);
                if (auto err = set(*o, name, {})) return err;
            } else if (eq != std::string_view::npos) {
                if (auto err = set(*o, name, s.substr(eq + 1))) return err;
            } else if (++i < argc) {
                if (auto err = set(*o, name, argv[i])) return err;
            } else {
                return std::format("option '{}' requires a value <{}>", name, o->hint);
            }
        } else if (!only_pos && s.size() > 1 && s.front() == '-') {
            for (size_t j = 1; j != s.size(); ++j) {
                char buf[] = {'-', s[j], '\0'};
                auto name  = std::string_view(buf, 2);
                auto o     = find(name);
                if (!o) return std::format("unknown option '{}'", name);

                if (!o->takes_value()) {
                    if (auto err = set(*o, name, {})) return err;
                } else if (j + 1 != s.size()) {
                    if (auto err = set(*o, name, s.substr(j + 1))) return err;
                    break;
                } else if (++i < argc) {
                    if (auto err = set(*o, name, argv[i])) return err;
                    break;
                } else {
                    return std::format("option '{}' requires a value <{}>", name, o->hint);
                }
            }
        } else {
            if (!pos) return std::format("unexpected argument '{}'", s);
            if (auto err = set(*pos, pos->hint, s)) return err;
            if (!pos->multi) next();
        }
    }

    for (const auto& o : opts_) {
        if (o.num < o.min) return std::format("missing {} '{}'", o.kind(), o.label());
        if (o.num > o.max) return std::format("{} '{}' must not occur more than {} times", o.kind(), o.label(), o.max);
    }

    return {};
}

void Cli::help(std::ostream& os) const {
    auto cols = std::clamp<size_t>(term::width(os).value_or(80), 40, 120);

    // Only the specs up to a third of the line dictate the description column; longer ones get a line of their own.
    size_t spec = 0;
    auto fits   = [&](size_t w) {
        if (w <= cols / 3) spec = std::max(spec, w);
    };
    for (const auto& o : opts_)
        fits(o.width());
    for (const auto& s : sections_)
        for (const auto& [name, descr] : s.rows)
            fits(name.size());
    auto col = std::clamp<size_t>(spec + 4, 8, cols / 2);

    auto wrap = [&](std::string_view text) {
        for (size_t begin = 0, row = 0; begin < text.size(); ++row) {
            while (begin < text.size() && text[begin] == ' ')
                ++begin;
            if (begin >= text.size()) break;

            auto end = std::min(text.size(), begin + cols - col);
            if (end < text.size())
                if (auto space = text.rfind(' ', end); space != std::string_view::npos && space > begin) end = space;
            if (row != 0) os << std::string(col, ' ');
            os << text.substr(begin, end - begin) << '\n';
            begin = end;
        }
    };

    auto section = [&](std::string_view title) {
        os << '\n' << term::FG::Yellow << title << ':' << term::FG::Reset << '\n';
    };

    // Pads what has just been written up to the description column - or breaks the line if it does not fit.
    auto tail = [&](size_t w, std::string_view descr) {
        if (w + 2 > col) {
            os << '\n';
            w = 0;
        }
        os << std::string(col - w, ' ');
        if (descr.empty())
            os << '\n';
        else
            wrap(descr);
    };

    auto entry = [&](const Opt& o) {
        os << "  ";
        if (o.is_arg()) {
            os << term::FG::Cyan << '<' << o.hint << '>' << term::FG::Reset;
        } else {
            os << term::FG::Green << o.names(true) << term::FG::Reset;
            if (o.takes_value()) os << ' ' << term::FG::Cyan << '<' << o.hint << '>' << term::FG::Reset;
        }

        auto descr = o.descr;
        if (!o.dflt.empty()) descr += std::format("{}[default: {}]", descr.empty() ? "" : " ", o.dflt);
        tail(o.width() + 2, descr);
    };

    os << term::FG::Yellow << "Usage:" << term::FG::Reset << ' ' << usage() << '\n';
    if (!descr_.empty()) os << '\n' << descr_ << '\n';

    if (std::ranges::any_of(opts_, [](const Opt& o) { return o.is_arg(); })) {
        section("Arguments");
        for (const auto& o : opts_)
            if (o.is_arg()) entry(o);
    }

    for (auto grp : grps()) {
        section(grp.empty() ? "Options" : grp);
        for (const auto& o : opts_)
            if (!o.is_arg() && o.grp == grp) entry(o);
    }

    for (const auto& s : sections_) {
        section(s.title);
        for (const auto& [name, descr] : s.rows) {
            os << "  " << term::FG::Green << name << term::FG::Reset;
            tail(name.size() + 2, descr);
        }
    }

    if (!epilog_.empty()) os << '\n' << epilog_ << '\n';
}

void Cli::markdown(std::ostream& os) const {
    // A code span is already verbatim - only `|`, which ends the table cell, still has to go.
    auto esc = [](std::string_view text) {
        std::string res;
        bool code = false;
        for (size_t i = 0, e = text.size(); i != e; ++i) {
            auto c = text[i];
            if (c == '`') code = !code;
            if (code && c != '|') {
                res += c;
                continue;
            }
            switch (c) {
                case '&': res += "&amp;"; break;
                case '<': res += "&lt;"; break;
                case '>': res += "&gt;"; break;
                case '|': res += "\\|"; break;
                // Doxygen turns a bare `--` into an en dash.
                case '-':
                    if (i + 1 != e && text[i + 1] == '-') res += '\\';
                    res += c;
                    break;
                default: res += c;
            }
        }
        return res;
    };

    // A code span still sits in a table cell, so only `|` needs to go.
    auto code = [](std::string_view text) {
        std::string res;
        for (auto c : text) {
            if (c == '|') res += '\\';
            res += c;
        }
        return res;
    };

    auto table = [&](std::string_view title, std::string_view head, auto&& pred) {
        os << std::format("\n### {}\n\n| {} | Description |\n| --- | --- |\n", title, head);
        for (const auto& o : opts_) {
            if (!pred(o)) continue;
            auto descr = esc(o.descr);
            if (!o.dflt.empty()) descr += std::format("{}[default: `{}`]", descr.empty() ? "" : " ", code(o.dflt));
            os << std::format("| `{}` | {} |\n", code(o.spec()), descr);
        }
    };

    os << std::format("```\n{}\n```\n", usage());
    if (!descr_.empty()) os << '\n' << esc(descr_) << '\n';

    if (std::ranges::any_of(opts_, [](const Opt& o) { return o.is_arg(); }))
        table("Arguments", "Argument", [](const Opt& o) { return o.is_arg(); });

    for (auto grp : grps())
        table(grp.empty() ? "Options" : grp, "Option", [&](const Opt& o) { return !o.is_arg() && o.grp == grp; });

    for (const auto& s : sections_) {
        if (s.rows.empty()) { // a Section without rows only groups the ones below it, hence one level up
            os << std::format("\n## {}\n", esc(s.title));
            continue;
        }
        os << std::format("\n### {}\n\n| {} | Description |\n| --- | --- |\n", esc(s.title), s.head);
        for (const auto& [name, descr] : s.rows)
            os << std::format("| `{}` | {} |\n", code(name), esc(descr));
    }

    if (!epilog_.empty()) os << '\n' << esc(epilog_) << '\n';
}

} // namespace fe
