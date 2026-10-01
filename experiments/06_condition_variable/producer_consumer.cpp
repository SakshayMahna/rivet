// Experiment 06b: handing data from a producer to a consumer through a queue.
// main pushes numbers into a std::queue protected by a mutex; a consumer thread
// sleeps on a condition variable until an item arrives or `done` is set. Observe
// the pattern: change shared state under the lock, notify after unlocking, wait
// with a predicate, and do slow work (printing) outside the lock. On shutdown the
// consumer drains any remaining items before stopping.

#include <chrono>
#include <condition_variable>
#include <functional>
#include <iostream>
#include <mutex>
#include <queue>
#include <thread>

namespace {
constexpr int num_items{10};
constexpr std::chrono::milliseconds produce_interval{100};

// Everything the producer and consumer share, kept together.
// Rule: every member except the mutex itself is only touched while holding `mtx`.
struct SharedQueue {
    std::queue<int> items;
    bool done{false};
    std::mutex mtx;
    std::condition_variable cv;
};

void consumer(SharedQueue& shared) {
    while (true) {
        std::unique_lock lock(shared.mtx);
        shared.cv.wait(lock, [&shared] { return !shared.items.empty() || shared.done; });
        if (shared.items.empty()) {
            break;
        }
        const int value = shared.items.front();
        shared.items.pop();
        lock.unlock();
        std::cout << "Consumed: " << value << "\n";
    }
}
}  // namespace

int main() {
    SharedQueue shared;
    std::jthread consumer_thread(consumer, std::ref(shared));

    for (int i = 1; i <= num_items; ++i) {
        {
            std::lock_guard lock(shared.mtx);
            shared.items.push(i);
        }
        shared.cv.notify_one();
        std::this_thread::sleep_for(produce_interval);
    }

    {
        std::lock_guard lock(shared.mtx);
        shared.done = true;
    }
    shared.cv.notify_one();

    return 0;
}
