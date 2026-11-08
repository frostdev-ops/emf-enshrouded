#include "dispatcher.h"

#include <algorithm>
#include <utility>

#include "fault_report.h"

namespace emf::core {
namespace {
thread_local EmfWorld* t_dispatching = nullptr;

struct DispatchScope {
    EmfWorld* previous;
    explicit DispatchScope(EmfWorld* w) : previous(t_dispatching) { t_dispatching = w; }
    ~DispatchScope() { t_dispatching = previous; }
};
}  // namespace

EmfWorld* dispatching_world() { return t_dispatching; }

bool guarded_world_call(EmfWorldCallback cb, EmfWorld* world, void* user) {
    __try {
        cb(world, user);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

void Dispatcher::subscribe_tick(EmfWorldCallback cb, void* user) {
    if (!cb) return;
    std::string module = module_name_of(reinterpret_cast<const void*>(cb));
    AcquireSRWLockExclusive(&lock_);
    subscribers_.push_back({cb, user, 0, true, std::move(module)});
    ReleaseSRWLockExclusive(&lock_);
}

void Dispatcher::post(uint8_t kind, EmfWorldCallback cb, void* user) {
    if (!cb) return;
    AcquireSRWLockExclusive(&lock_);
    queue_.push_back({cb, user, kind, true, {}});  // the module's name is looked up when it runs, if timed
    ReleaseSRWLockExclusive(&lock_);
}

size_t Dispatcher::active_subscribers() const {
    AcquireSRWLockShared(&lock_);
    size_t n = 0;
    for (const auto& s : subscribers_) n += s.enabled;
    ReleaseSRWLockShared(&lock_);
    return n;
}

void Dispatcher::set_perf(PerfBook* book, std::function<void(const std::string&, double)> slow, double (*clock_ms)()) {
    perf_ = book;
    slow_ = std::move(slow);
    clock_ms_ = clock_ms;
}

std::string world_kind_name(uint8_t kind) {
    if (kind == EMF_WORLD_SERVER) return "server";
    if (kind == EMF_WORLD_CLIENT) return "client";
    if (kind == EMF_WORLD_PREDICTION) return "prediction";
    return "world " + std::to_string(kind);
}

void Dispatcher::run(EmfWorld* world, uint8_t kind, const ErrorSink& on_error) {
    // Take this kind's queued items and a snapshot of subscribers under the lock, run them outside it.
    std::vector<Item> due;
    std::vector<size_t> subs;
    AcquireSRWLockExclusive(&lock_);
    for (auto it = queue_.begin(); it != queue_.end();) {
        if (it->kind == kind) {
            due.push_back(*it);
            it = queue_.erase(it);
        } else {
            ++it;
        }
    }
    for (size_t i = 0; i < subscribers_.size(); ++i)
        if (subscribers_[i].enabled) subs.push_back(i);
    std::vector<Item> sub_items;
    for (size_t i : subs) sub_items.push_back(subscribers_[i]);
    ReleaseSRWLockExclusive(&lock_);

    // Timing: each module's calls in this step are added up and go to the book once, after the step's callbacks.
    std::vector<std::pair<std::string, double>> spent;
    const auto call = [&](Item& item) {
        if (!perf_) return guarded_world_call(item.cb, world, item.user);
        if (item.module.empty()) item.module = module_name_of(reinterpret_cast<const void*>(item.cb));
        const double start = clock_ms_ ? clock_ms_() : perf_clock_ms();
        const bool ok = guarded_world_call(item.cb, world, item.user);
        const double ms = (clock_ms_ ? clock_ms_() : perf_clock_ms()) - start;
        auto it = std::find_if(spent.begin(), spent.end(), [&](const auto& s) { return s.first == item.module; });
        if (it == spent.end())
            spent.emplace_back(item.module, ms);
        else
            it->second += ms;
        if (ms > kSlowCallMs && slow_) slow_(item.module, ms);
        return ok;
    };

    const DispatchScope scope(world);
    for (auto& item : due) {
        if (!call(item))
            on_error("posted callback in " + module_name_of(reinterpret_cast<const void*>(item.cb)) +
                     " faulted (world kind " + std::to_string(kind) + ")");
    }
    for (size_t k = 0; k < sub_items.size(); ++k) {
        if (call(sub_items[k])) continue;
        AcquireSRWLockExclusive(&lock_);
        subscribers_[subs[k]].enabled = false;
        ReleaseSRWLockExclusive(&lock_);
        on_error("tick callback in " + module_name_of(reinterpret_cast<const void*>(sub_items[k].cb)) +
                 " faulted and was disabled (world kind " + std::to_string(kind) + ")");
    }
    if (perf_)
        for (const auto& [module, ms] : spent) perf_->add(module + " (" + world_kind_name(kind) + ")", ms);
}

}  // namespace emf::core
