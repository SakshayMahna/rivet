// Experiment 07b: store buffering - where acquire/release is not enough.
//
//   Thread 1:  x.store(1);  r1 = y.load();
//   Thread 2:  y.store(1);  r2 = x.load();
//
// Each thread writes before it reads, so it seems one of them must see the other's 1.
// But each core's store goes into its private store buffer first, and its following
// load can run before that store is visible to the other core. Both loads then read 0.
// release/acquire allows this (they order a store with LATER loads of the same
// variable, not a store with a later load of a different one); seq_cst forbids it.
// std::barrier lines both threads up at the start of every trial so their
// store/load pairs overlap, and resets x and y between trials.

#include <atomic>
#include <barrier>
#include <iostream>
#include <string>
#include <thread>

namespace {
constexpr int num_trials{200000};

void run_trials(const std::string& name, std::memory_order store_order,
                std::memory_order load_order) {
    std::atomic<int> x{0};
    std::atomic<int> y{0};

    // Plain ints: each is written by one thread before it arrives at `end_barrier`,
    // and only read in the completion function, which runs after both threads have
    // arrived. The barrier makes those writes visible, so no atomic is needed.
    int r1{0};
    int r2{0};
    int both_zero{0};

    // Both threads wait here at the start of each trial, so they run their
    // store/load at (nearly) the same moment.
    std::barrier start_barrier(2);

    // Runs exactly once per trial, after both threads arrive at `end_barrier` and
    // before either is released. Must be noexcept.
    auto on_trial_end = [&]() noexcept {
        if (r1 == 0 && r2 == 0) {
            ++both_zero;
        }
        x = 0;
        y = 0;
    };
    std::barrier end_barrier(2, on_trial_end);

    {
        // Lambdas instead of separate functions: they can use the barriers and
        // results above directly (captured by reference with [&]), which avoids
        // naming the barrier's type in a function parameter.
        std::jthread thread_1([&] {
            for (int trial = 0; trial < num_trials; ++trial) {
                start_barrier.arrive_and_wait();
                x.store(1, store_order);
                r1 = y.load(load_order);
                end_barrier.arrive_and_wait();
            }
        });
        std::jthread thread_2([&] {
            for (int trial = 0; trial < num_trials; ++trial) {
                start_barrier.arrive_and_wait();
                y.store(1, store_order);
                r2 = x.load(load_order);
                end_barrier.arrive_and_wait();
            }
        });
    }

    std::cout << name << " - trials: " << num_trials << ", both zero: " << both_zero << "\n";
}
}  // namespace

int main() {
    run_trials("release / acquire", std::memory_order_release, std::memory_order_acquire);
    run_trials("seq_cst          ", std::memory_order_seq_cst, std::memory_order_seq_cst);
    return 0;
}
