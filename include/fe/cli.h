#pragma once

#include <charconv>
#include <cstddef>

#include <format>
#include <functional>
#include <limits>
#include <optional>
#include <ostream>
#include <string>
#include <string_view>
#include <type_traits>
#include <vector>

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
/// if (auto err = cli.parse(argc, argv)) throw std::invalid_argument(*err);
/// if (show_help) std::cout << cli;
/// ```
/// A target may be a `bool` (for a flag), a `std::string`, an integral type, a `std::vector` of those, or a callable -
/// invoked with `true` for a flag and with the `std::string` value otherwise.
/// Such a callable may return a `std::string` to reject that value; a non-empty one becomes the error of Cli::parse.
/// An fe::cli::opt without a hint is a flag; one with a hint takes a value.
///
/// The parser understands `--name value`, `--name=value`, `-n value`, `-nvalue`, clustered short flags (`-abc`), and
/// `--` to end option processing.
///
/// Cli::help lays the switches out for a terminal - wrapped to its width and colored via fe::term - whereas
/// Cli::markdown renders the same information as Markdown tables; fe::cli::group splits both into sections.
namespace fe::cli {

// clang-format off
class Opt;
template<class T> Opt opt(T&);
template<class T> Opt opt(T&, std::string);
template<class T> Opt arg(T&, std::string);
// clang-format on

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
    Opt& operator[](std::string name) { return names_.emplace_back(std::move(name)), *this; }

    /// Sets the description shown in the help.
    Opt& operator()(std::string descr) { return descr_ = std::move(descr), *this; }

    /// This Opt must occur at least @p min and at most @p max times.
    Opt& cardinality(size_t min, size_t max) { return min_ = min, max_ = max, *this; }

private:
    /// How the names are spelled out in the help - long names align under each other.
    std::string names() const;
    size_t width() const;
    std::string_view label() const { return names_.empty() ? std::string_view(hint_) : names_.back(); }
    std::string_view kind() const { return names_.empty() ? "argument" : "option"; }

    std::vector<std::string> names_;
    std::string hint_, descr_, group_;
    std::function<std::string(std::string_view)> set_;
    std::function<std::string()> dflt_;
    bool value_ = false; ///< Takes a value as opposed to being a flag.
    bool multi_ = false; ///< Bound to a `std::vector` and hence soaks up any number of values.
    size_t min_ = 0;
    size_t max_ = std::numeric_limits<size_t>::max();
    size_t num_ = 0;

    // clang-format off
    friend class Cli;
    template<class T> friend Opt opt(T&);
    template<class T> friend Opt opt(T&, std::string);
    template<class T> friend Opt arg(T&, std::string);
    // clang-format on
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

    /// @name Add Option, Group, Section, or Epilog
    ///@{
    Cli& add(Group g) { return group_ = std::move(g.name), *this; }
    Cli& operator|(Opt o) & { return add(std::move(o)); }
    Cli& operator|(Group g) & { return add(std::move(g)); }
    Cli&& operator|(Opt o) && { return add(std::move(o)), std::move(*this); }
    Cli&& operator|(Group g) && { return add(std::move(g)), std::move(*this); }

    /// A titled table of `term`/description rows that are not Opt%s - `ENVIRONMENT`, plugin arguments, ...
    /// Both backends render it below the options; @p head names the first column in Cli::markdown.
    /// Pass no @p rows to get a bare header that groups the Section%s below it - one level up in Cli::markdown.
    Cli& section(std::string title, std::string head, std::vector<std::pair<std::string, std::string>> rows) {
        return sections_.emplace_back(std::move(title), std::move(head), std::move(rows)), *this;
    }

    /// Text printed below the option list.
    Cli& epilog(std::string s) { return epilog_ = std::move(s), *this; }
    ///@}

    /// Parses `argc`/`argv`; returns the error message - and nothing at all if all went well.
    std::optional<std::string> parse(int argc, const char* const* argv);
    void help(std::ostream&) const;     ///< Renders the help for a terminal.
    void markdown(std::ostream&) const; ///< Renders the same information as Doxygen-flavored Markdown tables.

private:
    std::string usage() const;

    /// Names of the Opt groups in the order they first occur; the default group is the empty name.
    std::vector<std::string_view> groups() const;

    Opt* find(std::string_view name);

    struct Section {
        std::string title, head;
        std::vector<std::pair<std::string, std::string>> rows;
    };

    std::string prog_, descr_, epilog_, group_;
    std::vector<Opt> opts_;
    std::vector<Section> sections_;

    friend std::ostream& operator<<(std::ostream& os, const Cli& cli) { return cli.help(os), os; }
};

} // namespace fe::cli
