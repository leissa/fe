#pragma once

#include <cassert>
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

namespace fe {

/// A small command-line parser for a single command - no subcommands.
///
/// Declare the switches with Cli::opt / Cli::arg and bind each one to a variable:
/// ```
/// bool show_help = false, verbose = false;
/// std::string out, in;
/// std::vector<std::string> plugins;
///
/// auto cli = fe::Cli("mim", "The MimIR compiler.")
///     .help(show_help)
///     .opt(verbose, ""      , "-V", "--verbose", "Be verbose.")
///     .opt(plugins, "plugin", "-p", "--plugin" , "Loads a plugin; repeatable.")
///     .grp("Output")
///     .opt(out    , "file"  , "-o", "--output" , "Where to write the result.")
///     .arg(in     , "file"  , "Input file.");
///
/// if (auto err = cli.parse(argc, argv)) throw std::invalid_argument(*err);
/// if (show_help) std::cout << cli;
/// ```
/// A `bool` target - or a callable taking one - is a flag: it takes no hint and is set each time it occurs.
/// Any other one needs a hint, listed as `<hint>` in the help, and is assigned the value: a `std::string`, an integral
/// type, a `std::vector` of those, or a callable, which may return a `std::string` to reject the value - a non-empty
/// one becomes the error of Cli::parse.
///
/// The parser understands `--name value`, `--name=value`, `-n value`, `-nvalue`, clustered short flags (`-abc`), and
/// `--` to end option processing.
///
/// Cli::help lays the switches out for a terminal - wrapped to its width and colored via fe::term - whereas
/// Cli::markdown renders the same information as Markdown tables; Cli::grp splits both into sections.
class Cli {
public:
    Cli() = default;
    Cli(std::string prog, std::string descr = {})
        : prog_(std::move(prog))
        , descr_(std::move(descr)) {}

    /// @name Declare Options and Arguments
    ///@{

    /// An option bound to @p target and named @p sname and/or @p lname - `"-o"` and `"--output"`.
    template<class T>
    Cli& opt(T& target, std::string hint = {}, std::string sname = {}, std::string lname = {}, std::string descr = {}) {
        static_assert(is_flag<T> || std::is_same_v<T, std::string> || Vec<T>::is
                          || is_num<T> || std::is_invocable_v<T&, std::string>,
                      "cannot bind this type to an option");
        auto& o = opts_.emplace_back(std::move(sname), std::move(lname), std::move(hint), std::move(descr), grp_);
        assert(is_flag<T> != o.takes_value() && "a flag takes no <hint>, anything else needs one");
        assert(!(is_flag<T> && o.is_arg()) && "a flag needs a name");
        o.multi = Vec<T>::is;
        o.dflt  = dflt(target);
        o.set   = [&target](std::string_view s) { return assign(target, s); };
        return *this;
    }

    Cli& opt(bool& target, std::string sname = {}, std::string lname = {}, std::string descr = {}) {
        return opt<bool>(target, "", std::move(sname), std::move(lname), std::move(descr));
    }

    /// A positional argument; @p hint names it in the help as `<hint>`.
    /// Bind a `std::vector` to soak up all remaining ones.
    template<class T>
    Cli& arg(T& target, std::string hint, std::string descr = {}) {
        opt(target, std::move(hint), {}, {}, std::move(descr));
        return opts_.back().dflt.clear(), *this;
    }

    /// The `-h`/`--help` flag.
    Cli& help(bool& target) { return opt(target, {}, "-h", "--help", "Display this help and exit."); }

    /// The Cli::opt / Cli::arg declared last must occur at least @p min and at most @p max times.
    Cli& cardinality(size_t min, size_t max) {
        assert(!opts_.empty() && "no option to apply a cardinality to");
        return opts_.back().min = min, opts_.back().max = max, *this;
    }

    /// Opens a section named @p name that all following options are listed under.
    Cli& grp(std::string name) { return grp_ = std::move(name), *this; }

    /// A titled table of `term`/description rows that are not options - `ENVIRONMENT`, plugin arguments, ...
    /// Both backends render it below the options; @p head names the first column in Cli::markdown.
    /// Pass no @p rows to get a bare header that groups the sections below it - one level up in Cli::markdown.
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
    // clang-format off
    /// `Vec<T>::Elem` is `T`'s element type, if `T` is a `std::vector`, and `T` itself otherwise.
    template<class T> struct Vec                 { using Elem = T; static constexpr bool is = false; };
    template<class T> struct Vec<std::vector<T>> { using Elem = T; static constexpr bool is = true;  };
    // clang-format on

    template<class T>
    static constexpr bool is_num = std::is_integral_v<T> && !std::is_same_v<T, bool>;

    /// A `bool` - or a callable taking one - is a flag; anything else is assigned the value.
    template<class T>
    static constexpr bool is_flag
        = std::is_same_v<T, bool> || (std::is_invocable_v<T&, bool> && !std::is_invocable_v<T&, std::string>);

    /// Applies @p s to @p t; returns a message if @p s does not scan as a `T`.
    template<class T>
    static std::string assign(T& t, std::string_view s) {
        if constexpr (std::is_same_v<T, bool>) {
            t = true;
        } else if constexpr (std::is_same_v<T, std::string>) {
            t = s;
        } else if constexpr (Vec<T>::is) {
            typename Vec<T>::Elem elem{};
            if (auto err = assign(elem, s); !err.empty()) return err;
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
            t(true); // a flag bound to a callable
        }
        return {};
    }

    /// What @p t holds before parsing and hence shows as `[default: ...]` in the help.
    template<class T>
    static std::string dflt(const T& t) {
        if constexpr (std::is_same_v<T, std::string>)
            return t;
        else if constexpr (is_num<T>)
            return std::format("{}", t);
        else
            return {};
    }

    struct Opt {
        std::string names(bool pad = false) const; ///< `-o, --output`; @p pad aligns a lone long name.
        std::string spec(bool pad = false) const;  ///< Cli::Opt::names plus `<hint>`, if it takes a value.
        size_t width() const { return spec(true).size(); }
        bool is_arg() const { return sname.empty() && lname.empty(); }
        bool takes_value() const { return !hint.empty(); }
        std::string_view kind() const { return is_arg() ? "argument" : "option"; }
        std::string_view label() const {
            return !lname.empty() ? lname : !sname.empty() ? std::string_view(sname) : std::string_view(hint);
        }

        std::string sname, lname, hint, descr, grp, dflt;
        std::function<std::string(std::string_view)> set;
        bool multi = false; ///< Bound to a `std::vector` and hence soaks up any number of values.
        size_t min = 0;
        size_t max = std::numeric_limits<size_t>::max();
        size_t num = 0;
    };

    struct Section {
        std::string title, head;
        std::vector<std::pair<std::string, std::string>> rows;
    };

    std::string usage() const;

    /// Names of the Opt groups in the order they first occur; the default group is the empty name.
    std::vector<std::string_view> grps() const;

    Opt* find(std::string_view name);

    std::string prog_, descr_, epilog_, grp_;
    std::vector<Opt> opts_;
    std::vector<Section> sections_;

    friend std::ostream& operator<<(std::ostream& os, const Cli& cli) { return cli.help(os), os; }
};

} // namespace fe
