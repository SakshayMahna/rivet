#include <chrono>
#include <iostream>
#include <thread>
#include <stop_token>
#include <string>

namespace {
constexpr std::chrono::milliseconds sleep_duration_a{500};
constexpr std::chrono::milliseconds sleep_duration_b{1000};

constexpr int run_duration{5};

void thread_function(std::stop_token stop_token, std::string name, std::chrono::milliseconds sleep_duration) {
    while (!stop_token.stop_requested()) {
        std::cout << name << "\n";
        std::this_thread::sleep_for(sleep_duration);
    }
    std::cout << name << " finished\n";
}
} // namespace

int main() {
    std::jthread thread_a(thread_function, "A", sleep_duration_a);
    std::jthread thread_b(thread_function, "B", sleep_duration_b);

    std::this_thread::sleep_for(std::chrono::seconds(run_duration));
    thread_a.request_stop();
    return 0;
}
