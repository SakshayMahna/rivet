# Experiment 05: Atomics

Notes from [`experiments/05_atomic`](../../experiments/05_atomic/main.cpp): the shared counter from experiments [03](experiment-03.md) and [04](experiment-04.md), fixed with `std::atomic<int>` instead of a mutex, plus a version that avoids sharing altogether.

## `std::atomic`

`std::atomic<T>` wraps a single value so that **each operation on it is indivisible**. No other thread can see it half-done or interleave with it.

```cpp
std::atomic<int> counter{0};
++counter;   // read, add and write happen as ONE step
```

On a plain `int`, `++counter` is three separate steps (read, add, write), and two threads can interleave them. On an atomic, the CPU does all three as one hardware instruction (`LDADD` on Apple Silicon). No lock, no operating system.

### Guarantees

1. **No lost updates:** `++`, `fetch_add`, `exchange` and similar operations are indivisible.
2. **No torn values:** a reader always gets a complete value, never half-old, half-new.
3. **No data race:** concurrent access to an atomic is well-defined, so TSan stays silent.
4. **Memory ordering:** by default (`memory_order_seq_cst`) atomics also make *other* memory writes visible between threads, like a mutex's unlock/lock. See below.

### Operations

| Operation | Does |
|---|---|
| `load()` / `store(v)` | Read or write |
| `++a`, `a++`, `a += n`, `fetch_add(n)` | Add as one step. `fetch_add` returns the *old* value. |
| `exchange(v)` | Set a new value and return the old one, as one step |
| `compare_exchange_strong(expected, desired)` | "If it's still `expected`, set it to `desired`", as one step. The building block of lock-free code. |

### The trap: combinations aren't atomic

**Each operation is atomic on its own. A combination of operations is not.**

```cpp
++counter;                        // atomic: one read-add-write
counter = counter + 1;            // NOT atomic: an atomic load, then an atomic store
if (counter == 0) counter = 1;    // NOT atomic: check, then act
```

Another thread can get in between the two operations. This is a **race condition** in the program's logic, not a data race: every individual access is atomic, so **TSan can't catch it**. It can only be found by reasoning about the code.

### Atomic vs. mutex

| | `std::atomic` | `std::mutex` |
|---|---|---|
| Protects | **One** variable, **one** operation at a time | **Any amount** of code and data together |
| How | A single CPU instruction | A lock; waiting threads go to sleep |
| Waiting | Never sleeps | Can sleep (OS involvement) |
| Use for | Counters, flags, a single pointer or index | Keeping several values consistent with each other (a queue's data and its size) |

Once **two or more** values must stay consistent with each other, use a mutex.

### Gotchas

- `#include <atomic>`.
- An atomic **can't be copied or moved**, like a mutex. Pass it with `std::ref`, take it as `std::atomic<int>&`.
- **Initialise it:** `std::atomic<int> counter{0};`. Before C++20 a default-constructed atomic held an unspecified value.
- **`counter += local` is atomic; `counter = counter + local` is not.**

## Memory order (preview)

Compilers and CPUs **reorder memory reads and writes** for speed. A single thread can never notice; another thread can, because it may see those writes in a different order:

```cpp
// Thread A                       // Thread B
data = 42;                        while (!ready) {}
ready = true;                     print(data);   // may print 0
```

Each atomic operation takes a **memory order** saying how much ordering it guarantees for the memory *around* it:

| Order | Guarantees | Typical use |
|---|---|---|
| `relaxed` | Only that this one operation is indivisible. No ordering for other memory. | Counters where only the total matters |
| `release` (write) | Everything written before it is visible to a thread that reads this value with `acquire`. | Publishing: `data = 42; ready.store(true, release);` |
| `acquire` (read) | Once it sees the released value, it also sees everything written before the release. | Receiving: `while (!ready.load(acquire)) {}` then read `data` |
| `seq_cst` (default) | Acquire and release, **plus** all threads agree on one order of all `seq_cst` operations. | The default; use it when unsure. |

A mutex does this internally: `lock()` is an acquire, `unlock()` is a release. Experiment 07 goes deeper.

## Measuring time

```cpp
auto start = std::chrono::steady_clock::now();
// ... work ...
std::chrono::duration<double, std::milli> elapsed = std::chrono::steady_clock::now() - start;
```

| Clock | Measures | Jumps? | Use for |
|---|---|---|---|
| `system_clock` | Wall-clock time (calendar date and time) | **Yes**: time sync (NTP), manual changes | Timestamps people read or compare across machines |
| `steady_clock` | Time since an arbitrary point (usually boot) | **Never**, only moves forward | **Durations:** benchmarks, latency, timeouts, `sleep_until` |
| `high_resolution_clock` | In theory, the finest clock | Depends | **Avoid.** It's an alias for `steady_clock` (libc++, macOS) or `system_clock` (libstdc++, GCC). |

Use `steady_clock` for "how long did this take" and `system_clock` for "what time is it".

## Results

Release build, 1,000,000 increments per thread, two threads, 5 runs after one warm-up run:

| Version | Time (ms) | Result (expected 2,000,000) |
|---|---|---|
| Atomic `++counter` (seq_cst) | 6.4 – 8.6 | 2,000,000 ✓ |
| Relaxed `fetch_add(1, relaxed)` | 7.0 – 9.0 | 2,000,000 ✓ |
| Local counter, one `+=` at the end | **0.02 – 0.03** | 2,000,000 ✓ |
| Broken `counter = counter + 1` | 5.1 – 6.0 | 1,000,000 – 1,011,968 ✗ |

ThreadSanitizer build (`RelWithDebInfo`): **0 reports**, with the broken version still wrong (1,771,071).

Reference points, measured the same way in a separate test file (not part of the experiment):

| Reference | Time (ms) |
|---|---|
| Experiment 04: mutex, lock inside the loop | 14 – 19 |
| 1 thread, atomic `++`, 2M times | 3.5 |
| 1 thread, plain `int`, 2M times | 0.5 |
| Only starting and joining two empty threads | 0.03 |

## What the results mean

**Atomic is about twice as fast as the mutex, but far from free.** About 7 ms against about 15 ms: the atomic never sleeps, the mutex had threads being put to sleep and woken up by the OS. Yet the same 2M atomic increments on **one** thread take 3.5 ms. **Two threads took twice as long as one.** The memory holding `counter` (its *cache line*) has to move between the two cores for almost every increment. Adding threads made it slower.

**Sharing less wins by a huge margin.** The local-counter version takes the same time as starting two empty threads, so almost all of it *is* thread start-up. At `-O3` the compiler turned the loop into `local_counter = 1000000`, so no loop runs at all. In Debug, where the loop really runs, it was about 0.8 ms: still roughly 25× faster than the shared atomic, because each thread works in its own core's cache. It is also a benchmarking warning: **a result suspiciously close to zero usually means the compiler removed the work.**

**Relaxed is about the same as seq_cst here.** The ranges overlap and vary more from run to run than they differ. On Apple Silicon the two compile to almost the same instruction (`LDADD` vs. `LDADDAL`), and the cost is dominated by moving the cache line between cores, not by the ordering guarantee. Relaxed ordering matters for *what is correct*, not much for speed here.

**The broken version is faster *and* wrong.** An atomic load followed by an atomic store is cheaper than a single read-modify-write instruction, and it loses about half the updates. One run gave exactly 1,000,000, one thread's worth. TSan reported nothing, because every access is individually atomic. **TSan finds data races, not logic errors.**

**Only compare performance in Release builds.** In Debug, the relaxed version looked noticeably faster than seq_cst (about 18 ms vs. 23 ms). That's an unoptimised-code effect: `++counter` goes through more layers of function calls at `-O0`.

**The first run is slower.** Timings drop over the first few runs while the CPU raises its clock speed. Run everything several times, and ignore the first run or vary the order.

## Code notes

- `run_experiment(name, function)` removes the repetition between versions. It takes a `std::function`, which can hold anything callable, including lambdas that capture variables. That flexibility costs an extra indirect call and sometimes a memory allocation. Here it's only called once per thread, so it doesn't matter. A plain function pointer, `void (*)(std::atomic<int>&)`, would also work, since none of the functions capture anything. The difference will matter later, for pipeline stages called on every frame.

## Follow-ups

- **Single-thread baseline:** add it to the experiment itself, since `run_experiment` always starts two threads.
- **False sharing:** give each thread its **own** atomic, declared next to each other (`std::atomic<int> counters[2];`). Nothing is shared, yet it's still slow, because both sit in one 64-byte cache line. Then add `alignas(64)` to put each in its own cache line and measure again. (Phase 7 topic.)
