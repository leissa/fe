#pragma once

#include <chrono>
#include <cstdint>

#include <limits>
#include <ostream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace fe {

/// Records wall-clock timings for (possibly nested) named Profiler::Span%s and reports them in various formats.
/// Span%s nest like a stack, so each one remembers its parent.
/// From these Span%s the Profiler can derive
/// - a flat Profiler::summary aggregated by name,
/// - a hierarchical Profiler::tree that shows the runtimes in context, and
/// - a [Chrome Trace Event](https://docs.google.com/document/d/1CvAClvFfyA5R-PhYUmn5OOQtYMH4h6I0nSsKchNAySU)
///   JSON dump (Profiler::chrome_trace) that can be loaded into `chrome://tracing`, Perfetto, or speedscope.
/// @see Profiler::start, Profiler::stop
class Profiler {
public:
    using Clock    = std::chrono::steady_clock;
    using Duration = Clock::duration;

    static constexpr size_t No_Parent = std::numeric_limits<size_t>::max();

    /// A single run.
    struct Span {
        std::string name;
        Clock::time_point start;
        Clock::time_point stop;
        size_t depth;  ///< Nesting level; a root Span has depth 0.
        size_t parent; ///< Index of the enclosing Span in Profiler::spans, or Profiler::No_Parent for a root.
        std::vector<std::pair<std::string, uint64_t>> counters; ///< Custom counters (insertion order).

        Duration elapsed() const { return stop - start; }
    };

    /// @name Getters
    ///@{
    bool empty() const { return spans_.empty(); }
    const auto& spans() const { return spans_; }
    ///@}

    /// @name Recording
    /// Bracket a run with Profiler::start / Profiler::stop; the calls must nest like a stack.
    ///@{
    /// Marks the start of a run named @p name.
    void start(std::string_view name) {
        auto parent = stack_.empty() ? No_Parent : stack_.back();
        stack_.emplace_back(spans_.size());
        spans_.emplace_back(std::string(name), Clock::now(), Clock::time_point{}, stack_.size() - 1, parent);
    }

    /// Marks the end of the most recently started run.
    void stop() {
        auto id = stack_.back();
        stack_.pop_back();
        spans_[id].stop = Clock::now();
    }

    /// Adds @p n to counter @p key of the currently running Span; no-op if no Span is running or @p n is `0`.
    void count(std::string_view key, uint64_t n = 1) {
        if (stack_.empty() || n == 0) return;
        auto& counters = spans_[stack_.back()].counters;
        for (auto& [k, v] : counters) {
            if (k == key) {
                v += n;
                return;
            }
        }
        counters.emplace_back(std::string(key), n);
    }
    ///@}

    /// @name Reporting
    ///@{
    /// Prints a flat table aggregated by name, sorted by total time, descending.
    void summary(std::ostream&) const;
    /// Prints the Span%s as an indented tree, preserving the order in which they ran.
    void tree(std::ostream&) const;
    /// Dumps all Span%s as Chrome Trace Event Format JSON.
    void chrome_trace(std::ostream&) const;
    ///@}

private:
    /// Per-Span time spent in *direct* children; `self = elapsed - children`.
    std::vector<Duration> children_durations() const;

    std::vector<size_t> stack_;
    std::vector<Span> spans_;
};

} // namespace fe
