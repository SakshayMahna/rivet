// Experiment 03: a data race on a shared counter.
// Two threads increment the same int with no synchronisation; observe lost
// updates in Debug, a misleadingly correct result in Release, and the race
// reported by ThreadSanitizer (-DRIVET_ENABLE_TSAN=ON).

#include <functional>
#include <iostream>
#include <thread>

namespace {
constexpr int num_iterations{1000000};
void increment(int& counter) {
    for (int i = 0; i < num_iterations; ++i)
        ++counter;
}
} // namespace

int main() {
    int counter{0};
    {
        std::jthread thread_a(increment, std::ref(counter));
        std::jthread thread_b(increment, std::ref(counter));
    }

    std::cout << "Final counter value: " << counter << "\n";
    std::cout << "Expected counter value: " << num_iterations * 2 << "\n";

    return 0;
}
