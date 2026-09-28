// Experiment 01: basic std::thread usage.
// Two threads print with different sleep periods; observe that they run
// concurrently, that output order is non-deterministic, and what join() does.

#include <chrono>
#include <iostream>
#include <string>
#include <thread>

namespace {
constexpr std::chrono::milliseconds sleep_duration_a{500};
constexpr std::chrono::milliseconds sleep_duration_b{1000};

constexpr int num_iterations{5};

void thread_function(std::string name, std::chrono::milliseconds sleep_duration) {
    for (int i = 0; i < num_iterations; ++i) {
        std::cout << name << "\n";
        std::this_thread::sleep_for(sleep_duration);
    }
    std::cout << name << " finished\n";
}
} // namespace

int main() {
    std::thread thread_a(thread_function, "A", sleep_duration_a);
    std::thread thread_b(thread_function, "B", sleep_duration_b);

    thread_a.join();
    thread_b.join();

    return 0;
}
