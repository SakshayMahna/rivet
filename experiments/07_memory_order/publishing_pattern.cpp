// Experiment 07a: the publishing pattern (message passing).
// A writer stores `data = i` and then `flag = i`; a reader loads `flag` and then
// `data`, counting a violation whenever it sees the flag for item i but data older
// than i. With release (flag store) / acquire (flag load), the writer's data store
// is guaranteed visible once the reader sees the flag: zero violations. With
// relaxed on the flag, the compiler and the ARM CPU may reorder either pair of
// operations, and violations appear. Swap the two flag orders to compare.

#include <atomic>
#include <functional>
#include <iostream>
#include <thread>

namespace {
constexpr int num_iterations{50000000};

void writer_thread(std::atomic<int>& data, std::atomic<int>& flag) {
    for (int i = 1; i <= num_iterations; ++i) {
        data.store(i, std::memory_order_relaxed);
        flag.store(i, std::memory_order_release);
    }
}

void reader_thread(std::atomic<int>& data, std::atomic<int>& flag) {
    int checks{0}; int violations{0};
    int current_flag{0}; int current_data{0};
    while (current_flag != num_iterations) {
        current_flag = flag.load(std::memory_order_acquire);
        current_data = data.load(std::memory_order_relaxed);
        ++checks;
        if (current_data < current_flag) {
            ++violations;
        }
    }

    std::cout << "Checks: " << checks << ", Violations: " << violations << "\n";
}
} // namespace

int main() {
    std::atomic<int> data{0};
    std::atomic<int> flag{0};

    std::jthread writer(writer_thread, std::ref(data), std::ref(flag));
    std::jthread reader(reader_thread, std::ref(data), std::ref(flag));

    return 0;
}
