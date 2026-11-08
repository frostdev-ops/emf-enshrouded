#pragma once
// Time spent per world step by named parts (the game's own world step, EMF's wait for the world's jobs, each mod), for
// the `perf` console command and the "slow" log lines: the steps, the total and the worst step per name since the last
// take. Thread-safe (world threads add, the console thread takes). Header-only: the core and the Lua host share it.
#include <windows.h>

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

namespace emf {

// QueryPerformanceCounter in milliseconds.
inline double perf_clock_ms() {
    static const double per_ms = [] {
        LARGE_INTEGER f;
        QueryPerformanceFrequency(&f);
        return static_cast<double>(f.QuadPart) / 1000.0;
    }();
    LARGE_INTEGER now;
    QueryPerformanceCounter(&now);
    return static_cast<double>(now.QuadPart) / per_ms;
}

class PerfBook {
public:
    struct Row {
        std::string name;
        uint64_t steps = 0;
        double total_ms = 0, max_ms = 0;
    };

    // `ms` spent by `name` in one step (callers add a name once per step).
    void add(const std::string& name, double ms) {
        AcquireSRWLockExclusive(&lock_);
        Row* row = nullptr;
        for (auto& r : rows_)
            if (r.name == name) row = &r;
        if (!row) {
            rows_.push_back(Row{name});
            row = &rows_.back();
        }
        ++row->steps;
        row->total_ms += ms;
        row->max_ms = (std::max)(row->max_ms, ms);
        ReleaseSRWLockExclusive(&lock_);
    }

    // Every name's row since the last take, the largest total first; the book is empty after it.
    std::vector<Row> take() {
        AcquireSRWLockExclusive(&lock_);
        std::vector<Row> out;
        out.swap(rows_);
        ReleaseSRWLockExclusive(&lock_);
        std::stable_sort(out.begin(), out.end(), [](const Row& a, const Row& b) { return a.total_ms > b.total_ms; });
        return out;
    }

private:
    SRWLOCK lock_ = SRWLOCK_INIT;
    std::vector<Row> rows_;  // a few dozen names: a list is enough
};

// "lua (server): 0.42 ms a step (600 steps), worst 18.3 ms, 2.5% of the time" for rows taken over `seconds`.
inline std::vector<std::string> perf_lines(const std::vector<PerfBook::Row>& rows, double seconds) {
    std::vector<std::string> out;
    for (const auto& r : rows) {
        char buf[320];
        std::snprintf(buf, sizeof buf, "%s: %.2f ms a step (%llu steps), worst %.1f ms, %.1f%% of the time",
                      r.name.c_str(), r.steps ? r.total_ms / static_cast<double>(r.steps) : 0.0,
                      static_cast<unsigned long long>(r.steps), r.max_ms,
                      seconds > 0 ? r.total_ms / (seconds * 10.0) : 0.0);
        out.emplace_back(buf);
    }
    return out;
}

}  // namespace emf
