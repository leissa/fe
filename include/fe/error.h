#pragma once

#include <cassert>

#include <algorithm>
#include <exception>
#include <format>
#include <functional>
#include <iostream>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

#include "fe/driver.h"
#include "fe/format.h"
#include "fe/loc.h"
#include "fe/snippet.h"
#include "fe/term.h"

namespace fe {

/// Streams @p str, rendering the `` `code` `` spans a diagnostic cites in color.
/// Color and backticks are alternative ways of setting a citation apart, so the backticks are dropped
/// when coloring and kept verbatim otherwise.
inline std::ostream& stream_code(std::ostream& os, std::string_view str) {
    if (!term::use_color(os)) return os << str;

    for (size_t i = 0, e = str.size(); i != e;) {
        auto l = str.find('`', i);
        if (l == std::string_view::npos) return os << str.substr(i);
        auto r = str.find('`', l + 1);
        if (r == std::string_view::npos) return os << str.substr(i); // unpaired: not a citation
        os << str.substr(i, l - i) << term::FG::Cyan << str.substr(l + 1, r - l - 1) << term::FG::Reset;
        i = r + 1;
    }
    return os;
}

/// Collects diagnostics and renders each with the source Snippet its Loc points at.
/// Throw it once you are done: it *is* the exception, and Error::what streams the whole collection.
class Error : public std::exception {
public:
    enum class Tag {
        Error,
        Warn,
        Note,
    };

    struct Msg {
        Loc loc;
        Tag tag;
        std::string str;
    };

    /// @name Constructors
    ///@{
    Error() = default; ///< Renders with a default Driver::Diag and no Driver::render hook.

    /// @warning A Msg::loc points into @p driver's SrcMap and Error::msg renders through Driver::render,
    /// so @p driver must outlive this Error.
    explicit Error(const Driver& driver)
        : driver_(&driver) {}
    ///@}

    /// @name Getters
    ///@{
    const auto& msgs() const { return msgs_; }
    bool empty() const { return msgs_.empty(); }
    explicit operator bool() const { return !empty(); } ///< Any message at all?
    size_t num_errors() const { return std::ranges::count(msgs_, Tag::Error, &Msg::tag); }
    size_t num_warnings() const { return std::ranges::count(msgs_, Tag::Warn, &Msg::tag); }
    size_t num_notes() const { return std::ranges::count(msgs_, Tag::Note, &Msg::tag); }
    ///@}

    /// @name Add a Message
    ///@{
    /// Records the message @p fmt renders; see Driver::render.
    Error& msg(Loc loc, Tag tag, const std::function<std::string()>& fmt) {
        msgs_.emplace_back(loc, tag, driver_ ? driver_->render(fmt) : fmt());
        return *this;
    }

    /// @note Formats via `std::vformat` because Driver::render may render @p s more than once.
    template<class... Args>
    Error& msg(Loc loc, Tag tag, std::format_string<Args...> s, Args&&... args) {
        return msg(loc, tag, [&] { return std::vformat(s.get(), std::make_format_args(args...)); });
    }

    // clang-format off
    template<class... Args> Error& error(Loc loc, std::format_string<Args...> s, Args&&... args) { return msg(loc, Tag::Error, s, std::forward<Args>(args)...); }
    template<class... Args> Error& warn (Loc loc, std::format_string<Args...> s, Args&&... args) { return msg(loc, Tag::Warn,  s, std::forward<Args>(args)...); }
    template<class... Args> Error& note (Loc loc, std::format_string<Args...> s, Args&&... args) {
        assert(num_errors() > 0 || num_warnings() > 0);
        return msg(loc, Tag::Note, s, std::forward<Args>(args)...);
    }
    // clang-format on

    /// Error::note whose whole point is to point *elsewhere*; dropped when @p loc adds nothing.
    /// A @p loc overlapping the primary one is already covered by its snippet and so points nowhere new.
    /// The renderer gives @p loc a header line of its own, so phrase the message to stand alone.
    template<class... Args>
    Error& note_at(Loc loc, std::format_string<Args...> s, Args&&... args) {
        if (!loc || (loc & primary_loc())) return *this;
        return note(loc, s, std::forward<Args>(args)...);
    }
    ///@}

    /// @name Handle Errors/Warnings
    ///@{
    void clear() { msgs_.clear(); }

    /// If errors occurred, claim them and throw; if warnings occurred, claim them and report to @p os.
    void ack(std::ostream& os = std::cerr) {
        auto e = std::move(*this);
        if (e.num_errors() != 0) throw e;
        if (e.num_warnings() != 0) os << e.num_warnings() << " warning(s) encountered\n" << e;
    }

    const char* what() const noexcept override {
        if (what_.empty()) {
            std::ostringstream oss;
            oss << *this;
            what_ = oss.str();
        }
        return what_.c_str();
    }
    ///@}

    static term::FG tag2color(Tag tag) {
        // clang-format off
        switch (tag) {
            case Tag::Error: return term::FG::Red;
            case Tag::Warn:  return term::FG::Magenta;
            case Tag::Note:  return term::FG::Green;
            default: unreachable();
        }
        // clang-format on
    }

    friend std::ostream& operator<<(std::ostream& os, Tag tag) {
        // clang-format off
        switch (tag) {
            case Tag::Error: return os << term::FG::Red     << "error";
            case Tag::Warn:  return os << term::FG::Magenta << "warning";
            case Tag::Note:  return os << term::FG::Green   << "note";
            default: unreachable();
        }
        // clang-format on
    }

    /// Renders each Tag::Error/Tag::Warn with its source snippet and its Tag::Note%s underneath.
    /// A Tag::Note pointing at a Loc of its own reads as a diagnostic of its own - header line plus snippet;
    /// one about the primary Loc has nowhere else to point and stays a `= note:` continuation line.
    friend std::ostream& operator<<(std::ostream& os, const Error& e) { return e.stream(os); }

private:
    Driver::Diag diag() const { return driver_ ? driver_->diag : Driver::Diag(); }

    /// Loc of the Tag::Error/Tag::Warn that subsequent Tag::Note%s belong to.
    Loc primary_loc() const {
        for (auto i = msgs_.rbegin(), e = msgs_.rend(); i != e; ++i)
            if (i->tag != Tag::Note) return i->loc;
        return {};
    }

    /// Streamed piecewise instead of via std::format: a std::formatter cannot see its destination stream,
    /// so embedded term::FG values would resolve Mode::Auto to "no color"; see fe/term.h.
    std::ostream& header(std::ostream& os, const Msg& msg) const {
        os << term::FG::Yellow << msg.loc << ": " << msg.tag << ": " << term::FG::Reset;
        return stream_code(os, msg.str);
    }

    std::ostream& stream(std::ostream& os) const {
        auto [gutter, max_rows, no_snippet] = diag();
        auto primary                        = Loc();

        auto snippet = [&](const Msg& msg) {
            if (!no_snippet) os << Snippet{msg.loc, tag2color(msg.tag), gutter, max_rows};
        };

        for (const auto& msg : msgs_) {
            if (msg.tag == Tag::Note) {
                // A note pointing elsewhere reads as a diagnostic of its own; one about the primary Loc has no
                // other place to name and stays a continuation line.
                if (msg.loc && msg.loc != primary) {
                    os << std::format("{:>{}} ", "", gutter);
                    header(os, msg) << '\n';
                    snippet(msg);
                } else {
                    os << term::FG::Gray << std::format("{:>{}} = ", "", gutter) << msg.tag << ": " << term::FG::Reset;
                    stream_code(os, msg.str) << '\n';
                }
            } else {
                primary = msg.loc;
                header(os, msg) << '\n';
                snippet(msg);
            }
        }

        return os.flush();
    }

    const Driver* driver_ = nullptr;
    std::vector<Msg> msgs_;
    mutable std::string what_;
};

} // namespace fe

#ifndef DOXYGEN // clang-format off
template<> struct std::formatter<fe::Error     > : fe::ostream_formatter {};
template<> struct std::formatter<fe::Error::Tag> : fe::ostream_formatter {};
#endif // clang-format on
