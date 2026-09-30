// Experiment 05: fixing the shared counter with std::atomic instead of a mutex.
// Compares a seq_cst atomic increment, a relaxed fetch_add, counting in a
// thread-local variable with a single atomic add at the end, and a broken
// `counter = counter + 1`; observe what contention on one atomic costs, how much
// faster it is to share less, and that the broken version loses updates without
// ThreadSanitizer reporting anything.

#include <atomic>
#include <chrono>
#include <functional>
#include <iostream>
#include <string>
#include <thread>

namespace {
constexpr int num_iterations{1000000};
void increment_atomic(std::atomic<int>& counter) {
    for (int i = 0; i < num_iterations; ++i) {
        ++counter;
    }
}

void increment_local_then_add(std::atomic<int>& counter) {
    int local_counter{0};
    for (int i = 0; i < num_iterations; ++i) {
        ++local_counter;
    }
    counter += local_counter;
}

void increment_relaxed(std::atomic<int>& counter) {
    for (int i = 0; i < num_iterations; ++i) {
        counter.fetch_add(1, std::memory_order_relaxed);
    }
}

void increment_broken(std::atomic<int>& counter) {
    for (int i = 0; i < num_iterations; ++i) {
        counter = counter + 1;
    }
}

void run_experiment(const std::string& experiment_name,
    std::function<void(std::atomic<int>&)> increment_function) {
    auto start = std::chrono::steady_clock::now();
    std::atomic<int> counter{0};
    {
        std::jthread thread_a(increment_function, std::ref(counter));
        std::jthread thread_b(increment_function, std::ref(counter));
    }
    auto end = std::chrono::steady_clock::now();
    std::chrono::duration<double, std::milli> duration = end - start;
    std::cout << experiment_name << " - Duration: " << duration.count() << " ms\n";
    std::cout << experiment_name << " - Final counter value: " << counter << "\n";
    std::cout << "Expected counter value: " << num_iterations * 2 << "\n";
}
} // namespace

int main() {
    run_experiment("Atomic Increment", increment_atomic);
    run_experiment("Atomic Increment Local Then Add", increment_local_then_add);
    run_experiment("Atomic Increment Relaxed", increment_relaxed);
    run_experiment("Atomic Increment Broken", increment_broken);
    return 0;
}
