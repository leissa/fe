#include "fe/error.h"

#include <cassert>

#include <sstream>

#include "fe/driver.h"

namespace fe {

const Diag& Error::diag() const { return driver_->diag(); }

Error& Error::msg(Loc loc, Tag tag, const std::function<std::string()>& fmt) {
    assert(tag != Tag::N && "a note belongs to Error::note");
    const auto& d = diag();
    if (tag == Tag::W && d.werror) tag = Tag::E;

    if (tag == Tag::E && d.max_errors != 0 && num_errors() >= d.max_errors) {
        truncated_ = dropped_ = true;
        return *this;
    }

    dropped_ = false;
    ++num_[size_t(tag)];
    msgs_.emplace_back(loc, tag, d.render(fmt));
    return *this;
}

void Error::note_(Loc loc, const std::function<std::string()>& fmt) {
    if (dropped_) return;
    assert(!msgs_.empty() && "a note needs an error or warning to attach to");
    ++num_[size_t(Tag::N)];
    msgs_.back().notes.emplace_back(loc, diag().render(fmt));
}

void Error::clear() {
    msgs_.clear();
    num_       = {};
    truncated_ = false;
    dropped_   = false;
}

std::string Error::str(std::ostream& os) const {
    auto scope = term::ScopedMode(term::use_color(os) ? term::Mode::Always : term::Mode::Never);
    auto oss   = std::ostringstream();
    oss << *this;
    return oss.str();
}

size_t Error::report(std::ostream& os) {
    auto num = num_errors();
    if (!empty()) os << *this;
    clear();
    return num;
}

void Error::bail(std::ostream& os) {
    auto bail = Bail(str(os), num_errors(), num_warnings());
    clear();
    throw bail;
}

void Error::ack(std::ostream& os) {
    if (num_errors() != 0) bail(os);
    report(os);
}

std::ostream& operator<<(std::ostream& os, const Error& e) {
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

} // namespace fe
