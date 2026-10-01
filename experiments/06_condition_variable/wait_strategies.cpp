// Experiment 06a: three ways for a thread to wait for a signal from another thread.
// A waiter thread blocks until main sets `ready` after 1 second, using busy-waiting,
// polling with sleep, or a condition variable. Observe the trade-off between
// reaction time (signal → waiter wakes) and CPU burned while waiting.

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <ctime>
#include <functional>
#include <iostream>
#include <mutex>
#include <string>
#include <thread>

namespace {
using Clock = std::chrono::steady_clock;

constexpr std::chrono::seconds signal_delay{1};
constexpr std::chrono::milliseconds poll_interval{10};
constexpr int num_runs{3};

void report(const std::string& name, Clock::time_point signalled_at, Clock::time_point woke_at,
            std::clock_t cpu_start, std::clock_t cpu_end) {
    // Wall-clock time from main setting `ready` to the waiter noticing it.
    std::chrono::duration<double, std::micro> reaction = woke_at - signalled_at;
    // std::clock() is CPU time used by the whole process (all threads). main is asleep
    // for almost the whole run, so this is roughly the CPU the waiter burned.
    const double cpu_ms = 1000.0 * static_cast<double>(cpu_end - cpu_start) / CLOCKS_PER_SEC;

    std::cout << name << " - reaction: " << reaction.count() << " us, CPU: " << cpu_ms
              << " ms\n";
}

// --- Waiters -------------------------------------------------------------------------

void busy_wait(std::atomic<bool>& ready, Clock::time_point& woke_at) {
    while (!ready.load()) {
        // Spin: re-check as fast as possible, keeping a CPU core at 100%.
    }
    woke_at = Clock::now();
}

void polling_wait(std::atomic<bool>& ready, Clock::time_point& woke_at) {
    while (!ready.load()) {
        std::this_thread::sleep_for(poll_interval);
    }
    woke_at = Clock::now();
}

// `ready` is a plain bool here: it is only read and written while holding `mtx`.
void condition_variable_wait(bool& ready, std::mutex& mtx, std::condition_variable& cv,
                             Clock::time_point& woke_at) {
    std::unique_lock<std::mutex> lock(mtx);
    cv.wait(lock, [&ready] { return ready; });
    woke_at = Clock::now();
}

// --- Runs ----------------------------------------------------------------------------
// `woke_at` is written by the waiter and read by main only after the jthread is joined
// (end of the inner scope), so join() makes it safe to read without a lock.

void run_busy_wait() {
    std::atomic<bool> ready{false};
    Clock::time_point signalled_at;
    Clock::time_point woke_at;
    const std::clock_t cpu_start = std::clock();
    {
        std::jthread waiter(busy_wait, std::ref(ready), std::ref(woke_at));
        std::this_thread::sleep_for(signal_delay);
        signalled_at = Clock::now();
        ready.store(true);
    }
    report("Busy-wait         ", signalled_at, woke_at, cpu_start, std::clock());
}

void run_polling_wait() {
    std::atomic<bool> ready{false};
    Clock::time_point signalled_at;
    Clock::time_point woke_at;
    const std::clock_t cpu_start = std::clock();
    {
        std::jthread waiter(polling_wait, std::ref(ready), std::ref(woke_at));
        std::this_thread::sleep_for(signal_delay);
        signalled_at = Clock::now();
        ready.store(true);
    }
    report("Polling (10 ms)   ", signalled_at, woke_at, cpu_start, std::clock());
}

void run_condition_variable_wait() {
    bool ready{false};
    std::mutex mtx;
    std::condition_variable cv;
    Clock::time_point signalled_at;
    Clock::time_point woke_at;
    const std::clock_t cpu_start = std::clock();
    {
        std::jthread waiter(condition_variable_wait, std::ref(ready), std::ref(mtx), std::ref(cv),
                            std::ref(woke_at));
        std::this_thread::sleep_for(signal_delay);
        {
            // Change the shared state under the lock, so it can't slip in between the
            // waiter checking `ready` and going to sleep (a lost wakeup).
            std::lock_guard<std::mutex> lock(mtx);
            signalled_at = Clock::now();
            ready = true;
        }
        cv.notify_one();  // after unlocking, so the waiter doesn't wake into a held mutex
    }
    report("Condition variable", signalled_at, woke_at, cpu_start, std::clock());
}
}  // namespace

int main() {
    for (int run = 1; run <= num_runs; ++run) {
        std::cout << "Run " << run << ":\n";
        run_busy_wait();
        run_polling_wait();
        run_condition_variable_wait();
    }
    return 0;
}
