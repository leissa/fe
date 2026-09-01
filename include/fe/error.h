#pragma once

#include <cassert>

#include <array>
#include <exception>
#include <format>
#include <functional>
#include <iostream>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "fe/diag.h"
#include "fe/format.h"
#include "fe/loc.h"
#include "fe/term.h"

namespace fe {

struct Driver;

/// Collects diagnostics and hands each to the Diag that lays it out.
/// Error::ack once you are done: it throws an Error::Bail if anything went wrong.
class Error {
public:
    using Tag = Diag::Tag;

    /// What Error::ack and Error::bail throw.
    /// It carries the finished text, so nothing in here points into a SrcMap any more and it may
    /// propagate past the Driver - and the Src%s - that produced it.
    class Bail : public std::exception {
    public:
        Bail(std::string what, size_t num_errors, size_t num_warnings)
            : what_(std::move(what))
            , num_errors_(num_errors)
            , num_warnings_(num_warnings) {}

        const char* what() const noexcept override { return what_.c_str(); }
        size_t num_errors() const { return num_errors_; }
        size_t num_warnings() const { return num_warnings_; }

        friend std::ostream& operator<<(std::ostream& os, const Bail& bail) { return os << bail.what_; }

    private:
        std::string what_;
        size_t num_errors_;
        size_t num_warnings_;
    };

    /// A secondary message elaborating a Msg.
    /// A Note with a Loc of its own reads as a diagnostic of its own - header line plus snippet;
    /// one without has nowhere else to point and renders as a `= note:` continuation line.
    struct Note {
        Loc loc;
        std::string str;
    };

    /// One Tag::Error or Tag::Warn together with the Note%s that belong to it.
    struct Msg {
        Loc loc;
        Tag tag;
        std::string str;
        std::vector<Note> notes;
    };

    /// A sink of your own; Driver::error is the one every frontend building block already reports to.
    /// @warning A Msg::loc points into @p driver's SrcMap and Error::msg renders through Diag::render,
    /// so @p driver must outlive this Error.
    explicit Error(const Driver& driver)
        : driver_(&driver) {}

    /// @name Getters
    ///@{
    const auto& msgs() const { return msgs_; }
    bool empty() const { return msgs_.empty(); }
    bool ok() const { return num_errors() == 0; } ///< Nothing recorded that must stop the compilation?
    size_t num_errors() const { return num_[size_t(Tag::Error)]; }
    size_t num_warnings() const { return num_[size_t(Tag::Warn)]; }
    size_t num_notes() const { return num_[size_t(Tag::Note)]; }
    bool truncated() const { return truncated_; } ///< Did Diag::max_errors drop anything?
    ///@}

    /// @name Add a Message
    /// Each of these yields the Error again, so a diagnostic, its Note%s, and a closing Error::bail chain.
    ///@{
    /// Records the message @p fmt renders; see Diag::render.
    /// @p tag must be Tag::Error or Tag::Warn - a Tag::Note belongs to Error::note.
    Error& msg(Loc loc, Tag tag, const std::function<std::string()>& fmt) {
        msg_(loc, tag, fmt);
        return *this;
    }

    // clang-format off
    /// @note Formats via `std::vformat` because Diag::render may render @p s more than once.
    template<class... Args> Error& msg(Loc loc, Tag tag, std::format_string<Args...> s, Args&&... args) {
        msg_(loc, tag, [&] { return std::vformat(s.get(), std::make_format_args(args...)); });
        return *this;
    }

    template<class... Args> Error& error(Loc loc, std::format_string<Args...> s, Args&&... args) { return msg(loc, Tag::Error, s, std::forward<Args>(args)...); }
    template<class... Args> Error& warn (Loc loc, std::format_string<Args...> s, Args&&... args) { return msg(loc, Tag::Warn,  s, std::forward<Args>(args)...); }

    /// A `= note:` continuation of the diagnostic being built; it has no Loc of its own to point at.
    template<class... Args> Error& note(std::format_string<Args...> s, Args&&... args) {
        note_(Loc(), [&] { return std::vformat(s.get(), std::make_format_args(args...)); });
        return *this;
    }

    /// A Note that points *elsewhere*; dropped when @p loc adds nothing.
    /// A @p loc overlapping the primary one is already covered by its snippet and so points nowhere new.
    /// An invalid @p loc points *nowhere* and degrades to the `= note:` continuation above.
    /// The renderer gives @p loc a header line of its own, so phrase the message to stand alone.
    template<class... Args> Error& note(Loc loc, std::format_string<Args...> s, Args&&... args) {
        if (loc && (loc & primary_loc_())) return *this;
        note_(loc, [&] { return std::vformat(s.get(), std::make_format_args(args...)); });
        return *this;
    }
    // clang-format on
    ///@}

    /// @name Shorthands
    /// Terse aliases of the above, in the spirit of Log::e and friends.
    ///@{
    // clang-format off
    template<class... Args> Error& e(Loc loc, std::format_string<Args...> s, Args&&... args) { return error(loc, s, std::forward<Args>(args)...); }
    template<class... Args> Error& w(Loc loc, std::format_string<Args...> s, Args&&... args) { return warn (loc, s, std::forward<Args>(args)...); }
    template<class... Args> Error& n(         std::format_string<Args...> s, Args&&... args) { return note(     s, std::forward<Args>(args)...); }
    template<class... Args> Error& n(Loc loc, std::format_string<Args...> s, Args&&... args) { return note(loc, s, std::forward<Args>(args)...); }
    // clang-format on
    ///@}

    /// @name Handle Errors/Warnings
    ///@{
    void clear() {
        msgs_.clear();
        num_       = {};
        truncated_ = false;
        dropped_   = false;
    }

    /// Renders everything collected so far the way it would appear on @p os; @p os only decides the coloring.
    std::string str(std::ostream& os = std::cerr) const {
        auto scope = term::ScopedMode(term::use_color(os) ? term::Mode::Always : term::Mode::Never);
        auto oss   = std::ostringstream();
        oss << *this;
        return oss.str();
    }

    /// Streams everything collected so far to @p os and claims it.
    /// @returns the number of Tag::Error%s that were reported.
    size_t report(std::ostream& os = std::cerr) {
        auto num = num_errors();
        if (!empty()) os << *this;
        clear();
        return num;
    }

    /// Claims everything collected so far and throws it as a Bail rendered for @p os.
    [[noreturn]] void bail(std::ostream& os = std::cerr) {
        auto bail = Bail(str(os), num_errors(), num_warnings());
        clear();
        throw bail;
    }

    /// If errors occurred, Error::bail; otherwise Error::report any warnings to @p os.
    void ack(std::ostream& os = std::cerr) {
        if (num_errors() != 0) bail(os);
        report(os);
    }
    ///@}

    /// Hands every Msg, its Note%s, and the closing summary to Diag.
    friend std::ostream& operator<<(std::ostream& os, const Error& e) {
        const auto& diag = e.diag();

        for (const auto& msg : e.msgs_) {
            diag.header(os, msg.loc, msg.tag, msg.str);
            diag.snippet(os, msg.loc, msg.tag);
            for (const auto& note : msg.notes)
                diag.note(os, note.loc, note.str);
        }

        diag.summary(os, e.num_errors(), e.num_warnings(), e.truncated_);
        return os;
    }

private:
    const Diag& diag() const; ///< Driver is incomplete here - it owns an Error of its own.

    /// Loc of the Msg that subsequent Note%s belong to.
    Loc primary_loc_() const { return msgs_.empty() ? Loc() : msgs_.back().loc; }

    void msg_(Loc loc, Tag tag, const std::function<std::string()>& fmt) {
        assert(tag != Tag::Note && "a note belongs to Error::note");
        const auto& d = diag();
        if (tag == Tag::Warn && d.werror) tag = Tag::Error;

        if (tag == Tag::Error && d.max_errors != 0 && num_errors() >= d.max_errors) {
            truncated_ = dropped_ = true;
            return;
        }

        dropped_ = false;
        ++num_[size_t(tag)];
        msgs_.emplace_back(loc, tag, d.render(fmt));
    }

    void note_(Loc loc, const std::function<std::string()>& fmt) {
        if (dropped_) return;
        assert(!msgs_.empty() && "a note needs an error or warning to attach to");
        ++num_[size_t(Tag::Note)];
        msgs_.back().notes.emplace_back(loc, diag().render(fmt));
    }

    const Driver* driver_;
    std::vector<Msg> msgs_;
    std::array<size_t, 3> num_ = {};
    bool truncated_            = false;
    bool dropped_              = false; ///< Was the Msg that Note%s would attach to dropped?
};

} // namespace fe

#ifndef DOXYGEN // clang-format off
template<> struct std::formatter<fe::Error      > : fe::ostream_formatter {};
template<> struct std::formatter<fe::Error::Bail> : fe::ostream_formatter {};
template<> struct std::formatter<fe::Diag::Tag  > : fe::ostream_formatter {};
#endif // clang-format on
