#pragma once

#include <charconv>
#include <cstddef>

#include <algorithm>
#include <filesystem>
#include <format>
#include <functional>
#include <limits>
#include <ostream>
#include <string>
#include <string_view>
#include <type_traits>
#include <vector>

#include "fe/term.h"

/// A small command-line parser for a single command - no subcommands.
///
/// Declare the switches by piping fe::cli::opt / fe::cli::arg into an fe::cli::Cli and bind each one to a variable:
/// ```
/// bool show_help = false, verbose = false;
/// std::string out, in;
/// std::vector<std::string> plugins;
///
/// auto cli = fe::cli::Cli("mim", "The MimIR compiler.")
///          | fe::cli::help(show_help)
///          | fe::cli::opt(verbose           )["-V"]["--verbose"]("Be verbose.")
///          | fe::cli::opt(plugins, "plugin" )["-p"]["--plugin" ]("Loads a plugin; repeatable.")
///          | fe::cli::group("Output")
///          | fe::cli::opt(out,     "file"   )["-o"]["--output" ]("Where to write the result.")
///          | fe::cli::arg(in,      "file"   )                   ("Input file.");
///
/// if (auto res = cli.parse(argc, argv); !res) throw std::invalid_argument(res.message());
/// if (show_help) std::cout << cli;
/// ```
/// A target may be a `bool` (for a flag), a `std::string`, an integral type, a `std::vector` of those, or a callable -
/// invoked with `true` for a flag and with the `std::string` value otherwise.
/// Such a callable may return a `std::string` to reject that value; a non-empty one becomes the Res::message.
/// An fe::cli::opt without a hint is a flag; one with a hint takes a value.
///
/// The parser understands `--name value`, `--name=value`, `-n value`, `-nvalue`, clustered short flags (`-abc`), and
/// `--` to end option processing.
///
/// Cli::help lays the switches out for a terminal - wrapped to its width and colored via fe::term - whereas Cli::md
/// renders the same information as Markdown tables; fe::cli::group splits both into sections.
namespace fe::cli {

class Opt;
template<class T>
Opt opt(T&);
template<class T>
Opt opt(T&, std::string);
template<class T>
Opt arg(T&, std::string);

/// Outcome of Cli::parse: falsish and carrying a Res::message when parsing failed.
class Res {
public:
    Res() = default;
    Res(std::string message)
        : message_(std::move(message)) {}

    explicit operator bool() const noexcept { return message_.empty(); }
    const std::string& message() const noexcept { return message_; }

private:
    std::string message_;
};

/// Starts a new section in the help output; see fe::cli::group.
struct Group {
    std::string name;
};

/// Opens a section named @p name that all following Cli options are listed under.
inline Group group(std::string name) { return Group{std::move(name)}; }

namespace detail {

template<class T>
inline constexpr bool always_false = false;

template<class T>
struct Elem {
    using type = T;
};

template<class T>
struct Elem<std::vector<T>> {
    using type = T;
};

template<class T>
inline constexpr bool is_vec = false;
template<class T>
inline constexpr bool is_vec<std::vector<T>> = true;

template<class T>
inline constexpr bool is_num = std::is_integral_v<T> && !std::is_same_v<T, bool>;

/// Assigns @p s to @p t; returns a message if @p s does not scan as a `T`.
template<class T>
std::string scan(T& t, std::string_view s) {
    if constexpr (std::is_same_v<T, std::string>) {
        t = s;
    } else if constexpr (is_vec<T>) {
        typename Elem<T>::type elem{};
        if (auto err = scan(elem, s); !err.empty()) return err;
        t.emplace_back(std::move(elem));
    } else if constexpr (is_num<T>) {
        auto begin = s.data(), end = begin + s.size();
        if (auto [ptr, ec] = std::from_chars(begin, end, t); ec != std::errc{} || ptr != end)
            return std::format("'{}' is not a number", s);
    } else if constexpr (std::is_invocable_r_v<std::string, T&, std::string>) {
        return t(std::string(s)); // a validating handler reports what it did not like
    } else if constexpr (std::is_invocable_v<T&, std::string>) {
        t(std::string(s));
    } else {
        static_assert(always_false<T>, "cannot bind this type to an option that takes a value");
    }
    return {};
}

template<class T>
std::function<std::string()> dflt(T& t) {
    if constexpr (std::is_same_v<T, std::string>)
        return [&t] { return t; };
    else if constexpr (is_num<T>)
        return [&t] { return std::format("{}", t); };
    else
        return {};
}

} // namespace detail

/// One option or positional argument; build one with fe::cli::opt, fe::cli::arg, or fe::cli::help.
class Opt {
public:
    /// Adds @p name - `"-o"` for a short, `"--output"` for a long one.
    Opt& operator[](std::string name) {
        names_.emplace_back(std::move(name));
        return *this;
    }

    /// Sets the description shown in the help.
    Opt& operator()(std::string descr) {
        descr_ = std::move(descr);
        return *this;
    }

    /// This Opt must occur at least @p min and at most @p max times.
    Opt& cardinality(size_t min, size_t max) {
        min_ = min, max_ = max;
        return *this;
    }

private:
    /// How the names are spelled out in the help - long names align under each other.
    std::string names() const {
        std::string shorts, longs;
        for (const auto& name : names_) {
            auto& to = name.starts_with("--") ? longs : shorts;
            if (!to.empty()) to += ", ";
            to += name;
        }
        if (longs.empty()) return shorts;
        if (shorts.empty()) return "    " + longs;
        return shorts + ", " + longs;
    }

    size_t width() const {
        return names_.empty() ? hint_.size() + 2 : names().size() + (value_ ? hint_.size() + 3 : 0);
    }

    std::string_view label() const { return names_.empty() ? std::string_view(hint_) : names_.back(); }

    std::vector<std::string> names_;
    std::string hint_, descr_, group_;
    std::function<std::string(std::string_view)> set_;
    std::function<std::string()> dflt_;
    bool value_ = false; ///< Takes a value as opposed to being a flag.
    bool multi_ = false; ///< Bound to a `std::vector` and hence soaks up any number of values.
    size_t min_ = 0;
    size_t max_ = std::numeric_limits<size_t>::max();
    size_t num_ = 0;

    friend class Cli;
    template<class T>
    friend Opt opt(T&);
    template<class T>
    friend Opt opt(T&, std::string);
    template<class T>
    friend Opt arg(T&, std::string);
};

/// A flag: sets @p target to `true` - or invokes it with `true` - each time it occurs.
template<class T>
Opt opt(T& target) {
    Opt o;
    o.set_ = [&target](std::string_view) -> std::string {
        if constexpr (std::is_same_v<T, bool>)
            target = true;
        else
            target(true);
        return {};
    };
    return o;
}

/// An option that takes a value; @p hint names it in the help as `<hint>`.
template<class T>
Opt opt(T& target, std::string hint) {
    Opt o;
    o.hint_  = std::move(hint);
    o.value_ = true;
    o.multi_ = detail::is_vec<T>;
    o.set_   = [&target](std::string_view s) { return detail::scan(target, s); };
    o.dflt_  = detail::dflt(target);
    return o;
}

/// A positional argument; @p hint names it in the help as `<hint>`.
/// Bind a `std::vector` to soak up all remaining ones.
template<class T>
Opt arg(T& target, std::string hint) {
    auto o  = opt(target, std::move(hint));
    o.dflt_ = {};
    return o;
}

/// The `-h`/`--help` flag; chain `["-?"]` to give it further names.
inline Opt help(bool& target) { return opt(target)["-h"]["--help"]("Display this help and exit."); }

/// Holds the Opt%s, parses `argc`/`argv`, and renders the help.
class Cli {
public:
    Cli() = default;
    Cli(std::string prog, std::string descr = {})
        : prog_(std::move(prog))
        , descr_(std::move(descr)) {}

    Cli& add(Opt o) {
        if (o.group_.empty()) o.group_ = group_;
        opts_.emplace_back(std::move(o));
        return *this;
    }

    Cli& add(Group g) {
        group_ = std::move(g.name);
        return *this;
    }

    Cli& operator|(Opt o) & { return add(std::move(o)); }
    Cli& operator|(Group g) & { return add(std::move(g)); }

    Cli&& operator|(Opt o) && {
        add(std::move(o));
        return std::move(*this);
    }

    Cli&& operator|(Group g) && {
        add(std::move(g));
        return std::move(*this);
    }

    /// Text printed below the option list.
    Cli& epilog(std::string s) {
        epilog_ = std::move(s);
        return *this;
    }

    Res parse(int argc, const char* const* argv);
    void help(std::ostream&) const; ///< Renders the help for a terminal.
    void md(std::ostream&) const;   ///< Renders the same information as Doxygen-flavored Markdown tables.

private:
    std::string usage() const {
        auto s = prog_;
        if (std::ranges::any_of(opts_, [](const Opt& o) { return !o.names_.empty(); })) s += " [options]";
        for (const auto& o : opts_)
            if (o.names_.empty()) s += std::format(" <{}>", o.hint_);
        return s;
    }

    /// Names of the Opt groups in the order they first occur; the default group is the empty name.
    std::vector<std::string_view> groups() const {
        std::vector<std::string_view> res;
        for (const auto& o : opts_)
            if (!o.names_.empty() && std::ranges::find(res, o.group_) == res.end()) res.emplace_back(o.group_);
        return res;
    }

    Opt* find(std::string_view name) {
        for (auto& o : opts_)
            for (const auto& n : o.names_)
                if (n == name) return &o;
        return nullptr;
    }

    std::string prog_, descr_, epilog_, group_;
    std::vector<Opt> opts_;
};

inline std::ostream& operator<<(std::ostream& os, const Cli& cli) {
    cli.help(os);
    return os;
}

inline Res Cli::parse(int argc, const char* const* argv) {
    if (prog_.empty() && argc > 0) {
        prog_ = std::filesystem::path(argv[0]).filename().string();
        if (prog_.ends_with(".exe")) prog_.resize(prog_.size() - 4);
    }

    Opt* pos  = nullptr;
    auto next = [&] {
        for (auto& o : opts_)
            if (o.names_.empty() && (o.num_ == 0 || o.multi_)) return pos = &o;
        return pos = nullptr;
    };
    next();

    auto set = [](Opt& o, std::string_view name, std::string_view value) -> Res {
        ++o.num_;
        if (auto err = o.set_(value); !err.empty()) return std::format("option '{}': {}", name, err);
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

            if (!o->value_) {
                if (eq != std::string_view::npos) return std::format("option '{}' does not take a value", name);
                if (auto res = set(*o, name, {}); !res) return res;
            } else if (eq != std::string_view::npos) {
                if (auto res = set(*o, name, s.substr(eq + 1)); !res) return res;
            } else if (++i < argc) {
                if (auto res = set(*o, name, argv[i]); !res) return res;
            } else {
                return std::format("option '{}' requires a value <{}>", name, o->hint_);
            }
        } else if (!only_pos && s.size() > 1 && s.front() == '-') {
            for (size_t j = 1; j != s.size(); ++j) {
                char buf[] = {'-', s[j], '\0'};
                auto name  = std::string_view(buf, 2);
                auto o     = find(name);
                if (!o) return std::format("unknown option '{}'", name);

                if (!o->value_) {
                    if (auto res = set(*o, name, {}); !res) return res;
                } else if (j + 1 != s.size()) {
                    if (auto res = set(*o, name, s.substr(j + 1)); !res) return res;
                    break;
                } else if (++i < argc) {
                    if (auto res = set(*o, name, argv[i]); !res) return res;
                    break;
                } else {
                    return std::format("option '{}' requires a value <{}>", name, o->hint_);
                }
            }
        } else {
            if (!pos) return std::format("unexpected argument '{}'", s);
            if (auto res = set(*pos, pos->hint_, s); !res) return res;
            if (!pos->multi_) next();
        }
    }

    for (const auto& o : opts_) {
        if (o.num_ < o.min_) return std::format("missing option '{}'", o.label());
        if (o.num_ > o.max_) return std::format("option '{}' must not occur more than {} times", o.label(), o.max_);
    }

    return {};
}

inline void Cli::help(std::ostream& os) const {
    auto cols = std::clamp<size_t>(term::width(os).value_or(80), 40, 120);

    // Only the specs up to a third of the line dictate the description column; longer ones get a line of their own.
    size_t spec = 0;
    for (const auto& o : opts_)
        if (auto w = o.width(); w <= cols / 3) spec = std::max(spec, w);
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

    auto entry = [&](const Opt& o) {
        os << "  ";
        if (o.names_.empty()) {
            os << term::FG::Cyan << '<' << o.hint_ << '>' << term::FG::Reset;
        } else {
            os << term::FG::Green << o.names() << term::FG::Reset;
            if (o.value_) os << ' ' << term::FG::Cyan << '<' << o.hint_ << '>' << term::FG::Reset;
        }

        auto w = o.width() + 2;
        if (w + 2 > col) {
            os << '\n';
            w = 0;
        }
        os << std::string(col - w, ' ');

        auto descr = o.descr_;
        if (o.dflt_)
            if (auto d = o.dflt_(); !d.empty()) descr += std::format("{}[default: {}]", descr.empty() ? "" : " ", d);
        if (descr.empty())
            os << '\n';
        else
            wrap(descr);
    };

    os << term::FG::Yellow << "Usage:" << term::FG::Reset << ' ' << usage() << '\n';
    if (!descr_.empty()) os << '\n' << descr_ << '\n';

    if (std::ranges::any_of(opts_, [](const Opt& o) { return o.names_.empty(); })) {
        section("Arguments");
        for (const auto& o : opts_)
            if (o.names_.empty()) entry(o);
    }

    for (auto group : groups()) {
        section(group.empty() ? "Options" : group);
        for (const auto& o : opts_)
            if (!o.names_.empty() && o.group_ == group) entry(o);
    }

    if (!epilog_.empty()) os << '\n' << epilog_ << '\n';
}

inline void Cli::md(std::ostream& os) const {
    auto esc = [](std::string_view text) {
        std::string res;
        for (size_t i = 0, e = text.size(); i != e; ++i) {
            switch (auto c = text[i]) {
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

    auto spec = [](const Opt& o) {
        if (o.names_.empty()) return std::format("<{}>", o.hint_);
        std::string res;
        for (const auto& name : o.names_) {
            if (!res.empty()) res += ", ";
            res += name;
        }
        if (o.value_) res += std::format(" <{}>", o.hint_);
        return res;
    };

    auto table = [&](std::string_view title, std::string_view head, auto&& pred) {
        os << std::format("\n### {}\n\n| {} | Description |\n| --- | --- |\n", title, head);
        for (const auto& o : opts_) {
            if (!pred(o)) continue;
            auto descr = esc(o.descr_);
            if (o.dflt_)
                if (auto d = o.dflt_(); !d.empty())
                    descr += std::format("{}[default: `{}`]", descr.empty() ? "" : " ", code(d));
            os << std::format("| `{}` | {} |\n", code(spec(o)), descr);
        }
    };

    os << std::format("```\n{}\n```\n", usage());
    if (!descr_.empty()) os << '\n' << esc(descr_) << '\n';

    if (std::ranges::any_of(opts_, [](const Opt& o) { return o.names_.empty(); }))
        table("Arguments", "Argument", [](const Opt& o) { return o.names_.empty(); });

    for (auto group : groups())
        table(group.empty() ? "Options" : group, "Option",
              [&](const Opt& o) { return !o.names_.empty() && o.group_ == group; });

    if (!epilog_.empty()) os << '\n' << esc(epilog_) << '\n';
}

} // namespace fe::cli
