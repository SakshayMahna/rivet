# Experiment 02: `std::jthread` and stop tokens

Notes from [`experiments/02_jthreads`](../../experiments/02_jthreads/main.cpp): threads that join themselves, and stopping a thread that would otherwise run forever.

## `std::jthread` (C++20)

`std::jthread` starts threads and takes arguments exactly like `std::thread`, with two additions:

- **Its destructor calls `request_stop()` and then `join()`.** You can't forget to join, so the `std::terminate()` crash from a destroyed, still-joinable `std::thread` can't happen.
- **It has a built-in stop mechanism** (`std::stop_source` / `std::stop_token`) for asking the thread to finish.

Everything about arguments still applies (see [experiment 01](experiment-01.md#passing-arguments-to-stdthread)): `jthread` copies its arguments, and `std::ref` is needed to pass a real reference.

## Requires C++20

`std::jthread` only exists from C++20. Compiling without `-std=c++20` gives `no member named 'jthread' in namespace 'std'`. Apple clang defaults to an older standard.

In this project, C++20 comes from the root `CMakeLists.txt`, but **only for files CMake knows about**. A new experiment folder must be:

1. given its own `CMakeLists.txt` (`add_executable` + `target_link_libraries`),
2. added to `experiments/CMakeLists.txt` with `add_subdirectory`,
3. picked up by re-running `cmake -S . -B build`.

Until then it has no entry in `build/compile_commands.json`, so VS Code's C++ extension also falls back to its default standard and shows the same error. Point **C_Cpp › Default: Compile Commands** at `${workspaceFolder}/build/compile_commands.json` so the editor always uses the real build flags.

## Stop tokens

Stopping is **cooperative**: nothing interrupts the thread. A stop request only sets a flag, and the thread checks it.

```cpp
void worker(std::stop_token stop_token, std::string name, std::chrono::milliseconds period) {
    while (!stop_token.stop_requested()) {
        // one unit of work
        std::this_thread::sleep_for(period);
    }
}

std::jthread thread_a(worker, "A", 500ms);   // no token passed here
```

- **The token must be the first parameter.** `jthread` then passes its own token automatically. Anywhere else, it isn't passed, and the call fails to compile with a confusing error.
- **Two ways to stop:** call `thread.request_stop()` explicitly, or let the `jthread` go out of scope (its destructor requests the stop).
- **Only useful for loops that don't end on their own.** A loop that runs a fixed number of times finishes without it, and the destructor's `request_stop()` has no effect.

## What the run showed

A sleeps 500 ms per loop, B 1000 ms. `main` sleeps 5 s, calls `thread_a.request_stop()`, then returns:

```
... A / B interleaved for ~5 s ...
B finished
A finished
```

**B finishes before A**, even though only A was explicitly stopped:

1. Objects are destroyed in **reverse order of declaration**, so `thread_b`'s destructor runs first.
2. It requests a stop and then **waits for B**, which may be in the middle of a 1 s sleep.
3. Only then is `thread_a` destroyed and joined. A was already asked to stop and is usually finished by then.

Two lessons:

- **Stopping isn't instant.** `stop_requested()` is only checked at the top of the loop, and `sleep_for` can't be interrupted, so shutdown can take up to one full sleep period. The fix is a wait a stop request can interrupt, using `std::condition_variable_any` with a `stop_token` (a later experiment).
- **Request all stops first, then let destructors join.** Calling `request_stop()` on every thread before returning lets them all shut down at the same time, instead of one after another.

## Related, for later

- **`thread.get_stop_token()`** returns a copy of the thread's token, to hand to other code that should also watch for the stop.
- **`std::stop_source`** is the sending side. Create your own and pass its token to several threads; one `request_stop()` stops them all. This becomes the "shut down the whole pipeline" mechanism.
- **`std::stop_callback`** runs a function when a stop is requested, for example to wake up something that's blocked. It runs on the thread that calls `request_stop()`, so what it touches must be thread-safe.
- **Class members:** a `jthread` member should be declared **last**, so it is destroyed (stopped and joined) before the data it uses.

## Follow-ups in the code

- Make `run_duration` a `std::chrono::seconds` instead of a plain `int`, like the sleep durations.
- Add a comment at the top of `main.cpp` explaining the experiment.
- Sort the includes (clang-format does this once it's installed).
