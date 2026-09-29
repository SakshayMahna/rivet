# Experiment 04: Mutex and lock guard

Notes from [`experiments/04_mutex`](../../experiments/04_mutex/main.cpp): the shared counter from [experiment 03](experiment-03.md), fixed with a `std::mutex` and a `std::lock_guard`.

## Mutex

**Mutex** = **mut**ual **ex**clusion. A `std::mutex` is either locked or unlocked, and **only one thread can have it locked at a time**.

```cpp
m.lock();     // free → take it and continue; held by another thread → wait here
++counter;    // critical section: only one thread at a time
m.unlock();   // release it; one waiting thread (if any) gets it next
```

The code between `lock()` and `unlock()` is the **critical section**. Analogy: a single key to a room. Take the key to enter; if someone has it, wait at the door; hand it back when you leave.

### A mutex does two things

1. **Only one thread at a time:** the read, add and write of `++counter` can no longer interleave with another thread's.
2. **Changes become visible:** everything written before `unlock()` is guaranteed to be visible to the next thread that `lock()`s the **same** mutex. This is a *happens-before* relationship, the same guarantee as `join()`. Taking turns alone wouldn't be enough; without this, a thread could still see an old cached value.

### A mutex protects nothing by itself

The mutex doesn't know which data it's "for". `counter` is only protected if **every** read and write of it locks the **same** mutex. Forget it in one place, even a read, and that's still a data race. TSan catches this; the compiler doesn't.

That's why the data and its mutex are usually kept together, for example as private members of one class, so the data can only be reached through the lock.

### Rules for `std::mutex`

- **The thread that locked it must unlock it.** Unlocking from another thread is undefined behaviour.
- **Never lock it twice on the same thread.** That deadlocks, or is undefined behaviour. (`std::recursive_mutex` allows it, but needing it usually points to a design problem.)
- **It can't be copied or moved.** Threads find it by its address; a copy would be a different mutex protecting nothing. That's why it's passed with `std::ref`.
- **`try_lock()`** tries once and returns `false` instead of waiting.

### What happens underneath

- **Mutex free:** locking is a single atomic CPU instruction, with no involvement from the operating system. Cheap.
- **Mutex held by another thread:** the waiting thread asks the OS to put it to sleep, and is woken up on `unlock()`. Expensive. It shows up as `sys` time in `time` output.

## Lock guard

Calling `lock()`/`unlock()` by hand breaks when the code in between returns early or throws. `unlock()` never runs, the mutex stays locked, and every other thread waits forever.

`std::lock_guard` makes that impossible:

```cpp
{
    std::lock_guard<std::mutex> lock(m);   // constructor → m.lock()
    if (error) return;                     // destructor runs → m.unlock()
    process();                             // throws → destructor runs → m.unlock()
}                                          // end of scope → destructor → m.unlock()
```

- **Constructor locks, destructor unlocks.** C++ always runs destructors when a scope ends, however it ends, so the unlock can't be missed.
- **RAII** ("Resource Acquisition Is Initialisation"): creating the object takes the resource, destroying it gives it back. The same idea as `jthread` joining in its destructor, and the reason modern C++ rarely calls `lock()`, `unlock()`, `delete` or `close()` directly.
- **The scope is the critical section.** Everything from the guard to the closing `}` is locked. Keep it short with an extra `{ }` block; never hold a lock during a sleep, I/O or other slow work.

### Related types

| Type | Use |
|---|---|
| `std::lock_guard` | Lock for the whole scope. Simple; can't unlock early. |
| `std::scoped_lock` (C++17) | The same, but locks **several** mutexes at once without deadlocking. The recommended default: `std::scoped_lock lock(m);`. |
| `std::unique_lock` | Flexible: unlock and lock again, lock later, move it. Required by condition variables. |

## Results

Same three builds as experiment 03, 1,000,000 increments per thread:

| Build | Result | Time |
|---|---|---|
| Debug | 2,000,000 every run | ~0.03 s |
| Release | 2,000,000 every run | ~0.01 s |
| TSan (`RelWithDebInfo`) | 2,000,000, **no report** | ~0.52 s (`sys` ≈ 0.47 s) |

The race from experiment 03 is gone in every build, and TSan is silent. TSan is about 20–50× slower here. Most of that is `sys` time: TSan intercepts every lock and unlock to track which thread holds the mutex.

## Where to put the lock

These were tested in a separate copy of the code (Release build, 10,000,000 increments per thread), not in the experiment itself.

| Version | real | user | sys |
|---|---|---|---|
| Lock **inside** the loop | ~0.16 s | ~0.15 s | ~0.11 s |
| Lock **around** the whole loop | ~0.00 s | 0.00 s | 0.00 s |
| Baseline: 1 thread, lock inside the loop, 20M increments | 0.09 s | 0.09 s | 0.00 s |

**Inside the loop:** correct, but pays for 10 million lock/unlock pairs per thread.

- Even with no other thread wanting the mutex, each lock and unlock costs something (the single-thread baseline: 0.09 s).
- With two threads competing, one often has to wait. The OS puts it to sleep and wakes it up again: that's the ~0.11 s of `sys`. The mutex's memory also moves back and forth between the two CPU cores.

**Around the loop:** also correct, with only one lock per thread, and at `-O3` the loop turns into a single `+=`. But **the threads no longer run in parallel**: while A holds the lock, B waits for A's whole loop.

**Lesson:** when all the work is on shared data, threads make it *slower*. The fastest correct version here is one thread with no mutex. The real fix is to **share less**:

- give each thread a local counter and combine the results once at the end,
- or use `std::atomic<int>`.

The general trade-off, which runs through the whole pipeline design:

- **Lock more code:** less overhead, but less running in parallel.
- **Lock less code:** more parallelism, but more overhead and more competition for the lock.
- **Share less:** the best option whenever the design allows it.

## The unnamed guard

**`std::lock_guard<std::mutex>(m);` does not compile:**

```
error: no matching constructor for initialization of 'std::lock_guard<std::mutex>'
```

C++ reads it as a *declaration* of a new variable named `m` of type `lock_guard`; the parentheses around `m` are ignored (the "most vexing parse"). `lock_guard` can't be created without a mutex, so it fails.

Two nearby forms **do** compile, and silently protect nothing:

| Written | What it actually does | Results | Warning | TSan |
|---|---|---|---|---|
| `std::lock_guard<std::mutex>{m};` | A **temporary** guard: locks and unlocks again at the `;` | 1,923,254 · **2,000,000** · 1,460,016 | `-Wunused-value` | Data race |
| `std::unique_lock<std::mutex>(m);` | Declares a new, empty `unique_lock` named `m`; never locks | 1,005,069 · 1,024,025 | `-Wvexing-parse` | Data race |

- One run gave the correct 2,000,000 **by luck**. A correct result proves nothing.
- The compiler warned in both cases, without `-Wall`. Read warnings.
- **Always name the guard:** `std::lock_guard<std::mutex> lock(m);` or `std::scoped_lock lock(m);`.

## Follow-ups in the code

- Optionally switch to `std::scoped_lock lock(mtx);` and confirm the result is identical.
