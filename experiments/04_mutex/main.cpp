// Experiment 04: fixing the data race from experiment 03 with std::mutex.
// Each increment is protected by a std::lock_guard; observe that the result is
// correct in every build, that ThreadSanitizer is silent, and what locking on
// every iteration costs.

#include <functional>
#include <iostream>
#include <mutex>
#include <thread>

namespace {
constexpr int num_iterations{1000000};
void increment(int& counter, std::mutex& mtx) {
    for (int i = 0; i < num_iterations; ++i) {
        std::lock_guard<std::mutex> lock(mtx);
        ++counter;
    }
}
} // namespace

int main() {
    int counter{0};
    std::mutex mtx;
    {
        std::jthread thread_a(increment, std::ref(counter), std::ref(mtx));
        std::jthread thread_b(increment, std::ref(counter), std::ref(mtx));
    }

    std::cout << "Final counter value: " << counter << "\n";
    std::cout << "Expected counter value: " << num_iterations * 2 << "\n";

    return 0;
}
