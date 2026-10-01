# Experiment 06: Condition variables

Notes from [`experiments/06_condition_variable`](../../experiments/06_condition_variable/), which has two programs:

- [`wait_strategies.cpp`](../../experiments/06_condition_variable/wait_strategies.cpp) (`exp_06_wait_strategies`): three ways for a thread to wait for a signal, measured.
- [`producer_consumer.cpp`](../../experiments/06_condition_variable/producer_consumer.cpp) (`exp_06_producer_consumer`): handing data from one thread to another through a queue.

A folder can build several programs: each `.cpp` gets its own `add_executable` in the folder's `CMakeLists.txt`.

## The problem: waiting efficiently

One thread needs to wait until another tells it something happened ("a frame is ready"). Without a condition variable there are two bad options:

```cpp
while (!ready) {}                      // busy-wait: burns 100% of a CPU core doing nothing
while (!ready) { sleep_for(10ms); }    // polling: cheap, but reacts up to 10ms late
```

A **condition variable** lets a thread **sleep until it's woken up**: no CPU while waiting, and it wakes almost immediately when notified.

## How it works

A condition variable always works together with:

1. **Shared state**: the actual condition (`bool ready`, a queue being non-empty).
2. **A mutex** protecting that state.
3. **The condition variable**: only the "wake up" signal.

```cpp
// Waiting thread
std::unique_lock lock(m);
cv.wait(lock, [&] { return ready; });   // sleeps until ready == true; returns holding the lock

// Notifying thread
{
    std::lock_guard lock(m);
    ready = true;                        // change the state UNDER the lock
}
cv.notify_one();                         // then wake a waiter
```

### What `wait(lock, condition)` does

```cpp
while (!condition()) {
    // atomically: unlock m AND go to sleep
    // ... woken up ...
    // lock m again, check the condition again
}
```

1. **Checks the condition first.** If it's already true, it doesn't wait.
2. **Unlocks and sleeps as one step**, so no notification can slip in between.
3. **On wake-up, locks again and re-checks.** If the condition is still false, it sleeps again.

### Always wait with a condition

- **Spurious wakeups:** the OS may wake a waiting thread with no notification at all. Without re-checking, the thread carries on as if something had changed.
- **Lost wakeups:** a condition variable doesn't remember notifications. If `notify_one()` happens before the other thread starts waiting, it's lost. With a condition, the waiter sees the state is already true and doesn't wait.

**The shared state is what matters; the notification is only a hint to go and check it.**

### Why `std::unique_lock`

`wait` must unlock the mutex while sleeping and lock it again on wake-up. `lock_guard` can only lock at creation and unlock at destruction; `unique_lock` can unlock and lock again. It can also:

- unlock early with `lock.unlock()`,
- be created without locking (`std::defer_lock`),
- be moved.

It's slightly heavier, so use `lock_guard` (or `scoped_lock`) when you don't need that flexibility.

**Unlock through the `unique_lock`, never the mutex directly.** `mtx.unlock()` behind the `unique_lock`'s back leaves it thinking it still holds the lock, so its destructor unlocks **again**: undefined behaviour. `lock.unlock()` updates the `unique_lock` too, so its destructor does nothing.

### `notify_one` vs. `notify_all`

- **`notify_one()`** wakes **one** waiter. Use it when any single waiter can handle the event, such as one item added to a queue.
- **`notify_all()`** wakes **every** waiter. Use it when the change matters to all of them, such as shutdown. Waking everyone when only one can make progress is a *thundering herd*: they all compete for the mutex and most go back to sleep.

### Rules

1. **Change the shared state while holding the mutex**, even if it's an atomic. Otherwise the change can land between the waiter's check and it going to sleep, and the wake-up is lost (see below).
2. **Always wait with a condition.**
3. **Notify after unlocking.** Notifying while holding the lock is correct, but the woken thread may find the mutex still held and have to wait for it.
4. **The condition runs with the mutex held.** Only read shared state in it; never sleep or do slow work there.
5. **Always use the same mutex** with a given condition variable.

### The lost-wakeup hang

Setting the shared state without the lock (even as an `atomic<bool>`) can hang the program:

```
Worker (holds mutex)                     Notifier (no mutex)
checks: queue empty, done == false
                                         done = true
                                         notify_one()   ← nobody waiting yet: lost
goes to sleep
sleeps forever                           returns → jthread destructor waits forever
```

Inside `wait`, checking the condition and going to sleep both happen while holding the mutex. If the notifier must take the mutex to change the state, it can't get in between those two steps. The window is tiny, so testing rarely finds it; it has to be prevented by design.

## Part A: Three ways to wait

`main` starts a waiter, sleeps 1 s, then signals. The program measures:

- **Reaction time:** `steady_clock` timestamp when `main` signals, subtracted from the timestamp when the waiter wakes.
- **CPU used:** `std::clock()`, the CPU time of the whole process. `main` sleeps for almost the entire run, so this is roughly what the waiter burned.

Each strategy runs on its own, one after another, so the busy-wait's 100% CPU can't distort the others.

### Results

Release build, 1 s wait, 3 runs:

| Strategy | Reaction time | CPU while waiting 1 s |
|---|---|---|
| Busy-wait | **0.3 – 0.4 µs** | **~1000 ms**: a full core for the whole second |
| Polling every 10 ms | 0.9 – 9.9 ms | 1.3 – 2.1 ms |
| Condition variable | 36 – 41 µs | **0.1 – 0.3 ms** |

ThreadSanitizer: no reports.

- **Busy-wait reacts fastest but wastes a whole core.** In a robot with many threads, that core is taken from real work.
- **Polling's reaction time is random,** anywhere up to the poll interval, depending on where in its sleep the waiter was when the signal came. Shorter intervals react faster but burn more CPU.
- **The condition variable is the balance:** almost no CPU, and it reacts about 40 µs after the signal. That's the time for the OS to wake the thread up and schedule it onto a core.

Busy-waiting has real uses (very short waits in latency-critical code, where 40 µs is too slow and a core can be dedicated to it), but it should be a deliberate, measured choice.

### Code notes

- `woke_at` is written by the waiter and read by `main` without a lock. That's safe because `main` only reads it after the thread is joined, and `join()` guarantees the thread's writes are visible.

## Part B: Producer / consumer

`main` pushes 1–10 into a `std::queue<int>`, 100 ms apart. A consumer thread waits for items and prints them.

```cpp
struct SharedQueue {
    std::queue<int> items;
    bool done{false};
    std::mutex mtx;
    std::condition_variable cv;
};
```

- **The shared state lives in one struct**, passed as a single `std::ref`. Rule: every member except the mutex is only touched while holding `mtx`. This is the first step toward Phase 2's `BlockingQueue`, where these become private members of a class.
- **`done` is a plain `bool`**, because the mutex already protects it.

### Consumer loop

1. Lock with a `std::unique_lock`.
2. Wait until `!items.empty() || done`.
3. If the queue is empty, it woke because of `done`: stop.
4. Copy the front item into a local variable and pop it.
5. `lock.unlock()`.
6. Print, **outside** the lock, so the producer isn't blocked by slow output.

**Shutdown design:** the consumer stops only when `done` is set **and** the queue is empty, so it **finishes any remaining items** first. Stopping immediately and discarding them would also be valid. It's a design choice that comes back in Phase 2.

### Results

- Prints 1–10 in order, then stops cleanly. ThreadSanitizer: no reports.
- **Stress test** (a modified copy, not part of the experiment): 100,000 items with no delay between pushes, TSan build, 3 runs. Every run received all 100,000 items with the correct total (5,000,050,000) and no TSan reports. The 10-item version barely exercises the threads, since they rarely overlap; the stress version makes them compete constantly.

### Mistakes made along the way

The first version of the consumer had these bugs:

| Bug | Effect |
|---|---|
| Queue passed **by value** | The consumer got its own empty copy and waited forever |
| Only handled one item, no loop | Consumed one number and exited |
| `done` not in the wait condition | The consumer could never wake up to stop |
| `mtx.unlock()` instead of `lock.unlock()` | Double unlock in the `unique_lock` destructor: undefined behaviour, silently ignored on macOS |
| `q.pop()` after unlocking | Changed the queue without the lock: a data race with `push`. TSan missed it because the 500 ms sleeps kept the threads from overlapping. |
| `done` set without the lock | The lost-wakeup hang described above |

It worked in testing despite the last three. That's exactly why these rules have to be followed by design rather than verified by running the program.

### Other notes

- **Declaration order:** `shared` is declared before `consumer_thread`, so the thread is joined before `shared` is destroyed. Reversed, the consumer could use `shared` after it's gone.
- **CTAD:** `std::unique_lock lock(shared.mtx);` without `<std::mutex>`. The compiler deduces the template type from the argument (*class template argument deduction*, C++17).

## Follow-ups

- **A slower consumer:** sleep about 300 ms after each print. Items pile up in the queue: the starting point for Phase 3 (bounded queues, backpressure).
- **Two consumers:** `notify_one` on `done` wakes only one of them; the other sleeps forever and the program hangs at shutdown. Shutdown should use `notify_all`.
- **Interruptible waits:** `std::condition_variable_any` can wait on a `std::stop_token`, which fixes the slow shutdown from [experiment 02](experiment-02.md): `cv.wait_for(lock, stop_token, 1000ms, [] { return false; })` sleeps 1 s or wakes as soon as a stop is requested.
